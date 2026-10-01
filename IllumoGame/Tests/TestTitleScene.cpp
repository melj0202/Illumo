#include "Game/CSimPlatform.h"
#include "Game/CSimScenes.h"
#include "Game/CSimSounds.h"
#include "Game/CanvasScene.h"
#include "Game/RuleCatalogLoader.h"
#include "Game/SimulatorSettings.h"
#include "Game/TitleScene.h"
#include "Rulesets/RuleSetRegistry.h"
#include "TestAccess.h"
#include "TestHarness.h"
#include <Illumo/Content/SceneDirector.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/TestAccess.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

static TestCounters g;

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
  DrawList scene;
  IllumoContext context;
  // The program's director: choosing a canvas adds it through CSimScenes.
  SceneDirector director;
  TitleScene module;
  bool started;

  // startTitle false leaves the title to the director (CSimScenes flows).
  explicit MainMenuFixture(bool startTitle = true)
    : window(640, 480)
    , env()
    , camera(glm::vec2(0.0f, 0.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , registry()
    , console(&env, &registry, &window, &renderer)
    , input(nullptr)
    , scene(&window, &camera)
    , context{ &scene,  &window, &console, &input,   &renderer,
               nullptr, &env,    &camera,  &registry }
    , director(context)
    , module()
    , started(false)
  {
    RuleCatalogLoader::loadFromDefaultLocations(RuleSetRegistry::instance());
    // Preferences are explicit so repeated runs cannot inherit a saved draft.
    env.setVar("fps", 60);
    env.setVar("showInspector", false);
    env.setVar("reducedUiMotion", false);
    env.setVar("uiScale", 1);
    env.setVar("WinX", 640);
    env.setVar("WinY", 480);
    env.setVar("FamilyString", "LIFE_LIKE_BINARY");
    env.setVar("RuleSetString", "GAME_OF_LIFE");
    env.setVar("ModeString", "GAME_OF_LIFE");
    env.setVar("tps", 30);
    env.setVar("startPaused", true);
    env.setVar("cellStyle", "led");
    env.setVar("cellGlow", 1.0);
    env.setVar("gridLines", false);
    env.setVar("zoomStep", 0.15);
    env.setVar("invertZoom", false);
    env.setVar("panSpeed", 600);
    env.setVar("autosaveMinutes", 0);
    env.setVar("confirmClear", true);
    mock.Initialize();
    context.scenes = &director;
    started = startTitle && module.start(context);
  }

  ~MainMenuFixture()
  {
    if (started) {
      module.stop();
    }
  }
};

// The title screen's layers: its first UI drawables, back to front.
static std::vector<GameVisual*>
menuLayers(DrawList& scene)
{
  std::vector<GameVisual*> layers;
  for (DrawableBase* drawable : scene.drawablesIn(RenderLayerId::UI)) {
    if (layers.size() == TitleScene::menuLayerCountForTesting()) {
      break;
    }
    layers.push_back(static_cast<GameVisual*>(drawable));
  }
  return layers;
}

// Every text of the title screen's layers, in draw order.
static std::vector<const TextPrimitive*>
menuTexts(DrawList& scene)
{
  std::vector<const TextPrimitive*> texts;
  for (const GameVisual* visual : menuLayers(scene)) {
    for (size_t index = 0; index < visual->textCount(); ++index) {
      texts.push_back(visual->getText(index));
    }
  }
  return texts;
}

static void
testMainMenuStartAndDrawables()
{
  testSection("TitleScene: start and drawables");
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "valid context starts TitleScene");
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            0,
            "default selected item is Play");
  testTrue(
    g, !fixture.module.isSettingsOpenForTesting(), "settings start closed");

  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  testEqSize(g,
             fixture.scene.drawableCount(),
             1u + TitleScene::menuLayerCountForTesting(),
             "menu dispatches ambient canvas and its UI layers");
  testEqSize(g,
             fixture.scene.drawablesIn(RenderLayerId::World).size(),
             1u,
             "ambient canvas is in World layer");
  testEqSize(g,
             fixture.scene.drawablesIn(RenderLayerId::UI).size(),
             TitleScene::menuLayerCountForTesting(),
             "menu layers are in UI layer");
}

// Every text the title screen draws this frame that starts with 'v' and a
// digit: the build version and nothing else.
static std::vector<TextPrimitive>
versionTexts(MainMenuFixture& fixture)
{
  fixture.module.update(1.0 / 60.0);
  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  std::vector<TextPrimitive> found;
  for (const TextPrimitive* text : menuTexts(fixture.scene)) {
    if (text->content.size() > 1 && text->content[0] == 'v' &&
        text->content[1] >= '0' && text->content[1] <= '9') {
      found.push_back(*text);
    }
  }
  return found;
}

