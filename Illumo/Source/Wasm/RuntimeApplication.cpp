#include <Illumo/Engine/Application.h>
#include <Illumo/Rendering/FrameCapture.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Wasm/WasmGameModule.h>
#include <IllumoGuest/Dialog.h>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

// Host ceilings. A package manifest requests limits; the runtime grants at
// most these, and command-line options are validated against the same range.
static constexpr std::uint64_t kMaximumMemoryMiB = 4095u;
static constexpr std::uint64_t kMaximumFuelPerCall = 100000000000ull;
static constexpr std::uint64_t kMaximumDeadlineMilliseconds = 600000u;
static constexpr std::uint64_t kMaximumCaptureFrame = 100000u;
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

// Plain file names inside the package; paths and parent references are not
// package members.
static bool
packageMember(const std::string& name)
{
  return !name.empty() && name.size() <= 128 &&
         name.find_first_of("/\\:") == std::string::npos && name != "." &&
         name != "..";
}

// Package ids and application names share one conservative alphabet, so an
// application name is always a single directory under apps/.
static bool
packageId(const std::string& id)
{
  if (id.empty() || id.size() > 64 || id == "." || id == "..") {
    return false;
  }
  for (const char character : id) {
    if (!((character >= 'a' && character <= 'z') ||
          (character >= '0' && character <= '9') || character == '.' ||
          character == '-' || character == '_')) {
      return false;
    }
  }
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

struct AppManifest
{
  std::string id;
  std::string module;
  std::string worker;
  std::string title;
  bool launchEditable = false;
  std::uint64_t memoryMiB = 64;
  std::uint64_t fuelPerCall = 10000000u;
  std::uint64_t deadlineMilliseconds = 1000u;
};

// app.json is generic package metadata: identity, member modules, window
// title, launch-document access and requested budgets. Product policy stays
// in the guest.
static bool
readManifest(const std::filesystem::path& path, AppManifest& manifest)
{
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  const std::streamoff size = input.tellg();
  if (!input || size <= 0 || size > 64 * 1024) {
    Logger::LogError("Missing or oversized package manifest app.json");
    return false;
  }
  std::string text(static_cast<std::size_t>(size), '\0');
  input.seekg(0);
  if (!input.read(text.data(), size)) {
    return false;
  }
  const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
  if (!document.is_object() || !document.contains("id") ||
      !document["id"].is_string() || !document.contains("module") ||
      !document["module"].is_string()) {
    Logger::LogError("Package manifest requires string id and module");
    return false;
  }
  manifest.id = document["id"].get<std::string>();
  manifest.module = document["module"].get<std::string>();
  const std::pair<const char*, std::string*> strings[] = {
    { "worker", &manifest.worker }, { "title", &manifest.title }
  };
  for (const std::pair<const char*, std::string*>& field : strings) {
    if (!document.contains(field.first)) {
      continue;
    }
    if (!document[field.first].is_string()) {
      Logger::LogError(std::string("Package manifest field ") + field.first +
                       " must be a string");
      return false;
    }
    *field.second = document[field.first].get<std::string>();
  }
  if (document.contains("launchAccess")) {
    const nlohmann::json& access = document["launchAccess"];
    if (!access.is_string() || (access != "read" && access != "edit")) {
      Logger::LogError("Package launchAccess must be \"read\" or \"edit\"");
      return false;
    }
    manifest.launchEditable = access == "edit";
  }
  // Requests are clamped to host ceilings; packages cannot raise them.
  const std::pair<const char*, std::uint64_t*> limits[] = {
    { "memoryMiB", &manifest.memoryMiB },
    { "fuelPerCall", &manifest.fuelPerCall },
    { "deadlineMilliseconds", &manifest.deadlineMilliseconds }
  };
  const std::uint64_t ceilings[] = { kMaximumMemoryMiB,
                                     kMaximumFuelPerCall,
                                     kMaximumDeadlineMilliseconds };
  for (std::size_t index = 0; index < 3; ++index) {
    if (!document.contains(limits[index].first)) {
      continue;
    }
    const nlohmann::json& value = document[limits[index].first];
    if (!value.is_number_unsigned() || value.get<std::uint64_t>() == 0) {
      Logger::LogError(std::string("Invalid package limit ") +
                       limits[index].first);
      return false;
    }
    *limits[index].second =
      std::min(value.get<std::uint64_t>(), ceilings[index]);
  }
  if (!packageId(manifest.id) || !packageMember(manifest.module) ||
      (!manifest.worker.empty() && !packageMember(manifest.worker)) ||
      manifest.title.size() > 128) {
    Logger::LogError("Package manifest names an invalid id, module or title");
    return false;
  }
  return true;
}

static const char* const kLaunchOptions[] = {
  "GuestModule",  "GuestMod",  "GuestWorker",    "GuestPackage",
  "GuestStorage", "GuestFuel", "GuestMemoryMiB", "GuestDeadline",
  "GuestApp",     "GuestOpen", "GuestCapture",   "GuestCaptureFrame"
};

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
  RuntimeModule(std::unique_ptr<WasmGameModule> guest,
                std::string title,
                std::string application,
                std::filesystem::path capture,
                std::uint64_t captureFrame)
    : m_guest(std::move(guest))
    , m_title(std::move(title))
    , m_application(std::move(application))
    , m_capture(std::move(capture))
    , m_captureFrame(captureFrame)
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
    if (!started) {
      Logger::LogError("The " + m_application +
                       " package failed to start: " + m_guest->error());
      if (!m_capture.empty()) {
        report(false, 0, 0, "The package failed to start: " + m_guest->error());
      }
    }
    return started;
  }
  void Update(double dt) override { m_guest->Update(dt); }
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
    m_guest->Exit();
  }
  bool OnCloseRequested() override
  {
    // A finished capture closes without product confirmation dialogs.
    return m_done || m_guest->OnCloseRequested();
  }

