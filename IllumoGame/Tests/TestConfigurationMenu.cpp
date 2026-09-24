#include "Game/CSimSounds.h"
#include "Game/ConfigurationMenu.h"
#include "Game/RuleCatalogLoader.h"
#include "Rulesets/RuleSetRegistry.h"
#include "TestHarness.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Font.h>
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
    RuleCatalogLoader::loadFromDefaultLocations(RuleSetRegistry::instance());
    // Preferences are explicit so repeated runs cannot inherit a saved draft.
    env.setVar("fps", 60);
    env.setVar("showInspector", false);
    env.setVar("reducedUiMotion", false);
    env.setVar("uiScale", 1);
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

static bool
hasSwitchBeside(GameVisual& visual, const std::string& label)
{
  float labelY = -1000.0f;
  for (std::size_t index = 0u; index < visual.textCount(); ++index) {
    TextPrimitive* text = visual.getText(index);
    if (text != nullptr && text->content == label) {
      labelY = text->y;
      break;
    }
  }
  if (labelY < 0.0f) {
    return false;
  }
  for (std::size_t index = 0u; index < visual.shapeCount(); ++index) {
    ShapePrimitive* shape = visual.getShape(index);
    if (shape != nullptr && shape->kind == ShapeKind::FilledRect &&
        std::abs(shape->rect.w - 20.0f) < 0.1f &&
        std::abs(shape->rect.h - 16.0f) < 0.1f &&
        std::abs(shape->rect.y - labelY) < 4.0f) {
      return true;
    }
  }
  return false;
}

static bool
hasTextCaret(GameVisual& visual)
{
  for (std::size_t index = 0u; index < visual.textCount(); ++index) {
    TextPrimitive* text = visual.getText(index);
    if (text != nullptr && text->content == "|") {
      return true;
    }
  }
  return false;
}