static void
testMainMenuShowsVersion()
{
  testSection("TitleScene: build version in the corner");
  CSimPlatform& platform = CSimPlatform::current();
  const std::string previous = platform.packageVersion();
  {
    MainMenuFixture fixture;
    platform.setPackageVersion(std::string());
    testTrue(g,
             versionTexts(fixture).empty(),
             "no version is drawn when the manifest supplied none");

    platform.setPackageVersion("26.09_7");
    const std::vector<TextPrimitive> texts = versionTexts(fixture);
    testEqSize(g, texts.size(), 1u, "the version is drawn once");
    if (!texts.empty()) {
      testTrue(g,
               texts.front().content == "v26.09_7",
               "the menu shows v<release>_<build>");
      testTrue(g,
               texts.front().x > 320.0f && texts.front().y > 240.0f,
               "the version sits in the lower right corner");
    }
  }
  platform.setPackageVersion(previous);
}

static void
testTitleRasterResolution()
{
  MainMenuFixture fixture;
  fixture.window.handleResize(3840, 2160);
  for (int scale : { 1, 2, 4 }) {
    fixture.env.setVar("uiScale", scale);
    fixture.module.update(0.0);
    fixture.scene.ClearDrawables();
    fixture.module.dispatch(fixture.scene);
    const float scaleY =
      menuLayers(fixture.scene).front()->getTransform().scaleY;
    // The title is drawn letter by letter so each can animate; every letter
    // shares the dedicated title atlas.
    std::string title;
    bool sharp = true;
    for (const TextPrimitive* text : menuTexts(fixture.scene)) {
      if (text->font == nullptr || text->font == Font::getDefaultFont()) {
        continue;
      }
      title += text->content;
      // Letters overshoot by up to 8% while landing.
      sharp =
        sharp && text->font->getMetrics().pixelSize >=
                   text->sizePt * 1.08f * scaleY * static_cast<float>(scale);
    }
    testTrue(g,
             title == "CSIM",
             "main-menu title letters use their own atlas, not the default");
    testTrue(
      g, sharp, "title glyphs are never magnified at supported UI scales");
  }
}

// The title letters as drawn this frame, in order.
static std::vector<TextPrimitive>
titleLetters(MainMenuFixture& fixture)
{
  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  std::vector<TextPrimitive> letters;
  for (const TextPrimitive* text : menuTexts(fixture.scene)) {
    if (text->font != nullptr && text->font != Font::getDefaultFont()) {
      letters.push_back(*text);
    }
  }
  return letters;
}

static float
stretchOf(const TextPrimitive& text)
{
  return std::max(std::abs(text.stretchX - 1.0f),
                  std::abs(text.stretchY - 1.0f));
}

static void
testTitleLettersPose()
{
  testSection("TitleScene: title letters pose one at a time");
  MainMenuFixture fixture;
  float strongest = 0.0f;
  int soloFrames = 0;
  bool fourLetters = true;
  for (int frame = 0; frame < 720; ++frame) {
    fixture.module.update(1.0 / 60.0);
    if (frame < 120) {
      continue; // the entrance
    }
    const std::vector<TextPrimitive> letters = titleLetters(fixture);
    fourLetters = fourLetters && letters.size() == 4u;
    int posing = 0;
    for (const TextPrimitive& letter : letters) {
      strongest = std::max(strongest, stretchOf(letter));
      if (stretchOf(letter) > 0.08f) {
        ++posing;
      }
    }
    if (posing == 1) {
      ++soloFrames;
    }
  }
  testTrue(g, fourLetters, "the word is drawn as four letters");
  testTrue(g, strongest > 0.15f, "resting letters squash and stretch in poses");
  testTrue(g, soloFrames > 30, "single letters pose on their own");

  MainMenuFixture still;
  still.env.setVar("reducedUiMotion", true);
  float stillest = 0.0f;
  for (int frame = 0; frame < 480; ++frame) {
    still.module.update(1.0 / 60.0);
    for (const TextPrimitive& letter : titleLetters(still)) {
      stillest = std::max(stillest, stretchOf(letter));
    }
  }
  testTrue(g, stillest == 0.0f, "reduced motion keeps the letters unstretched");
}

static void
testTitlePokeHops()
{
  testSection("TitleScene: clicking the title sends a hop through it");
  // A control menu gets the same pointer without the click, so the only
  // difference between the two is the hop.
  MainMenuFixture fixture;
  MainMenuFixture control;
  for (int frame = 0; frame < 96; ++frame) {
    fixture.module.update(1.0 / 60.0);
    control.module.update(1.0 / 60.0);
  }
  const std::array<float, 4> bounds = fixture.module.titleBoundsForTesting();
  testTrue(g, bounds[2] > 0.0f && bounds[3] > 0.0f, "the title has bounds");
  const float scale = fixture.module.layoutScaleForTesting();
  for (MainMenuFixture* menu : { &fixture, &control }) {
    menu->window.mouseX = (bounds[0] + bounds[2] * 0.5f) * scale;
    menu->window.mouseY = (bounds[1] + bounds[3] * 0.5f) * scale;
  }
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(1.0 / 60.0);
  control.module.update(1.0 / 60.0);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  float firstLift = 0.0f;
  float lastLift = 0.0f;
  for (int frame = 0; frame < 30; ++frame) {
    fixture.module.update(1.0 / 60.0);
    control.module.update(1.0 / 60.0);
    const std::vector<TextPrimitive> poked = titleLetters(fixture);
    const std::vector<TextPrimitive> still = titleLetters(control);
    firstLift = std::max(firstLift, still.front().y - poked.front().y);
    lastLift = std::max(lastLift, still.back().y - poked.back().y);
  }
  testTrue(g, firstLift > 5.0f && lastLift > 5.0f, "every letter hops");
  testTrue(g,
           !fixture.director.hasPendingSwitch() &&
             !fixture.module.isSettingsOpenForTesting() &&
             !fixture.module.isCanvasSetupOpenForTesting(),
           "poking the title activates nothing");
}

