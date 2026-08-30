#include "MeshViewerConfig.h"
#include "MeshViewerModule.h"

#include <Illumo/Engine/Application.h>
#include <Illumo/Engine/IModule.h>
#include <Illumo/Services/IEnvVars.h>
#include <memory>

static std::unique_ptr<IModule>
createIllMeshViewerModule(IEnvVars* environment)
{
  std::string path;
  if (environment != nullptr) {
    path = environment->getVar("LaunchMesh").value;
  }
  return std::make_unique<MeshViewerModule>(path);
}

IllumoApplicationDefinition
CreateIllumoApplication()
{
  IllumoApplicationDefinition application;
  application.applicationName = MeshViewerConfig::applicationName();
  application.commandLine.applicationName = application.applicationName;
  application.commandLine.description = "3D Mesh Viewer";
  application.commandLine.usage = "IllMeshViewer.exe [OPTION] ... [FILE]";
  application.commandLine.positionalEnvironmentVariable = "LaunchMesh";
  application.commandLine.applicationOptions = {
    { "-m", "path", "LaunchMesh", "Mesh file to view (.obj)" },
    { "--mesh", "path", "LaunchMesh", "Mesh file to view (.obj)" },
  };
  application.commandLine.helpSections = {
    "Controls:\n"
    "  LMB / RMB Drag\t Orbit / Rotate camera\n"
    "  MMB / Shift+Drag\t Pan camera\n"
    "  WASD / Arrow Keys\t Pan camera\n"
    "  Scroll Wheel\t\t Zoom camera\n"
    "  Q / E / Alt+Drag\t Roll / Tilt camera\n"
    "  O\t\t\t Open Mesh file dialog\n"
    "  R / F\t\t\t Reset / Frame camera view\n"
    "  G\t\t\t Toggle 3D reference grid\n"
    "  X\t\t\t Toggle wireframe / bounding box\n",
  };
  application.applyDefaults = MeshViewerConfig::ApplyDefaults;
  application.createRequiredModule = createIllMeshViewerModule;
  return application;
}
