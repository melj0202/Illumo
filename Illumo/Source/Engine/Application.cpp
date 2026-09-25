#include <Illumo/Engine/Application.h>

#include <Illumo/Engine/IModule.h>
#include <Illumo/Engine/Illumo.h>
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
#include <Illumo/Engine/DebugModule.h>
#endif
#include <Illumo/Engine/PresentationTiming.h>
#include <Illumo/Foundation/BuildInfo.h>
#include <Illumo/Platform/PlatformTimer.h>
#include <Illumo/Platform/ProcessRelaunch.h>
#include <Illumo/Platform/SystemInfo.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/Logger.h>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <tracy/Tracy.hpp>
#include <utility>

static const char*
buildConfiguration()
{
#if !defined(NDEBUG)
  return "Debug";
#elif defined(ILLUMO_ENABLE_DEBUG_TOOLS)
  return "RelWithDebInfo";
#else
  return "Release";
#endif
}

static std::string
compilerDescription()
{
#if defined(__clang__)
  return std::string("Clang ") + __clang_version__;
#elif defined(_MSC_VER)
  // _MSC_FULL_VER is MMmmBBBBB: 195136257 reads as 19.51.36257.
  return "MSVC " + std::to_string(_MSC_VER / 100) + "." +
         std::to_string(_MSC_VER % 100) + "." +
         std::to_string(_MSC_FULL_VER % 100000);
#elif defined(__GNUC__)
  return "GCC " + std::to_string(__GNUC__) + "." +
         std::to_string(__GNUC_MINOR__);
#else
  return "an unknown compiler";
#endif
}