static void
testMainMenuKeepsRulePreference()
{
  testSection("TitleScene: the ambient world keeps the player's ruleset");
  MainMenuFixture fixture;
  fixture.module.stop();
  fixture.env.setVar("FamilyString", "LIFE_LIKE_BINARY");
  fixture.env.setVar("RuleSetString", "HIGHLIFE");
  fixture.env.setVar("ModeString", "HIGHLIFE");
  testTrue(g, fixture.module.start(fixture.context), "menu restarts");
  testTrue(g,
           fixture.env.getVar("RuleSetString").value == "HIGHLIFE" &&
             fixture.env.getVar("ModeString").value == "HIGHLIFE" &&
             fixture.env.getVar("FamilyString").value == "LIFE_LIKE_BINARY",
           "the background ruleset never replaces the saved preference");
}

static void
testMainMenuAmbientWorld()
{
  testSection("TitleScene: the ambient world is seeded and alive");
  MainMenuFixture fixture;
  fixture.window.handleResize(1280, 720);
  const CellContext* world = fixture.module.ambientContextForTesting();
  testTrue(g,
           world != nullptr && world->getRuleSet() != nullptr &&
             world->getModeString() == "IMMIGRATION",
           "the background runs Immigration Life");
  const std::size_t seeded =
    world != nullptr ? world->getGrid()->getAllocatedChunkCount() : 0u;
  testTrue(g, seeded > 0u, "the gun and methuselahs are stamped");
  for (int frame = 0; frame < 600; ++frame) {
    fixture.module.update(1.0 / 60.0);
  }
  testTrue(g,
           world != nullptr && world->getGrid()->getAllocatedChunkCount() > 0u,
           "the world is still alive after ten seconds");
}

static void
testMainMenuPrimitiveBudget()
{
  testSection("TitleScene: primitive budget at 720p and 4K");
  MainMenuFixture fixture;
  const int sizes[2][3] = { { 1280, 720, 1 }, { 3840, 2160, 4 } };
  for (const int* size : sizes) {
    fixture.window.handleResize(size[0], size[1]);
    fixture.env.setVar("uiScale", size[2]);
    // Walk through the entrance, a selection change and a press.
    for (int frame = 0; frame < 30; ++frame) {
      fixture.module.update(1.0 / 60.0);
    }
    fixture.module.selectItemForTesting(2);
    fixture.module.update(0.1);
    fixture.scene.ClearDrawables();
    fixture.module.dispatch(fixture.scene);
    testEqSize(g,
               fixture.scene.drawablesIn(RenderLayerId::UI).size(),
               TitleScene::menuLayerCountForTesting(),
               "the title screen is only its layers while no overlay is open");
    unsigned int quads = 0;
    fixture.renderer.BeginFrame();
    for (GameVisual* visual : menuLayers(fixture.scene)) {
      visual->AppendCommands(&fixture.renderer);
      quads += visual->builtQuadCount();
    }
    fixture.renderer.EndFrame();
    testTrue(g,
             quads > 200u && quads <= 2600u,
             "the living title screen stays within its quad budget");
  }
}

static void
testMainMenuNavigation()
{
  testSection("TitleScene: keyboard navigation");
  CSimSounds::resetCounts();
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "menu started");

  // Key Down: 0 -> 1 -> 2 -> 3 -> 0
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Down, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            1,
            "Down moves to item 1 (Load)");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Down, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            2,
            "Down moves to item 2 (Settings)");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Down, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            3,
            "Down moves to item 3 (Exit)");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Down, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            0,
            "Down wraps back to item 0 (Play)");

  // Key Up: 0 -> 3
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Up, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            3,
            "Up wraps to item 3 (Exit)");
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 5 &&
             CSimSounds::playCount(CSimSound::MenuSelect) == 0,
           "every selection move plays one hover cue");
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Up, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           fixture.module.isSettingsOpenForTesting() &&
             CSimSounds::playCount(CSimSound::MenuSelect) == 1,
           "activating an item plays the select cue");
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           !fixture.module.isSettingsOpenForTesting() &&
             CSimSounds::playCount(CSimSound::MenuBack) == 1,
           "leaving settings plays the back cue");
  testTrue(g,
           CSimSounds::musicPlaying(CSimMusic::MainMenu),
           "the main menu loops its music through its overlays");
  fixture.module.leave();
  testTrue(g,
           !CSimSounds::musicPlaying(CSimMusic::MainMenu),
           "leaving the main menu stops its music");
  fixture.module.enter();
  testTrue(g,
           CSimSounds::musicPlaying(CSimMusic::MainMenu),
           "returning to the main menu starts its music again");
  CSimSounds::resetCounts();
}

