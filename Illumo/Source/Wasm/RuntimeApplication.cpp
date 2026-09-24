#include <Illumo/Audio/AudioDevice.h>
#include <Illumo/Content/PackageMounts.h>
#include <Illumo/Engine/Application.h>
#include <Illumo/Rendering/FrameCapture.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Wasm/WasmGameModule.h>
#include <IllumoGuest/Dialog.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

// Host ceilings. A package manifest requests limits; the runtime grants at
// most these, and command-line options are validated against the same range.
static constexpr std::uint64_t kMaximumMemoryMiB = 4095u;
static constexpr std::uint64_t kMaximumFuelPerCall = 100000000000ull;
static constexpr std::uint64_t kMaximumDeadlineMilliseconds = 600000u;
static constexpr std::uint64_t kMaximumCaptureFrame = 100000u;
static constexpr std::uint64_t kMaximumBenchFrames = 1000000u;
static constexpr const char* kDefaultApplication = "game";

// The directory the runtime was started from. The runtime then works from its
// own directory, where the engine's shaders and assets are staged, so
// relative command-line paths are resolved against this one instead.
static std::filesystem::path s_invocationDirectory;

static std::filesystem::path
optionPath(const std::string& value)
{
  std::filesystem::path path(std::u8string(value.begin(), value.end()));
  if (!path.empty() && path.is_relative() && !s_invocationDirectory.empty()) {
    path = s_invocationDirectory / path;
  }
  return path;
}

static std::filesystem::path
runtimeDirectory()
{
  return EnvVars::ApplicationConfigPath().parent_path();
}

static std::vector<std::byte>
readModule(const std::filesystem::path& path)
{
  if (path.empty()) {
    return {};
  }
  // Package module bytes only. No native artifacts or DLL search paths.
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  const std::streamoff size = input.tellg();
  if (!input || size <= 0 || size > 64 * 1024 * 1024) {
    return {};
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) {
    return {};
  }
  return bytes;
}

// An empty option leaves the root, and therefore its grant, absent. A named
// root must already be a directory; the runtime never guesses one.
static bool
readRoot(const std::string& value, std::filesystem::path& root)
{
  root.clear();
  if (value.empty()) {
    return true;
  }
  std::error_code error;
  const std::filesystem::path path = optionPath(value);
  if (!std::filesystem::is_directory(path, error) || error) {
    return false;
  }
  root = std::filesystem::absolute(path, error);
  return !error;
}

// Budgets are explicit per launch. An empty option keeps the current value;
// a malformed or out-of-range value refuses to start instead of guessing.
static bool
readLimit(const std::string& value,
          std::uint64_t maximum,
          std::uint64_t& output)
{
  if (value.empty()) {
    return true;
  }
  std::uint64_t parsed = 0;
  const char* end = value.data() + value.size();
  const std::from_chars_result result =
    std::from_chars(value.data(), end, parsed);
  if (result.ec != std::errc() || result.ptr != end || parsed == 0 ||
      parsed > maximum) {
    return false;
  }
  output = parsed;
  return true;
}

// Truncates at a UTF-8 boundary.
static std::string
boundedText(const std::string& text, std::size_t maximum)
{
  if (text.size() <= maximum) {
    return text;
  }
  std::size_t end = maximum;
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0u) == 0x80u) {
    --end;
  }
  return text.substr(0, end);
}

static PackageCeilings
manifestCeilings()
{
  PackageCeilings ceilings;
  ceilings.memoryMiB = kMaximumMemoryMiB;
  ceilings.fuelPerCall = kMaximumFuelPerCall;
  ceilings.deadlineMilliseconds = kMaximumDeadlineMilliseconds;
  // Compute lanes leave two hardware threads for the control store and the
  // window/render thread; at least one lane is always available.
  const unsigned int hardware = std::thread::hardware_concurrency();
  ceilings.workers =
    std::clamp<std::uint64_t>(hardware > 2u ? hardware - 2u : 1u, 1u, 8u);
  return ceilings;
}

// A module named by the launched package, read through its /app view (a
// directory or an .ilpk).
static std::vector<std::byte>
readPackageModule(const VirtualFileSystem& vfs, const std::string& member)
{
  const std::string path = "/app/" + member;
  VfsStat stat;
  std::string error;
  std::vector<uint8_t> data;
  if (!vfs.stat(path, stat, error) || stat.kind != VfsKind::File ||
      stat.size == 0 || stat.size > 64u * 1024u * 1024u ||
      !vfs.read(path, data, error)) {
    return {};
  }
  std::vector<std::byte> bytes(data.size());
  std::memcpy(bytes.data(), data.data(), data.size());
  return bytes;
}

// Repeatable options arrive newline-separated.
static std::vector<std::string>
splitLines(const std::string& text)
{
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start < text.size()) {
    std::size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      end = text.size();
    }
    if (end > start) {
      lines.push_back(text.substr(start, end - start));
    }
    start = end + 1;
  }
  return lines;
}

