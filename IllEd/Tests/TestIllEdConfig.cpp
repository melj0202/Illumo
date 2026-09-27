#include "IllEdConfig.h"
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <filesystem>

static TestCounters g;

static void
testDefaultsAndIdentity()
{
  testSection("IllEdConfig: product identity and defaults");
  testEqStr(g, IllEdConfig::applicationName(), "IllEd", "application name");


  const std::filesystem::path path =
    std::filesystem::temp_directory_path() / "illed-defaults.json";
  std::error_code error;
  std::filesystem::remove(path, error);
  {
    EnvVars environment(path);
    IllEdConfig::ApplyDefaults(&environment);
    testEqStr(g, environment.getVar("uiScale").value, "1", "uiScale default");
    testEqStr(g, environment.getVar("msaa").value, "4", "msaa default");
    testEqStr(
      g, environment.getVar("fontSize").value, "13", "fontSize default");
  }
  std::filesystem::remove(path, error);
}

void
registerIllEdConfigTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Config.Identity", []() {
    g = {};
    testDefaultsAndIdentity();
    return g.failures;
  });
}
