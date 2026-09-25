#include "Game/CSimSounds.h"
#include "Game/ConfigurationMenu.h"
#include "Game/PerformanceOverlay.h"
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
#include <Illumo/Testing/TestAccess.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <array>
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

  void pressRepeated(KeyCode key, int count)
  {
    for (int index = 0; index < count; ++index) {
      press(key);
    }
  }

  void type(char character)
  {
    input.getCharQueue().push(static_cast<unsigned int>(character));
  }

  // Put the pointer at a menu virtual-space point.
  void pointAt(float x, float y)
  {
    const float scale = menu.getLayoutScaleForTesting();
    window.mouseX = static_cast<double>(x * scale);
    window.mouseY = static_cast<double>(y * scale);
  }

  void setMouseDown(bool down)
  {
    InputManagerTestAccess::setAction(input,
                                      KeyCode::MouseLeft,
                                      down ? InputAction::Press
                                           : InputAction::Release);
  }

  void render(Scene& scene)
  {
    renderer.BeginFrame();
    renderer.RenderScene(&scene, &camera);
    renderer.EndFrame();
  }

  SimulatorConfiguration read()
  {
    SimulatorConfiguration parsed;
    std::string error;
    menu.readConfiguration(&parsed, &error);
    return parsed;
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
  configuration.fpsCap = 60;
  return configuration;
}

static int
row(ConfigurationSetting setting)
{
  return ConfigurationMenu::rowOfSettingForTesting(setting);
}

static bool
hasText(GameVisual& visual, const std::string& content)
{
  for (std::size_t index = 0u; index < visual.textCount(); ++index) {
    TextPrimitive* text = visual.getText(index);
    if (text != nullptr && text->content == content) {
      return true;
    }
  }
  return false;
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

  // Typed values stay available as an exact-entry shortcut.
  fixture.pressRepeated(KeyCode::Down, row(ConfigurationSetting::WorldWidth));
  fixture.type('4');
  fixture.menu.update(&fixture.input);
  testTrue(g,
           !fixture.menu.readConfiguration(&parsed, &error),
           "a typed finite axis beside an infinite one is rejected");
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

  // The world sliders move both axes across the infinite boundary together.
  fixture.menu.open(defaultConfiguration());
  fixture.pressRepeated(KeyCode::Down, row(ConfigurationSetting::WorldWidth));
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.worldChunkWidth == 1 && parsed.worldChunkHeight == 1,
           "sliding width off infinite makes both axes finite");
  fixture.pressRepeated(KeyCode::Right, 3);
  fixture.press(KeyCode::Down);
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.worldChunkWidth == 8 && parsed.worldChunkHeight == 2,
           "finite axes then slide independently");
  fixture.pressRepeated(KeyCode::Left, 2);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.readConfiguration(&parsed, &error) &&
             parsed.worldChunkWidth == 0 && parsed.worldChunkHeight == 0,
           "sliding height to infinite makes both axes infinite");
}

