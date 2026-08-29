#include "EditorModule.h"
#include "IllEdConfig.h"

#include <Illumo/Engine/Application.h>
#include <Illumo/Engine/IModule.h>
#include <Illumo/Services/IEnvVars.h>
#include <memory>

static std::unique_ptr<IModule>
createIllEdModule(IEnvVars* environment)
{
  std::string path;
  if (environment != nullptr) {
    path = environment->getVar("LaunchScene").value;
  }
  return std::make_unique<EditorModule>(path);
}

IllumoApplicationDefinition
CreateIllumoApplication()
{
  IllumoApplicationDefinition application;
  application.applicationName = IllEdConfig::applicationName();
  application.commandLine.applicationName = application.applicationName;
  application.commandLine.description = "Illumo world editor";
  application.commandLine.usage = "IllEd.exe [FILE]";
  application.commandLine.helpSections = {
    "IllEd authors SceneGraph documents and writes .ilsc files.\n"
    "Open a scene through File > Open or set LaunchScene in envvars.json.\n",
  };
  application.applyDefaults = IllEdConfig::ApplyDefaults;
  application.createRequiredModule = createIllEdModule;
  return application;
}