static const char* const kLaunchOptions[] = {
  "GuestModule",        "GuestMod",         "GuestWorker",
  "GuestPackage",       "GuestStorage",     "GuestFuel",
  "GuestMemoryMiB",     "GuestDeadline",    "GuestApp",
  "GuestOpen",          "GuestCapture",     "GuestCaptureFrame",
  "GuestBenchFrames",   "GuestBenchWarmup", "GuestBenchScript",
  "GuestCaptureScript", "GuestMount",       "GuestProject"
};

// --bench-frames: warm up, time a fixed number of frames, print one JSON line
// and close. The optional script queues host console lines, each once its
// command exists (guest commands register asynchronously). A --capture run
// may carry the same script (--capture-script) to reach a later screen.
struct BenchOptions
{
  std::uint64_t frames = 0;
  std::uint64_t warmup = 120;
  std::vector<std::vector<std::string>> script;
};

static bool
readBenchScript(const std::filesystem::path& path,
                std::vector<std::vector<std::string>>& script)
{
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  std::string line;
  while (std::getline(input, line) && script.size() < 256) {
    std::vector<std::string> words;
    std::size_t position = 0;
    while (position < line.size()) {
      const std::size_t start = line.find_first_not_of(" \t\r", position);
      if (start == std::string::npos || line[start] == '#') {
        break;
      }
      const std::size_t end = line.find_first_of(" \t\r", start);
      words.push_back(line.substr(start, end - start));
      position = end == std::string::npos ? line.size() : end;
    }
    if (!words.empty()) {
      script.push_back(std::move(words));
    }
  }
  return true;
}

static double
samplePercentile(std::vector<double> samples, double fraction)
{
  if (samples.empty()) {
    return 0.0;
  }
  std::sort(samples.begin(), samples.end());
  const std::size_t index = static_cast<std::size_t>(
    std::clamp(fraction, 0.0, 1.0) * static_cast<double>(samples.size() - 1));
  return samples[index];
}

static nlohmann::json
distribution(const std::vector<double>& samples)
{
  nlohmann::json result;
  result["p50"] = samplePercentile(samples, 0.5);
  result["p95"] = samplePercentile(samples, 0.95);
  result["p99"] = samplePercentile(samples, 0.99);
  result["max"] = samplePercentile(samples, 1.0);
  return result;
}

static nlohmann::json
rolling(const RollingMetric& metric)
{
  nlohmann::json result;
  result["p50"] = metric.median();
  result["p95"] = metric.p95();
  result["max"] = metric.maximum();
  return result;
}

// Launch options select what this invocation runs. The engine persists
// settings on exit, so clear them before command-line parsing (a stale saved
// package must never replay) and again once read.
static void
clearLaunchOptions(IEnvVars* environment)
{
  if (environment == nullptr) {
    return;
  }
  for (const char* option : kLaunchOptions) {
    environment->setVar(option, std::string());
  }
}

// Runs before command-line parsing and window creation. The engine loads its
// shaders and assets by relative path, so the runtime works from its own
// directory wherever it was started.
static void
prepareRuntime(IEnvVars* environment)
{
  // stdout carries only the --capture and --bench JSON results; terminal log
  // lines go to stderr so callers can parse stdout as-is.
  Logger::setConsoleToStderr(true);
  if (s_invocationDirectory.empty()) {
    std::error_code error;
    s_invocationDirectory = std::filesystem::current_path(error);
    std::filesystem::current_path(runtimeDirectory(), error);
    if (error) {
      Logger::LogWarning("Cannot work from the runtime directory; engine "
                         "shaders and assets resolve from the current one");
    }
  }
  clearLaunchOptions(environment);
}

// Outcome of a --capture run, reported through the process exit code.
static int s_exitCode = 0;

static int
runtimeExitCode()
{
  return s_exitCode;
}

// Runtime shell around the package: applies the manifest title and, for
// --capture, reads back one presented frame and closes.
class RuntimeModule final : public IModule
{
public:
  RuntimeModule(std::unique_ptr<AudioDevice> audio,
                std::unique_ptr<WasmGameModule> guest,
                std::string title,
                std::string application,
                std::filesystem::path capture,
                std::uint64_t captureFrame,
                BenchOptions bench)
    : m_audio(std::move(audio))
    , m_guest(std::move(guest))
    , m_title(std::move(title))
    , m_application(std::move(application))
    , m_capture(std::move(capture))
    , m_captureFrame(captureFrame)
    , m_bench(std::move(bench))
  {
  }
  ~RuntimeModule() override
  {
    if (ic != nullptr && ic->renderer != nullptr && m_hookInstalled) {
      ic->renderer->setBeforePresent({});
    }
  }
  RuntimeModule(const RuntimeModule&) = delete;
  RuntimeModule& operator=(const RuntimeModule&) = delete;
  RuntimeModule(RuntimeModule&&) = delete;
  RuntimeModule& operator=(RuntimeModule&&) = delete;