static float
textCaretGap(GameVisual& visual, const std::string& value)
{
  TextPrimitive* valueText = nullptr;
  TextPrimitive* caretText = nullptr;
  for (std::size_t index = 0u; index < visual.textCount(); ++index) {
    TextPrimitive* text = visual.getText(index);
    if (text != nullptr && text->content == value) {
      valueText = text;
    } else if (text != nullptr && text->content == "|") {
      caretText = text;
    }
  }
  std::shared_ptr<Font> font = Font::getDefaultFont();
  if (valueText == nullptr || caretText == nullptr || font == nullptr ||
      value.empty()) {
    return 1000.0f;
  }
  const FontMetrics& metrics = font->getMetrics();
  const float scale =
    metrics.pixelSize > 0.0f ? valueText->sizePt / metrics.pixelSize : 1.0f;
  const TextBounds bounds = font->measureText(value, valueText->sizePt);
  const GlyphInfo* valueGlyph =
    font->getGlyph(static_cast<unsigned char>(value.back()));
  const GlyphInfo* caretGlyph = font->getGlyph('|');
  float valueRight = valueText->x + bounds.width;
  if (valueGlyph != nullptr) {
    valueRight +=
      (valueGlyph->bearingX + valueGlyph->width - valueGlyph->advanceX) * scale;
  }
  const float caretLeft =
    caretText->x +
    (caretGlyph == nullptr ? 0.0f : caretGlyph->bearingX * scale);
  return caretLeft - valueRight;
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

  for (int row = 0; row < 16; ++row) {
    fixture.press(KeyCode::Down);
  }
  fixture.press(KeyCode::Enter);
  testTrue(g,
           fixture.menu.update(&fixture.input) ==
             ConfigurationMenuAction::Apply,
           "Enter activates the Apply row");

  fixture.menu.open(defaultConfiguration());
  for (int row = 0; row < 9; ++row) {
    fixture.press(KeyCode::Down);
  }
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.uiScale == 2,
           "right cycles UI scale from 1x to 2x");

  fixture.menu.open(defaultConfiguration());
  for (int row = 0; row < 10; ++row) {
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
  for (int row = 0; row < 14; ++row) {
    fixture.press(KeyCode::Down);
  }
  fixture.press(KeyCode::Enter);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) && !parsed.editHints,
           "Enter toggles edit hints off");
  fixture.menu.open(parsed);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) && !parsed.editHints,
           "reopening preserves the hints setting");

  fixture.menu.open(defaultConfiguration());
  for (int row = 0; row < 18; ++row) {
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
      foundFriendlyRuleName || text->content == "Conway's Game of Life";
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
  testTrue(g,
           !hasSwitchBeside(visual, "Fade speed"),
           "numeric fade speed does not render a misplaced switch");
  testTrue(g,
           hasSwitchBeside(visual, "Vertical sync"),
           "vertical sync renders its switch on the matching row");
  testTrue(
    g, !foundExitAction, "offscreen actions are not drawn over the footer");
  for (int row = 0; row < 6; ++row) {
    fixture.press(KeyCode::Down);
  }
  fixture.menu.update(&fixture.input);
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();
  testTrue(g,
           hasTextCaret(visual),
           "focused editable setting renders a visible text caret");
  const float caretGap = textCaretGap(visual, "8");
  testTrue(g,
           caretGap >= 0.0f && caretGap <= 2.0f,
           "text caret sits directly beside the final rendered glyph");
  fixture.menu.tick(0.6f);
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();
  testTrue(g,
           !hasTextCaret(visual),
           "editable setting caret alternates off during its blink cycle");
  fixture.press(KeyCode::End);
  fixture.menu.update(&fixture.input);
  fixture.menu.tick(1.0f);
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();
  for (std::size_t index = 0; index < visual.textCount(); ++index) {
    TextPrimitive* text = visual.getText(index);
    foundExitAction =
      foundExitAction || (text != nullptr && text->content == "Exit simulator");
  }
  testTrue(g, foundExitAction, "End scrolls Exit into the visible viewport");
  testTrue(g,
           !hasSwitchBeside(visual, "FPS cap"),
           "numeric FPS cap does not render a misplaced switch");
  testTrue(g,
           hasSwitchBeside(visual, "Simulation inspector") &&
             hasSwitchBeside(visual, "Reduced menu motion") &&
             hasSwitchBeside(visual, "Edit control hints"),
           "lower boolean settings render switches on their matching rows");
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

static void
testDisplaySettingsAndScrolling()
{
  ConfigurationMenuFixture fixture;
  fixture.env.setVar("uiScale", 4);
  fixture.menu.open(defaultConfiguration());
  for (int row = 0; row < 11; ++row) {
    fixture.press(KeyCode::Down);
  }
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  SimulatorConfiguration parsed;
  std::string error;
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.fpsCap == 90,
           "FPS presets cycle from 60 to 90");
  fixture.type('1');
  fixture.type('4');
  fixture.type('4');
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.fpsCap == 144,
           "custom FPS cap replaces the selected value");
  fixture.press(KeyCode::Backspace);
  fixture.type('9');
  fixture.type('9');
  fixture.type('9');
  fixture.type('9');
  fixture.menu.update(&fixture.input);
  testTrue(g,
           !fixture.menu.readConfiguration(&parsed, &error) &&
             error.find("FPS cap") != std::string::npos,
           "out of range FPS cap is rejected");
  fixture.press(KeyCode::Delete);
  fixture.menu.update(&fixture.input);
  // Selecting again restores replace-on-type, even after an invalid long draft.
  fixture.press(KeyCode::Up);
  fixture.press(KeyCode::Down);
  fixture.type('0');
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.fpsCap == 0,
           "zero explicitly disables the software FPS cap");
  fixture.press(KeyCode::Down);
  fixture.press(KeyCode::Right);
  fixture.press(KeyCode::Down);
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.showInspector && parsed.reducedUiMotion,
           "inspector and reduced motion are independent toggles");
  testTrue(g,
           fixture.menu.getAnimationProgressForTesting() == 1.0f &&
             fixture.menu.getSelectionPositionForTesting() == 13.0f &&
             fixture.menu.getValuePulseForTesting() == 0.0f,
           "reduced motion immediately snaps reveal, selection, and pulse");
  fixture.press(KeyCode::Home);
  fixture.menu.update(&fixture.input);
  *fixture.input.getMouseScrollOffset() = -1.0;
  fixture.menu.update(&fixture.input);
  testTrue(
    g,
    fixture.menu.getSelectedRowForTesting() == 0 &&
      fixture.menu.getFirstVisibleRowForTesting() == 1 &&
      *fixture.input.getMouseScrollOffset() == 0.0,
    "wheel moves the view without selecting another button and is consumed");
  fixture.menu.tick(0.1f);
  fixture.menu.update(&fixture.input);
  testEqInt(g,
            fixture.menu.getFirstVisibleRowForTesting(),
            1,
            "later layout updates do not snap back to the selected row");
  *fixture.input.getMouseScrollOffset() = -100.0;
  fixture.menu.update(&fixture.input);
  const int bottomRow = fixture.menu.getFirstVisibleRowForTesting();
  testTrue(g,
           bottomRow > 1 && fixture.menu.getSelectedRowForTesting() == 0,
           "wheel clamps at the bottom while selection stays offscreen");
  *fixture.input.getMouseScrollOffset() = -1.0;
  fixture.menu.update(&fixture.input);
  testEqInt(g,
            fixture.menu.getFirstVisibleRowForTesting(),
            bottomRow,
            "scrolling past the bottom does not wrap");
  *fixture.input.getMouseScrollOffset() = 100.0;
  fixture.menu.update(&fixture.input);
  testEqInt(g,
            fixture.menu.getFirstVisibleRowForTesting(),
            0,
            "wheel clamps at the top");
  *fixture.input.getMouseScrollOffset() = -3.0;
  fixture.menu.update(&fixture.input);
  fixture.press(KeyCode::Down);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.getSelectedRowForTesting() == 1 &&
             fixture.menu.getFirstVisibleRowForTesting() == 1,
           "keyboard navigation brings the selected row back into view");
  fixture.press(KeyCode::End);
  fixture.menu.update(&fixture.input);
  Scene scene(&fixture.window, &fixture.camera);
  scene.AddDrawable(&fixture.menu, RenderLayerId::UI);
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();
  GameVisual& visual = fixture.menu.getVisual();
  const float fittedScale =
    visual.getTransform().scaleX * fixture.renderer.getUiScale();
  bool foundCap = false;
  for (std::size_t index = 0; index < visual.textCount(); ++index) {
    TextPrimitive* text = visual.getText(index);
    if (text == nullptr) {
      continue;
    }
    foundCap = foundCap || text->content == "FPS cap";
    testTrue(
      g,
      text->y * fittedScale >= 0.0f &&
        (text->y + text->sizePt) * fittedScale <= 480.0f,
      "scrolled text stays within the small window at 4x preferred UI scale");
  }
  testTrue(g, foundCap, "scrolling to actions retains nearby display controls");
}

