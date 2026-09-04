#include <Illumo/Engine/Application.h>

#include <Illumo/Engine/IModule.h>
#include <Illumo/Engine/Illumo.h>
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
#include <Illumo/Engine/DebugModule.h>
#endif
#include <Illumo/Engine/PresentationTiming.h>
#include <Illumo/Platform/PlatformTimer.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/Logger.h>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <tracy/Tracy.hpp>
#include <utility>

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

int
RunIllumoApplication(int argc,
                     char** argv,
                     IllumoApplicationDefinition application)
{
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
    illumo.addModule(std::make_unique<DebugModule>(),
                     ModuleRequirement::Optional);
#endif
    if (!illumo.startModules()) {
      Logger::LogError(application.applicationName +
                       " could not start its required module");
      illumo.shutdown();
      return 1;
    }

    FramePacer framePacer;
    std::chrono::steady_clock::time_point lastTime =
      std::chrono::steady_clock::now();
    while (!illumo.shouldClose()) {
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
        const long targetFps = getTargetFps(&illumo.environment());
        const bool vsyncEnabled = isVsyncRequested(&illumo.environment());
        const int refreshRate = illumo.context().window != nullptr
                                  ? illumo.context().window->getRefreshRate()
                                  : 60;
        framePacer.pace(targetFps, vsyncEnabled, refreshRate);
      }
    }

    illumo.shutdown();
    Logger::LogTrace(application.applicationName + " main loop finished");
    return 0;
  } catch (const std::exception& exception) {
    Logger::LogError(std::string("Illumo application failed: ") +
                     exception.what());
  } catch (...) {
    Logger::LogError("Illumo application failed with an unknown error");
  }
  return 1;
}