static void
testConfigurationNavigationAndActions()
{
  testSection("ConfigurationMenu: keyboard navigation and actions");
  ConfigurationMenuFixture fixture;
  fixture.menu.open(defaultConfiguration());
  testTrue(g,
           fixture.menu.getActiveTabForTesting() ==
             ConfigurationTab::Simulation,
           "a fresh menu opens on the Simulation tab");
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
  testTrue(g,
           fixture.read().ruleSet == "BRIANS_BRAIN",
           "right on the family row moves to the next family's first rule");
  testTrue(g,
           fixture.menu.getValuePulseForTesting() > 0.0f,
           "changed values start an accent pulse");
  fixture.menu.tick(1.0f);
  testTrue(g,
           fixture.menu.getValuePulseForTesting() == 0.0f,
           "value accent pulse fades to rest");

  // Down past the last row lands on the Apply footer button.
  fixture.pressRepeated(KeyCode::Down, 10);
  fixture.menu.update(&fixture.input);
  testEqInt(g,
            fixture.menu.getSelectedRowForTesting(),
            row(ConfigurationSetting::StartPaused) + 1,
            "down stops on Apply below the tab's last row");
  fixture.press(KeyCode::Enter);
  testTrue(g,
           fixture.menu.update(&fixture.input) ==
             ConfigurationMenuAction::Apply,
           "Enter activates the Apply button");

  fixture.menu.open(defaultConfiguration());
  fixture.press(KeyCode::End);
  fixture.press(KeyCode::Left);
  fixture.press(KeyCode::Enter);
  testTrue(g,
           fixture.menu.update(&fixture.input) ==
             ConfigurationMenuAction::Cancel,
           "left walks the footer from Exit to Discard");

  fixture.menu.open(defaultConfiguration());
  fixture.press(KeyCode::End);
  fixture.press(KeyCode::Enter);
  testTrue(g,
           fixture.menu.update(&fixture.input) == ConfigurationMenuAction::Exit,
           "End and Enter activate Exit");

  // Tabs: TAB and PGDN step forward, PGUP back, all wrapping.
  fixture.menu.open(defaultConfiguration());
  fixture.press(KeyCode::Tab);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.getActiveTabForTesting() == ConfigurationTab::Canvas &&
             fixture.menu.getSelectedRowForTesting() == 0,
           "TAB opens the next tab on its first row");
  fixture.press(KeyCode::PageUp);
  fixture.press(KeyCode::PageUp);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.getActiveTabForTesting() == ConfigurationTab::General,
           "PGUP wraps backwards from the first tab to the last");
  fixture.press(KeyCode::PageDown);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.getActiveTabForTesting() ==
             ConfigurationTab::Simulation,
           "PGDN wraps forwards from the last tab to the first");

  fixture.menu.selectTabForTesting(ConfigurationTab::General);
  fixture.menu.open(defaultConfiguration());
  testTrue(g,
           fixture.menu.getActiveTabForTesting() == ConfigurationTab::General,
           "reopening keeps the last tab");
  // UI scale is a slider: Auto at the far left, then 1x to 4x in fractions.
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(
    g, fixture.read().uiScale == 1.25, "right steps UI scale 1x to 1.25x");
  fixture.pressRepeated(KeyCode::Left, 2);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.read().uiScale == 0.0,
           "left past 1x selects automatic UI scale");
  fixture.press(KeyCode::Left);
  fixture.menu.update(&fixture.input);
  testTrue(
    g, fixture.read().uiScale == 0.0, "automatic is the slider's left end");
  fixture.pressRepeated(KeyCode::Right, 20);
  fixture.menu.update(&fixture.input);
  testTrue(g, fixture.read().uiScale == 4.0, "UI scale tops out at 4x");
  SimulatorConfiguration fractional = defaultConfiguration();
  fractional.uiScale = 1.6;
  fixture.menu.open(fractional);
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.read().uiScale == 1.75,
           "an off-stop stored scale steps to the next stop");

  fixture.menu.selectTabForTesting(ConfigurationTab::Video);
  fixture.menu.open(defaultConfiguration());
  fixture.pressRepeated(KeyCode::Down, row(ConfigurationSetting::Msaa));
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g, fixture.read().msaa == 8, "right steps MSAA from 4x to 8x");

  fixture.menu.open(defaultConfiguration());
  fixture.press(KeyCode::F1);
  testTrue(g,
           fixture.menu.update(&fixture.input) ==
             ConfigurationMenuAction::Cancel,
           "F1 cancels an open menu");

  fixture.menu.selectTabForTesting(ConfigurationTab::Canvas);
  fixture.menu.open(defaultConfiguration());
  fixture.pressRepeated(KeyCode::Down, row(ConfigurationSetting::EditHints));
  fixture.press(KeyCode::Enter);
  fixture.menu.update(&fixture.input);
  SimulatorConfiguration parsed = fixture.read();
  testTrue(g, !parsed.editHints, "Enter toggles edit hints off");
  fixture.menu.open(parsed);
  testTrue(
    g, !fixture.read().editHints, "reopening preserves the hints setting");

  fixture.menu.selectTabForTesting(ConfigurationTab::General);
  fixture.menu.open(defaultConfiguration());
  fixture.pressRepeated(KeyCode::Down,
                        row(ConfigurationSetting::SoftwareCursor));
  fixture.press(KeyCode::Enter);
  fixture.menu.update(&fixture.input);
  parsed = fixture.read();
  testTrue(g,
           !parsed.softwareCursor && parsed.editHints,
           "Enter toggles the software cursor off");
  fixture.menu.open(parsed);
  testTrue(g,
           !fixture.read().softwareCursor,
           "reopening preserves the software cursor setting");
}

