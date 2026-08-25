#include "Game/MainMenuModule.h"
#include "TestHarness.h"
#include <Illumo/Engine/IModuleHost.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <memory>
#include <vector>

static TestCounters g;

class MockModuleHost : public IModuleHost
{
public:
  std::unique_ptr<IModule> transitionRequested;

  void RequestTransition(std::unique_ptr<IModule> nextModule) override
  {
    transitionRequested = std::move(nextModule);
  }

  bool HasPendingTransition() const override
  {
    return transitionRequested != nullptr;
  }
};

struct MainMenuFixture
{
  NullRenderWindow window;
  EnvVars env;
  Camera camera;
  MockBackend mock;
  Renderer renderer;
  CommandRegistry registry;
  CommandLine console;
  InputManager input;
  Scene scene;
  MockModuleHost host;
  IllumoContext context;
  MainMenuModule module;
  bool started;

  MainMenuFixture()
    : window(640, 480)
    , env()
    , camera(glm::vec2(0.0f, 0.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , registry()
    , console(&env, &registry, &window, &renderer)
    , input(nullptr)
    , scene(&window, &camera)
    , host()
    , context{ &scene,  &window, &console, &input,    &renderer,
               nullptr, &env,    &camera,  &registry, &host }
    , module()
    , started(false)
  {
    env.setVar("WinX", 640);
    env.setVar("WinY", 480);
    env.setVar("ModeString", "GAME_OF_LIFE");
    env.setVar("tps", 30);
    mock.Initialize();
    started = module.Start(&context);
  }

  ~MainMenuFixture()
  {
    if (started) {
      module.Exit();
    }
  }
};

static void
testMainMenuStartAndDrawables()
{
  testSection("MainMenuModule: start and drawables");
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "valid context starts MainMenuModule");
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            0,
            "default selected item is Play");
  testTrue(
    g, !fixture.module.isSettingsOpenForTesting(), "settings start closed");

  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  testEqSize(g,
             fixture.scene.drawableCount(),
             2u,
             "menu dispatches ambient canvas and UI visual");
  testEqSize(g,
             fixture.scene.drawablesIn(RenderLayerId::World).size(),
             1u,
             "ambient canvas is in World layer");
  testEqSize(g,
             fixture.scene.drawablesIn(RenderLayerId::UI).size(),
             1u,
             "menu visual is in UI layer");
}

static void
testMainMenuNavigation()
{
  testSection("MainMenuModule: keyboard navigation");
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "menu started");

  // Key Down: 0 -> 1 -> 2 -> 3 -> 0
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Down, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            1,
            "Down moves to item 1 (Load)");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Down, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            2,
            "Down moves to item 2 (Settings)");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Down, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            3,
            "Down moves to item 3 (Exit)");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Down, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            0,
            "Down wraps back to item 0 (Play)");

  // Key Up: 0 -> 3
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Up, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            3,
            "Up wraps to item 3 (Exit)");
}

static void
testMainMenuPlayTransition()
{
  testSection("MainMenuModule: Play action requests transition");
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "menu started");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.Update(0.016);

  testTrue(g,
           fixture.host.HasPendingTransition(),
           "Play action requests module transition");
  testTrue(g,
           fixture.host.transitionRequested != nullptr,
           "transitioned module instance is valid");
}

static void
testMainMenuSettingsWorkflow()
{
  testSection("MainMenuModule: Settings dialog open and close");
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "menu started");

  // Select Settings (item 2)
  fixture.module.selectItemForTesting(2);
  fixture.module.activateSelectedItemForTesting();
  testTrue(g,
           fixture.module.isSettingsOpenForTesting(),
           "activating Settings opens dialog");

  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  testEqSize(g,
             fixture.scene.drawablesIn(RenderLayerId::UI).size(),
             2u,
             "settings menu is dispatched as second UI drawable");

  // Close Settings with Escape
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g,
           !fixture.module.isSettingsOpenForTesting(),
           "Escape closes settings dialog");
}

static void
testMainMenuConsoleCommands()
{
  testSection("MainMenuModule: console commands");
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "menu started");
  testTrue(
    g, fixture.registry.HasCommand("play"), "play command is registered");

  fixture.registry.QueueCommand("play", {});
  fixture.registry.ExecuteQueue();

  testTrue(
    g, fixture.host.HasPendingTransition(), "play command triggers transition");
}

static int
runMainMenuCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerMainMenuTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.MainMenu.StartAndDrawables",
               []() { return runMainMenuCase(testMainMenuStartAndDrawables); });
  registry.add("IllumoGame.MainMenu.Navigation",
               []() { return runMainMenuCase(testMainMenuNavigation); });
  registry.add("IllumoGame.MainMenu.PlayTransition",
               []() { return runMainMenuCase(testMainMenuPlayTransition); });
  registry.add("IllumoGame.MainMenu.SettingsWorkflow",
               []() { return runMainMenuCase(testMainMenuSettingsWorkflow); });
  registry.add("IllumoGame.MainMenu.ConsoleCommands",
               []() { return runMainMenuCase(testMainMenuConsoleCommands); });
}