  bool Start(IllumoContext* context) override
  {
    ic = context;
    if (context != nullptr && context->window != nullptr && !m_title.empty()) {
      context->window->setTitle(m_title);
    }
    const bool started = m_guest->Start(context);
    if (started) {
      Logger::LogInfo("The " + m_application + " package started");
    } else {
      Logger::LogError("The " + m_application +
                       " package failed to start: " + m_guest->error());
      if (!m_capture.empty()) {
        report(false, 0, 0, "The package failed to start: " + m_guest->error());
      }
      if (m_bench.frames != 0) {
        reportBench("The package failed to start: " + m_guest->error());
      }
    }
    return started;
  }
  void Update(double dt) override
  {
    if (m_bench.frames == 0 || m_benchDone) {
      if (!m_capture.empty() && !m_done) {
        std::string error;
        if (!feedScript(&error)) {
          report(false, 0, 0, error);
          m_done = true;
          if (ic != nullptr && ic->window != nullptr) {
            ic->window->requestClose();
          }
        }
      }
      m_guest->Update(dt);
      return;
    }
    std::string scriptError;
    if (!feedScript(&scriptError)) {
      reportBench(scriptError);
      return;
    }
    const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
    m_guest->Update(dt);
    const std::chrono::steady_clock::time_point end =
      std::chrono::steady_clock::now();
    // Warm-up counts from the end of the script.
    if (scriptFinished()) {
      ++m_benchUpdates;
    }
    if (m_benchUpdates > m_bench.warmup) {
      if (m_benchFrameIntervals.empty() && m_benchUpdateMilliseconds.empty()) {
        m_benchStart = start;
      } else {
        m_benchFrameIntervals.push_back(
          std::chrono::duration<double, std::milli>(start - m_benchLastStart)
            .count());
      }
      m_benchUpdateMilliseconds.push_back(
        std::chrono::duration<double, std::milli>(end - start).count());
      if (m_benchUpdateMilliseconds.size() >= m_bench.frames) {
        m_benchEnd = end;
        reportBench({});
      }
    }
    m_benchLastStart = start;
    if (!m_guest->error().empty() && !m_benchDone) {
      reportBench("The package failed: " + m_guest->error());
    }
  }
  void DispatchDrawables(Scene* scene) override
  {
    m_guest->DispatchDrawables(scene);
    if (m_clearHook) {
      // Cleared here, never from inside the running hook.
      ic->renderer->setBeforePresent({});
      m_hookInstalled = false;
      m_clearHook = false;
    }
    if (m_capture.empty() || m_done || ic == nullptr ||
        ic->renderer == nullptr) {
      return;
    }
    // Like bench warm-up, the capture frame counts from the end of the script.
    if (!scriptFinished()) {
      return;
    }
    ++m_frames;
    if (m_frames == m_captureFrame && !m_hookInstalled) {
      // The frame dispatched now is presented at the end of this render.
      m_hookInstalled = true;
      ic->renderer->setBeforePresent(
        [this](Renderer& renderer) { captureFrame(renderer); });
    } else if (m_frames > m_captureFrame + 2 && !m_done) {
      // Every presentation of the target frame failed (frame errors skip
      // presentation, and with it the hook).
      report(false, 0, 0, "The target frame was never presented");
      m_done = true;
      ic->renderer->setBeforePresent({});
      m_hookInstalled = false;
      ic->window->requestClose();
    }
  }
  void Exit() override
  {
    if (ic != nullptr && ic->renderer != nullptr && m_hookInstalled) {
      ic->renderer->setBeforePresent({});
      m_hookInstalled = false;
    }
    if (!m_capture.empty() && !m_done) {
      report(false, 0, 0, "The runtime closed before the capture frame");
    }
    if (m_bench.frames != 0 && !m_benchDone) {
      reportBench("The runtime closed before the benchmark finished");
    }
    m_guest->Exit();
    Logger::LogInfo("The " + m_application + " package stopped");
  }
  bool OnCloseRequested() override
  {
    // A finished capture or benchmark closes without product dialogs.
    return m_done || m_benchDone || m_guest->OnCloseRequested();
  }

private:
  bool scriptFinished() const
  {
    return m_benchScriptLine >= m_bench.script.size() && m_benchWaitFrames == 0;
  }
  // Runs script lines in order: "@wait n" pauses n frames, "@key Name"
  // presses one key, and any other line is a console command queued once it
  // exists. An unknown key or directive fails the run (false with *error).
  bool feedScript(std::string* error)
  {
    if (ic == nullptr || ic->commandRegistry == nullptr ||
        ic->inputManager == nullptr) {
      m_benchScriptLine = m_bench.script.size();
      m_benchWaitFrames = 0;
      return true;
    }
    if (m_benchWaitFrames > 0) {
      --m_benchWaitFrames;
      return true;
    }
    CommandRegistry& commands = *ic->commandRegistry;
    bool queued = false;
    while (m_benchScriptLine < m_bench.script.size()) {
      const std::vector<std::string>& words = m_bench.script[m_benchScriptLine];
      if (words.front() == "@wait") {
        std::uint64_t frames = 0;
        if (words.size() != 2 ||
            !readLimit(words[1], kMaximumBenchFrames, frames)) {
          *error = "Invalid @wait in the script";
          return false;
        }
        m_benchWaitFrames = frames;
        ++m_benchScriptLine;
        break;
      }
      if (words.front() == "@key") {
        KeyCode key = KeyCode::None;
        if (words.size() != 2 || !keyNamed(words[1], key)) {
          *error = "Invalid @key in the script";
          return false;
        }
        ic->inputManager->getKeyQueue().push({ key, InputAction::Press, 0 });
        ++m_benchScriptLine;
        m_benchWaitFrames = 2;
        break;
      }
      if (words.front().starts_with("@")) {
        *error = "Unknown script directive " + words.front();
        return false;
      }
      if (!commands.HasCommand(words.front())) {
        // Guest commands register asynchronously; one that never appears
        // fails the run instead of stalling it.
        if (++m_scriptCommandWaitFrames > kScriptCommandWaitFrames) {
          *error = "Script command never registered: " + words.front();
          return false;
        }
        break;
      }
      m_scriptCommandWaitFrames = 0;
      commands.QueueCommand(
        words.front(),
        std::vector<std::string>(words.begin() + 1, words.end()));
      ++m_benchScriptLine;
      queued = true;
    }
    if (queued) {
      commands.ExecuteQueue();
    }
    return true;
  }
  static bool keyNamed(const std::string& name, KeyCode& key)
  {
#define ILLUMO_GUEST_KEY(keyName, number)                                      \
  if (name == #keyName) {                                                      \
    key = KeyCode::keyName;                                                    \
    return true;                                                               \
  }
#include <IllumoGuest/Keys.inc>
#undef ILLUMO_GUEST_KEY
    return false;
  }
  void reportBench(const std::string& error)
  {
    if (m_benchDone) {
      return;
    }
    m_benchDone = true;
    s_exitCode = error.empty() ? 0 : 1;
    nlohmann::json result;
    result["success"] = error.empty();
    result["application"] = m_application;
    result["error"] = error;
    result["warmupFrames"] = m_bench.warmup;
    result["frames"] = m_benchUpdateMilliseconds.size();
    const double seconds =
      std::chrono::duration<double>(m_benchEnd - m_benchStart).count();
    result["seconds"] = seconds;
    result["fps"] =
      seconds > 0.0
        ? static_cast<double>(m_benchFrameIntervals.size()) / seconds
        : 0.0;
    result["frameIntervalMs"] = distribution(m_benchFrameIntervals);
    result["moduleUpdateMs"] = distribution(m_benchUpdateMilliseconds);
    const WasmFrameStats& stats = m_guest->stats();
    nlohmann::json host;
    host["services"] = rolling(stats.servicesMilliseconds);
    host["update"] = rolling(stats.updateMilliseconds);
    host["receive"] = rolling(stats.receiveMilliseconds);
    host["frame"] = rolling(stats.frameMilliseconds);
    host["accept"] = rolling(stats.acceptMilliseconds);
    host["frameBytes"] = rolling(stats.frameBytes);
    result["wasmMs"] = host;
    const WasmFrameCounters* counters = m_guest->frameCounters();
    if (counters != nullptr) {
      nlohmann::json frame;
      frame["batches"] = counters->batches;
      frame["retainedBatches"] = counters->retainedBatches;
      frame["inlineVertexBytes"] = counters->inlineVertexBytes;
      frame["inlineIndexBytes"] = counters->inlineIndexBytes;
      frame["textureWrites"] = counters->textureWrites;
      frame["textureWriteBytes"] = counters->textureWriteBytes;
      frame["meshWriteBytes"] = counters->meshWriteBytes;
      frame["meshEnrollments"] = counters->meshEnrollments;
      frame["meshReplacements"] = counters->meshReplacements;
      result["lastFrame"] = frame;
    }
    if (!m_guest->error().empty()) {
      result["guestError"] = m_guest->error();
    }
    std::cout << result.dump() << std::endl;
    if (ic != nullptr && ic->window != nullptr) {
      ic->window->requestClose();
    }
  }

