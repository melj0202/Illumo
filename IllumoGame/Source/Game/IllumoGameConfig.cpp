#include "IllumoGameConfig.h"

#include <Illumo/Services/IEnvVars.h>

#include <string>

void
IllumoGameConfig::ApplyDefaults(IEnvVars* environment)
{
  if (environment == nullptr) {
    return;
  }

  // A pre-family configuration may contain only ModeString. Seed the new
  // ruleset key from that alias before filling defaults so it keeps its value.
  if (environment->getVar("RuleSetString").value.empty()) {
    const std::string legacyRule = environment->getVar("ModeString").value;
    if (!legacyRule.empty()) {
      environment->setVar("RuleSetString", legacyRule);
    }
  }

  struct DefaultValue
  {
    const char* name;
    const char* value;
  };
  const DefaultValue defaults[] = {
    { "CanvasX", "80" },
    { "CanvasY", "60" },
    { "FamilyString", "LIFE_LIKE_BINARY" },
    { "RuleSetString", "GAME_OF_LIFE" },
    { "ModeString", "GAME_OF_LIFE" },
    { "WorldChunksX", "0" },
    { "WorldChunksY", "0" },
    { "speedFactor", "1" },
    { "tps", "30" },
    { "cellFadeSpeed", "8" },
    { "uiScale", "1" },
    { "msaa", "4" },
    { "reducedUiMotion", "0" },
    { "showInspector", "0" },
    { "editHints", "1" },
  };
  for (const DefaultValue& defaultValue : defaults) {
    if (environment->getVar(defaultValue.name).value.empty()) {
      environment->setVar(defaultValue.name, defaultValue.value);
    }
  }
}
