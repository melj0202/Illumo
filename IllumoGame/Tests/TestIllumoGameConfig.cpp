#include "Game/IllumoGameConfig.h"
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <filesystem>

static TestCounters g;

static void
testSimulatorDefaults()
{
  testSection("IllumoGameConfig: simulator defaults");
  const std::filesystem::path path =
    std::filesystem::temp_directory_path() / "illumogame-defaults.json";
  std::error_code error;
  std::filesystem::remove(path, error);
  {
    EnvVars environment(path);
    IllumoGameConfig::ApplyDefaults(&environment);
    testTrue(g,
             environment.getVar("CanvasX").value == "80",
             "canvas width default is product-owned");
    testTrue(g,
             environment.getVar("CanvasY").value == "60",
             "canvas height default is product-owned");
    testTrue(g,
             environment.getVar("ModeString").value == "GAME_OF_LIFE",
             "ruleset default is product-owned");
    testTrue(g,
             environment.getVar("tps").value == "30",
             "TPS default is product-owned");
    testTrue(g,
             environment.getVar("cellFadeSpeed").value == "8",
             "fade default is product-owned");
    testTrue(g,
             environment.getVar("uiScale").value == "auto",
             "uiScale defaults to automatic");
    testTrue(
      g, environment.getVar("msaa").value == "4", "msaa default is 4 samples");
  }
  std::filesystem::remove(path, error);
}

static void
testSimulatorOverridesArePreserved()
{
  testSection("IllumoGameConfig: persisted values win");
  const std::filesystem::path path =
    std::filesystem::temp_directory_path() / "illumogame-overrides.json";
  std::error_code error;
  std::filesystem::remove(path, error);
  {
    EnvVars environment(path);
    environment.setVar("CanvasX", 320);
    environment.setVar("ModeString", "WIREWORLD");
    environment.setVar("tps", 144);
    IllumoGameConfig::ApplyDefaults(&environment);
    testTrue(g,
             environment.getVar("CanvasX").value == "320",
             "persisted dimensions are retained");
    testTrue(g,
             environment.getVar("ModeString").value == "WIREWORLD",
             "persisted ruleset is retained");
    testTrue(g,
             environment.getVar("RuleSetString").value == "WIREWORLD",
             "legacy ModeString is migrated into the explicit ruleset key");
    testTrue(g,
             environment.getVar("tps").value == "144",
             "persisted timing is retained");
    testTrue(g,
             environment.getVar("WorldChunksX").value == "0",
             "missing topology receives product default");
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
registerIllumoGameConfigTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.Config.Defaults",
               []() { return runConfigCase(testSimulatorDefaults); });
  registry.add("IllumoGame.Config.PreservesOverrides", []() {
    return runConfigCase(testSimulatorOverridesArePreserved);
  });
}
