#include "Game/ConfigurationMenu.h"
#include "TestHarness.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <string>

static TestCounters g;

struct ConfigurationMenuFixture
{
  NullRenderWindow window;
  EnvVars env;
  Camera camera;
  MockBackend mock;
  Renderer renderer;
  InputManager input;
  ConfigurationMenu menu;

  ConfigurationMenuFixture()
    : window(640, 480)
    , env()
    , camera(glm::vec2(0.0f, 0.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , input(nullptr)
    , menu(&window, &renderer)
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

  void type(char character)
  {
    input.getCharQueue().push(static_cast<unsigned int>(character));
  }
};

static SimulatorConfiguration
defaultConfiguration()
{
  SimulatorConfiguration configuration;
  configuration.ruleSet = "GAME_OF_LIFE";
  configuration.worldChunkWidth = 0;
  configuration.worldChunkHeight = 0;
  configuration.tps = 30;
  configuration.speedFactor = 1.5;
  configuration.fadeSpeed = 8.0;
  configuration.vsync = true;
  configuration.fullscreen = false;
  configuration.uiScale = 1;
  configuration.msaa = 4;
  return configuration;
}

static void
testConfigurationParsingAndTopologyValidation()
{
  testSection("ConfigurationMenu: parsing and topology validation");
  ConfigurationMenuFixture fixture;
  fixture.menu.open(defaultConfiguration());

  SimulatorConfiguration parsed;
  std::string error;
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error),
           "default infinite configuration parses");
  testTrue(g,
           parsed.worldChunkWidth == 0 && parsed.worldChunkHeight == 0,
           "inf / inf maps to zero topology dimensions");

  fixture.press(KeyCode::Down);
  fixture.type('4');
  fixture.menu.update(&fixture.input);
  testTrue(g,
           !fixture.menu.readConfiguration(&parsed, &error),
           "mixed finite and infinite dimensions are rejected");
  testTrue(g,
           error.find("both axes") != std::string::npos,
           "mixed topology reports an actionable error");

  fixture.press(KeyCode::Down);
  fixture.type('3');
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error),
           "finite dimensions parse after both axes are entered");
  testTrue(g,
           parsed.worldChunkWidth == 4 && parsed.worldChunkHeight == 3,
           "finite topology preserves chunk dimensions");
  testTrue(g,
           parsed.tps == 30 && std::abs(parsed.speedFactor - 1.5) < 0.0001 &&
             std::abs(parsed.fadeSpeed - 8.0) < 0.0001,
           "unchanged timing fields are preserved");
}

static void
testConfigurationNavigationAndActions()
{
  testSection("ConfigurationMenu: keyboard navigation and actions");
  ConfigurationMenuFixture fixture;
  fixture.menu.open(defaultConfiguration());
  testTrue(g,
           fixture.menu.getAnimationProgressForTesting() == 0.0f,
           "opening resets the reveal animation");
  fixture.menu.tick(0.12f);
  testTrue(g,
           fixture.menu.getAnimationProgressForTesting() > 0.0f &&
             fixture.menu.getAnimationProgressForTesting() < 1.0f,
           "reveal animation advances incrementally");
  fixture.menu.tick(1.0f);
  testTrue(g,
           fixture.menu.getAnimationProgressForTesting() == 1.0f,
           "reveal animation clamps at completion");

  fixture.press(KeyCode::Down);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.getSelectionPositionForTesting() == 0.0f,
           "selection highlight begins at its previous row");
  fixture.menu.tick(0.07f);
  testTrue(g,
           fixture.menu.getSelectionPositionForTesting() > 0.0f &&
             fixture.menu.getSelectionPositionForTesting() < 1.0f,
           "selection highlight glides between rows");
  fixture.menu.tick(1.0f);
  testTrue(g,
           fixture.menu.getSelectionPositionForTesting() == 1.0f,
           "selection highlight settles on the selected row");

  fixture.press(KeyCode::Up);
  fixture.menu.update(&fixture.input);
  fixture.menu.tick(1.0f);

  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  SimulatorConfiguration parsed;
  std::string error;
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.ruleSet == "BRIANS_BRAIN",
           "right cycles to the next ruleset");
  testTrue(g,
           fixture.menu.getValuePulseForTesting() > 0.0f,
           "changed values start an accent pulse");
  fixture.menu.tick(1.0f);
  testTrue(g,
           fixture.menu.getValuePulseForTesting() == 0.0f,
           "value accent pulse fades to rest");

  for (int row = 0; row < 10; ++row) {
    fixture.press(KeyCode::Down);
  }
  fixture.press(KeyCode::Enter);
  testTrue(g,
           fixture.menu.update(&fixture.input) ==
             ConfigurationMenuAction::Apply,
           "Enter activates the Apply row");

  fixture.menu.open(defaultConfiguration());
  for (int row = 0; row < 8; ++row) {
    fixture.press(KeyCode::Down);
  }
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.uiScale == 2,
           "right cycles UI scale from 1x to 2x");

  fixture.menu.open(defaultConfiguration());
  for (int row = 0; row < 9; ++row) {
    fixture.press(KeyCode::Down);
  }
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) && parsed.msaa == 8,
           "right cycles MSAA from 4x to 8x");

  fixture.menu.open(defaultConfiguration());
  fixture.press(KeyCode::F1);
  testTrue(g,
           fixture.menu.update(&fixture.input) ==
             ConfigurationMenuAction::Cancel,
           "F1 cancels an open menu");

  fixture.menu.open(defaultConfiguration());
  for (int row = 0; row < 12; ++row) {
    fixture.press(KeyCode::Down);
  }
  fixture.press(KeyCode::Enter);
  testTrue(g,
           fixture.menu.update(&fixture.input) == ConfigurationMenuAction::Exit,
           "Enter activates the Exit row");
}