private:
  void captureFrame(Renderer& renderer)
  {
    m_done = true;
    const std::array<int, 2> size = ic->window->getWindowDimensions();
    const FrameReadback image =
      renderer.getBackend()->readBackbuffer(size[0], size[1]);
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
  std::string title = "Illumo Runtime";
  std::string application;
  if (!applicationOption.empty() &&
      (!gamePath.empty() || !packageOption.empty())) {
    Logger::LogError("--app selects an installed package; it cannot be "
                     "combined with --package or --game");
    return nullptr;
  }
  if (gamePath.empty()) {
    // Package launch: <exe>/apps/<name>, the game by default, unless
    // --package names another package directory.
    std::filesystem::path package;
    if (packageOption.empty()) {
      application =
        applicationOption.empty() ? kDefaultApplication : applicationOption;
      if (!packageId(application)) {
        Logger::LogError("Invalid application name: " + application);
        return nullptr;
      }
      package = runtimeDirectory() / "apps" / application;
      std::error_code error;
      if (!std::filesystem::is_directory(package, error)) {
        Logger::LogError("No installed application named '" + application +
                         "' in " + (runtimeDirectory() / "apps").string());
        return nullptr;
      }
    } else if (!readRoot(packageOption, package)) {
      return nullptr;
    }
    AppManifest manifest;
    if (!readManifest(package / "app.json", manifest)) {
      return nullptr;
    }
    if (application.empty()) {
      application = manifest.id;
    }
    if (!manifest.title.empty()) {
      title = manifest.title;
    }
    gamePath = package / manifest.module;
    if (workerPath.empty() && !manifest.worker.empty()) {
      workerPath = package / manifest.worker;
    }
    files.package = std::filesystem::absolute(package);
    files.launchEditable = manifest.launchEditable;
    if (storageOption.empty()) {
      // Default private storage beside the runtime, one directory per app.
      std::error_code error;
      files.storage = runtimeDirectory() / "storage" / manifest.id;
      std::filesystem::create_directories(files.storage, error);
      if (error) {
        Logger::LogError("Cannot create the package storage directory");
        return nullptr;
      }
    } else if (!readRoot(storageOption, files.storage)) {
      return nullptr;
    }
    limits.memoryBytes = manifest.memoryMiB * 1024u * 1024u;
    limits.fuelPerCall = manifest.fuelPerCall;
    limits.deadlineMilliseconds =
      static_cast<std::uint32_t>(manifest.deadlineMilliseconds);
  } else if (!readRoot(packageOption, files.package) ||
             !readRoot(storageOption, files.storage)) {
    return nullptr;
  }
  std::uint64_t memoryMiB = limits.memoryBytes / (1024u * 1024u);
  std::uint64_t deadline = limits.deadlineMilliseconds;
  std::uint64_t captureFrame = 60;
  if (!readLimit(environment->getVar("GuestMemoryMiB").value,
                 kMaximumMemoryMiB,
                 memoryMiB) ||
      !readLimit(environment->getVar("GuestFuel").value,
                 kMaximumFuelPerCall,
                 limits.fuelPerCall) ||
      !readLimit(environment->getVar("GuestDeadline").value,
                 kMaximumDeadlineMilliseconds,
                 deadline) ||
      !readLimit(environment->getVar("GuestCaptureFrame").value,
                 kMaximumCaptureFrame,
                 captureFrame)) {
    return nullptr;
  }
  limits.memoryBytes = memoryMiB * 1024u * 1024u;
  limits.deadlineMilliseconds = static_cast<std::uint32_t>(deadline);
  // The launch document: granted by the host, described to the guest by its
  // base name only.
  std::vector<std::byte> startup;
  if (!openOption.empty()) {
    if (files.package.empty() && files.storage.empty()) {
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
  std::vector<std::byte> game = readModule(gamePath);
  std::vector<std::byte> mod = readModule(modPath);
  std::vector<std::byte> worker = readModule(workerPath);
  if (game.empty() || (!modPath.empty() && mod.empty()) ||
      (!workerPath.empty() && worker.empty())) {
    Logger::LogError("A requested WASM module is missing or unreadable");
    return nullptr;
  }
  std::unique_ptr<WasmGameModule> guest =
    std::make_unique<WasmGameModule>(std::move(game),
                                     std::move(startup),
                                     limits,
                                     std::move(mod),
                                     std::move(worker),
                                     std::move(files));
  return std::make_unique<RuntimeModule>(std::move(guest),
                                         std::move(title),
                                         std::move(application),
                                         std::move(capture),
                                         captureFrame);
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
    "apps/<name>/ beside the runtime; the game runs by default.";
  application.commandLine.usage =
    "IllumoRuntime.exe [--app name] [--open file] [--capture out.png "
    "[--capture-frame n]] [--package dir] [--storage dir] "
    "[--game module.wasm] [--mod module.wasm] [--worker module.wasm] "
    "[--memory-mib n] [--fuel n] [--deadline-ms n]";
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
    { "--package",
      "path",
      "GuestPackage",
      "Run the package in this directory instead of an installed application" },
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
    { "--fuel", "count", "GuestFuel", "Fuel budget per call" },
    { "--deadline-ms",
      "milliseconds",
      "GuestDeadline",
      "Wall-clock deadline per call" }
  };
  application.applyDefaults = prepareRuntime;
  application.createRequiredModule = createGuestModule;
  application.exitCode = runtimeExitCode;
  return application;
}