static void
testMainMenuPlayTransition()
{
  testSection("TitleScene: Play action requests transition");
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "menu started");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.016);

  testTrue(g,
           fixture.module.isCanvasSetupOpenForTesting() &&
             !fixture.module.isSettingsOpenForTesting() &&
             !fixture.director.hasPendingSwitch(),
           "New simulation opens its own setup without starting");
  fixture.input.getKeyQueue().push({ KeyCode::End, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Up, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(
    g, fixture.director.hasPendingSwitch(), "Create requests the canvas scene");
  testTrue(g,
           fixture.director.has(CSimScenes::kCanvas),
           "the new canvas scene is added");
}

static void
testMainMenuSettingsWorkflow()
{
  testSection("TitleScene: Settings dialog open and close");
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "menu started");

  // Select Settings (item 2)
  fixture.module.selectItemForTesting(2);
  fixture.module.activateSelectedItemForTesting();
  testTrue(g,
           fixture.module.isSettingsOpenForTesting(),
           "activating Settings opens dialog");

  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  testEqSize(g,
             fixture.scene.drawablesIn(RenderLayerId::UI).size(),
             TitleScene::menuLayerCountForTesting() + 1u,
             "settings menu is dispatched after the title screen's layers");

  // Close Settings with Escape
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.update(0.016);
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
  fixture.module.update(0.016);
  testTrue(g,
           fixture.module.isSettingsOpenForTesting(),
           "F1 opens settings on the main menu");
  // VIDEO tab (third), FPS cap row (third), one stop up, then down to Apply.
  fixture.input.getKeyQueue().push({ KeyCode::Tab, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Tab, InputAction::Press, 0 });
  for (int row = 0; row < 2; ++row) {
    fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  }
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0 });
  // Down past the tab's last row lands on Apply.
  for (int row = 0; row < 8; ++row) {
    fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  }
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.016);
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
  fixture.module.update(0.016);
  // The menu reopens on the VIDEO tab it closed on.
  for (int row = 0; row < 2; ++row) {
    fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  }
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           fixture.env.getVar("fps").valueAsLong == 165,
           "Discard preserves the applied FPS cap");
}

static void
testMainMenuRestartPrompt()
{
  MainMenuFixture fixture;
  fixture.env.setVar("reducedUiMotion", true);
  fixture.env.setVar("msaa", 4);
  fixture.window.msaaSamples = 4;
  std::queue<InputManager::KeyPressEvent>& keys = fixture.input.getKeyQueue();
  keys.push({ KeyCode::F1, InputAction::Press, 0 });
  fixture.module.update(0.016);
  // VIDEO tab (third), Anti-aliasing row (fourth) 4x -> 8x, then Apply.
  keys.push({ KeyCode::Tab, InputAction::Press, 0 });
  keys.push({ KeyCode::Tab, InputAction::Press, 0 });
  for (int row = 0; row < 3; ++row) {
    keys.push({ KeyCode::Down, InputAction::Press, 0 });
  }
  keys.push({ KeyCode::Right, InputAction::Press, 0 });
  for (int row = 0; row < 8; ++row) {
    keys.push({ KeyCode::Down, InputAction::Press, 0 });
  }
  keys.push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           !fixture.module.isSettingsOpenForTesting() &&
             fixture.module.isRestartPromptOpenForTesting() &&
             fixture.env.getVar("msaa").valueAsLong == 8,
           "applying a new MSAA from the main menu offers to restart");
  // Escape answers Later; on the main menu behind it, it would quit.
  keys.push({ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           !fixture.module.isRestartPromptOpenForTesting() &&
             !fixture.window.closeRequested &&
             !fixture.window.restartRequested(),
           "Escape answers Later without reaching the menu behind it");

  // Applying again (MSAA still differs from the window) asks again.
  keys.push({ KeyCode::F1, InputAction::Press, 0 });
  fixture.module.update(0.016);
  keys.push({ KeyCode::End, InputAction::Press, 0 });
  keys.push({ KeyCode::Left, InputAction::Press, 0 });
  keys.push({ KeyCode::Left, InputAction::Press, 0 });
  keys.push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.016);
  keys.push({ KeyCode::Y, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           fixture.window.restartRequested() && fixture.window.closeRequested,
           "Restart now asks the host to close and relaunch");
}

