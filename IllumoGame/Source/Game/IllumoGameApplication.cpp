#include "CellGameModule.h"
#include "IllumoGameConfig.h"
#include "MainMenuModule.h"

#include <Illumo/Engine/Application.h>
#include <Illumo/Engine/IModule.h>
#include <Illumo/Services/IEnvVars.h>
#include <memory>

static std::unique_ptr<IModule>
createIllumoGameModule(IEnvVars* environment)
{
  if (environment != nullptr &&
      environment->getVar("LaunchDirect").valueAsBool) {
    return std::make_unique<CellGameModule>();
  }
  return std::make_unique<MainMenuModule>();
}

IllumoApplicationDefinition
CreateIllumoApplication()
{
  IllumoApplicationDefinition application;
  application.applicationName = "IllumoGame";
  application.commandLine.applicationName = application.applicationName;
  application.commandLine.description = "Cell Automata Simulator";
  application.commandLine.usage =
    "IllumoGame.exe RULESET [OPTION] ... [FILE] ...";
  application.commandLine.applicationOptions = {
    { "-cw", "cells", "CanvasX", "Cell canvas width" },
    { "-ch", "cells", "CanvasY", "Cell canvas height" },
  };
  application.commandLine.helpSections = {
    "Rulesets:\n"
    "GAME_OF_LIFE\t\t Conway's Game of Life\n"
    "BRIANS_BRAIN\t\t Brian's Brain\n"
    "LIFE_WITHOUT_DEATH\t Life Without Death\n"
    "HIGHLIFE\t\t HighLife\n"
    "SEEDS\t\t\t Seeds\n"
    "DAY_AND_NIGHT\t\t Day & Night\n"
    "WIREWORLD\t\t Wireworld\n",
  };
  application.applyDefaults = IllumoGameConfig::ApplyDefaults;
  application.createRequiredModule = createIllumoGameModule;
  return application;
}