static void
testSoundVolumeAndCues()
{
  ConfigurationMenuFixture fixture;
  CSimSounds::resetCounts();
  SimulatorConfiguration initial = defaultConfiguration();
  initial.soundVolume = 90;
  fixture.menu.open(initial);
  fixture.menu.tick(1.0f);
  fixture.press(KeyCode::Up);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 0,
           "a selection that cannot move is silent");
  for (int row = 0; row < 15; ++row) {
    fixture.press(KeyCode::Down);
  }
  fixture.menu.update(&fixture.input);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 15,
           "every selection change plays the hover cue");

  SimulatorConfiguration parsed;
  std::string error;
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.soundVolume == 100 &&
             CSimSounds::playCount(CSimSound::MenuSelect) == 1,
           "RIGHT raises the volume by 10 and previews it");
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.soundVolume == 100 &&
             CSimSounds::playCount(CSimSound::MenuError) == 1,
           "the volume stops at 100 with the error cue");
  fixture.press(KeyCode::Enter);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.soundVolume == 0,
           "ENTER wraps the volume from 100 to off");
  fixture.press(KeyCode::Left);
  for (int step = 0; step < 3; ++step) {
    fixture.press(KeyCode::Right);
  }
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.soundVolume == 30 &&
             CSimSounds::playCount(CSimSound::MenuError) == 2,
           "LEFT stops at off and steps build the draft volume");

  fixture.menu.setError("Settings could not be applied.");
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuError) == 3,
           "a reported error plays the error cue");
  fixture.press(KeyCode::Escape);
  testTrue(g,
           fixture.menu.update(&fixture.input) ==
               ConfigurationMenuAction::Cancel &&
             CSimSounds::playCount(CSimSound::MenuBack) == 1,
           "discarding plays the back cue");

  initial.soundVolume = 250;
  fixture.menu.open(initial);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.soundVolume == 100,
           "an out-of-range stored volume opens clamped");
  CSimSounds::resetCounts();
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
  registry.add("IllumoGame.ConfigurationMenu.DisplaySettings", []() {
    return runConfigurationMenuCase(testDisplaySettingsAndScrolling);
  });
  registry.add("IllumoGame.ConfigurationMenu.Validation", []() {
    return runConfigurationMenuCase(
      testConfigurationParsingAndTopologyValidation);
  });
  registry.add("IllumoGame.ConfigurationMenu.Navigation", []() {
    return runConfigurationMenuCase(testConfigurationNavigationAndActions);
  });
  registry.add("IllumoGame.ConfigurationMenu.SoundVolume", []() {
    return runConfigurationMenuCase(testSoundVolumeAndCues);
  });
  registry.add("IllumoGame.ConfigurationMenu.Tokens", []() {
    return runConfigurationMenuCase(
      testConfigurationMenuTokensAtReleaseWindowSize);
  });
}