static void
testSliders()
{
  testSection("ConfigurationMenu: sliders");
  ConfigurationMenuFixture fixture;
  CSimSounds::resetCounts();
  fixture.menu.open(defaultConfiguration());
  fixture.pressRepeated(KeyCode::Down, row(ConfigurationSetting::Tps));
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g, fixture.read().tps == 40, "right steps TPS to the next stop");
  fixture.pressRepeated(KeyCode::Left, 2);
  fixture.menu.update(&fixture.input);
  testTrue(g, fixture.read().tps == 24, "left steps TPS back down");

  // Typing an exact value between stops, then stepping from it.
  fixture.type('7');
  fixture.menu.update(&fixture.input);
  testTrue(g, fixture.read().tps == 7, "digits type an exact TPS");
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.read().tps == 8,
           "stepping from a typed value lands on the next stop");
  fixture.pressRepeated(KeyCode::Left, 40);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.read().tps == 1 &&
             CSimSounds::playCount(CSimSound::MenuError) > 0,
           "TPS stops at 1 with the error cue");

  fixture.press(KeyCode::Down);
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           std::abs(fixture.read().speedFactor - 2.0) < 0.0001,
           "speed multiplier steps from 1.5x to 2x");

  fixture.menu.selectTabForTesting(ConfigurationTab::Video);
  fixture.pressRepeated(KeyCode::Down, row(ConfigurationSetting::FpsCap));
  fixture.pressRepeated(KeyCode::Right, 11);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.read().fpsCap == 1000,
           "the FPS cap climbs through its presets to 1000");
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.read().fpsCap == 0,
           "past 1000 the FPS cap slider reaches uncapped");
  fixture.press(KeyCode::Left);
  fixture.menu.update(&fixture.input);
  testTrue(g, fixture.read().fpsCap == 1000, "left leaves uncapped");

  fixture.menu.selectTabForTesting(ConfigurationTab::Canvas);
  fixture.pressRepeated(KeyCode::Down, row(ConfigurationSetting::Fade));
  fixture.pressRepeated(KeyCode::Left, 40);
  fixture.menu.update(&fixture.input);
  testTrue(
    g, fixture.read().fadeSpeed == 0.0, "fade speed slides down to instant");

  // Pointer: press on a track, drag along it, release.
  fixture.menu.selectTabForTesting(ConfigurationTab::Simulation);
  fixture.menu.open(defaultConfiguration());
  fixture.setMouseDown(false);
  fixture.menu.update(&fixture.input);
  const int tpsRow = row(ConfigurationSetting::Tps);
  const std::array<float, 4> track =
    fixture.menu.getSliderTrackBoundsForTesting(tpsRow);
  testTrue(g,
           track[2] > 100.0f &&
             fixture.menu.getSliderTrackBoundsForTesting(0)[2] == 0.0f,
           "slider rows have a wide track; choice rows have none");
  const float trackY = track[1] + track[3] * 0.5f;
  fixture.pointAt(track[0] + track[2], trackY);
  fixture.setMouseDown(true);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.read().tps == 1000 &&
             fixture.menu.getSelectedRowForTesting() == tpsRow,
           "pressing the track's right end selects the row and sets 1000");
  fixture.pointAt(track[0] - 200.0f, trackY + 60.0f);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.read().tps == 1 &&
             fixture.menu.getSelectedRowForTesting() == tpsRow,
           "dragging past the left end clamps without changing rows");
  fixture.pointAt(track[0] + track[2] * 0.5f, trackY);
  fixture.menu.update(&fixture.input);
  const long middle = fixture.read().tps;
  testTrue(g,
           middle > 1 && middle < 1000,
           "dragging to the middle picks a middle stop");
  const std::uint64_t selectsBefore =
    CSimSounds::playCount(CSimSound::MenuSelect);
  fixture.setMouseDown(false);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuSelect) == selectsBefore + 1,
           "releasing a moved slider plays the select cue");
  fixture.pointAt(track[0] + track[2], trackY);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.read().tps == middle,
           "after release the pointer no longer drags the value");

  // A click on a tab switches to it.
  fixture.menu.selectTabForTesting(ConfigurationTab::General);
  fixture.menu.update(&fixture.input);
  const std::array<float, 4> videoTab =
    fixture.menu.getTabBoundsForTesting(ConfigurationTab::Video);
  fixture.pointAt(videoTab[0] + videoTab[2] * 0.5f,
                  videoTab[1] + videoTab[3] * 0.5f);
  fixture.menu.update(&fixture.input);
  fixture.setMouseDown(true);
  fixture.menu.update(&fixture.input);
  fixture.setMouseDown(false);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.getActiveTabForTesting() == ConfigurationTab::Video,
           "clicking a tab switches to it");
  CSimSounds::resetCounts();
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
  fixture.render(scene);

  testTrue(g,
           fixture.mock.getLastNonEmptySubmittedCount() > 0u,
           "open settings menu emits render commands");
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) > 0u,
           "settings menu emits primitive draw tokens");

  GameVisual& visual = fixture.menu.getVisual();
  bool foundLargeTitle = false;
  bool foundReadableLabel = false;
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
  }
  testTrue(g, foundLargeTitle, "settings title uses larger text");
  testTrue(g,
           foundReadableLabel,
           "setting labels use opaque high-contrast text at readable size");
  testTrue(g,
           hasText(visual, "SIMULATION") && hasText(visual, "VIDEO") &&
             hasText(visual, "AUDIO") && hasText(visual, "GENERAL"),
           "every section tab is labelled");
  testTrue(g,
           hasText(visual, "Conway's Game of Life"),
           "ruleset value uses a human-readable display name");
  testTrue(g,
           hasText(visual, "select") && hasText(visual, "adjust") &&
             hasText(visual, "section"),
           "keyboard controls are shown as keycap hints");
  testTrue(g,
           hasText(visual, "Infinite") && hasText(visual, "30 TPS") &&
             hasText(visual, "1.5x"),
           "sliders show friendly readouts");
  testTrue(g,
           !hasText(visual, "|"),
           "sliders show no text caret until digits are typed");
  testTrue(g,
           hasText(visual, "Apply changes") &&
             hasText(visual, "Discard changes") &&
             hasText(visual, "Exit simulator"),
           "footer actions stay visible on every tab");
  testTrue(g,
           hasText(visual, "Example validation message"),
           "validation errors show in the footer");
  testTrue(g,
           hasSwitchBeside(visual, "Start paused") &&
             !hasSwitchBeside(visual, "Speed multiplier"),
           "switches render only beside boolean settings");
  testTrue(
    g, !hasText(visual, "Vertical sync"), "other tabs' settings are not drawn");
  const std::array<float, 4> exitButton =
    fixture.menu.getFooterButtonBoundsForTesting(
      ConfigurationMenu::kExitButton);
  testTrue(g,
           exitButton[1] + exitButton[3] <= 480.0f &&
             exitButton[0] + exitButton[2] <= 640.0f,
           "footer buttons fit the 640x480 window");

  fixture.menu.selectTabForTesting(ConfigurationTab::Video);
  fixture.menu.tick(1.0f);
  fixture.render(scene);
  testTrue(g,
           hasSwitchBeside(visual, "Vertical sync") &&
             hasSwitchBeside(visual, "Fullscreen") &&
             hasSwitchBeside(visual, "FPS counter") &&
             hasSwitchBeside(visual, "Memory readout") &&
             !hasSwitchBeside(visual, "FPS cap"),
           "video toggles render switches; its sliders do not");
  testTrue(g,
           hasText(visual, "60 FPS") && hasText(visual, "4x"),
           "video readouts and segments show their values");
  bool foundRestartNote = false;
  for (std::size_t index = 0u; index < visual.textCount(); ++index) {
    TextPrimitive* text = visual.getText(index);
    foundRestartNote =
      foundRestartNote ||
      (text != nullptr &&
       text->content.find("Applies after a restart") != std::string::npos);
  }
  testTrue(g, foundRestartNote, "video tab renders the restart note");

  fixture.menu.selectTabForTesting(ConfigurationTab::Canvas);
  fixture.menu.tick(1.0f);
  fixture.render(scene);
  testTrue(g,
           hasText(visual, "LED keys") && hasText(visual, "Flat") &&
             hasText(visual, "100%") && hasText(visual, "8") &&
             hasSwitchBeside(visual, "Grid lines") &&
             hasSwitchBeside(visual, "Simulation inspector") &&
             !hasSwitchBeside(visual, "Cell glow"),
           "canvas tab shows the style picker, glow and fade readouts");

  fixture.pressRepeated(KeyCode::Down, row(ConfigurationSetting::Fade));
  fixture.type('9');
  fixture.menu.update(&fixture.input);
  fixture.render(scene);
  testTrue(
    g, hasText(visual, "|"), "typing into a slider shows a visible text caret");
  const float caretGap = textCaretGap(visual, "9");
  testTrue(g,
           caretGap >= 0.0f && caretGap <= 2.0f,
           "text caret sits directly beside the final rendered glyph");
  fixture.menu.tick(0.6f);
  fixture.render(scene);
  testTrue(g,
           !hasText(visual, "|"),
           "the typing caret alternates off during its blink cycle");
  fixture.press(KeyCode::Enter);
  fixture.menu.update(&fixture.input);
  fixture.menu.tick(0.5f);
  fixture.render(scene);
  testTrue(g,
           !hasText(visual, "|") && fixture.read().fadeSpeed == 9.0,
           "Enter keeps the typed value and hides the caret");

  SimulatorConfiguration automatic = defaultConfiguration();
  automatic.uiScale = 0.0;
  fixture.menu.selectTabForTesting(ConfigurationTab::General);
  fixture.menu.open(automatic);
  fixture.render(scene);
  testTrue(g,
           hasText(visual, "Auto 1x"),
           "automatic UI scale shows the factor it picked for the window");
  fixture.menu.open(defaultConfiguration());
  fixture.render(scene);
  testTrue(g, hasText(visual, "1x"), "a fixed UI scale shows its factor");

  fixture.menu.close();
  fixture.mock.resetCounters();
  fixture.render(scene);
  testTrue(g,
           fixture.mock.getLastSubmittedCount() == 0u,
           "closed settings menu emits no commands");
}

