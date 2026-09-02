#include "MeshViewerConfig.h"

#include <Illumo/Services/IEnvVars.h>

const std::string&
MeshViewerConfig::applicationName()
{
  static const std::string name = "IllMeshViewer";
  return name;
}

void
MeshViewerConfig::ApplyDefaults(IEnvVars* environment)
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
    { "fontSize", "13" },
    { "msaa", "4" },
    { "vsync", "1" },
    { "fullscreen", "0" },
    { "LaunchMesh", "" },
    { "showGrid", "1" },
    { "showAxes", "1" },
    { "showWireframe", "0" },
    { "lightingEnabled", "1" },
    { "lightDirX", "0.5" },
    { "lightDirY", "1" },
    { "lightDirZ", "0.3" },
    { "lightColorR", "1" },
    { "lightColorG", "0.95" },
    { "lightColorB", "0.9" },
    { "ambientColorR", "0.2" },
    { "ambientColorG", "0.22" },
    { "ambientColorB", "0.25" },
    { "shadowsEnabled", "1" },
    { "shadowMapSize", "1024" },
    { "shadowRadius", "2.5" },
    { "lightDistance", "8" },
    { "shadowBias", "0.001" },
    { "shadowSlopeScale", "0.004" },
    { "shadowNormalOffset", "0.015" },
    { "shadowPcf", "1" },
    { "motionBlurEnabled", "1" },
    { "motionBlurAmount", "0.5" },
    { "motionBlurMax", "0.2" },
  };
  for (const DefaultValue& defaultValue : defaults) {
    if (environment->getVar(defaultValue.name).value.empty()) {
      environment->setVar(defaultValue.name, defaultValue.value);
    }
  }
}
