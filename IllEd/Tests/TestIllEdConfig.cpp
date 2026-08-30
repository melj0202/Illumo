#include "IllEdConfig.h"
#include <Illumo/Engine/Application.h>
#include <Illumo/Engine/IModule.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <filesystem>

static TestCounters g;

static void
testDefaultsAndIdentity()
{
  testSection("IllEdConfig: product identity and defaults");
  const IllumoApplicationDefinition application = CreateIllumoApplication();
  testEqStr(g, application.applicationName, "IllEd", "application name");
  testTrue(g,
           application.applyDefaults == IllEdConfig::ApplyDefaults,
           "defaults callback");
  testTrue(
    g, application.createRequiredModule != nullptr, "required module factory");
  std::unique_ptr<IModule> module = application.createRequiredModule(nullptr);
  testTrue(g, module != nullptr, "factory constructs editor module");
  testTrue(g,
           application.commandLine.applicationOptions.empty(),
           "no CA command-line options");

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