static void
testDisplaySettingsAndScrolling()
{
  testSection("ConfigurationMenu: display settings and scrolling");
  ConfigurationMenuFixture fixture;
  fixture.menu.selectTabForTesting(ConfigurationTab::General);
  fixture.menu.open(defaultConfiguration());
  fixture.pressRepeated(KeyCode::Down,
                        row(ConfigurationSetting::ReducedMotion));
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(
    g, fixture.read().reducedUiMotion, "reduced motion toggles independently");
  testTrue(g,
           fixture.menu.getAnimationProgressForTesting() == 1.0f &&
             fixture.menu.getSelectionPositionForTesting() ==
               static_cast<float>(row(ConfigurationSetting::ReducedMotion)) &&
             fixture.menu.getValuePulseForTesting() == 0.0f,
           "reduced motion immediately snaps reveal, selection, and pulse");
  fixture.menu.selectTabForTesting(ConfigurationTab::Canvas);
  fixture.pressRepeated(KeyCode::Down, row(ConfigurationSetting::Inspector));
  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.read().showInspector && fixture.read().reducedUiMotion,
           "the inspector toggles independently of reduced motion");

  // A 640x480 window shows every row of the tallest tab.
  fixture.menu.update(&fixture.input);
  testEqInt(g,
            fixture.menu.getFirstVisibleRowForTesting(),
            0,
            "the tallest tab fits the release window without scrolling");

  // A tiny window forces the fitted panel to scroll its rows.
  fixture.window.width = 100;
  fixture.window.height = 100;
  fixture.menu.open(defaultConfiguration());
  fixture.press(KeyCode::Home);
  fixture.menu.update(&fixture.input);
  *fixture.input.getMouseScrollOffset() = -1.0;
  fixture.menu.update(&fixture.input);
  testTrue(
    g,
    fixture.menu.getSelectedRowForTesting() == 0 &&
      fixture.menu.getFirstVisibleRowForTesting() == 1 &&
      *fixture.input.getMouseScrollOffset() == 0.0,
    "wheel moves the view without selecting another row and is consumed");
  *fixture.input.getMouseScrollOffset() = -100.0;
  fixture.menu.update(&fixture.input);
  const int bottomRow = fixture.menu.getFirstVisibleRowForTesting();
  testTrue(g,
           bottomRow > 1 && fixture.menu.getSelectedRowForTesting() == 0,
           "wheel clamps at the bottom while selection stays offscreen");
  *fixture.input.getMouseScrollOffset() = 100.0;
  fixture.menu.update(&fixture.input);
  testEqInt(g,
            fixture.menu.getFirstVisibleRowForTesting(),
            0,
            "wheel clamps at the top");
  fixture.pressRepeated(KeyCode::Down, row(ConfigurationSetting::Inspector));
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.menu.getFirstVisibleRowForTesting() > 0,
           "keyboard navigation scrolls the selected row into view");

  Scene scene(&fixture.window, &fixture.camera);
  scene.AddDrawable(&fixture.menu, RenderLayerId::UI);
  fixture.menu.tick(1.0f);
  fixture.render(scene);
  GameVisual& visual = fixture.menu.getVisual();
  const float fittedScale =
    visual.getTransform().scaleX * fixture.renderer.getUiScale();
  bool withinWindow = true;
  for (std::size_t index = 0; index < visual.textCount(); ++index) {
    TextPrimitive* text = visual.getText(index);
    if (text != nullptr) {
      withinWindow = withinWindow && text->y * fittedScale >= 0.0f &&
                     (text->y + text->sizePt) * fittedScale <= 100.0f;
    }
  }
  testTrue(g, withinWindow, "fitted text stays within a tiny window");
  testTrue(g,
           hasText(visual, "Simulation inspector") &&
             hasText(visual, "Apply changes"),
           "the scrolled row and the footer are both visible");
}

