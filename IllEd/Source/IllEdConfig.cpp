#include "IllEdConfig.h"

#include <Illumo/Services/IEnvVars.h>

void
IllEdConfig::ApplyDefaults(IEnvVars* environment)
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
    { "uiScale", "1" },
    { "msaa", "4" },
    { "vsync", "1" },
    { "fullscreen", "0" },
  };
  for (const DefaultValue& defaultValue : defaults) {
    if (environment->getVar(defaultValue.name).value.empty()) {
      environment->setVar(defaultValue.name, defaultValue.value);
    }
  }
}
