#include "Game/CellGameModule.h"
#include "Game/MainMenuModule.h"
#include "TestAccess.h"
#include "TestHarness.h"
#include <Illumo/Engine/IModuleHost.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/TestAccess.h>
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
    // Preferences are explicit so repeated runs cannot inherit a saved draft.
    env.setVar("fps", 60);
    env.setVar("showInspector", false);
    env.setVar("reducedUiMotion", false);
    env.setVar("uiScale", 1);
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
testTitleRasterResolution()
{
  MainMenuFixture fixture;
  fixture.window.handleResize(3840, 2160);
  for (int scale : { 1, 2, 4 }) {
    fixture.env.setVar("uiScale", scale);
    fixture.module.Update(0.0);
    fixture.scene.ClearDrawables();
    fixture.module.DispatchDrawables(&fixture.scene);
    GameVisual* visual = static_cast<GameVisual*>(
      fixture.scene.drawablesIn(RenderLayerId::UI).front());
    bool found = false;
    for (size_t index = 0; index < visual->textCount(); ++index) {
      const TextPrimitive* text = visual->getText(index);
      if (text->content == "ILLUMO") {
        found = true;
        testTrue(g,
                 text->font != nullptr && text->font->getMetrics().pixelSize >=
                                            text->sizePt *
                                              visual->getTransform().scaleY *
                                              static_cast<float>(scale),
                 "title glyphs are never magnified at supported UI scales");
        testTrue(g,
                 text->font != Font::getDefaultFont(),
                 "title atlas does not replace the small-text default font");
      }
    }
    testTrue(g, found, "main-menu title is present");
  }
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
           fixture.module.isCanvasSetupOpenForTesting() &&
             !fixture.module.isSettingsOpenForTesting() &&
             !fixture.host.HasPendingTransition(),
           "New simulation opens its own setup without starting");
  fixture.input.getKeyQueue().push({ KeyCode::End, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Up, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g,
           fixture.host.HasPendingTransition(),
           "Create requests module transition");
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
testMainMenuSettingsApply()
{
  MainMenuFixture fixture;
  fixture.env.setVar("fps", 144);
  fixture.env.setVar("uiScale", 2);
  fixture.env.setVar("msaa", 8);
  fixture.env.setVar("cellFadeSpeed", 0);
  fixture.env.setVar("showInspector", true);
  fixture.env.setVar("reducedUiMotion", true);
  fixture.input.getKeyQueue().push({ KeyCode::F1, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g,
           fixture.module.isSettingsOpenForTesting(),
           "F1 opens settings on the main menu");
  for (int row = 0; row < 10; ++row) {
    fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  }
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0 });
  for (int row = 0; row < 4; ++row) {
    fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  }
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g,
           !fixture.module.isSettingsOpenForTesting() &&
             fixture.env.getVar("fps").valueAsLong == 165,
           "Apply commits changed FPS cap from main menu");
  testTrue(g,
           fixture.env.getVar("uiScale").valueAsLong == 2 &&
             fixture.env.getVar("msaa").valueAsLong == 8 &&
             fixture.env.getVar("cellFadeSpeed").valueAsDouble == 0.0 &&
             fixture.env.getVar("showInspector").valueAsBool &&
             fixture.env.getVar("reducedUiMotion").valueAsBool,
           "Apply preserves existing display and zero-fade preferences");
  fixture.input.getKeyQueue().push({ KeyCode::F1, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  for (int row = 0; row < 10; ++row) {
    fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  }
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g,
           fixture.env.getVar("fps").valueAsLong == 165,
           "Discard preserves the applied FPS cap");
}

static void
testSettingsMouseIsolation()
{
  MainMenuFixture fixture;
  fixture.env.setVar("reducedUiMotion", true);
  fixture.env.setVar("fps", 60);
  fixture.input.getKeyQueue().push({ KeyCode::F1, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  for (int row = 0; row < 10; ++row) {
    fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  }
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0 });
  for (int row = 0; row < 4; ++row) {
    fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  }
  fixture.module.Update(0.016);
  // In the 640x480 viewport Apply overlaps the underlying main-menu Exit card.
  fixture.window.mouseX = 390.0;
  fixture.window.mouseY = 390.0;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  testTrue(g,
           !fixture.module.isSettingsOpenForTesting() &&
             fixture.env.getVar("fps").valueAsLong == 90,
           "clicking the scrolled Apply row commits the draft");
  fixture.module.Update(0.016);
  testTrue(g,
           !fixture.window.closeRequested &&
             !fixture.host.HasPendingTransition(),
           "holding Apply does not activate the underlying main-menu action");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
  fixture.input.getKeyQueue().push({ KeyCode::F1, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  fixture.input.getKeyQueue().push({ KeyCode::End, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g,
           fixture.window.closeRequested,
           "settings Exit requests normal close from the main menu");
}

static void
testMainMenuYieldsToConsole()
{
  testSection("MainMenuModule: open console blocks menu input");
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "menu started");

  fixture.console.Toggle();
  testTrue(g, fixture.console.isOpen, "console is open");
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Down, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            0,
            "open console blocks Down navigation");
  testTrue(g,
           !fixture.host.HasPendingTransition(),
           "open console does not activate Play");

  fixture.console.Toggle();
  testTrue(g, !fixture.console.isOpen, "console is closed");
  fixture.input.clearKeyQueue();
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Grave, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Down, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            1,
            "closed console still allows Down navigation");
  testTrue(g,
           !fixture.input.getKeyQueue().empty() &&
             fixture.input.getKeyQueue().front().key == KeyCode::Grave,
           "menu leaves Grave for the global debug overlay");
}

