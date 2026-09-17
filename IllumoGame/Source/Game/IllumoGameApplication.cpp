#include "CellGameModule.h"
#include "IllumoGameConfig.h"
#include "MainMenuModule.h"
#include "RuleCatalogLoader.h"
#include "Rulesets/RuleSetRegistry.h"

#include <Illumo/Engine/Application.h>
#include <Illumo/Engine/IModule.h>
#include <Illumo/Services/IEnvVars.h>
#include <memory>

static std::unique_ptr<IModule>
createIllumoGameModule(IEnvVars* environment)
{
  // Catalog policy is initialized once, before either product module reads
  // modes.
  static const bool catalogLoaded =
    RuleCatalogLoader::loadFromDefaultLocations(RuleSetRegistry::instance());
  if (!catalogLoaded) {
    return nullptr;
  }
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
  application.applicationName = "CSim";
  application.commandLine.applicationName = application.applicationName;
  application.commandLine.description = "Cell Automata Simulator";
  application.commandLine.usage = "CSim.exe RULESET [OPTION] ... [FILE] ...";
  application.commandLine.applicationOptions = {
    { "-cw", "cells", "CanvasX", "Cell canvas width" },
    { "-ch", "cells", "CanvasY", "Cell canvas height" },
  };
  application.commandLine.helpSections = {
    "Built-in rulesets (catalogs may add more IDs):\n"
    "GAME_OF_LIFE\t\t Conway's Game of Life\n"
    "BRIANS_BRAIN\t\t Brian's Brain\n"
    "LIFE_WITHOUT_DEATH\t Life Without Death\n"
    "HIGHLIFE\t\t HighLife\n"
    "SEEDS\t\t\t Seeds\n"
    "DAY_AND_NIGHT\t\t Day & Night\n"
    "WIREWORLD\t\t Wireworld\n"
    "RULE_90 / RULE_184\t Elementary 1D rules\n"
    "Define families in families.json and rules in rulesets.json; custom "
    "definitions are saved in families.user.json and rulesets.user.json. "
    "Press F2 in the canvas to edit a rule.\n",
  };
  application.applyDefaults = IllumoGameConfig::ApplyDefaults;
  application.createRequiredModule = createIllumoGameModule;
  return application;
}