static void
testSettingsMouseIsolation()
{
  MainMenuFixture fixture;
  fixture.env.setVar("reducedUiMotion", true);
  fixture.env.setVar("fps", 60);
  fixture.input.getKeyQueue().push({ KeyCode::F1, InputAction::Press, 0 });
  fixture.module.update(0.016);
  // VIDEO tab (third), FPS cap row, one stop up (60 -> 75).
  fixture.input.getKeyQueue().push({ KeyCode::Tab, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Tab, InputAction::Press, 0 });
  for (int row = 0; row < 2; ++row) {
    fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  }
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0 });
  fixture.module.update(0.016);
  // The Apply footer button sits over the underlying main-menu cards.
  const ConfigurationMenu* settings = fixture.module.settingsMenuForTesting();
  const std::array<float, 4> apply =
    settings->getFooterButtonBoundsForTesting(ConfigurationMenu::kApplyButton);
  const float scale = settings->getLayoutScaleForTesting();
  fixture.window.mouseX =
    static_cast<double>((apply[0] + apply[2] * 0.5f) * scale);
  fixture.window.mouseY =
    static_cast<double>((apply[1] + apply[3] * 0.5f) * scale);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  testTrue(g,
           !fixture.module.isSettingsOpenForTesting() &&
             fixture.env.getVar("fps").valueAsLong == 75,
           "clicking the Apply button commits the draft");
  fixture.module.update(0.016);
  testTrue(g,
           !fixture.window.closeRequested &&
             !fixture.director.hasPendingSwitch(),
           "holding Apply does not activate the underlying main-menu action");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
  fixture.input.getKeyQueue().push({ KeyCode::F1, InputAction::Press, 0 });
  fixture.module.update(0.016);
  fixture.input.getKeyQueue().push({ KeyCode::End, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           fixture.window.closeRequested,
           "settings Exit requests normal close from the main menu");
}

static void
testMainMenuYieldsToConsole()
{
  testSection("TitleScene: open console blocks menu input");
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "menu started");

  fixture.console.Toggle();
  testTrue(g, fixture.console.isOpen, "console is open");
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Down, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            0,
            "open console blocks Down navigation");
  testTrue(g,
           !fixture.director.hasPendingSwitch(),
           "open console does not activate Play");

  fixture.console.Toggle();
  testTrue(g, !fixture.console.isOpen, "console is closed");
  fixture.input.clearKeyQueue();
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Grave, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Down, InputAction::Press, 0 });
  fixture.module.update(0.016);
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
  testSection("TitleScene: open console blocks settings input");
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
  fixture.module.update(0.016);
  testTrue(g,
           fixture.module.isSettingsOpenForTesting(),
           "open console blocks settings Escape");
}

static void
testMainMenuConsoleCommands()
{
  testSection("TitleScene: console commands");
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "menu started");
  testTrue(
    g, fixture.registry.HasCommand("play"), "play command is registered");

  fixture.registry.QueueCommand("play", {});
  fixture.registry.ExecuteQueue();

  testTrue(g,
           fixture.module.isCanvasSetupOpenForTesting() &&
             !fixture.director.hasPendingSwitch(),
           "play command opens canvas setup");
}