static void
testConfigurationMenuTokensAtReleaseWindowSize()
{
  testSection("ConfigurationMenu: primitive-composed rendering");
  ConfigurationMenuFixture fixture;
  fixture.menu.open(defaultConfiguration());
  fixture.menu.tick(1.0f);
  fixture.menu.setError("Example validation message");

  Scene scene(&fixture.window, &fixture.camera);
  scene.AddDrawable(&fixture.menu, RenderLayerId::UI);
  fixture.mock.resetCounters();
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();

  testTrue(g,
           fixture.mock.getLastNonEmptySubmittedCount() > 0u,
           "open settings menu emits render commands");
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) > 0u,
           "settings menu emits primitive draw tokens");

  GameVisual& visual = fixture.menu.getVisual();
  bool foundLargeTitle = false;
  bool foundReadableLabel = false;
  bool foundFriendlyRuleName = false;
  bool foundControlHelp = false;
  bool foundExitAction = false;
  bool foundRestartNote = false;
  for (std::size_t index = 0u; index < visual.textCount(); ++index) {
    TextPrimitive* text = visual.getText(index);
    if (text == nullptr) {
      continue;
    }
    foundLargeTitle =
      foundLargeTitle ||
      (text->content == "SIMULATOR SETTINGS" && text->sizePt >= 24.0f);
    foundReadableLabel =
      foundReadableLabel || (text->content == "Ruleset" &&
                             text->sizePt >= 16.0f && text->color.a == 255);
    foundFriendlyRuleName =
      foundFriendlyRuleName || text->content == "Game of Life";
    foundControlHelp =
      foundControlHelp ||
      text->content.find("UP/DOWN: select") != std::string::npos;
    foundExitAction = foundExitAction || text->content == "Exit simulator";
    foundRestartNote =
      foundRestartNote ||
      text->content.find("Marked settings") != std::string::npos;
  }
  testTrue(g, foundLargeTitle, "settings title uses larger text");
  testTrue(g,
           foundReadableLabel,
           "setting labels use opaque high-contrast text at readable size");
  testTrue(g,
           foundFriendlyRuleName,
           "ruleset value uses a human-readable display name");
  testTrue(g, foundControlHelp, "keyboard controls are split into clear help");
  testTrue(g, foundExitAction, "settings menu renders an Exit action");
  testTrue(g, foundRestartNote, "settings menu renders restart note footer");

  fixture.menu.close();
  fixture.mock.resetCounters();
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();
  testTrue(g,
           fixture.mock.getLastSubmittedCount() == 0u,
           "closed settings menu emits no commands");
}

static int
runConfigurationMenuCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerConfigurationMenuTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.ConfigurationMenu.Validation", []() {
    return runConfigurationMenuCase(
      testConfigurationParsingAndTopologyValidation);
  });
  registry.add("IllumoGame.ConfigurationMenu.Navigation", []() {
    return runConfigurationMenuCase(testConfigurationNavigationAndActions);
  });
  registry.add("IllumoGame.ConfigurationMenu.Tokens", []() {
    return runConfigurationMenuCase(
      testConfigurationMenuTokensAtReleaseWindowSize);
  });
}