  void captureFrame(Renderer& renderer)
  {
    m_done = true;
    const std::array<int, 2> size = ic->window->getWindowDimensions();
    FrameReadback image =
      renderer.getBackend()->readBackbuffer(size[0], size[1]);
    // The window presents opaquely whatever alpha translucent UI leaves in
    // the backbuffer, so the screenshot must be opaque to match it.
    for (std::size_t index = 3; index < image.pixels.size(); index += 4) {
      image.pixels[index] = 255;
    }
    std::string error = image.error;
    if (image.success() && !FrameCapture::savePng(m_capture, image, &error) &&
        error.empty()) {
      error = "The PNG could not be written";
    }
    report(image.success() && error.empty(), image.width, image.height, error);
    // The hook must not outlive this presentation; clear it after returning.
    m_clearHook = true;
    ic->window->requestClose();
  }
  void report(bool success, int width, int height, const std::string& error)
  {
    if (m_reported) {
      return;
    }
    m_reported = true;
    s_exitCode = success ? 0 : 1;
    nlohmann::json result;
    result["success"] = success;
    result["application"] = m_application;
    const std::u8string output = m_capture.u8string();
    result["output"] =
      std::string(reinterpret_cast<const char*>(output.data()), output.size());
    result["frame"] = m_frames;
    result["width"] = width;
    result["height"] = height;
    result["error"] = error;
    if (!m_guest->error().empty()) {
      result["guestError"] = m_guest->error();
    }
    std::cout << result.dump() << std::endl;
  }

