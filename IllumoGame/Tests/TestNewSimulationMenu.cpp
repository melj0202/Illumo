#include "Game/NewSimulationMenu.h"
#include "Game/RuleCatalogLoader.h"
#include "Rulesets/RuleSetRegistry.h"
#include "TestHarness.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>

static TestCounters g;

struct NewSimulationMenuFixture
{
  NullRenderWindow window;
  EnvVars env;
  Camera camera;
  MockBackend mock;
  Renderer renderer;
  InputManager input;
  NewSimulationMenu menu;

  NewSimulationMenuFixture()
    : window(640, 480)
    , env()
    , camera(glm::vec2(0.0f, 0.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , input(nullptr)
    , menu(&window, &renderer)
  {
    RuleCatalogLoader::loadFromDefaultLocations(RuleSetRegistry::instance());
    env.setVar("uiScale", 1);
    mock.Initialize();
  }

  NewSimulationAction press(KeyCode key)
  {
    input.getKeyQueue().push(
      InputManager::KeyPressEvent{ key, InputAction::Press, 0 });
    return menu.update(&input);
  }
};

static void
testFamilyAndRulesetLists()
{
  testSection("NewSimulationMenu: family and ruleset pick from drop-down "
              "lists");
  NewSimulationMenuFixture fixture;
  fixture.menu.open(NewSimulationConfiguration{}, true);
  const GuiDropdownList& list = fixture.menu.getDropdownForTesting();

  fixture.press(KeyCode::Enter);
  testTrue(g,
           list.isOpen() && list.itemCount() > 1 && list.currentIndex() >= 0 &&
             list.highlightedIndex() == list.currentIndex(),
           "ENTER on Cell family opens its list at the current family");
  testTrue(g,
           fixture.press(KeyCode::Escape) == NewSimulationAction::None &&
             !list.isOpen() && fixture.menu.isOpen(),
           "Escape closes only the list, not the New Canvas screen");

  fixture.press(KeyCode::Enter);
  fixture.press(KeyCode::Down);
  fixture.press(KeyCode::Enter);
  testTrue(g,
           !list.isOpen() &&
             fixture.menu.configuration().ruleSet == "BRIANS_BRAIN",
           "choosing the next family takes that family's first rule");
  testTrue(g,
           fixture.menu.configuration().isValid(),
           "the chosen family and rule make a valid canvas");

  fixture.press(KeyCode::Down);
  fixture.press(KeyCode::Enter);
  const int ruleCount = list.itemCount();
  testTrue(g,
           list.isOpen() && ruleCount >= 1,
           "ENTER on Ruleset opens the chosen family's rules");
  fixture.press(KeyCode::End);
  fixture.press(KeyCode::Enter);
  testTrue(g,
           !list.isOpen() && list.chosenIndex() == ruleCount - 1 &&
             fixture.menu.configuration().isValid(),
           "End then ENTER chooses the family's last rule");

  // LEFT/RIGHT still step through families without opening the list.
  fixture.press(KeyCode::Up);
  fixture.press(KeyCode::Left);
  testTrue(g,
           !list.isOpen() &&
             fixture.menu.configuration().ruleSet == "GAME_OF_LIFE",
           "LEFT steps back to the previous family's first rule");
}

static int
runNewSimulationMenuCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerNewSimulationMenuTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.NewSimulationMenu.FamilyAndRulesetLists", []() {
    return runNewSimulationMenuCase(testFamilyAndRulesetLists);
  });
}