// The title and the canvas keep CSim's settings through SimulatorSettings
// (D-E31 M4c): stored values out of range read as the product defaults on
// either screen, and both refuse the same configurations.
static void
testSettingsShared()
{
  testSection("TitleScene: settings read and check like the canvas's");
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "menu started");
  fixture.env.setVar("tps", 5000);
  fixture.env.setVar("speedFactor", 0.0);
  fixture.env.setVar("cellFadeSpeed", -1.0);
  fixture.env.setVar("uiScale", 0.5);
  fixture.env.setVar("WorldChunksX", 4);
  fixture.env.setVar("WorldChunksY", 0);
  fixture.env.setVar("RuleSetString", "NOT_A_RULE");

  SimulatorConfiguration stored;
  SimulatorSettings::read(&fixture.env, &stored);
  testTrue(g,
           stored.tps == SimulatorSettings::kDefaultTps &&
             stored.speedFactor == 1.0 &&
             stored.fadeSpeed == SimulatorSettings::kDefaultFadeSpeed &&
             stored.uiScale == 1.0 && stored.worldChunkWidth == 0 &&
             stored.worldChunkHeight == 0 && stored.ruleSet == "GAME_OF_LIFE" &&
             stored.family == "LIFE_LIKE_BINARY",
           "settings out of range read as the product defaults");
  testTrue(g, SimulatorSettings::valid(stored), "what is read can be applied");

  fixture.module.selectItemForTesting(2);
  fixture.module.activateSelectedItemForTesting();
  const ConfigurationMenu* menu = fixture.module.settingsMenuForTesting();
  SimulatorConfiguration shown;
  testTrue(g,
           fixture.module.isSettingsOpenForTesting() && menu != nullptr &&
             menu->readConfiguration(&shown, nullptr) &&
             shown.tps == stored.tps && shown.fadeSpeed == stored.fadeSpeed &&
             shown.ruleSet == stored.ruleSet && shown.worldChunkWidth == 0 &&
             shown.worldChunkHeight == 0,
           "the title's Settings menu opens on the shared read");

  SimulatorConfiguration refused = stored;
  refused.tps = 0;
  bool anyAccepted = SimulatorSettings::valid(refused);
  refused = stored;
  refused.uiScale = 0.5;
  anyAccepted = anyAccepted || SimulatorSettings::valid(refused);
  refused = stored;
  refused.worldChunkWidth = 4;
  anyAccepted = anyAccepted || SimulatorSettings::valid(refused);
  refused = stored;
  refused.family = "WIREWORLD_FAMILY";
  anyAccepted = anyAccepted || SimulatorSettings::valid(refused);
  testTrue(g, !anyAccepted, "out-of-range and mismatched settings are refused");

  SimulatorConfiguration changed = stored;
  changed.tps = 120;
  changed.fadeSpeed = 2.5;
  changed.worldChunkWidth = 3;
  changed.worldChunkHeight = 5;
  changed.panSpeed = 900;
  SimulatorSettings::write(&fixture.env, changed);
  SimulatorConfiguration reread;
  SimulatorSettings::read(&fixture.env, &reread);
  testTrue(g,
           reread.tps == 120 && reread.fadeSpeed == 2.5 &&
             reread.worldChunkWidth == 3 && reread.worldChunkHeight == 5 &&
             reread.panSpeed == 900 && reread.ruleSet == "GAME_OF_LIFE",
           "written settings read back");

  // The renderer reads as OpenGL unless the setting names Vulkan or
  // Direct3D 12, and a choice unlike the running window's asks for a restart.
  fixture.env.setVar("GraphicsAPI", "vulkan");
  SimulatorConfiguration vulkan;
  SimulatorSettings::read(&fixture.env, &vulkan);
  fixture.env.setVar("GraphicsAPI", "metal");
  SimulatorConfiguration fallback;
  SimulatorSettings::read(&fixture.env, &fallback);
  NullRenderWindow running;
  const bool unknownAsks = SimulatorSettings::restartNeeded(vulkan, &running);
  running.graphicsApiName = "OPENGL";
  testTrue(g,
           vulkan.graphicsApi == "VULKAN" && fallback.graphicsApi == "OPENGL" &&
             !unknownAsks &&
             SimulatorSettings::restartNeeded(vulkan, &running) &&
             !SimulatorSettings::restartNeeded(fallback, &running),
           "a renderer unlike the running window's asks for a restart");
  SimulatorSettings::write(&fixture.env, vulkan);
  testTrue(g,
           fixture.env.getVar("GraphicsAPI").value == "VULKAN",
           "the renderer choice is written as GraphicsAPI");
  fixture.env.setVar("GraphicsAPI", "d3d12");
  SimulatorConfiguration direct3d;
  SimulatorSettings::read(&fixture.env, &direct3d);
  running.graphicsApiName = "DIRECTX12";
  testTrue(g,
           direct3d.graphicsApi == "DIRECTX12" &&
             !SimulatorSettings::restartNeeded(direct3d, &running),
           "d3d12 reads as the DirectX 12 renderer it is running on");

  // The canvas reads the same settings over its live world, and refuses what
  // the title refuses.
  fixture.module.stop();
  fixture.started = false;
  fixture.env.setVar("tps", 5000);
  CanvasScene canvas{ NewSimulationConfiguration() };
  testTrue(g, canvas.start(fixture.context), "canvas started");
  const SimulatorConfiguration live =
    CanvasSceneTestAccess::currentConfiguration(canvas);
  testTrue(g,
           live.tps == SimulatorSettings::kDefaultTps &&
             live.fadeSpeed == 2.5 && live.panSpeed == 900,
           "the canvas reads the shared settings");
  refused = live;
  refused.tps = 0;
  testTrue(g,
           !CanvasSceneTestAccess::applyConfiguration(canvas, refused),
           "the canvas refuses what the title refuses");
  canvas.stop();
}

static void
testCanvasSetupValidation()
{
  MainMenuFixture fixture;
  NewSimulationConfiguration config;
  config.worldChunkWidth = 2;
  config.worldChunkHeight = 0;
  CanvasScene invalid(config);
  testTrue(g,
           !invalid.start(fixture.context),
           "mixed infinite/finite topology rejected before startup");
  config.worldChunkHeight = 2;
  config.ruleSet = "NOT_A_RULE";
  testTrue(g, !config.isValid(), "unknown ruleset rejected");
  config.ruleSet = "WIREWORLD";
  config.family = "WIREWORLD_FAMILY";
  fixture.module.stop();
  fixture.started = false;
  CanvasScene seeded(config);
  testTrue(
    g, seeded.start(fixture.context), "valid non-default ruleset starts");
  CellContext* cells = CanvasSceneTestAccess::getCellContext(seeded);
  testTrue(g,
           cells && cells->getModeString() == "WIREWORLD" &&
             cells->getGrid()->getAllocatedChunkCount() > 0,
           "chosen ruleset and starter pattern reach canvas");
  seeded.stop();
}

static void
testCanvasSetupDraft()
{
  MainMenuFixture fixture;
  fixture.env.setVar("WorldChunksX", 0);
  fixture.env.setVar("WorldChunksY", 0);
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           !fixture.director.hasPendingSwitch(),
           "opening Enter cannot create a canvas");
  fixture.input.getKeyQueue().push({ KeyCode::F1, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           !fixture.module.isCanvasSetupOpenForTesting() &&
             !fixture.module.isSettingsOpenForTesting() &&
             !fixture.director.hasPendingSwitch(),
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
                       KeyCode::Down,
                       KeyCode::Right,
                       KeyCode::Down,
                       KeyCode::Right,
                       KeyCode::Down,
                       KeyCode::Down,
                       KeyCode::Enter,
                       KeyCode::Down,
                       KeyCode::Enter }) {
    fixture.input.getKeyQueue().push({ key, InputAction::Press, 0 });
  }
  fixture.module.update(0.016);
  testTrue(
    g, fixture.director.hasPendingSwitch(), "configured canvas is submitted");
  if (!fixture.director.hasPendingSwitch())
    return;
  fixture.module.stop();
  fixture.started = false;
  CanvasScene* game =
    static_cast<CanvasScene*>(fixture.director.find(CSimScenes::kCanvas));
  const bool started = game->start(fixture.context);
  testTrue(g, started, "configured game starts");
  CellContext* cells = CanvasSceneTestAccess::getCellContext(*game);
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
  game->stop();
}

