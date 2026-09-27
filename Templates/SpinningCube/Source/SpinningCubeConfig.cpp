#include "SpinningCubeConfig.h"

#include <Illumo/Services/IEnvVars.h>

const std::string&
SpinningCubeConfig::applicationName()
{
  static const std::string name = "@PROJECT_NAME@";
  return name;
}

void
SpinningCubeConfig::ApplyDefaults(IEnvVars* environment)
{
  if (environment == nullptr) {
    return;
  }

  struct DefaultValue
  {
    const char* name;
    const char* value;
  };
  // Settings the cube scene reads; the packaged envvars.json seeds them too.
  const DefaultValue defaults[] = {
    { "cubeRotationSpeed", "1.2" },
    { "showGrid", "1" },
  };
  for (const DefaultValue& defaultValue : defaults) {
    if (environment->getVar(defaultValue.name).value.empty()) {
      environment->setVar(defaultValue.name, defaultValue.value);
    }
  }
}
