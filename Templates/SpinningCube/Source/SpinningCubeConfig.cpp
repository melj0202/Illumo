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
  const DefaultValue defaults[] = {
    { "windowWidth", "1280" },
    { "windowHeight", "720" },
    { "windowTitle", "@PROJECT_NAME@ - Spinning Cube" },
    { "vsync", "1" },
    { "cubeRotationSpeed", "1.2" },
    { "lightingEnabled", "1" },
    { "showGrid", "1" },
    { "lightDirX", "0.6" },
    { "lightDirY", "1.0" },
    { "lightDirZ", "0.8" },
    { "lightColorR", "1.0" },
    { "lightColorG", "0.98" },
    { "lightColorB", "0.92" },
    { "ambientColorR", "0.25" },
    { "ambientColorG", "0.28" },
    { "ambientColorB", "0.35" },
  };
  for (const DefaultValue& defaultValue : defaults) {
    if (environment->getVar(defaultValue.name).value.empty()) {
      environment->setVar(defaultValue.name, defaultValue.value);
    }
  }
}
