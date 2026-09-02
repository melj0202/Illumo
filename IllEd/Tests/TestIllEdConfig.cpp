#include "IllEdConfig.h"
#include <Illumo/Engine/Application.h>
#include <Illumo/Engine/IModule.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/SysCmdLine.h>
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

static void
testPositionalFileWiresLaunchScene()
{
  testSection("IllEdConfig: positional FILE wires LaunchScene");
  const IllumoApplicationDefinition application = CreateIllumoApplication();
  testEqStr(g,
            application.commandLine.positionalEnvironmentVariable,
            "LaunchScene",
            "positional FILE maps to LaunchScene");
  testEqStr(g, application.commandLine.usage, "IllEd.exe [FILE]", "usage");

  const std::filesystem::path path =
    std::filesystem::temp_directory_path() / "illed-positional.json";
  std::error_code error;
  std::filesystem::remove(path, error);
  {
    EnvVars environment(path);
    char executable[] = "IllEd.exe";
    char scene[] = "Scene.ilsc";
    char* arguments[] = { executable, scene };
    const SysCmdLineResult result = SysCmdLine::ParseCommandLine(
      2, arguments, &environment, application.commandLine);
    testTrue(g, !result.shouldExit(), "positional FILE continues startup");
    testEqStr(g,
              environment.getVar("LaunchScene").value,
              "Scene.ilsc",
              "positional FILE stored as LaunchScene");
    std::unique_ptr<IModule> module =
      application.createRequiredModule(&environment);
    testTrue(g, module != nullptr, "factory constructs with LaunchScene");
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
  registry.add("IllEd.Config.PositionalLaunchScene", []() {
    g = {};
    testPositionalFileWiresLaunchScene();
    return g.failures;
  });
}