// The program's flow (D-E31): returning to the title keeps the canvas, the
// title offers to resume it, and a new simulation replaces it.
static void
testSceneFlowKeepsCanvas()
{
  testSection("CSimScenes: a kept canvas resumes where it was left");
  MainMenuFixture fixture(false);
  SceneDirector& director = fixture.director;
  testTrue(g,
           CSimScenes::openTitle(director) && director.applyPending() &&
             director.activeName() == CSimScenes::kTitle,
           "the title is the first scene");
  TitleScene* title =
    static_cast<TitleScene*>(director.find(CSimScenes::kTitle));
  testTrue(g,
           title->rowCountForTesting() == 4 && !CSimScenes::canResume(director),
           "without a canvas the title shows its four rows");

  NewSimulationConfiguration configuration;
  configuration.starterPattern = false;
  testTrue(g,
           CSimScenes::newSimulation(director, configuration) &&
             director.applyPending() &&
             director.activeName() == CSimScenes::kCanvas,
           "a new simulation switches to its canvas");
  CanvasScene* canvas =
    static_cast<CanvasScene*>(director.find(CSimScenes::kCanvas));
  CellContext* cells = CanvasSceneTestAccess::getCellContext(*canvas);
  // A block is a still life, so it survives the running simulation.
  const unsigned char live = SparseCellGrid::BackgroundState == 0 ? 1 : 0;
  const CellAddress marked{ 5, 7 };
  for (const CellAddress& cell : { marked,
                                   CellAddress{ 6, 7 },
                                   CellAddress{ 5, 8 },
                                   CellAddress{ 6, 8 } }) {
    cells->getGrid()->setCell(cell, live);
  }
  testTrue(g,
           fixture.camera.GetZoom() == CanvasScene::kHomeZoom,
           "the canvas opens at its home zoom");
  fixture.camera.SetZoom(2.0f);
  fixture.camera.SetPositionPrecise(30.0, 40.0);
  fixture.registry.QueueCommand("run", {});
  fixture.registry.ExecuteQueue();
  for (int frame = 0; frame < 5; ++frame) {
    canvas->update(0.05);
  }
  testTrue(g,
           fixture.registry.HasCommand("pause") &&
             !fixture.registry.HasCommand("play"),
           "the canvas's commands are registered while it is active");

  testTrue(g,
           CSimScenes::returnToTitle(director) && director.applyPending() &&
             director.activeName() == CSimScenes::kTitle &&
             director.find(CSimScenes::kCanvas) == canvas,
           "returning to the title keeps the canvas");
  testTrue(g,
           !CanvasSceneTestAccess::isSimulationBusy(*canvas) &&
             !fixture.registry.HasCommand("pause") &&
             fixture.registry.HasCommand("play"),
           "the kept canvas has no generation running and its commands are "
           "withdrawn");
  testTrue(g,
           title->rowCountForTesting() == 5 &&
             title->rowKindForTesting(0) == TitleScene::kResumeItem &&
             title->getSelectedItemForTesting() == 0,
           "the title offers to resume the canvas, selected first");
  // Leaving finished the generation in flight; nothing runs after that.
  const std::uint64_t generation =
    CanvasSceneTestAccess::getSimulationGeneration(*canvas);
  director.update(0.5);
  director.update(0.5);
  testTrue(g,
           CanvasSceneTestAccess::getSimulationGeneration(*canvas) ==
             generation,
           "a kept canvas does not simulate");

  title->activateSelectedItemForTesting();
  testTrue(g,
           director.applyPending() &&
             director.activeName() == CSimScenes::kCanvas &&
             director.find(CSimScenes::kCanvas) == canvas,
           "Resume switches back to the same canvas");
  testTrue(g,
           cells->getGrid()->getCell(marked) == live &&
             fixture.camera.GetZoom() == 2.0f &&
             fixture.camera.GetPositionPrecise() == glm::dvec2(30.0, 40.0) &&
             fixture.registry.HasCommand("pause"),
           "the resumed canvas has its world, camera and commands back");

  CSimScenes::returnToTitle(director);
  director.applyPending();
  testTrue(g,
           CSimScenes::newSimulation(director, configuration) &&
             director.applyPending() &&
             director.activeName() == CSimScenes::kCanvas,
           "a new simulation from the title starts a canvas");
  CanvasScene* fresh =
    static_cast<CanvasScene*>(director.find(CSimScenes::kCanvas));
  testTrue(g,
           CanvasSceneTestAccess::getCellContext(*fresh)->getGrid()->getCell(
             marked) != live &&
             fixture.camera.GetZoom() == CanvasScene::kHomeZoom,
           "it replaces the kept canvas with a fresh world on its home view");
  CSimScenes::returnToTitle(director);
  director.applyPending();
  testTrue(g,
           title->rowCountForTesting() == 5,
           "the replacement is kept for resuming in turn");
}