static void
testCanvasControlsAndBehaviourSettings()
{
  testSection("ConfigurationMenu: canvas, controls and behaviour settings");
  ConfigurationMenuFixture fixture;
  fixture.menu.open(defaultConfiguration());
  SimulatorConfiguration parsed = fixture.read();
  testTrue(g,
           parsed.startPaused && parsed.ledCells && parsed.cellGlow == 1.0 &&
             !parsed.gridLines && !parsed.showFps && !parsed.showMemory &&
             parsed.zoomStep == 0.15 && !parsed.invertZoom &&
             parsed.panSpeed == 600 && parsed.autosaveMinutes == 0 &&
             parsed.confirmClear,
           "new settings open with their defaults");

  fixture.pressRepeated(KeyCode::Down, row(ConfigurationSetting::StartPaused));
  fixture.press(KeyCode::Enter);
  fixture.menu.update(&fixture.input);
  testTrue(g, !fixture.read().startPaused, "start paused toggles off");

  fixture.menu.selectTabForTesting(ConfigurationTab::Canvas);
  fixture.press(KeyCode::Right);
  fixture.press(KeyCode::Down);
  fixture.pressRepeated(KeyCode::Right, 2);
  fixture.press(KeyCode::Down);
  fixture.press(KeyCode::Enter);
  fixture.menu.update(&fixture.input);
  parsed = fixture.read();
  testTrue(g,
           !parsed.ledCells && parsed.cellGlow == 1.5 && parsed.gridLines,
           "cell style, glow and grid lines change from the canvas tab");

  fixture.menu.selectTabForTesting(ConfigurationTab::Video);
  fixture.pressRepeated(KeyCode::Down, row(ConfigurationSetting::ShowFps));
  fixture.press(KeyCode::Enter);
  fixture.press(KeyCode::Down);
  fixture.press(KeyCode::Enter);
  fixture.menu.update(&fixture.input);
  parsed = fixture.read();
  testTrue(g,
           parsed.showFps && parsed.showMemory,
           "FPS counter and memory readout switch on");

  fixture.menu.selectTabForTesting(ConfigurationTab::Controls);
  fixture.press(KeyCode::Right);
  fixture.press(KeyCode::Down);
  fixture.press(KeyCode::Enter);
  fixture.press(KeyCode::Down);
  fixture.pressRepeated(KeyCode::Left, 10);
  fixture.menu.update(&fixture.input);
  parsed = fixture.read();
  testTrue(g,
           parsed.zoomStep == 0.2 && parsed.invertZoom && parsed.panSpeed == 0,
           "zoom step, invert zoom and pan speed change from controls");

  fixture.menu.selectTabForTesting(ConfigurationTab::General);
  fixture.pressRepeated(KeyCode::Down, row(ConfigurationSetting::Autosave));
  fixture.pressRepeated(KeyCode::Right, 3);
  fixture.press(KeyCode::Down);
  fixture.press(KeyCode::Enter);
  fixture.menu.update(&fixture.input);
  parsed = fixture.read();
  testTrue(g,
           parsed.autosaveMinutes == 5 && !parsed.confirmClear,
           "autosave interval and confirm clearing change from general");

  Scene scene(&fixture.window, &fixture.camera);
  scene.AddDrawable(&fixture.menu, RenderLayerId::UI);
  fixture.render(scene);
  testTrue(g,
           hasText(fixture.menu.getVisual(), "5 min"),
           "autosave shows its interval in minutes");

  fixture.menu.open(parsed);
  const SimulatorConfiguration reopened = fixture.read();
  testTrue(g,
           !reopened.startPaused && !reopened.ledCells &&
             reopened.cellGlow == 1.5 && reopened.gridLines &&
             reopened.showFps && reopened.showMemory &&
             reopened.zoomStep == 0.2 && reopened.invertZoom &&
             reopened.panSpeed == 0 && reopened.autosaveMinutes == 5 &&
             !reopened.confirmClear,
           "reopening preserves every new setting");
}

