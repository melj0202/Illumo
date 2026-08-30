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
    { "uiScale", "1" },  { "fontSize", "13" },  { "msaa", "4" },
    { "vsync", "1" },    { "fullscreen", "0" }, { "LaunchMesh", "" },
    { "showGrid", "1" }, { "showAxes", "1" },   { "showWireframe", "0" },
  };
  for (const DefaultValue& defaultValue : defaults) {
    if (environment->getVar(defaultValue.name).value.empty()) {
      environment->setVar(defaultValue.name, defaultValue.value);
    }
  }
}