static std::string
gibibytes(std::uint64_t bytes)
{
  char text[32] = {};
  std::snprintf(text,
                sizeof(text),
                "%.1f GiB",
                static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
  return text;
}

// One block at startup describing the build and the machine, so every log
// and bug report starts with the environment it came from.
static void
logStartupReport(const std::string& applicationName)
{
  Logger::LogInfo(applicationName + " starting: Illumo " +
                  BuildInfo::VersionNumber + ", " + buildConfiguration() +
                  " build of " + __DATE__ + " " + __TIME__ + " (" +
                  compilerDescription() + ")");
  const SystemInfo system = QuerySystemInfo();
  std::string machine = "Operating system: " + (system.operatingSystem.empty()
                                                  ? std::string("unknown")
                                                  : system.operatingSystem);
  if (!system.architecture.empty()) {
    machine += ", " + system.architecture;
  }
  Logger::LogInfo(machine);
  std::string processor =
    "CPU: " + (system.cpuName.empty() ? std::string("unknown processor")
                                      : system.cpuName);
  if (system.physicalCores != 0) {
    processor += ", " + std::to_string(system.physicalCores) + " cores";
  }
  if (system.logicalProcessors != 0) {
    processor += ", " + std::to_string(system.logicalProcessors) + " threads";
  }
  Logger::LogInfo(processor);
  if (system.totalMemoryBytes != 0) {
    Logger::LogInfo("Memory: " + gibibytes(system.totalMemoryBytes) +
                    " total, " + gibibytes(system.availableMemoryBytes) +
                    " available");
  }
  const std::filesystem::path logFile = Logger::getLogFilePath();
  if (!logFile.empty()) {
    Logger::LogInfo("Log file: " + logFile.string());
  }
  Logger::LogInfo("Settings file: " +
                  EnvVars::ApplicationConfigPath().string());
}

class ApplicationLoggerLifetime
{
public:
  ApplicationLoggerLifetime() { Logger::initLogger(); }
  ~ApplicationLoggerLifetime() { Logger::shutdownLogger(); }

  ApplicationLoggerLifetime(const ApplicationLoggerLifetime&) = delete;
  ApplicationLoggerLifetime& operator=(const ApplicationLoggerLifetime&) =
    delete;
  ApplicationLoggerLifetime(ApplicationLoggerLifetime&&) = delete;
  ApplicationLoggerLifetime& operator=(ApplicationLoggerLifetime&&) = delete;
};

// The whole run, logger lifetime included. *restart reports that the
// application asked to start again after this normal shutdown.
static int
runApplication(int argc,
               char** argv,
               IllumoApplicationDefinition application,
               bool* restart)
{
  *restart = false;
  const std::chrono::steady_clock::time_point launched =
    std::chrono::steady_clock::now();
  ApplicationLoggerLifetime loggerLifetime;
  PlatformTimerScope timerScope;
  try {
    if (application.applicationName.empty()) {
      application.applicationName = "Illumo";
    }
    if (application.commandLine.applicationName.empty() ||
        application.commandLine.applicationName == "Illumo") {
      application.commandLine.applicationName = application.applicationName;
    }

    IllumoConfig config;
    config.applicationName = application.applicationName;
    config.environmentPath = EnvVars::ApplicationConfigPath().string();
    Illumo illumo(config);
    if (application.applyDefaults != nullptr) {
      application.applyDefaults(&illumo.environment());
    }

    const SysCmdLineResult commandLineResult = SysCmdLine::ParseCommandLine(
      argc, argv, &illumo.environment(), application.commandLine);
    if (commandLineResult.shouldExit()) {
      return commandLineResult.exitCode();
    }
    logStartupReport(application.applicationName);

    if (!illumo.initialize()) {
      Logger::LogError(application.applicationName +
                       " could not initialize Illumo");
      return 1;
    }
    if (application.createRequiredModule == nullptr) {
      Logger::LogError(application.applicationName +
                       " did not provide a required module factory");
      illumo.shutdown();
      return 1;
    }
    std::unique_ptr<IModule> requiredModule =
      application.createRequiredModule(&illumo.environment());
    if (!requiredModule) {
      Logger::LogError(application.applicationName +
                       " returned an empty required module");
      illumo.shutdown();
      return 1;
    }
    illumo.addModule(std::move(requiredModule), ModuleRequirement::Required);
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
    illumo.addModule(std::make_unique<DebugModule>(&illumo.frameProfiler()),
                     ModuleRequirement::Optional);
#endif
    if (!illumo.startModules()) {
      Logger::LogError(application.applicationName +
                       " could not start its required module");
      illumo.shutdown();
      return 1;
    }
    Logger::LogInfo(
      application.applicationName + " ready in " +
      std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - launched)
                       .count()) +
      " ms");

    FramePacer framePacer;
    std::chrono::steady_clock::time_point lastTime =
      std::chrono::steady_clock::now();
    while (!illumo.processCloseRequest()) {
      illumo.frameProfiler().beginFrame();
      FrameMark;
      const std::chrono::steady_clock::time_point currentTime =
        std::chrono::steady_clock::now();
      const double dt =
        std::chrono::duration<double>(currentTime - lastTime).count();
      lastTime = currentTime;

      {
        ZoneScopedN("Frame.Update");
        illumo.update(dt);
      }
      {
        ZoneScopedN("Frame.Render");
        illumo.render();
      }

      {
        ZoneScopedN("Frame.Pacing");
        illumo.frameProfiler().mark(FramePhase::Pacing);
        const long targetFps = getTargetFps(&illumo.environment());
        const bool vsyncEnabled = isVsyncRequested(&illumo.environment());
        const int refreshRate = illumo.context().window != nullptr
                                  ? illumo.context().window->getRefreshRate()
                                  : 60;
        framePacer.pace(targetFps, vsyncEnabled, refreshRate);
      }
      illumo.frameProfiler().endFrame();
    }

    *restart = illumo.context().window != nullptr &&
               illumo.context().window->restartRequested();
    Logger::LogInfo(
      application.applicationName +
      (*restart ? " shutting down to restart" : " shutting down"));
    illumo.shutdown();
    const int exitCode =
      application.exitCode != nullptr ? application.exitCode() : 0;
    Logger::LogInfo(application.applicationName + " exited with code " +
                    std::to_string(exitCode));
    return exitCode;
  } catch (const std::exception& exception) {
    Logger::LogError(std::string("Illumo application failed: ") +
                     exception.what());
  } catch (...) {
    Logger::LogError("Illumo application failed with an unknown error");
  }
  *restart = false;
  return 1;
}

int
RunIllumoApplication(int argc,
                     char** argv,
                     IllumoApplicationDefinition application)
{
  bool restart = false;
  const int exitCode =
    runApplication(argc, argv, std::move(application), &restart);
  // The new copy starts only once this one has released its window, logger
  // and settings file.
  if (restart) {
    std::string error;
    if (!RelaunchCurrentProcess(argc, argv, &error)) {
      std::fprintf(stderr, "Restart failed: %s\n", error.c_str());
    }
  }
  return exitCode;
}
