#include "Game/IllumoGameConfig.h"
#include <Illumo/Engine/Application.h>
#include <Illumo/Engine/IModule.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/SysCmdLine.h>
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
             environment.getVar("tps").value == "144",
             "persisted timing is retained");
    testTrue(g,
             environment.getVar("WorldChunksX").value == "0",
             "missing topology receives product default");
  }
  std::filesystem::remove(path, error);
}

static void
testCanvasCommandLineOptions()
{
  testSection("IllumoGameConfig: CA command-line metadata");
  const IllumoApplicationDefinition application = CreateIllumoApplication();
  testTrue(g,
           application.applicationName == "IllumoGame",
           "game supplies its product identity");
  testTrue(g,
           application.applyDefaults == IllumoGameConfig::ApplyDefaults,
           "game supplies its CA defaults callback");
  testTrue(g,
           application.createRequiredModule != nullptr,
           "game supplies its required module factory");
  std::unique_ptr<IModule> module = application.createRequiredModule(nullptr);
  testTrue(g, module != nullptr, "game module factory creates required module");

  testEqSize(g,
             application.commandLine.applicationOptions.size(),
             2u,
             "game contributes only its two canvas options");
  testTrue(
    g,
    application.commandLine.applicationOptions[0].option == "-cw" &&
      application.commandLine.applicationOptions[0].environmentVariable ==
        "CanvasX",
    "canvas width metadata is game-owned");
  testTrue(
    g,
    application.commandLine.applicationOptions[1].option == "-ch" &&
      application.commandLine.applicationOptions[1].environmentVariable ==
        "CanvasY",
    "canvas height metadata is game-owned");
}

static void
testInvalidCanvasDimensions()
{
  testSection("IllumoGameConfig: invalid canvas options use engine validation");
  const IllumoApplicationDefinition application = CreateIllumoApplication();
  char executable[] = "IllumoGame";
  char zero[] = "0";
  char overflow[] = "999999999999999999999999999999999999";
  char cw[] = "-cw";
  char ch[] = "-ch";
  char* options[] = { cw, ch };
  const char* variables[] = { "CanvasX", "CanvasY" };

  for (std::size_t i = 0; i < 2; ++i) {
    const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      (std::string("illumogame-invalid-canvas-") + std::to_string(i) + ".json");
    std::error_code error;
    std::filesystem::remove(path, error);
    {
      EnvVars environment(path);
      char* zeroArguments[] = { executable, options[i], zero };
      const SysCmdLineResult zeroResult = SysCmdLine::ParseCommandLine(
        3, zeroArguments, &environment, application.commandLine);
      testTrue(g,
               zeroResult.action == SysCmdLineAction::ExitFailure,
               "zero canvas dimension is rejected");
      testTrue(g,
               environment.getVar(variables[i]).value.empty(),
               "rejected zero is not stored");

      char* overflowArguments[] = { executable, options[i], overflow };
      const SysCmdLineResult overflowResult = SysCmdLine::ParseCommandLine(
        3, overflowArguments, &environment, application.commandLine);
      testTrue(g,
               overflowResult.action == SysCmdLineAction::ExitFailure,
               "overflow canvas dimension is rejected");
      testTrue(g,
               environment.getVar(variables[i]).value.empty(),
               "rejected overflow is not stored");
    }
    std::filesystem::remove(path, error);
  }
}

static void
testHelpContent()
{
  testSection("IllumoGameConfig: CA help content remains game-owned");
  const IllumoApplicationDefinition application = CreateIllumoApplication();
  testTrue(g,
           application.commandLine.description == "Cell Automata Simulator",
           "game supplies its CLI description");
  testTrue(g,
           application.commandLine.usage.find("RULESET") != std::string::npos,
           "game usage describes its ruleset argument");
  testEqSize(g,
             application.commandLine.helpSections.size(),
             1u,
             "game supplies one ruleset help section");
  testTrue(g,
           application.commandLine.helpSections[0].find("GAME_OF_LIFE") !=
               std::string::npos &&
             application.commandLine.helpSections[0].find("WIREWORLD") !=
               std::string::npos,
           "game help enumerates supported rulesets");
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
  registry.add("IllumoGame.Config.CanvasCommandLineOptions",
               []() { return runConfigCase(testCanvasCommandLineOptions); });
  registry.add("IllumoGame.Config.InvalidCanvasDimensions",
               []() { return runConfigCase(testInvalidCanvasDimensions); });
  registry.add("IllumoGame.Config.HelpContent",
               []() { return runConfigCase(testHelpContent); });
}