  // Declared first so it outlives the guest, which borrows it.
  std::unique_ptr<AudioDevice> m_audio;
  std::unique_ptr<WasmGameModule> m_guest;
  std::string m_title;
  std::string m_application;
  std::filesystem::path m_capture;
  std::uint64_t m_captureFrame = 0;
  std::uint64_t m_frames = 0;
  bool m_hookInstalled = false;
  bool m_clearHook = false;
  bool m_done = false;
  bool m_reported = false;
  BenchOptions m_bench;
  static constexpr std::uint64_t kScriptCommandWaitFrames = 600;
  std::size_t m_benchScriptLine = 0;
  std::uint64_t m_benchWaitFrames = 0;
  std::uint64_t m_scriptCommandWaitFrames = 0;
  std::uint64_t m_benchUpdates = 0;
  std::vector<double> m_benchFrameIntervals;
  std::vector<double> m_benchUpdateMilliseconds;
  std::chrono::steady_clock::time_point m_benchStart{};
  std::chrono::steady_clock::time_point m_benchEnd{};
  std::chrono::steady_clock::time_point m_benchLastStart{};
  bool m_benchDone = false;
};

static std::unique_ptr<IModule>
createGuestModuleFrom(IEnvVars* environment)
{
  std::filesystem::path gamePath =
    optionPath(environment->getVar("GuestModule").value);
  std::filesystem::path workerPath =
    optionPath(environment->getVar("GuestWorker").value);
  const std::filesystem::path modPath =
    optionPath(environment->getVar("GuestMod").value);
  const std::string packageOption = environment->getVar("GuestPackage").value;
  const std::string storageOption = environment->getVar("GuestStorage").value;
  const std::string applicationOption = environment->getVar("GuestApp").value;
  const std::string openOption = environment->getVar("GuestOpen").value;
  const std::string captureOption = environment->getVar("GuestCapture").value;
  WasmFileRoots files;
  WasmLimits limits;
  // Compute lanes: the historical single worker budget unless a manifest
  // requests otherwise.
  WasmLimits workerLimits;
  workerLimits.memoryBytes = 512ull * 1024ull * 1024ull;
  workerLimits.fuelPerCall = 1000000000u;
  workerLimits.deadlineMilliseconds = 10000u;
  std::uint32_t workerLanes = 1u;
  std::string title = "Illumo Runtime";
  std::string application;
  if (!applicationOption.empty() &&
      (!gamePath.empty() || !packageOption.empty())) {
    Logger::LogError("--app selects an installed package; it cannot be "
                     "combined with --package or --game");
    return nullptr;
  }
  std::vector<std::byte> game;
  std::vector<std::byte> worker;
  bool workerRequested = !workerPath.empty();
  if (gamePath.empty()) {
    // Package launch: <exe>/apps/<name> (a directory or <name>.ilpk), the
    // game by default, unless --package names another package.
    std::filesystem::path packagePath;
    if (packageOption.empty()) {
      application =
        applicationOption.empty() ? kDefaultApplication : applicationOption;
      if (!validPackageId(application)) {
        Logger::LogError("Invalid application name: " + application);
        return nullptr;
      }
      const std::filesystem::path apps = runtimeDirectory() / "apps";
      std::error_code error;
      if (std::filesystem::is_directory(apps / application, error)) {
        packagePath = apps / application;
      } else if (std::filesystem::is_regular_file(
                   apps / (application + ".ilpk"), error)) {
        packagePath = apps / (application + ".ilpk");
      } else {
        Logger::LogError("No installed application named '" + application +
                         "' in " + apps.string());
        return nullptr;
      }
    } else {
      packagePath = optionPath(packageOption);
    }
    const PackageCeilings ceilings = manifestCeilings();
    LoadedPackage package;
    std::string error;
    if (!PackageMounts::open(packagePath, ceilings, package, error)) {
      Logger::LogError(error);
      return nullptr;
    }
    const PackageManifest& manifest = package.manifest;
    if (manifest.kind != PackageKind::App) {
      Logger::LogError(package.origin + " is not an application package");
      return nullptr;
    }
    Logger::LogInfo(
      "Application package: " + manifest.id +
      (manifest.title.empty() ? std::string() : " (" + manifest.title + ")") +
      " from " + package.origin);
    if (application.empty()) {
      application = manifest.id;
    }
    if (!manifest.title.empty()) {
      title = manifest.title;
    }
    // Every other package: those installed beside the runtime, then each
    // --mount, which must exist and may not reuse an id.
    std::vector<std::string> takenIds{ manifest.id };
    std::vector<std::string> warnings;
    std::vector<LoadedPackage> packages = PackageMounts::discover(
      runtimeDirectory() / "packages", ceilings, takenIds, warnings);
    for (const std::string& mountOption :
         splitLines(environment->getVar("GuestMount").value)) {
      LoadedPackage mounted;
      if (!PackageMounts::open(
            optionPath(mountOption), ceilings, mounted, error)) {
        Logger::LogError("--mount " + mountOption + ": " + error);
        return nullptr;
      }
      if (std::find(takenIds.begin(), takenIds.end(), mounted.manifest.id) !=
          takenIds.end()) {
        Logger::LogError("--mount " + mountOption + ": the package id '" +
                         mounted.manifest.id + "' is already mounted");
        return nullptr;
      }
      takenIds.push_back(mounted.manifest.id);
      packages.push_back(std::move(mounted));
    }
    std::filesystem::path project;
    if (!readRoot(environment->getVar("GuestProject").value, project)) {
      Logger::LogError("--project names no directory");
      return nullptr;
    }
    std::shared_ptr<VirtualFileSystem> vfs =
      std::make_shared<VirtualFileSystem>();
    if (!PackageMounts::mountAll(*vfs,
                                 package,
                                 packages,
                                 runtimeDirectory() / "Assets",
                                 project,
                                 warnings,
                                 error)) {
      Logger::LogError("Cannot mount packages: " + error);
      return nullptr;
    }
    for (const std::string& warning : warnings) {
      Logger::LogWarning(warning);
    }
    for (const LoadedPackage& mounted : packages) {
      Logger::LogInfo("Mounted package " + mounted.manifest.id +
                      " at /packages/" + mounted.manifest.id + " from " +
                      mounted.origin);
    }
    if (!project.empty()) {
      Logger::LogInfo("Project directory mounted writable at /project: " +
                      project.string());
    }
    game = readPackageModule(*vfs, manifest.app.module);
    if (!workerRequested && !manifest.app.worker.empty()) {
      workerRequested = true;
      worker = readPackageModule(*vfs, manifest.app.worker);
    }
    files.packages = vfs;
    files.launchEditable = manifest.app.launchEditable;
    if (storageOption.empty()) {
      // Default private storage beside the runtime, one directory per app.
      std::error_code created;
      files.storage = runtimeDirectory() / "storage" / manifest.id;
      std::filesystem::create_directories(files.storage, created);
      if (created) {
        Logger::LogError("Cannot create the package storage directory");
        return nullptr;
      }
    } else if (!readRoot(storageOption, files.storage)) {
      return nullptr;
    }
    limits.memoryBytes = manifest.app.memoryMiB * 1024u * 1024u;
    limits.meterFuel = manifest.app.meterFuel;
    limits.fuelPerCall = manifest.app.fuelPerCall;
    limits.deadlineMilliseconds =
      static_cast<std::uint32_t>(manifest.app.deadlineMilliseconds);
    workerLimits.memoryBytes = manifest.app.workerMemoryMiB * 1024u * 1024u;
    workerLimits.meterFuel = manifest.app.meterFuel;
    workerLimits.deadlineMilliseconds =
      static_cast<std::uint32_t>(manifest.app.workerDeadlineMilliseconds);
    workerLanes = static_cast<std::uint32_t>(manifest.app.workers);
  } else if (!environment->getVar("GuestMount").value.empty() ||
             !environment->getVar("GuestProject").value.empty()) {
    Logger::LogError("--mount and --project need a package launch, not --game");
    return nullptr;
  } else if (!readRoot(packageOption, files.package) ||
             !readRoot(storageOption, files.storage)) {
    return nullptr;
  }
  std::uint64_t memoryMiB = limits.memoryBytes / (1024u * 1024u);
  std::uint64_t deadline = limits.deadlineMilliseconds;
  std::uint64_t captureFrame = 60;
  BenchOptions bench;
  const std::string fuelOption = environment->getVar("GuestFuel").value;
  if (!fuelOption.empty()) {
    // --fuel forces metering for this launch (comparisons, runaway triage).
    limits.meterFuel = true;
    workerLimits.meterFuel = true;
  }
  if (!readLimit(environment->getVar("GuestMemoryMiB").value,
                 kMaximumMemoryMiB,
                 memoryMiB) ||
      !readLimit(fuelOption, kMaximumFuelPerCall, limits.fuelPerCall) ||
      !readLimit(environment->getVar("GuestDeadline").value,
                 kMaximumDeadlineMilliseconds,
                 deadline) ||
      !readLimit(environment->getVar("GuestCaptureFrame").value,
                 kMaximumCaptureFrame,
                 captureFrame) ||
      !readLimit(environment->getVar("GuestBenchFrames").value,
                 kMaximumBenchFrames,
                 bench.frames) ||
      !readLimit(environment->getVar("GuestBenchWarmup").value,
                 kMaximumBenchFrames,
                 bench.warmup)) {
    return nullptr;
  }
  const std::string benchScript = environment->getVar("GuestBenchScript").value;
  const std::string captureScript =
    environment->getVar("GuestCaptureScript").value;
  if ((bench.frames == 0 && !benchScript.empty()) ||
      (bench.frames != 0 && !captureOption.empty())) {
    Logger::LogError("--bench-script needs --bench-frames, and a benchmark "
                     "cannot be combined with --capture");
    return nullptr;
  }
  if (!captureScript.empty() && captureOption.empty()) {
    Logger::LogError("--capture-script needs --capture");
    return nullptr;
  }
  if (!benchScript.empty() &&
      !readBenchScript(optionPath(benchScript), bench.script)) {
    Logger::LogError("--bench-script names no readable file: " + benchScript);
    return nullptr;
  }
  if (!captureScript.empty() &&
      !readBenchScript(optionPath(captureScript), bench.script)) {
    Logger::LogError("--capture-script names no readable file: " +
                     captureScript);
    return nullptr;
  }
  limits.memoryBytes = memoryMiB * 1024u * 1024u;
  limits.deadlineMilliseconds = static_cast<std::uint32_t>(deadline);
  // The launch document: granted by the host, described to the guest by its
  // base name only.
  std::vector<std::byte> startup;
  if (!openOption.empty()) {
    if (files.package.empty() && files.storage.empty() && !files.packages) {
      Logger::LogError("--open needs a package or storage root");
      return nullptr;
    }
    std::error_code error;
    const std::filesystem::path document =
      std::filesystem::absolute(optionPath(openOption), error);
    if (error || !std::filesystem::is_regular_file(document, error)) {
      Logger::LogError("--open names no readable file: " + openOption);
      return nullptr;
    }
    files.launch = document;
    GuestLaunch launch;
    const std::u8string name = document.filename().u8string();
    launch.label = boundedText(
      std::string(reinterpret_cast<const char*>(name.data()), name.size()),
      128);
    launch.editable = files.launchEditable;
    launch.size = std::filesystem::file_size(document, error);
    GuestWireWriter writer;
    launch.write(writer);
    startup = writer.take();
  }
  std::filesystem::path capture;
  if (!captureOption.empty()) {
    capture = optionPath(captureOption);
    std::error_code error;
    if (capture.extension() != ".png" ||
        std::filesystem::exists(capture, error)) {
      Logger::LogError("--capture needs a new .png path: " + captureOption);
      return nullptr;
    }
  }
  if (game.empty()) {
    game = readModule(gamePath);
  }
  std::vector<std::byte> mod = readModule(modPath);
  if (!workerPath.empty()) {
    worker = readModule(workerPath);
  }
  if (game.empty() || (!modPath.empty() && mod.empty()) ||
      (workerRequested && worker.empty())) {
    Logger::LogError("A requested WASM module is missing or unreadable");
    return nullptr;
  }
  Logger::LogInfo(
    "Guest module: " + std::to_string(game.size() / 1024) + " KiB" +
    (worker.empty() ? std::string()
                    : ", worker " + std::to_string(worker.size() / 1024) +
                        " KiB x " + std::to_string(workerLanes) + " lanes") +
    (mod.empty() ? std::string()
                 : ", mod " + std::to_string(mod.size() / 1024) + " KiB"));
  Logger::LogInfo(
    "Guest budget: " + std::to_string(limits.memoryBytes / (1024u * 1024u)) +
    " MiB memory, " + std::to_string(limits.deadlineMilliseconds) +
    " ms deadline per call" +
    (limits.meterFuel
       ? ", " + std::to_string(limits.fuelPerCall) + " fuel per call"
       : std::string(", fuel unmetered")));
  if (!files.storage.empty()) {
    Logger::LogInfo("Private storage: " + files.storage.string());
  }
  if (!files.launch.empty()) {
    Logger::LogInfo("Opening " + files.launch.string());
  }
  if (!capture.empty()) {
    Logger::LogInfo("Capture mode: frame " + std::to_string(captureFrame) +
                    " to " + capture.string());
  } else if (bench.frames != 0) {
    Logger::LogInfo("Benchmark mode: " + std::to_string(bench.frames) +
                    " frames after " + std::to_string(bench.warmup) +
                    " warm-up frames");
  }
  std::unique_ptr<WasmGameModule> guest =
    std::make_unique<WasmGameModule>(std::move(game),
                                     std::move(startup),
                                     limits,
                                     std::move(mod),
                                     std::move(worker),
                                     std::move(files));
  guest->setWorkerLimits(workerLimits, workerLanes);
  // Captures and benchmarks measure the main window alone: panels stay
  // docked there.
  if (!capture.empty() || bench.frames != 0) {
    guest->setSurfaceWindows(nullptr);
  }
  // Sound plays through the default output device. Captures and benchmarks
  // stay silent, and a machine without an output runs without audio.
  std::unique_ptr<AudioDevice> audio;
  if (capture.empty() && bench.frames == 0) {
    std::string audioError;
    audio = AudioDevice::create({}, audioError);
    if (!audio) {
      Logger::LogWarning("Audio disabled: " + audioError);
    }
    guest->setAudio(audio.get());
  } else {
    Logger::LogTrace("Audio disabled for capture and benchmark runs");
  }
  return std::make_unique<RuntimeModule>(std::move(audio),
                                         std::move(guest),
                                         std::move(title),
                                         std::move(application),
                                         std::move(capture),
                                         captureFrame,
                                         std::move(bench));
}