static void
testMainMenuSettingsYieldToConsole()
{
  testSection("MainMenuModule: open console blocks settings input");
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "menu started");

  fixture.module.selectItemForTesting(2);
  fixture.module.activateSelectedItemForTesting();
  testTrue(g,
           fixture.module.isSettingsOpenForTesting(),
           "settings open for console-yield check");

  fixture.console.Toggle();
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g,
           fixture.module.isSettingsOpenForTesting(),
           "open console blocks settings Escape");
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

  testTrue(g,
           fixture.module.isCanvasSetupOpenForTesting() &&
             !fixture.host.HasPendingTransition(),
           "play command opens canvas setup");
}

static void
testCanvasSetupValidation()
{
  MainMenuFixture fixture;
  NewSimulationConfiguration config;
  config.worldChunkWidth = 2;
  config.worldChunkHeight = 0;
  CellGameModule invalid(config);
  testTrue(g,
           !invalid.Start(&fixture.context),
           "mixed infinite/finite topology rejected before startup");
  config.worldChunkHeight = 2;
  config.ruleSet = "NOT_A_RULE";
  testTrue(g, !config.isValid(), "unknown ruleset rejected");
  config.ruleSet = "WIREWORLD";
  fixture.module.Exit();
  fixture.started = false;
  CellGameModule seeded(config);
  testTrue(
    g, seeded.Start(&fixture.context), "valid non-default ruleset starts");
  CellContext* cells = CellGameModuleTestAccess::getCellContext(seeded);
  testTrue(g,
           cells && cells->getModeString() == "WIREWORLD" &&
             cells->getGrid()->getAllocatedChunkCount() > 0,
           "chosen ruleset and starter pattern reach canvas");
  seeded.Exit();
}

static void
testCanvasSetupDraft()
{
  MainMenuFixture fixture;
  fixture.env.setVar("WorldChunksX", 0);
  fixture.env.setVar("WorldChunksY", 0);
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g,
           !fixture.host.HasPendingTransition(),
           "opening Enter cannot create a canvas");
  fixture.input.getKeyQueue().push({ KeyCode::F1, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g,
           !fixture.module.isCanvasSetupOpenForTesting() &&
             !fixture.module.isSettingsOpenForTesting() &&
             !fixture.host.HasPendingTransition(),
           "Escape discards setup and F1 does not open global settings");
  testTrue(g,
           fixture.env.getVar("WorldChunksX").valueAsLong == 0 &&
             fixture.env.getVar("WorldChunksY").valueAsLong == 0 &&
             fixture.env.getVar("fps").valueAsLong == 60,
           "discard changes neither canvas defaults nor display preferences");
}

static void
testCanvasSetupCreatesConfiguredWorld()
{
  MainMenuFixture fixture;
  fixture.env.setVar("WorldChunksX", 0);
  fixture.env.setVar("WorldChunksY", 0);
  fixture.module.activateSelectedItemForTesting();
  for (KeyCode key : { KeyCode::Down,
                       KeyCode::Right,
                       KeyCode::Down,
                       KeyCode::Right,
                       KeyCode::Down,
                       KeyCode::Down,
                       KeyCode::Right,
                       KeyCode::Down,
                       KeyCode::Enter }) {
    fixture.input.getKeyQueue().push({ key, InputAction::Press, 0 });
  }
  fixture.module.Update(0.016);
  testTrue(
    g, fixture.host.HasPendingTransition(), "configured canvas is submitted");
  if (!fixture.host.HasPendingTransition())
    return;
  fixture.module.Exit();
  fixture.started = false;
  CellGameModule* game =
    static_cast<CellGameModule*>(fixture.host.transitionRequested.get());
  const bool started = game->Start(&fixture.context);
  testTrue(g, started, "configured game starts");
  CellContext* cells = CellGameModuleTestAccess::getCellContext(*game);
  testTrue(g,
           cells && cells->getWorldChunkWidth() == 33 &&
             cells->getWorldChunkHeight() == 24,
           "chosen dimensions reach actual sparse canvas");
  testTrue(g,
           cells && cells->getGrid()->getAllocatedChunkCount() == 0,
           "empty choice omits startup pattern");
  testTrue(g,
           fixture.env.getVar("fps").valueAsLong == 60 &&
             fixture.env.getVar("tps").valueAsLong == 30,
           "canvas creation preserves performance settings");
  game->Exit();
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
  registry.add("IllumoGame.MainMenu.CanvasSetupValidation",
               []() { return runMainMenuCase(testCanvasSetupValidation); });
  registry.add("IllumoGame.MainMenu.CanvasSetupDiscard",
               []() { return runMainMenuCase(testCanvasSetupDraft); });
  registry.add("IllumoGame.MainMenu.CanvasSetupCreate", []() {
    return runMainMenuCase(testCanvasSetupCreatesConfiguredWorld);
  });
  registry.add("IllumoGame.MainMenu.TitleResolution",
               []() { return runMainMenuCase(testTitleRasterResolution); });

  registry.add("IllumoGame.MainMenu.MouseIsolation",
               []() { return runMainMenuCase(testSettingsMouseIsolation); });
  registry.add("IllumoGame.MainMenu.SettingsApply",
               []() { return runMainMenuCase(testMainMenuSettingsApply); });
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
  registry.add("IllumoGame.MainMenu.YieldsToConsole",
               []() { return runMainMenuCase(testMainMenuYieldsToConsole); });
  registry.add("IllumoGame.MainMenu.SettingsYieldToConsole", []() {
    return runMainMenuCase(testMainMenuSettingsYieldToConsole);
  });
}