static void
testPerformanceOverlay()
{
  testSection("PerformanceOverlay: corner FPS and memory readout");
  ConfigurationMenuFixture fixture;
  PerformanceOverlay overlay;
  overlay.prepare(&fixture.window, &fixture.renderer);
  overlay.update(1.0f / 60.0f, false, false, 0u);
  testTrue(g, !overlay.isVisible(), "the readout hides with both lines off");
  // A frame past half a second, whatever the float rounding of 30 steps.
  for (int frame = 0; frame < 31; ++frame) {
    overlay.update(1.0f / 60.0f, true, false, 0u);
  }
  testTrue(g,
           std::abs(overlay.fps() - 60.0f) < 0.5f &&
             std::abs(overlay.frameMilliseconds() - 16.67f) < 0.1f &&
             overlay.fpsText() == "60 FPS  16.7 ms",
           "half a second of frames refreshes FPS and frame time");
  testTrue(g,
           overlay.isVisible() &&
             hasText(overlay.getVisual(), "60 FPS  16.7 ms") &&
             !hasText(overlay.getVisual(), overlay.memoryText()),
           "only the FPS line draws when memory is off");
  overlay.update(1.0f / 60.0f, false, true, 48u * 1024u * 1024u);
  testTrue(g,
           overlay.memoryText() == "Memory 48.0 MiB" &&
             hasText(overlay.getVisual(), "Memory 48.0 MiB") &&
             !hasText(overlay.getVisual(), "60 FPS  16.7 ms"),
           "the memory line shows the reported size in MiB");
  overlay.update(0.0f, false, true, 0u);
  testTrue(g,
           hasText(overlay.getVisual(), "Memory --"),
           "an unknown memory size reads as a dash");
}

