#include <Illumo/Audio/AudioDevice.h>
#include <Illumo/Content/EnginePackage.h>
#include <Illumo/Content/PackageMounts.h>
#include <Illumo/Engine/Application.h>
#include <Illumo/Engine/Illumo.h>
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Gui/GuiEngineBrand.h>
#include <Illumo/Rendering/WindowIcon.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Wasm/RuntimeAppLauncher.h>
#include <Illumo/Wasm/RuntimeShell.h>
#include <Illumo/Wasm/WasmProgram.h>
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
static constexpr std::uint64_t kMaximumBenchFrames =
  RuntimeBench::kMaximumFrames;
static constexpr const char* kDefaultApplication = "game";

// The directory the runtime was started from. The runtime then works from its
// own directory, where the engine's shaders and assets are staged, so
// relative command-line paths are resolved against this one instead.
static std::filesystem::path s_invocationDirectory;

// A distribution's engine package (engine.ilpk, D-E38), opened before the
// engine starts and kept for the process: host shaders, fonts and textures
// read through its source, and guests see it at /engine. Both stay null in a
// development build, whose loose Assets/ and Shader/ win.
static std::shared_ptr<EngineArchiveBackend> s_enginePackage;
static std::unique_ptr<EnginePackageSource> s_engineSource;
// Why a present engine.ilpk could not be used; the launch then fails.
static std::string s_enginePackageError;

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
  ILLUMO_PROFILE_ZONE("Runtime.readModule");
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
static std::vector<uint8_t>
readPackageFile(const VirtualFileSystem& vfs,
                const std::string& member,
                std::uint64_t maximumBytes)
{
  const std::string path = "/app/" + member;
  VfsStat stat;
  std::string error;
  std::vector<uint8_t> data;
  if (!vfs.stat(path, stat, error) || stat.kind != VfsKind::File ||
      stat.size == 0 || stat.size > maximumBytes ||
      !vfs.read(path, data, error)) {
    return {};
  }
  return data;
}

static std::vector<std::byte>
readPackageModule(const VirtualFileSystem& vfs, const std::string& member)
{
  ILLUMO_PROFILE_ZONE("Runtime.readPackageModule");
  const std::vector<uint8_t> data =
    readPackageFile(vfs, member, 64u * 1024u * 1024u);
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
  ILLUMO_PROFILE_ZONE("Runtime.prepareRuntime");
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
  if (!s_engineSource && s_enginePackageError.empty()) {
    s_enginePackage = EnginePackage::open(
      runtimeDirectory(), manifestCeilings(), s_enginePackageError);
    if (s_enginePackage) {
      // The file source taken before this one replaces it serves every name
      // the package does not hold.
      s_engineSource = std::make_unique<EnginePackageSource>(
        s_enginePackage, runtimeDirectory(), DefaultAssetSource());
      SetDefaultAssetSource(s_engineSource.get());
      Logger::LogInfo(std::string("Engine files from ") +
                      EnginePackage::kFileName);
    } else if (!s_enginePackageError.empty()) {
      Logger::LogError("Cannot use " + std::string(EnginePackage::kFileName) +
                       ": " + s_enginePackageError);
    }
  }
  clearLaunchOptions(environment);
}

