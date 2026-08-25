#include "Game/ExitConfirmDialog.h"
#include "TestHarness.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <string>

static TestCounters g;

struct ExitConfirmFixture
{
  NullRenderWindow window;
  EnvVars env;
  Camera camera;
  MockBackend mock;
  Renderer renderer;
  InputManager input;
  ExitConfirmDialog dialog;

  ExitConfirmFixture()
    : window(640, 480)
    , env()
    , camera(glm::vec2(0.0f, 0.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , input(nullptr)
    , dialog(&window, &renderer)
  {
    env.setVar("WinX", 640);
    env.setVar("WinY", 480);
    mock.Initialize();
  }

  void press(KeyCode key)
  {
    input.getKeyQueue().push(
      InputManager::KeyPressEvent{ key, InputAction::Press, 0 });
  }
};

static void
testExitConfirmActions()
{
  testSection("ExitConfirmDialog: confirm and cancel");
  ExitConfirmFixture fixture;
  testTrue(g, !fixture.dialog.isOpen(), "dialog starts closed");
  testTrue(g,
           fixture.dialog.update(&fixture.input) == ExitConfirmAction::None,
           "closed dialog ignores input");

  fixture.dialog.open();
  testTrue(g, fixture.dialog.isOpen(), "open makes the dialog visible");
  testEqInt(g,
            fixture.dialog.getSelectedButtonForTesting(),
            0,
            "Cancel is selected by default");

  fixture.press(KeyCode::Enter);
  testTrue(g,
           fixture.dialog.update(&fixture.input) == ExitConfirmAction::Cancel,
           "Enter on Cancel stays in the program");

  fixture.dialog.open();
  fixture.press(KeyCode::Right);
  fixture.press(KeyCode::Enter);
  testTrue(g,
           fixture.dialog.update(&fixture.input) == ExitConfirmAction::MainMenu,
           "Enter on Main Menu requests menu transition");

  fixture.dialog.open();
  fixture.press(KeyCode::Right);
  fixture.press(KeyCode::Right);
  fixture.press(KeyCode::Enter);
  testTrue(g,
           fixture.dialog.update(&fixture.input) == ExitConfirmAction::Confirm,
           "Enter on Exit confirms shutdown");

  fixture.dialog.open();
  fixture.press(KeyCode::M);
  testTrue(g,
           fixture.dialog.update(&fixture.input) == ExitConfirmAction::MainMenu,
           "M selects main menu");

  fixture.dialog.open();
  fixture.press(KeyCode::Y);
  testTrue(g,
           fixture.dialog.update(&fixture.input) == ExitConfirmAction::Confirm,
           "Y confirms exit");

  fixture.dialog.open();
  fixture.press(KeyCode::Escape);
  testTrue(g,
           fixture.dialog.update(&fixture.input) == ExitConfirmAction::Cancel,
           "Escape cancels exit");

  fixture.dialog.open();
  fixture.press(KeyCode::N);
  testTrue(g,
           fixture.dialog.update(&fixture.input) == ExitConfirmAction::Cancel,
           "N cancels exit");

  fixture.dialog.close();
  testTrue(g, !fixture.dialog.isOpen(), "close hides the dialog");
}

static void
testExitConfirmAnimation()
{
  testSection("ExitConfirmDialog: reveal and selection motion");
  ExitConfirmFixture fixture;
  fixture.dialog.open();
  testTrue(g,
           fixture.dialog.getAnimationProgressForTesting() == 0.0f,
           "opening resets the reveal animation");
  fixture.dialog.tick(0.08f);
  const float midProgress = fixture.dialog.getAnimationProgressForTesting();
  testTrue(g,
           midProgress > 0.0f && midProgress < 1.0f,
           "reveal animation advances incrementally");
  fixture.dialog.tick(1.0f);
  testTrue(g,
           fixture.dialog.getAnimationProgressForTesting() == 1.0f,
           "reveal animation clamps at completion");

  fixture.press(KeyCode::Right);
  fixture.dialog.update(&fixture.input);
  testTrue(g,
           fixture.dialog.getSelectionPositionForTesting() == 0.0f,
           "selection highlight begins at Cancel");
  fixture.dialog.tick(0.07f);
  testTrue(g,
           fixture.dialog.getSelectionPositionForTesting() > 0.0f &&
             fixture.dialog.getSelectionPositionForTesting() < 1.0f,
           "selection highlight glides between buttons");
  fixture.dialog.tick(1.0f);
  testTrue(g,
           fixture.dialog.getSelectionPositionForTesting() == 1.0f,
           "selection highlight settles on next button");
}

static void
testExitConfirmTokens()
{
  testSection("ExitConfirmDialog: primitive-composed rendering");
  ExitConfirmFixture fixture;
  fixture.dialog.open();
  fixture.dialog.tick(1.0f);

  Scene scene(&fixture.window, &fixture.camera);
  scene.AddDrawable(&fixture.dialog, RenderLayerId::UI);
  fixture.mock.resetCounters();
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();

  testTrue(g,
           fixture.mock.getLastNonEmptySubmittedCount() > 0u,
           "open exit confirmation emits render commands");
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) > 0u,
           "exit confirmation emits primitive draw tokens");

  GameVisual& visual = fixture.dialog.getVisual();
  bool foundTitle = false;
  bool foundResume = false;
  bool foundMenu = false;
  bool foundExit = false;
  for (std::size_t index = 0u; index < visual.textCount(); ++index) {
    TextPrimitive* text = visual.getText(index);
    if (text == nullptr) {
      continue;
    }
    foundTitle = foundTitle || text->content == "SIMULATION PAUSED";
    foundResume = foundResume || text->content == "Resume";
    foundMenu = foundMenu || text->content == "Main Menu";
    foundExit = foundExit || text->content == "Exit App";
  }
  testTrue(g, foundTitle, "confirmation title is present");
  testTrue(g, foundResume, "Resume action is present");
  testTrue(g, foundMenu, "Main Menu action is present");
  testTrue(g, foundExit, "Exit action is present");

  fixture.dialog.close();
  fixture.mock.resetCounters();
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();
  testTrue(g,
           fixture.mock.getLastSubmittedCount() == 0u,
           "closed confirmation emits no commands");
}

static int
runExitConfirmCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerExitConfirmDialogTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.ExitConfirm.Actions",
               []() { return runExitConfirmCase(testExitConfirmActions); });
  registry.add("IllumoGame.ExitConfirm.Tokens",
               []() { return runExitConfirmCase(testExitConfirmTokens); });
  registry.add("IllumoGame.ExitConfirm.Animation",
               []() { return runExitConfirmCase(testExitConfirmAnimation); });
}