static void
testSoundVolumeAndCues()
{
  testSection("ConfigurationMenu: sound volume and cues");
  ConfigurationMenuFixture fixture;
  SimulatorConfiguration initial = defaultConfiguration();
  initial.soundVolume = 90;
  fixture.menu.selectTabForTesting(ConfigurationTab::Audio);
  fixture.menu.open(initial);
  fixture.menu.tick(1.0f);
  CSimSounds::resetCounts();
  fixture.press(KeyCode::Up);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 0,
           "a selection that cannot move is silent");
  fixture.press(KeyCode::Down);
  fixture.press(KeyCode::Right);
  fixture.press(KeyCode::Up);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 3 &&
             fixture.menu.getSelectedRowForTesting() == 0,
           "every selection change, footer included, plays the hover cue");

  fixture.press(KeyCode::Right);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.read().soundVolume == 95 &&
             CSimSounds::playCount(CSimSound::MenuSelect) == 1,
           "RIGHT raises the volume by 5 and previews it");
  fixture.pressRepeated(KeyCode::Right, 2);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.read().soundVolume == 100 &&
             CSimSounds::playCount(CSimSound::MenuError) == 1,
           "the volume stops at 100 with the error cue");
  fixture.pressRepeated(KeyCode::Left, 21);
  fixture.menu.update(&fixture.input);
  testTrue(g,
           fixture.read().soundVolume == 0 &&
             CSimSounds::playCount(CSimSound::MenuError) == 2,
           "LEFT stops at off");
  fixture.pressRepeated(KeyCode::Right, 6);
  fixture.menu.update(&fixture.input);
  testTrue(g, fixture.read().soundVolume == 30, "steps build the draft volume");

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
           fixture.read().soundVolume == 100,
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
  registry.add("IllumoGame.ConfigurationMenu.Sliders",
               []() { return runConfigurationMenuCase(testSliders); });
  registry.add("IllumoGame.PerformanceOverlay.Readout", []() {
    return runConfigurationMenuCase(testPerformanceOverlay);
  });
  registry.add("IllumoGame.ConfigurationMenu.CanvasControlsBehaviour", []() {
    return runConfigurationMenuCase(testCanvasControlsAndBehaviourSettings);
  });
  registry.add("IllumoGame.ConfigurationMenu.SoundVolume", []() {
    return runConfigurationMenuCase(testSoundVolumeAndCues);
  });
  registry.add("IllumoGame.ConfigurationMenu.Tokens", []() {
    return runConfigurationMenuCase(
      testConfigurationMenuTokensAtReleaseWindowSize);
  });
}
