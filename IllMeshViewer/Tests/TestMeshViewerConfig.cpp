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
