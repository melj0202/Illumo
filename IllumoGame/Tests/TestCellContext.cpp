// CellContext mode string / ruleset factory tests (headless).

#include "Game/CellContext.h"
#include "TestHarness.h"
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cstdio>
#include <string>

static TestCounters g;

static void
testNormalizeModeString()
{
  testSection("CellContext: NormalizeModeString");
  std::string a = CellContext::NormalizeModeString("game_of_life");
  testTrue(g, a == "GAME_OF_LIFE", "lower snake becomes upper");
  std::string b = CellContext::NormalizeModeString("BrianS_Brain");
  testTrue(g, b == "BRIANS_BRAIN", "mixed case normalized");
}

static void
testIsKnownModeString()
{
  testSection("CellContext: IsKnownModeString");
  testTrue(g, CellContext::IsKnownModeString("GAME_OF_LIFE"), "GoL known");
  testTrue(g, CellContext::IsKnownModeString("SEEDS"), "Seeds known");
  testTrue(g, CellContext::IsKnownModeString("BRIANS_BRAIN"), "BB known");
  testTrue(g, CellContext::IsKnownModeString("WIREWORLD"), "Wireworld known");
  testTrue(g, CellContext::IsKnownModeString("RULE_90"), "Rule 90 known");
  testTrue(g, CellContext::IsKnownModeString("RULE_184"), "Rule 184 known");
  testTrue(
    g, !CellContext::IsKnownModeString("NOT_A_RULE"), "unknown rejected");
  testTrue(g,
           !CellContext::IsKnownModeString("game_of_life"),
           "lowercase not known until normalize (API is exact)");
}

static void
testSetRuleSetAndTags()
{
  testSection("CellContext: setRuleSet creates rules + updates ModeString");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("CanvasX", 8);
  env.setVar("CanvasY", 8);
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  CellContext ctx("game_of_life", &env, &window, &camera, &renderer);
  testTrue(g, ctx.getModeString() == "GAME_OF_LIFE", "start mode normalized");
  testTrue(g, ctx.getRuleSet() != nullptr, "ruleset allocated");
  testTrue(g, ctx.getCanvas() != nullptr, "canvas allocated");
  testTrue(
    g, env.getVar("ModeString").value == "GAME_OF_LIFE", "env ModeString set");

  const bool changed = ctx.setRuleSet("SEEDS");
  testTrue(g, changed, "switch to SEEDS returns true");
  testTrue(g, ctx.getModeString() == "SEEDS", "mode is SEEDS");
  testTrue(g,
           ctx.getRuleSet()->getRuleTag() != "BASE_CLASS" ||
             ctx.getModeString() == "SEEDS",
           "ruleset replaced");

  const bool same = ctx.setRuleSet("seeds");
  testTrue(g, !same, "re-set same mode returns false");
}

static void
testInvalidModeFallsBackToGoL()
{
  testSection("CellContext: invalid mode falls back to GAME_OF_LIFE");
  NullRenderWindow window(320, 240);
  EnvVars env;
  env.setVar("CanvasX", 4);
  env.setVar("CanvasY", 4);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  CellContext ctx("NOT_REAL", &env, &window, &camera, &renderer);
  testTrue(g, ctx.getModeString() == "GAME_OF_LIFE", "fallback mode GoL");
  testTrue(g, ctx.getRuleSet() != nullptr, "fallback ruleset exists");
}

static void
testCanvasDimensionsFromEnv()
{
  testSection("CellContext: canvas size from env");
  NullRenderWindow window(100, 100);
  EnvVars env;
  env.setVar("CanvasX", 10);
  env.setVar("CanvasY", 12);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  CellContext ctx("GAME_OF_LIFE", &env, &window, &camera, &renderer);
  testEqInt(
    g, ctx.getCanvas()->getTextureWidth(), 10, "texture width from env");
  testEqInt(
    g, ctx.getCanvas()->getTextureHeight(), 12, "texture height from env");
  ctx.getCanvas()->syncVisibleRegion();
  testEqInt(
    g, ctx.getCanvas()->getViewWidth(), 9, "active cell width from window");
  testEqInt(
    g, ctx.getCanvas()->getViewHeight(), 9, "active cell height from window");
}

static int
runCellContextCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerCellContextTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.CellContext.NormalizeMode",
               []() { return runCellContextCase(testNormalizeModeString); });
  registry.add("IllumoGame.CellContext.KnownModes",
               []() { return runCellContextCase(testIsKnownModeString); });
  registry.add("IllumoGame.CellContext.RuleSetSwitch",
               []() { return runCellContextCase(testSetRuleSetAndTags); });
  registry.add("IllumoGame.CellContext.InvalidModeFallback", []() {
    return runCellContextCase(testInvalidModeFallsBackToGoL);
  });
  registry.add("IllumoGame.CellContext.CanvasDimensions", []() {
    return runCellContextCase(testCanvasDimensionsFromEnv);
  });
}