// The program a launch asks for, with its shell options; null (logged) when
// the options or the package are unusable.
static std::unique_ptr<RuntimeShell>
prepareShell(Illumo& illumo)
{
  ILLUMO_PROFILE_ZONE("Runtime.prepareShell");
  if (!s_enginePackageError.empty()) {
    // Logged when it was opened; a broken distribution must not half-run.
    return nullptr;
  }
  IEnvVars* environment = &illumo.environment();
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
  // The package's app.ico, when it has one; the window shows it instead of
  // the engine's icon.
  std::vector<uint8_t> appIcon;
  bool workerRequested = !workerPath.empty();
  // An application with launchApps: the installed applications it may start
  // and the package options each launch repeats.
  bool launchApps = false;
  std::vector<std::string> launchable;
  std::vector<std::string> packageArguments;
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
      std::error_code absolute;
      const std::u8string mountPath =
        std::filesystem::absolute(optionPath(mountOption), absolute).u8string();
      packageArguments.push_back("--mount");
      packageArguments.push_back(std::string(
        reinterpret_cast<const char*>(mountPath.data()), mountPath.size()));
    }
    std::filesystem::path project;
    if (!readRoot(environment->getVar("GuestProject").value, project)) {
      Logger::LogError("--project names no directory");
      return nullptr;
    }
    std::shared_ptr<VirtualFileSystem> vfs =
      std::make_shared<VirtualFileSystem>();
    // /engine: the engine package when the runtime was shipped with one,
    // otherwise the loose Assets/ directory a development build stages.
    std::shared_ptr<IVfsBackend> engine = s_enginePackage;
    if (!engine) {
      std::string engineError;
      engine = DirectoryVfsBackend::open(
        runtimeDirectory() / "Assets", false, engineError);
      if (!engine) {
        warnings.push_back("No engine assets to mount: " + engineError);
      }
    }
    if (!PackageMounts::mountAll(*vfs,
                                 package,
                                 packages,
                                 std::move(engine),
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
      const std::u8string projectPath = project.u8string();
      packageArguments.push_back("--project");
      packageArguments.push_back(std::string(
        reinterpret_cast<const char*>(projectPath.data()), projectPath.size()));
    }
    if (manifest.app.launchApps) {
      // Every installed application, read-only at /apps/<id>, so the
      // launcher (an editor) can read their behaviour schemas.
      launchApps = true;
      std::vector<std::string> seen;
      std::vector<std::string> appWarnings;
      const std::vector<LoadedPackage> installed = PackageMounts::discover(
        runtimeDirectory() / "apps", ceilings, seen, appWarnings);
      if (!PackageMounts::mountApplications(*vfs, installed, error)) {
        Logger::LogError("Cannot mount the installed applications: " + error);
        return nullptr;
      }
      for (const LoadedPackage& installedApp : installed) {
        if (installedApp.manifest.kind == PackageKind::App) {
          launchable.push_back(installedApp.manifest.id);
        }
      }
      for (const std::string& warning : appWarnings) {
        Logger::LogWarning(warning);
      }
      Logger::LogInfo(std::to_string(launchable.size()) +
                      " installed applications mounted at /apps");
    }
    game = readPackageModule(*vfs, manifest.app.module);
    appIcon = readPackageFile(*vfs, "app.ico", WindowIcon::kMaximumBytes);
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
  RuntimeBench bench;
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
  std::unique_ptr<WasmProgram> guest =
    std::make_unique<WasmProgram>(std::move(game),
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
    ILLUMO_PROFILE_ZONE("Runtime.createAudio");
    std::string audioError;
    audio = AudioDevice::create({}, audioError);
    if (!audio) {
      Logger::LogWarning("Audio disabled: " + audioError);
    }
    guest->setAudio(audio.get());
  } else {
    Logger::LogTrace("Audio disabled for capture and benchmark runs");
  }
  // An application allowed to launch others does so in child processes;
  // captures and benchmarks stay one process.
  if (launchApps && capture.empty() && bench.frames == 0) {
    const std::filesystem::path play =
      std::filesystem::temp_directory_path() /
      ("illumo-play-" + application + "-" +
       std::to_string(
         std::chrono::steady_clock::now().time_since_epoch().count()));
    guest->setAppLauncher(std::make_unique<RuntimeAppLauncher>(
      std::move(launchable), play, std::move(packageArguments)));
  }
  // An application may ask to relaunch (settings read at startup); a capture
  // or benchmark run closes instead so its caller sees one process.
  guest->setRestartAllowed(capture.empty() && bench.frames == 0);
  RuntimeShellOptions options;
  options.title = std::move(title);
  options.appIcon = std::move(appIcon);
  options.application = std::move(application);
  options.capture = std::move(capture);
  options.captureFrame = captureFrame;
  // Every interactive launch opens on the engine splash; capture and
  // benchmark runs start straight into the program.
  if (options.capture.empty() && bench.frames == 0) {
    options.splashImage =
      runtimeDirectory() / "Assets" / GuiEngineSplash::kAssetName;
  }
  options.bench = std::move(bench);
  return std::make_unique<RuntimeShell>(
    illumo, std::move(audio), std::move(guest), std::move(options));
}

// The runtime's loop: the launch's program under a RuntimeShell.
static int
runRuntime(Illumo& illumo, std::chrono::steady_clock::time_point launched)
{
  IEnvVars* environment = &illumo.environment();
  std::unique_ptr<RuntimeShell> shell = prepareShell(illumo);
  const std::string capture = environment->getVar("GuestCapture").value;
  if (!shell && !capture.empty()) {
    // Capture callers read one JSON result line whatever the outcome.
    nlohmann::json result;
    result["success"] = false;
    result["application"] = environment->getVar("GuestApp").value;
    result["output"] = capture;
    result["error"] =
      "The requested application could not be prepared; see the log";
    std::cout << result.dump(
                   -1, ' ', false, nlohmann::json::error_handler_t::replace)
              << std::endl;
  }
  clearLaunchOptions(environment);
  if (!shell) {
    Logger::LogError(illumo.applicationName() +
                     " could not prepare the requested application");
    return 1;
  }
  return shell->run(launched);
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
      "Console lines, @key and @wait run before --capture-frame counts "
      "(from @begin, if present)" },
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
      "Console lines, @key and @wait run before warm-up and timing start "
      "(from @begin, if present, with later lines still running)" }
  };
  application.applyDefaults = prepareRuntime;
  application.run = runRuntime;
  return application;
}
