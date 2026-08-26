#include "IllumoGameConfig.h"

#include <Illumo/Services/IEnvVars.h>

void
IllumoGameConfig::ApplyDefaults(IEnvVars* environment)
{
  if (environment == nullptr) {
    return;
  }

  struct DefaultValue
  {
    const char* name;
    const char* value;
  };
  const DefaultValue defaults[] = {
    { "CanvasX", "80" },
    { "CanvasY", "60" },
    { "ModeString", "GAME_OF_LIFE" },
    { "WorldChunksX", "0" },
    { "WorldChunksY", "0" },
    { "speedFactor", "1" },
    { "tps", "30" },
    { "cellFadeSpeed", "8" },
    { "uiScale", "1" },
    { "msaa", "4" },
  };
  for (const DefaultValue& defaultValue : defaults) {
    if (environment->getVar(defaultValue.name).value.empty()) {
      environment->setVar(defaultValue.name, defaultValue.value);
    }
  }
}