static std::unique_ptr<IModule>
createGuestModule(IEnvVars* environment)
{
  if (environment == nullptr) {
    return nullptr;
  }
  std::unique_ptr<IModule> module = createGuestModuleFrom(environment);
  const std::string capture = environment->getVar("GuestCapture").value;
  if (!module && !capture.empty()) {
    // Capture callers read one JSON result line whatever the outcome.
    nlohmann::json result;
    result["success"] = false;
    result["application"] = environment->getVar("GuestApp").value;
    result["output"] = capture;
    result["error"] =
      "The requested application could not be prepared; see the log";
    std::cout << result.dump() << std::endl;
  }
  clearLaunchOptions(environment);
  return module;
}

IllumoApplicationDefinition
CreateIllumoApplication()
{
  IllumoApplicationDefinition application;
  application.applicationName = "Illumo Runtime";
  application.commandLine.applicationName = "IllumoRuntime";
  application.commandLine.description =
    "Isolated WASM application host. Installed applications live in "
    "apps/<name>/ or apps/<name>.ilpk beside the runtime; the game runs by "
    "default. Every package in packages/ is mounted too.";
  application.commandLine.usage =
    "IllumoRuntime.exe [--app name] [--open file] [--capture out.png "
    "[--capture-frame n] [--capture-script file]] [--package dir|file.ilpk] "
    "[--mount dir|file.ilpk]... [--project dir] [--storage dir] "
    "[--game module.wasm] [--mod module.wasm] [--worker module.wasm] "
    "[--memory-mib n] [--fuel n] [--deadline-ms n] "
    "[--bench-frames n [--bench-warmup n] [--bench-script file]]";
  // SysCmdLine parses "path"/"name"/"file" values as strings and other names
  // as positive integers.
  application.commandLine.applicationOptions = {
    { "--app",
      "name",
      "GuestApp",
      "Installed application to run from apps/<name> (default: game)" },
    { "--open",
      "file",
      "GuestOpen",
      "Document handed to the application (it sees only the file name)" },
    { "--capture",
      "file",
      "GuestCapture",
      "Render until --capture-frame, write this new PNG, print a JSON "
      "result and exit" },
    { "--capture-frame",
      "count",
      "GuestCaptureFrame",
      "Frame to capture (default: 60)" },
    { "--capture-script",
      "file",
      "GuestCaptureScript",
      "Console lines, @key and @wait run before --capture-frame counts" },
    { "--package",
      "path",
      "GuestPackage",
      "Run the package in this directory or .ilpk instead of an installed "
      "application" },
    { "--mount",
      "paths",
      "GuestMount",
      "Also mount this package directory or .ilpk (repeatable)" },
    { "--project",
      "path",
      "GuestProject",
      "Mount this directory writable at /project (authoring)" },
    { "--storage",
      "path",
      "GuestStorage",
      "Writable storage directory (default: storage/<id> beside the runtime)" },
    { "--game",
      "path",
      "GuestModule",
      "Run a WASM module directly instead of a package manifest" },
    { "--mod", "path", "GuestMod", "Isolated game-compatible mod reactor" },
    { "--worker", "path", "GuestWorker", "Isolated compute reactor" },
    { "--memory-mib",
      "count",
      "GuestMemoryMiB",
      "Linear-memory ceiling in MiB" },
    { "--fuel",
      "count",
      "GuestFuel",
      "Fuel budget per call; forces fuel metering for this launch" },
    { "--deadline-ms",
      "milliseconds",
      "GuestDeadline",
      "Wall-clock deadline per call" },
    { "--bench-frames",
      "count",
      "GuestBenchFrames",
      "Time this many frames after warm-up, print a JSON result and exit" },
    { "--bench-warmup",
      "count",
      "GuestBenchWarmup",
      "Frames to run before timing starts (default: 120)" },
    { "--bench-script",
      "file",
      "GuestBenchScript",
      "Console lines queued in order before timing starts" }
  };
  application.applyDefaults = prepareRuntime;
  application.createRequiredModule = createGuestModule;
  application.exitCode = runtimeExitCode;
  return application;
}
