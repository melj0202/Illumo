#include "SpinningCubeConfig.h"
#include "SpinningCubeModule.h"

#include <Illumo/Engine/Application.h>
#include <Illumo/Engine/IModule.h>
#include <Illumo/Services/IEnvVars.h>
#include <memory>

static std::unique_ptr<IModule>
createSpinningCubeModule(IEnvVars* /*environment*/)
{
  return std::make_unique<SpinningCubeModule>();
}

IllumoApplicationDefinition
CreateIllumoApplication()
{
  IllumoApplicationDefinition application;
  application.applicationName = SpinningCubeConfig::applicationName();
  application.commandLine.applicationName = application.applicationName;
  application.commandLine.description =
    "Illumo Spinning Cube Starter Application";
  application.commandLine.usage = "@PROJECT_NAME@.exe [OPTION] ...";
  application.commandLine.applicationOptions = {
    { "--speed",
      "float",
      "cubeRotationSpeed",
      "Initial rotation speed of the cube" },
  };
  application.commandLine.helpSections = {
    "Controls:\n"
    "  Space\t\t Toggle rotation pause\n"
    "  R\t\t Reset cube rotation\n"
    "  G\t\t Toggle spatial grid\n"
    "  Up / Down\t Increase / decrease rotation speed\n"
    "  Escape\t Quit application\n",
  };
  application.applyDefaults = SpinningCubeConfig::ApplyDefaults;
  application.createRequiredModule = createSpinningCubeModule;
  return application;
}