static int
runMainMenuCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

static void
testMainMenuPanelTilt()
{
  testSection("TitleScene: the panel swivels toward the pointer");
  MainMenuFixture fixture;
  testTrue(g, fixture.started, "menu started");
  // Centered pointer: the entrance settles and the panel stays level.
  fixture.window.mouseX = 320.0;
  fixture.window.mouseY = 240.0;
  for (int frame = 0; frame < 150; ++frame) {
    fixture.module.update(1.0 / 60.0);
  }
  testTrue(g,
           std::abs(fixture.module.tiltXForTesting()) < 0.01f &&
             std::abs(fixture.module.tiltYForTesting()) < 0.01f,
           "a centered pointer keeps the panel level");
  const std::array<float, 4> level = fixture.module.itemHitBoundsForTesting(2);

  // The bottom-right corner lies outside every row.
  fixture.window.mouseX = 639.0;
  fixture.window.mouseY = 479.0;
  for (int frame = 0; frame < 120; ++frame) {
    fixture.module.update(1.0 / 60.0);
  }
  testTrue(g,
           fixture.module.tiltXForTesting() > 0.9f &&
             fixture.module.tiltYForTesting() > 0.9f,
           "the panel tilts toward a pointer in the corner");
  const std::array<float, 4> tilted = fixture.module.itemHitBoundsForTesting(2);
  testTrue(g,
           tilted[0] - level[0] > 5.0f && tilted[1] - level[1] > 5.0f,
           "rows swing toward the pointer with the tilt");

  // Just inside the tilted row's bottom edge is the gap below the level row,
  // so only a hit test that follows the drawing selects row 2.
  const float pointY = tilted[1] + tilted[3] - 2.0f;
  testTrue(g,
           pointY > level[1] + level[3],
           "the probe lies below where the level row would be");
  fixture.window.mouseX = static_cast<double>(tilted[0] + tilted[2] * 0.5f);
  fixture.window.mouseY = static_cast<double>(pointY);
  fixture.module.update(1.0 / 60.0);
  testEqInt(g,
            fixture.module.getSelectedItemForTesting(),
            2,
            "a row is hit where it is drawn");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::F1, InputAction::Press, 0 });
  for (int frame = 0; frame < 150; ++frame) {
    fixture.module.update(1.0 / 60.0);
  }
  testTrue(g,
           fixture.module.isSettingsOpenForTesting() &&
             std::abs(fixture.module.tiltXForTesting()) < 0.01f &&
             std::abs(fixture.module.tiltYForTesting()) < 0.01f,
           "the panel swings level behind an open overlay");
}

void
registerMainMenuTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.MainMenu.SceneFlowKeepsCanvas",
               []() { return runMainMenuCase(testSceneFlowKeepsCanvas); });
  registry.add("IllumoGame.MainMenu.PanelTilt",
               []() { return runMainMenuCase(testMainMenuPanelTilt); });
  registry.add("IllumoGame.MainMenu.CanvasSetupValidation",
               []() { return runMainMenuCase(testCanvasSetupValidation); });
  registry.add("IllumoGame.MainMenu.CanvasSetupDiscard",
               []() { return runMainMenuCase(testCanvasSetupDraft); });
  registry.add("IllumoGame.MainMenu.CanvasSetupCreate", []() {
    return runMainMenuCase(testCanvasSetupCreatesConfiguredWorld);
  });
  registry.add("IllumoGame.MainMenu.TitleResolution",
               []() { return runMainMenuCase(testTitleRasterResolution); });
  registry.add("IllumoGame.MainMenu.TitleLettersPose",
               []() { return runMainMenuCase(testTitleLettersPose); });
  registry.add("IllumoGame.MainMenu.TitlePokeHops",
               []() { return runMainMenuCase(testTitlePokeHops); });
  registry.add("IllumoGame.MainMenu.KeepsRulePreference", []() {
    return runMainMenuCase(testMainMenuKeepsRulePreference);
  });
  registry.add("IllumoGame.MainMenu.AmbientWorld",
               []() { return runMainMenuCase(testMainMenuAmbientWorld); });
  registry.add("IllumoGame.MainMenu.ShowsVersion",
               []() { return runMainMenuCase(testMainMenuShowsVersion); });
  registry.add("IllumoGame.MainMenu.PrimitiveBudget",
               []() { return runMainMenuCase(testMainMenuPrimitiveBudget); });
  registry.add("IllumoGame.MainMenu.MouseIsolation",
               []() { return runMainMenuCase(testSettingsMouseIsolation); });
  registry.add("IllumoGame.MainMenu.RestartPrompt",
               []() { return runMainMenuCase(testMainMenuRestartPrompt); });
  registry.add("IllumoGame.MainMenu.SettingsShared",
               []() { return runMainMenuCase(testSettingsShared); });
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
