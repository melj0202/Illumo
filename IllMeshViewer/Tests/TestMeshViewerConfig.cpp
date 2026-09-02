#include "MeshViewerConfig.h"
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <filesystem>

static TestCounters g;

static std::filesystem::path
environmentPath(const char* name)
{
  return std::filesystem::temp_directory_path() /
         (std::string("illmeshviewer-config-") + name + ".json");
}

static void
testConfigDefaults()
{
  testSection("MeshViewerConfig: application name and defaults");
  testTrue(g,
           MeshViewerConfig::applicationName() == "IllMeshViewer",
           "application name matches");

  const std::filesystem::path path = environmentPath("defaults");
  std::error_code error;
  std::filesystem::remove(path, error);
  {
    EnvVars env(path);
    MeshViewerConfig::ApplyDefaults(&env);
    testTrue(g, env.getVar("uiScale").value == "1", "uiScale is 1");
    testTrue(g, env.getVar("fontSize").value == "13", "fontSize is 13");
    testTrue(g, env.getVar("msaa").value == "4", "msaa is 4");
    testTrue(g, env.getVar("vsync").value == "1", "vsync is 1");
    testTrue(g, env.getVar("showGrid").value == "1", "showGrid is 1");
    testTrue(g, env.getVar("showAxes").value == "1", "showAxes is 1");
    testTrue(g, env.getVar("showWireframe").value == "0", "showWireframe is 0");
    testTrue(
      g, env.getVar("lightingEnabled").value == "1", "lightingEnabled is 1");
    testTrue(g, env.getVar("lightDirX").value == "0.5", "lightDirX is 0.5");
    testTrue(g, env.getVar("lightDirY").value == "1", "lightDirY is 1");
    testTrue(g, env.getVar("lightDirZ").value == "0.3", "lightDirZ is 0.3");
    testTrue(g, env.getVar("lightColorR").value == "1", "lightColorR is 1");
    testTrue(
      g, env.getVar("lightColorG").value == "0.95", "lightColorG is 0.95");
    testTrue(g, env.getVar("lightColorB").value == "0.9", "lightColorB is 0.9");
    testTrue(
      g, env.getVar("ambientColorR").value == "0.2", "ambientColorR is 0.2");
    testTrue(
      g, env.getVar("ambientColorG").value == "0.22", "ambientColorG is 0.22");
    testTrue(
      g, env.getVar("ambientColorB").value == "0.25", "ambientColorB is 0.25");
    testTrue(
      g, env.getVar("shadowsEnabled").value == "1", "shadowsEnabled is 1");
    testTrue(
      g, env.getVar("shadowMapSize").value == "1024", "shadowMapSize is 1024");
    testTrue(
      g, env.getVar("shadowRadius").value == "2.5", "shadowRadius is 2.5");
    testTrue(g, env.getVar("lightDistance").value == "8", "lightDistance is 8");
    testTrue(
      g, env.getVar("shadowBias").value == "0.001", "shadowBias is 0.001");
    testTrue(g,
             env.getVar("shadowSlopeScale").value == "0.004",
             "shadowSlopeScale is 0.004");
    testTrue(g,
             env.getVar("shadowNormalOffset").value == "0.015",
             "shadowNormalOffset is 0.015");
    testTrue(g, env.getVar("shadowPcf").value == "1", "shadowPcf is 1");
    testTrue(g,
             env.getVar("motionBlurEnabled").value == "1",
             "motionBlurEnabled is 1");
    testTrue(g,
             env.getVar("motionBlurAmount").value == "0.5",
             "motionBlurAmount is 0.5");
    testTrue(
      g, env.getVar("motionBlurMax").value == "0.2", "motionBlurMax is 0.2");
  }
  std::filesystem::remove(path, error);
}

static int
runConfigCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerMeshViewerConfigTests(IllumoTestRegistry& registry)
{
  registry.add("IllMeshViewer.Config.Defaults",
               []() { return runConfigCase(testConfigDefaults); });
}
