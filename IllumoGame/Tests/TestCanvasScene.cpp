#include "Game/CSimSounds.h"
#include "Game/CanvasCoordinatePolicy.h"
#include "Game/CSimScenes.h"
#include "Game/CanvasScene.h"
#include "Game/RuleCatalogLoader.h"
#include "Game/SimulatorSettings.h"
#include "Game/SoftwareCursor.h"
#include "Rulesets/RuleSet.h"
#include "Rulesets/RuleSetRegistry.h"
#include "Rulesets/WireworldRuleSet.h"
#include "TestAccess.h"
#include "TestHarness.h"
#include <Illumo/Content/VfsAssetSource.h>
#include <Illumo/Content/SceneDirector.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

static TestCounters g;
static std::string gSaveDialogResult;
static std::string gLoadDialogResult;

class WorkingDirectoryFixture
{
public:
  WorkingDirectoryFixture()
  {
    std::error_code error;
    previousDirectory = std::filesystem::current_path(error);
    if (error) {
      return;
    }
    const std::filesystem::path temporaryDirectory =
      std::filesystem::temp_directory_path(error) /
      ("illumo-rule-workshop-" +
       std::to_string(
         std::chrono::steady_clock::now().time_since_epoch().count()));
    if (error ||
        !std::filesystem::create_directory(temporaryDirectory, error) ||
        error) {
      return;
    }
    directory = temporaryDirectory;
    std::filesystem::current_path(directory, error);
    ready = !error;
  }

  ~WorkingDirectoryFixture()
  {
    std::error_code error;
    if (!previousDirectory.empty()) {
      std::filesystem::current_path(previousDirectory, error);
    }
    if (!directory.empty()) {
      std::filesystem::remove_all(directory, error);
    }
  }

  WorkingDirectoryFixture(const WorkingDirectoryFixture&) = delete;
  WorkingDirectoryFixture& operator=(const WorkingDirectoryFixture&) = delete;
  WorkingDirectoryFixture(WorkingDirectoryFixture&&) = delete;
  WorkingDirectoryFixture& operator=(WorkingDirectoryFixture&&) = delete;

  bool isReady() const { return ready; }
  const std::filesystem::path& getDirectory() const { return directory; }

private:
  std::filesystem::path previousDirectory;
  std::filesystem::path directory;
  bool ready = false;
};

std::string
SaveLoad::GetSaveLocation(const SaveLoadDialogSpec&)
{
  return gSaveDialogResult;
}

std::string
SaveLoad::GetLoadLocation(const SaveLoadDialogSpec&)
{
  return gLoadDialogResult;
}

static bool
historyContains(const CommandLine& console, const std::string& text)
{
  const std::vector<CommandLine::historyBuffer>& history = console.getHistory();
  for (const CommandLine::historyBuffer& entry : history) {
    if (entry.content.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

static bool
focusWorkshopControl(RulesetWorkshopMenu& menu,
                     InputManager& input,
                     const std::string& label)
{
  input.getKeyQueue().push({ KeyCode::Home, InputAction::Press, 0 });
  menu.update(&input);
  for (int attempt = 0; attempt < 32; ++attempt) {
    if (menu.getSelectedControlForTesting() == label) {
      return true;
    }
    input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
    menu.update(&input);
  }
  return menu.getSelectedControlForTesting() == label;
}

static bool
hasWorkshopTextCaret(RulesetWorkshopMenu& menu)
{
  GameVisual& visual = menu.getVisual();
  for (std::size_t index = 0u; index < visual.textCount(); ++index) {
    TextPrimitive* text = visual.getText(index);
    if (text != nullptr && text->content == "|") {
      return true;
    }
  }
  return false;
}

static bool
openWorkshop(RulesetWorkshopMenu& menu,
             const RuleSetDefinition& rule,
             bool reducedMotion)
{
  const RuleFamilyDefinition* family =
    RuleSetRegistry::instance().getFamilyDefinition(rule.familyId);
  return family != nullptr && menu.open(*family, rule, reducedMotion);
}

static void
setWorkshopDraft(RulesetWorkshopMenu& menu, const RuleSetDefinition& rule)
{
  const RuleFamilyDefinition* family =
    RuleSetRegistry::instance().getFamilyDefinition(rule.familyId);
  if (family != nullptr) {
    menu.setDraft(*family, rule);
  }
}

// The oracle's package: files staged beside the test executable, mounted at
// /app as the runtime mounts a launched package.
static std::shared_ptr<VirtualFileSystem>
stagedPackage()
{
  std::shared_ptr<VirtualFileSystem> files =
    std::make_shared<VirtualFileSystem>();
  std::string error;
  std::shared_ptr<DirectoryVfsBackend> backend = DirectoryVfsBackend::open(
    EnvVars::ApplicationConfigPath().parent_path(), false, error);
  if (backend) {
    files->mount({ "/app", { { backend, "illumogame" } }, {} }, error);
  }
  return files;
}

struct CellGameFixture
{
  NullRenderWindow window;
  EnvVars env;
  Camera camera;
  MockBackend mock;
  Renderer renderer;
  VfsAssetSource source;
  AssetManager assets;
  CommandRegistry registry;
  CommandLine console;
  InputManager input;
  DrawList scene;
  IllumoContext context;
  CanvasScene module;
  bool started;

  CellGameFixture(int width = 8, int height = 6)
    : window(640, 480)
    , env()
    , camera(glm::vec2(1.0f, 1.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , source(stagedPackage())
    , assets(&renderer, false, &source)
    , registry()
    , console(&env, &registry, &window, &renderer)
    , input(nullptr)
    , scene(&window, &camera)
    , context{ &scene,  &window, &console, &input,   &renderer,
               &assets, &env,    &camera,  &registry }
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
    env.setVar("CanvasX", width);
    env.setVar("CanvasY", height);
    env.setVar("FamilyString", "LIFE_LIKE_BINARY");
    env.setVar("RuleSetString", "GAME_OF_LIFE");
    env.setVar("ModeString", "GAME_OF_LIFE");
    env.setVar("tps", 30);
    env.setVar("speedFactor", 1.0);
    env.setVar("cellFadeSpeed", 8.0);
    env.setVar("WorldChunksX", 0);
    env.setVar("WorldChunksY", 0);
    env.setVar("vsync", true);
    env.setVar("fullscreen", false);
    env.setVar("render3dTest", false);
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
    module.start(context);
    started = CanvasSceneTestAccess::getCellContext(module) != nullptr;
  }

  ~CellGameFixture()
  {
    if (started) {
      module.stop();
    }
  }

  void execute(const std::string& command,
               const std::vector<std::string>& args = {})
  {
    const bool queued = registry.QueueCommand(command, args);
    testTrue(g, queued, ("registered command queues: " + command).c_str());
    registry.ExecuteQueue();
  }

  void executeThroughConsole(const std::string& command)
  {
    console.ClearInput();
    for (const char character : command) {
      console.AddCharacter(static_cast<unsigned int>(character));
    }
    console.ExecuteCommand();
    registry.ExecuteQueue();
  }
};

static void
writeSaveFile(const std::filesystem::path& path,
              const std::string& rule,
              int width,
              int height,
              const std::vector<unsigned char>& cells)
{
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  char tag[MAX_RULETAG_SIZE] = {};
  const std::size_t count =
    rule.size() < static_cast<std::size_t>(MAX_RULETAG_SIZE - 1)
      ? rule.size()
      : static_cast<std::size_t>(MAX_RULETAG_SIZE - 1);
  std::memcpy(tag, rule.data(), count);
  output.write(tag, MAX_RULETAG_SIZE);
  output.write(reinterpret_cast<const char*>(&width), sizeof(width));
  output.write(reinterpret_cast<const char*>(&height), sizeof(height));
  if (!cells.empty()) {
    output.write(reinterpret_cast<const char*>(cells.data()),
                 static_cast<std::streamsize>(cells.size()));
  }
}

static std::vector<char>
readFileBytes(const std::filesystem::path& path)
{
  std::ifstream input(path, std::ios::binary);
  return std::vector<char>(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
}

static void
testCanvasPreferences()
{
  testSection("CanvasScene: camera, look, start and autosave settings");
  CellGameFixture fixture;

  // Zoom sensitivity and direction.
  fixture.env.setVar("zoomStep", 0.5);
  fixture.camera.SetZoom(1.0f);
  InputManager::scrollCallback(nullptr, 0.0, 1.0);
  fixture.module.update(-1.0);
  testTrue(g,
           std::abs(fixture.camera.GetTargetZoom() - 1.5f) < 0.001f,
           "one wheel notch zooms by the persisted step");
  fixture.env.setVar("invertZoom", true);
  InputManager::scrollCallback(nullptr, 0.0, 1.0);
  fixture.module.update(-1.0);
  testTrue(g,
           std::abs(fixture.camera.GetTargetZoom() - 0.75f) < 0.001f,
           "inverted zoom turns a wheel-up notch into a zoom out");
  fixture.env.setVar("invertZoom", false);
  fixture.env.setVar("zoomStep", 0.15);
  // Nothing drains the headless wheel between frames.
  *fixture.input.getMouseScrollOffset() = 0.0;
  fixture.camera.SetZoom(1.0f);

  // Arrow keys pan at the persisted speed in screen pixels per second.
  fixture.env.setVar("panSpeed", 600);
  const glm::dvec2 before = fixture.camera.GetTargetPositionPrecise();
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::Right, InputAction::Press);
  fixture.module.update(0.5);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::Right, InputAction::Release);
  const glm::dvec2 panned = fixture.camera.GetTargetPositionPrecise();
  testTrue(g,
           std::abs(panned.x - before.x - 300.0) < 0.01 && panned.y == before.y,
           "a held arrow key pans at the persisted speed");
  fixture.env.setVar("panSpeed", 0);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::Up, InputAction::Press);
  fixture.module.update(0.5);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::Up, InputAction::Release);
  testTrue(g,
           fixture.camera.GetTargetPositionPrecise() == panned,
           "a pan speed of zero turns keyboard panning off");

  // The cell look travels in the cell quad's colour channel.
  SimulatorConfiguration look =
    CanvasSceneTestAccess::currentConfiguration(fixture.module);
  look.ledCells = false;
  look.cellGlow = 2.0;
  look.gridLines = true;
  testTrue(g,
           CanvasSceneTestAccess::applyConfiguration(fixture.module, look),
           "canvas look settings apply");
  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&fixture.scene, &fixture.camera);
  fixture.renderer.EndFrame();
  const std::array<float, 32>& quad =
    CanvasSceneTestAccess::getCellContext(fixture.module)
      ->getCanvasView()
      ->getCellQuadVertices();
  testTrue(g,
           quad[3] == 1.0f && quad[4] == 0.0f && quad[5] == 1.0f &&
             quad[27] == 1.0f && quad[28] == 0.0f && quad[29] == 1.0f,
           "flat cells, double glow and grid lines reach the cell quad");
  testTrue(g,
           fixture.env.getVar("cellStyle").value == "flat" &&
             fixture.env.getVar("gridLines").valueAsBool,
           "canvas look settings persist");
  SimulatorConfiguration invalid = look;
  invalid.zoomStep = 3.0;
  testTrue(
    g,
    !CanvasSceneTestAccess::applyConfiguration(fixture.module, invalid),
    "out-of-range new settings are rejected");

  // Autosave writes to private storage once the interval elapses.
  const std::filesystem::path autosave = SimulatorSettings::kAutosaveFile;
  std::error_code ignored;
  std::filesystem::remove(autosave, ignored);
  fixture.env.setVar("autosaveMinutes", 1);
  fixture.module.update(30.0);
  testTrue(
    g, !std::filesystem::exists(autosave), "autosave waits for its interval");
  fixture.module.update(31.0);
  testTrue(g,
           std::filesystem::exists(autosave),
           "autosave writes once the interval elapses");
  std::filesystem::remove(autosave, ignored);

  // Start paused off opens the next canvas running.
  fixture.module.stop();
  fixture.started = false;
  fixture.env.setVar("startPaused", false);
  CanvasScene running;
  testTrue(g,
           running.start(fixture.context) &&
             CanvasSceneTestAccess::getState(running) == CellState::NORMAL,
           "with start paused off a canvas opens running");
  running.stop();
}

static void
openCanvasSettings(CellGameFixture& fixture)
{
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::F1, InputAction::Press);
  fixture.module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::F1, InputAction::Release);
}

// VIDEO tab, Anti-aliasing row (fourth), one step, then Apply.
static void
applyMsaaStep(CellGameFixture& fixture, KeyCode step)
{
  CanvasSceneTestAccess::getConfigurationMenu(fixture.module)
    ->selectTabForTesting(ConfigurationTab::Video);
  std::queue<InputManager::KeyPressEvent>& keys = fixture.input.getKeyQueue();
  for (int row = 0; row < 3; ++row) {
    keys.push({ KeyCode::Down, InputAction::Press, 0 });
  }
  keys.push({ step, InputAction::Press, 0 });
  for (int row = 0; row < 8; ++row) {
    keys.push({ KeyCode::Down, InputAction::Press, 0 });
  }
  keys.push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.016);
}

static void
testRestartPrompt()
{
  testSection("CanvasScene: restart prompt for restart-only settings");
  CellGameFixture fixture;
  fixture.env.setVar("msaa", 4);
  fixture.window.msaaSamples = 4;
  ConfigurationMenu* menu =
    CanvasSceneTestAccess::getConfigurationMenu(fixture.module);
  ExitConfirmDialog* prompt =
    CanvasSceneTestAccess::getExitConfirmDialog(fixture.module);

  // Unchanged MSAA: Apply closes the menu and asks nothing.
  openCanvasSettings(fixture);
  fixture.input.getKeyQueue().push({ KeyCode::End, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Left, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Left, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           !menu->isOpen() && !prompt->isOpen(),
           "applying without a restart-only change asks nothing");

  // A new MSAA asks; Later keeps running with the saved choice.
  openCanvasSettings(fixture);
  applyMsaaStep(fixture, KeyCode::Right);
  testTrue(g,
           !menu->isOpen() && prompt->isOpen() &&
             fixture.env.getVar("msaa").valueAsLong == 8,
           "applying a new MSAA saves it and offers to restart");
  fixture.input.getKeyQueue().push({ KeyCode::N, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           !prompt->isOpen() && !fixture.window.closeRequested &&
             !fixture.window.restartRequested(),
           "Later keeps the canvas running");

  // Asked again (still differs from the window); Restart now relaunches.
  openCanvasSettings(fixture);
  fixture.input.getKeyQueue().push({ KeyCode::End, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Left, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Left, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g, prompt->isOpen(), "the prompt returns while MSAA still differs");
  fixture.input.getKeyQueue().push({ KeyCode::Y, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           !prompt->isOpen() && fixture.window.closeRequested &&
             fixture.window.restartRequested(),
           "Restart now asks the host to close and relaunch");
  fixture.window.cancelCloseRequest();
  fixture.window.clearRestartRequest();

  // Unknown window samples (hosts that cannot tell) never ask.
  fixture.window.msaaSamples = -1;
  openCanvasSettings(fixture);
  applyMsaaStep(fixture, KeyCode::Left);
  testTrue(g,
           !menu->isOpen() && !prompt->isOpen(),
           "a window that cannot report its samples never asks");
}

static void
testStartRegistersGameFeatures()
{
  testSection("CanvasScene: start and command registration");
  CellGameFixture fixture;
  testTrue(g, fixture.started, "valid headless context starts the game module");
  testTrue(g,
           CanvasSceneTestAccess::getState(fixture.module) ==
             CellState::EDIT,
           "module starts in edit mode");
  testEqSize(g,
             fixture.registry.GetCommandNames().size(),
             32,
             "all game commands are registered");
  testTrue(
    g, fixture.registry.HasCommand("select"), "select command registered");
  testTrue(
    g, fixture.registry.HasCommand("inspect"), "inspect command registered");
  testTrue(
    g, fixture.registry.HasCommand("ruleset"), "ruleset command registered");
  testTrue(g, fixture.registry.HasCommand("save"), "save command registered");
  testTrue(g, fixture.registry.HasCommand("load"), "load command registered");
  testTrue(g, fixture.registry.HasCommand("tps"), "TPS command registered");
  testTrue(g, fixture.registry.HasCommand("speed"), "speed command registered");
  testTrue(g, fixture.registry.HasCommand("fade"), "fade command registered");
  testTrue(g, fixture.registry.HasCommand("menu"), "menu command registered");
  testTrue(g,
           fixture.registry.GetCommandUsage("setcell") ==
             "setcell <x> <y> <state>",
           "command usage metadata registered");
  testEqSize(g,
             fixture.registry.GetCommandCompletions("ruleset").size(),
             RuleSetRegistry::instance().getKnownRules().size(),
             "ruleset completion candidates registered");
  testTrue(g,
           CanvasSceneTestAccess::getConfigurationMenu(fixture.module) !=
             nullptr,
           "Release-visible configuration menu is constructed at startup");

  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  testEqSize(g,
             fixture.scene.drawableCount(),
             2,
             "canvas and entrance veil are dispatched while splash is hidden");
  testEqSize(g,
             fixture.scene.drawablesIn(RenderLayerId::World).size(),
             1,
             "canvas is on the World layer");

  fixture.module.stop();
  fixture.started = false;
  testEqSize(g,
             fixture.registry.GetCommandNames().size(),
             2,
             "Exit unregisters every game command");
}

static void
testInvalidContextStartIsContained()
{
  testSection("CanvasScene: invalid start context");
  CellGameFixture fixture;
  IllumoContext empty;
  CanvasScene module;
  module.start(empty);
  testTrue(g,
           CanvasSceneTestAccess::getCellContext(module) == nullptr,
           "an empty context does not create game state");
  // A failed start must not crash on later frame hooks.
  module.update(0.016);
  module.dispatch(fixture.scene);
  module.stop();

  IllumoContext incomplete = fixture.context;
  incomplete.commandRegistry = nullptr;
  CanvasScene incompleteModule;
  incompleteModule.start(incomplete);
  testTrue(g,
           CanvasSceneTestAccess::getCellContext(incompleteModule) ==
             nullptr,
           "missing service rejects startup");
  incompleteModule.update(0.016);
  incompleteModule.dispatch(fixture.scene);
  incompleteModule.stop();
}

static void
testRender3dTestFlag()
{
  testSection("CanvasScene: render3dTest diagnostic scene");
  CellGameFixture fixture;
  fixture.env.setVar("render3dTest", true);
  fixture.module.update(0.5);
  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);

  SceneInstance* diagnostic =
    CanvasSceneTestAccess::getRender3dScene(fixture.module);
  testTrue(g,
           diagnostic != nullptr,
           "the flag loads Scenes/render3d-test.ilsc into a scene instance");
  testEqSize(g,
             fixture.scene.drawablesIn(RenderLayerId::World).size(),
             1u,
             "flag replaces CanvasView with the scene's drawable");
  if (diagnostic != nullptr) {
    testTrue(g,
             fixture.scene.drawablesIn(RenderLayerId::World)[0] ==
               &diagnostic->drawable(),
             "the scene draws in the World layer");
    testTrue(g,
             diagnostic->findNode("orbit") != nullptr &&
               diagnostic->findNode("child") != nullptr &&
               diagnostic->parentOf("child") == "orbit",
             "the scene holds the animated orbit and child nodes");
    const Transform3D before = diagnostic->findNode("orbit")->transform;
    fixture.module.update(0.25);
    fixture.scene.ClearDrawables();
    fixture.module.dispatch(fixture.scene);
    const Transform3D after = diagnostic->findNode("orbit")->transform;
    testTrue(g,
             std::abs(before.position.x - after.position.x) > 1e-4f,
             "the orbit node is animated by id");
  }
  testTrue(g,
           fixture.camera.getProjectionType() == ProjectionType::Perspective,
           "flag drives the product camera in perspective");

  fixture.env.setVar("render3dTest", false);
  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  testEqSize(g,
             fixture.scene.drawablesIn(RenderLayerId::World).size(),
             1u,
             "disabling the flag restores CanvasView");
  testTrue(
    g,
    fixture.scene.drawablesIn(RenderLayerId::World)[0] ==
      CanvasSceneTestAccess::getCellContext(fixture.module)->getCanvasView(),
    "normal World presentation is restored");
  testTrue(g,
           fixture.camera.getProjectionType() == ProjectionType::Orthographic,
           "disabling the flag restores the orthographic CA camera");
}

static void
testWireworldSeedAndBrush()
{
  testSection("CanvasScene: Wireworld seed and brush state");
  CellGameFixture fixture(16, 12);
  fixture.env.setVar("FamilyString", "WIREWORLD_FAMILY");
  fixture.env.setVar("RuleSetString", "WIREWORLD");
  fixture.env.setVar("ModeString", "WIREWORLD");
  // Restart under Wireworld so seedInitialPattern runs for that ruleset.
  fixture.module.stop();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "WIREWORLD_FAMILY");
  fixture.env.setVar("RuleSetString", "WIREWORLD");
  fixture.env.setVar("ModeString", "WIREWORLD");
  fixture.module.start(fixture.context);
  fixture.started =
    CanvasSceneTestAccess::getCellContext(fixture.module) != nullptr;
  testTrue(g, fixture.started, "Wireworld Start succeeds");

  CellContext* cellContext =
    CanvasSceneTestAccess::getCellContext(fixture.module);
  testTrue(g, cellContext != nullptr, "Wireworld cellContext exists");
  testTrue(
    g, cellContext->getModeString() == "WIREWORLD", "ModeString is WIREWORLD");
  testTrue(g,
           cellContext->getRuleSet()->getStateCount() == 4u &&
             cellContext->getRuleSet()->getStateName(0u) == "Head",
           "Wireworld rule metadata is present before seeding");

  CanvasView* canvas = cellContext->getCanvasView();
  const std::int64_t y = 0;
  const std::int64_t startX = -4;
  testEqInt(g,
            static_cast<int>(canvas->getCanvasPixel(startX, y)),
            static_cast<int>(WireworldRuleSet::CELL_HEAD),
            "seed places electron head");
  testEqInt(g,
            static_cast<int>(canvas->getCanvasPixel(startX + 1, y)),
            static_cast<int>(WireworldRuleSet::CELL_TAIL),
            "seed places electron tail");
  testEqInt(g,
            static_cast<int>(canvas->getCanvasPixel(startX + 2, y)),
            static_cast<int>(WireworldRuleSet::CELL_CONDUCTOR),
            "seed places conductor wire");

  testEqInt(g,
            static_cast<int>(
              CanvasSceneTestAccess::getWireworldBrush(fixture.module)),
            static_cast<int>(WireworldRuleSet::CELL_CONDUCTOR),
            "default brush is conductor");
  CanvasSceneTestAccess::setWireworldBrush(fixture.module,
                                              WireworldRuleSet::CELL_HEAD);
  testEqInt(g,
            static_cast<int>(
              CanvasSceneTestAccess::getWireworldBrush(fixture.module)),
            static_cast<int>(WireworldRuleSet::CELL_HEAD),
            "brush can select head for left-paint");
}

static void
testResetCanvas()
{
  testSection("CanvasScene: reset_canvas puts back the starting pattern");
  CellGameFixture fixture(16, 12);
  fixture.module.stop();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "WIREWORLD_FAMILY");
  fixture.env.setVar("RuleSetString", "WIREWORLD");
  fixture.env.setVar("ModeString", "WIREWORLD");
  fixture.started = fixture.module.start(fixture.context);
  CellContext* context = CanvasSceneTestAccess::getCellContext(fixture.module);
  testTrue(g, fixture.started && context != nullptr, "Wireworld canvas starts");
  if (context == nullptr) {
    return;
  }
  SparseCellGrid* grid = context->getGrid();
  const std::size_t seededCount = grid->getStoredCellCount();

  // Draw elsewhere, run the electron along and look away.
  fixture.execute("setcell", { "20", "20", "3" });
  fixture.execute("step", { "3" });
  fixture.camera.SetPositionPrecise(400.0, -250.0);
  testTrue(g,
           grid->getCell({ 20, 20 }) == WireworldRuleSet::CELL_CONDUCTOR &&
             grid->getCell({ -4, 0 }) != WireworldRuleSet::CELL_HEAD &&
             CanvasSceneTestAccess::getSimulationGeneration(fixture.module) ==
               3u,
           "the world has moved on from its starting pattern");

  // Confirm clearing is on by default: the command asks first.
  fixture.execute("reset_canvas");
  ExitConfirmDialog* dialog =
    CanvasSceneTestAccess::getExitConfirmDialog(fixture.module);
  testTrue(g,
           dialog != nullptr && dialog->isOpen() &&
             grid->getCell({ 20, 20 }) == WireworldRuleSet::CELL_CONDUCTOR,
           "reset_canvas asks before replacing the world");
  if (dialog == nullptr) {
    return;
  }
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Y, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g, !dialog->isOpen(), "confirming closes the dialog");
  testTrue(g,
           grid->getCell({ -4, 0 }) == WireworldRuleSet::CELL_HEAD &&
             grid->getCell({ -3, 0 }) == WireworldRuleSet::CELL_TAIL &&
             grid->getCell({ -2, 0 }) == WireworldRuleSet::CELL_CONDUCTOR &&
             grid->getCell({ 20, 20 }) == SparseCellGrid::BackgroundState &&
             grid->getStoredCellCount() == seededCount,
           "confirming puts back exactly the electron on its wire");
  const glm::dvec2 home = fixture.camera.GetPositionPrecise();
  testTrue(g,
           CanvasSceneTestAccess::getSimulationGeneration(fixture.module) ==
               0u &&
             home.x == 0.0 && home.y == 0.0,
           "reset starts the count over and returns to the home view");

  fixture.execute("setcell", { "20", "20", "3" });
  fixture.execute("reset_canvas", { "yes" });
  testTrue(g,
           !dialog->isOpen() &&
             grid->getCell({ 20, 20 }) == SparseCellGrid::BackgroundState &&
             grid->getStoredCellCount() == seededCount,
           "reset_canvas yes resets without asking");
  fixture.execute("reset_canvas", { "now" });
  testTrue(g,
           historyContains(fixture.console, "Usage: reset_canvas"),
           "reset_canvas rejects other arguments");
}

static void
testCyclicMultistateSeed()
{
  testSection("CanvasScene: cyclic rules seed many interacting states");
  CellGameFixture fixture(24, 18);
  fixture.module.stop();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "PRISMATIC_ECOLOGY_12");
  fixture.env.setVar("RuleSetString", "PRISM_RUSH");
  fixture.env.setVar("ModeString", "PRISM_RUSH");
  fixture.started = fixture.module.start(fixture.context);
  CellContext* context =
    CanvasSceneTestAccess::getCellContext(fixture.module);
  testTrue(g,
           fixture.started && context != nullptr &&
             context->getRuleSet()->getStateCount() == 12u,
           "Prism Rush starts with its twelve-state family");
  if (context == nullptr) {
    return;
  }
  std::array<bool, 12u> seen{};
  for (int y = -9; y <= 9; ++y) {
    for (int x = -9; x <= 9; ++x) {
      const unsigned char state = context->getGrid()->getCell({ x, y });
      if (state < seen.size()) {
        seen[state] = true;
      }
    }
  }
  const std::size_t distinct =
    static_cast<std::size_t>(std::count(seen.begin(), seen.end(), true));
  testTrue(g,
           distinct == seen.size(),
           "startup medallion includes every declared cell kind");
}

static void
testEveryShippedRuleStarts()
{
  testSection("CanvasScene: every shipped rule starts and advances");
  CellGameFixture fixture(24, 18);
  const std::vector<RuleSetDefinition> definitions =
    RuleSetRegistry::instance().getDefinitions();
  for (const RuleSetDefinition& definition : definitions) {
    fixture.module.stop();
    fixture.started = false;
    fixture.env.setVar("FamilyString", definition.familyId);
    fixture.env.setVar("RuleSetString", definition.id);
    fixture.env.setVar("ModeString", definition.id);
    fixture.started = fixture.module.start(fixture.context);
    CellContext* context =
      CanvasSceneTestAccess::getCellContext(fixture.module);
    const std::string started = definition.id + " starts";
    testTrue(g,
             fixture.started && context != nullptr &&
               context->getRuleSet()->getRuleTag() == definition.id,
             started.c_str());
    if (context == nullptr) {
      continue;
    }
    const std::string seeded = definition.id + " seeds a starter";
    testTrue(
      g, context->getGrid()->getAllocatedChunkCount() > 0u, seeded.c_str());
    bool advanced = true;
    for (int generation = 0; generation < 3; ++generation) {
      advanced =
        advanced && context->getGrid()->advance(*context->getRuleSet());
    }
    const std::string stepped = definition.id + " advances its starter";
    testTrue(g, advanced, stepped.c_str());
  }
}

static void
testResearchedStarterSeeds()
{
  testSection("CanvasScene: researched rules start with active populations");
  CellGameFixture fixture(24, 18);
  fixture.module.stop();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "QUADLIFE_4_SPECIES");
  fixture.env.setVar("RuleSetString", "QUADLIFE");
  fixture.env.setVar("ModeString", "QUADLIFE");
  fixture.started = fixture.module.start(fixture.context);
  CellContext* context =
    CanvasSceneTestAccess::getCellContext(fixture.module);
  testTrue(g,
           fixture.started && context != nullptr,
           "QuadLife starts from its researched catalog entry");
  if (context != nullptr) {
    std::array<bool, 5u> seen{};
    for (int y = -30; y <= 30; ++y) {
      for (int x = -30; x <= 30; ++x) {
        const unsigned char state = context->getGrid()->getCell({ x, y });
        if (state < seen.size()) {
          seen[state] = true;
        }
      }
    }
    testTrue(g,
             std::count(seen.begin(), seen.end(), true) ==
               static_cast<std::ptrdiff_t>(seen.size()),
             "QuadLife starter includes all four species and background");
  }

  fixture.module.stop();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "GENERATIONS_21_PHASE");
  fixture.env.setVar("RuleSetString", "FIREWORKS");
  fixture.env.setVar("ModeString", "FIREWORKS");
  fixture.started = fixture.module.start(fixture.context);
  context = CanvasSceneTestAccess::getCellContext(fixture.module);
  testTrue(g,
           fixture.started && context != nullptr,
           "Fireworks starts from its twenty-one-state family");
  if (context != nullptr) {
    for (int generation = 0; generation < 8; ++generation) {
      testTrue(g,
               context->getGrid()->advance(*context->getRuleSet()),
               "Fireworks starter advances without becoming invalid");
    }
    std::array<bool, 21u> seen{};
    for (int y = -40; y <= 40; ++y) {
      for (int x = -40; x <= 40; ++x) {
        const unsigned char state = context->getGrid()->getCell({ x, y });
        if (state < seen.size()) {
          seen[state] = true;
        }
      }
    }
    testTrue(g,
             std::count(seen.begin(), seen.end(), true) >= 6,
             "Fireworks starter develops a multi-phase colored trail");
  }

  fixture.module.stop();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "HODGEPODGE_101_LEVEL");
  fixture.env.setVar("RuleSetString", "HODGEPODGE_SPIRAL_G28");
  fixture.env.setVar("ModeString", "HODGEPODGE_SPIRAL_G28");
  fixture.started = fixture.module.start(fixture.context);
  context = CanvasSceneTestAccess::getCellContext(fixture.module);
  testTrue(g,
           fixture.started && context != nullptr,
           "Hodgepodge starts from its 101-level family");
  if (context != nullptr) {
    std::array<bool, 101u> seen{};
    for (int y = -34; y <= 34; ++y) {
      for (int x = -34; x <= 34; ++x) {
        const unsigned char state = context->getGrid()->getCell({ x, y });
        if (state < seen.size()) {
          seen[state] = true;
        }
      }
    }
    testTrue(g,
             std::count(seen.begin(), seen.end(), true) >= 90,
             "Hodgepodge starter exposes nearly its full infection spectrum");
  }

  fixture.module.stop();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "TURMITE_3_COLOR");
  fixture.env.setVar("RuleSetString", "TURMITE_RRL");
  fixture.env.setVar("ModeString", "TURMITE_RRL");
  fixture.started = fixture.module.start(fixture.context);
  context = CanvasSceneTestAccess::getCellContext(fixture.module);
  testTrue(g,
           fixture.started && context != nullptr,
           "three-color Turmite starts from its directional family");
  if (context != nullptr) {
    int agentCount = 0;
    for (int y = -12; y <= 12; ++y) {
      for (int x = -12; x <= 12; ++x) {
        if (context->getGrid()->getCell({ x, y }) >= 3u) {
          agentCount += 1;
        }
      }
    }
    testTrue(g, agentCount == 9, "Turmite starter launches a nine-agent swarm");
    testTrue(g,
             context->getGrid()->advance(*context->getRuleSet()),
             "Turmite swarm advances immediately");
  }

  fixture.module.stop();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "HPP_LATTICE_GAS_16");
  fixture.env.setVar("RuleSetString", "HPP_GAS");
  fixture.env.setVar("ModeString", "HPP_GAS");
  fixture.started = fixture.module.start(fixture.context);
  context = CanvasSceneTestAccess::getCellContext(fixture.module);
  testTrue(g,
           fixture.started && context != nullptr,
           "HPP starts from its sixteen-state particle family");
  if (context != nullptr) {
    std::array<bool, 16u> seen{};
    for (int y = -34; y <= 34; ++y) {
      for (int x = -34; x <= 34; ++x) {
        const unsigned char state = context->getGrid()->getCell({ x, y });
        if (state < seen.size()) {
          seen[state] = true;
        }
      }
    }
    testTrue(g,
             std::count(seen.begin(), seen.end(), true) >= 14,
             "particle cloud includes almost every directional occupancy");
    testTrue(g,
             context->getGrid()->advance(*context->getRuleSet()),
             "particle cloud streams and collides immediately");
  }

  fixture.module.stop();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "RPSLS_5_SPECIES");
  fixture.env.setVar("RuleSetString", "RPSLS_INVASION_T1");
  fixture.env.setVar("ModeString", "RPSLS_INVASION_T1");
  fixture.started = fixture.module.start(fixture.context);
  context = CanvasSceneTestAccess::getCellContext(fixture.module);
  testTrue(g,
           fixture.started && context != nullptr,
           "RPSLS starts from its five-species family");
  if (context != nullptr) {
    std::array<bool, 6u> seen{};
    for (int y = -35; y <= 35; ++y) {
      for (int x = -35; x <= 35; ++x) {
        const unsigned char state = context->getGrid()->getCell({ x, y });
        if (state < seen.size()) {
          seen[state] = true;
        }
      }
    }
    testTrue(g,
             std::count(seen.begin(), seen.end(), true) ==
               static_cast<std::ptrdiff_t>(seen.size()),
             "RPSLS starter includes all five species and empty space");
  }
}

static void
testSaveLoadRoundTrip()
{
  testSection("CanvasScene: save/load round trip");
  CellGameFixture fixture(5, 4);
  CellContext* cellContext =
    CanvasSceneTestAccess::getCellContext(fixture.module);
  CanvasView* canvas = cellContext->getCanvasView();
  testTrue(g,
           cellContext->resetWorld(2, 2),
           "finite topology allocates for round-trip coverage");
  cellContext->setRuleSet("WIREWORLD");
  canvas->clearCanvas();
  canvas->setCanvasPixel(0, 0, WireworldRuleSet::CELL_HEAD);
  canvas->setCanvasPixel(2, 1, WireworldRuleSet::CELL_TAIL);
  canvas->setCanvasPixel(4, 3, WireworldRuleSet::CELL_CONDUCTOR);
  canvas->setCanvasPixel(-10, 4, WireworldRuleSet::CELL_CONDUCTOR);
  fixture.camera.SetPositionPrecise(123456789.25, -987654321.5);
  fixture.camera.SetZoom(2.5f);

  const std::filesystem::path savePath = "roundtrip.illumo";
  testTrue(g,
           CanvasSceneTestAccess::save(fixture.module, savePath.string()),
           "valid canvas saves");
  const std::vector<char> firstSave = readFileBytes(savePath);
  canvas->clearCanvas();
  testTrue(g,
           cellContext->resetWorld(0, 0),
           "world can switch back to infinite before loading");
  cellContext->setRuleSet("SEEDS");
  fixture.camera.SetPositionPrecise(1.0, 2.0);
  fixture.camera.SetZoom(1.0f);
  testTrue(g,
           CanvasSceneTestAccess::load(fixture.module, savePath.string()),
           "saved canvas loads");
  testTrue(g,
           cellContext->getModeString() == "WIREWORLD" &&
             cellContext->getFamilyString() == "WIREWORLD_FAMILY",
           "saved family and ruleset pair are restored");
  testTrue(g,
           cellContext->getWorldChunkWidth() == 2 &&
             cellContext->getWorldChunkHeight() == 2,
           "saved finite topology is restored");
  testEqUChar(g,
              canvas->getCanvasPixel(0, 0),
              WireworldRuleSet::CELL_HEAD,
              "head state round-trips");
  testEqUChar(g,
              canvas->getCanvasPixel(2, 1),
              WireworldRuleSet::CELL_TAIL,
              "tail state round-trips");
  testEqUChar(g,
              canvas->getCanvasPixel(4, 3),
              WireworldRuleSet::CELL_CONDUCTOR,
              "conductor state round-trips");
  testEqUChar(g,
              canvas->getCanvasPixel(-10, 4),
              WireworldRuleSet::CELL_CONDUCTOR,
              "far chunk state round-trips");
  testTrue(g,
           fixture.camera.GetPositionPrecise() ==
             glm::dvec2(123456789.25, -987654321.5),
           "saved camera position round-trips precisely");
  testTrue(
    g, fixture.camera.GetZoom() == 2.5f, "saved camera zoom round-trips");

  testTrue(
    g,
    CanvasSceneTestAccess::save(fixture.module, "roundtrip-again.illumo"),
    "same sparse state saves again");
  const std::vector<char> secondSave = readFileBytes("roundtrip-again.illumo");
  testTrue(g, firstSave == secondSave, "sparse save output is deterministic");
  testEqSize(g,
             std::filesystem::file_size(savePath),
             static_cast<std::size_t>(316 + 2 * (16 * 16 + 16)),
             "save contains the sparse header and two chunk records");
}

static void
testSparseV2Compatibility()
{
  testSection("CanvasScene: sparse v2 compatibility");
  CellGameFixture fixture;
  CellContext* cellContext =
    CanvasSceneTestAccess::getCellContext(fixture.module);
  testTrue(
    g, cellContext->resetWorld(2, 2), "compatibility fixture starts finite");

  const char magic[8] = { 'I', 'L', 'L', 'U', 'M', 'O', '2', '\0' };
  const std::uint32_t version = 2;
  char ruleTag[MAX_RULETAG_SIZE] = {};
  std::memcpy(ruleTag, "SEEDS", 5);
  const double cameraX = 12.5;
  const double cameraY = -9.25;
  const double cameraZoom = 1.75;
  const std::uint64_t chunkCount = 1;
  const std::int64_t chunkX = 2;
  const std::int64_t chunkY = -3;
  SparseCellGrid::ChunkCells cells;
  cells.fill(SparseCellGrid::BackgroundState);
  cells[0] = 0;
  {
    std::ofstream output("valid-v2.illumo", std::ios::binary | std::ios::trunc);
    output.write(magic, sizeof(magic));
    output.write(reinterpret_cast<const char*>(&version), sizeof(version));
    output.write(ruleTag, sizeof(ruleTag));
    output.write(reinterpret_cast<const char*>(&cameraX), sizeof(cameraX));
    output.write(reinterpret_cast<const char*>(&cameraY), sizeof(cameraY));
    output.write(reinterpret_cast<const char*>(&cameraZoom),
                 sizeof(cameraZoom));
    output.write(reinterpret_cast<const char*>(&chunkCount),
                 sizeof(chunkCount));
    output.write(reinterpret_cast<const char*>(&chunkX), sizeof(chunkX));
    output.write(reinterpret_cast<const char*>(&chunkY), sizeof(chunkY));
    output.write(reinterpret_cast<const char*>(cells.data()),
                 static_cast<std::streamsize>(cells.size()));
  }

  testTrue(g,
           CanvasSceneTestAccess::load(fixture.module, "valid-v2.illumo"),
           "version 2 sparse save remains readable");
  testTrue(g,
           !cellContext->getGrid()->isToroidal() &&
             cellContext->getWorldChunkWidth() == 0 &&
             cellContext->getWorldChunkHeight() == 0,
           "version 2 loads as the historical infinite topology");
  testEqUChar(g,
              cellContext->getGrid()->getCell(CellAddress{ 32, -48 }),
              0,
              "version 2 sparse cell is restored");
  testTrue(g,
           cellContext->getModeString() == "SEEDS" &&
             cellContext->getFamilyString() == "LIFE_LIKE_BINARY" &&
             fixture.camera.GetPositionPrecise() ==
               glm::dvec2(cameraX, cameraY) &&
             fixture.camera.GetZoom() == static_cast<float>(cameraZoom),
           "version 2 ruleset and camera are restored");

  const char magicV3[8] = { 'I', 'L', 'L', 'U', 'M', 'O', '3', '\0' };
  const std::uint32_t versionV3 = 3u;
  const std::int64_t infiniteTopology = 0;
  const std::uint64_t noChunks = 0u;
  {
    std::ofstream output("valid-v3.illumo", std::ios::binary | std::ios::trunc);
    output.write(magicV3, sizeof(magicV3));
    output.write(reinterpret_cast<const char*>(&versionV3), sizeof(versionV3));
    output.write(ruleTag, sizeof(ruleTag));
    output.write(reinterpret_cast<const char*>(&cameraX), sizeof(cameraX));
    output.write(reinterpret_cast<const char*>(&cameraY), sizeof(cameraY));
    output.write(reinterpret_cast<const char*>(&cameraZoom),
                 sizeof(cameraZoom));
    output.write(reinterpret_cast<const char*>(&infiniteTopology),
                 sizeof(infiniteTopology));
    output.write(reinterpret_cast<const char*>(&infiniteTopology),
                 sizeof(infiniteTopology));
    output.write(reinterpret_cast<const char*>(&noChunks), sizeof(noChunks));
  }
  testTrue(g,
           CanvasSceneTestAccess::load(fixture.module, "valid-v3.illumo") &&
             cellContext->getModeString() == "SEEDS" &&
             cellContext->getFamilyString() == "LIFE_LIKE_BINARY",
           "version 3 derives the family from its ruleset ID");
}

static void
testSettingsYieldToConsole()
{
  testSection("CanvasScene: open console blocks settings input");
  CellGameFixture fixture;
  ConfigurationMenu* menu =
    CanvasSceneTestAccess::getConfigurationMenu(fixture.module);
  testTrue(g, menu != nullptr && !menu->isOpen(), "settings start closed");
  menu->open(CanvasSceneTestAccess::currentConfiguration(fixture.module));
  testTrue(g, menu->isOpen(), "settings open for console-yield check");

  fixture.console.Toggle();
  testTrue(g, fixture.console.isOpen, "console is open");
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g, menu->isOpen(), "open console blocks settings Escape");
}

static void
testReleaseConfigurationWorkflow()
{
  testSection("CanvasScene: Release configuration workflow");
  CellGameFixture fixture;
  ConfigurationMenu* menu =
    CanvasSceneTestAccess::getConfigurationMenu(fixture.module);
  testTrue(g, menu != nullptr && !menu->isOpen(), "settings start closed");

  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::F1, InputAction::Press);
  fixture.module.update(0.016);
  testTrue(g, menu != nullptr && menu->isOpen(), "F1 opens settings");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::F1, InputAction::Release);
  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  testEqSize(g,
             fixture.scene.drawableCount(),
             3u,
             "settings render above the canvas and entrance veil");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g, !menu->isOpen(), "Escape closes settings without applying");

  CellContext* cellContext =
    CanvasSceneTestAccess::getCellContext(fixture.module);
  cellContext->getGrid()->setCell(CellAddress{ 77, 88 }, 0);
  SimulatorConfiguration configuration =
    CanvasSceneTestAccess::currentConfiguration(fixture.module);
  configuration.ruleSet = "SEEDS";
  configuration.worldChunkWidth = 2;
  configuration.worldChunkHeight = 3;
  configuration.tps = 48;
  configuration.speedFactor = 2.0;
  configuration.fadeSpeed = 4.0;
  configuration.vsync = false;
  configuration.fullscreen = true;
  configuration.fpsCap = 144;
  configuration.showInspector = true;
  configuration.reducedUiMotion = true;
  configuration.softwareCursor = false;
  testTrue(
    g,
    CanvasSceneTestAccess::applyConfiguration(fixture.module, configuration),
    "valid settings apply atomically");
  testTrue(g,
           cellContext->getGrid()->isToroidal() &&
             cellContext->getWorldChunkWidth() == 2 &&
             cellContext->getWorldChunkHeight() == 3,
           "positive dimensions create a finite torus");
  testEqUChar(g,
              cellContext->getGrid()->getCell(CellAddress{ 77, 88 }),
              SparseCellGrid::BackgroundState,
              "topology change starts a fresh world");
  testTrue(g,
           cellContext->getModeString() == "SEEDS" &&
             fixture.env.getVar("ModeString").value == "SEEDS",
           "ruleset updates runtime and persisted configuration");
  testTrue(g,
           fixture.env.getVar("WorldChunksX").valueAsLong == 2 &&
             fixture.env.getVar("WorldChunksY").valueAsLong == 3 &&
             fixture.env.getVar("tps").valueAsLong == 48,
           "world and timing settings persist");
  testEqInt(g,
            fixture.window.fullscreenToggleCount,
            1,
            "fullscreen applies immediately once");

  const SimulatorConfiguration applied =
    CanvasSceneTestAccess::currentConfiguration(fixture.module);
  testTrue(
    g,
    applied.fpsCap == 144 && applied.showInspector && applied.reducedUiMotion &&
      !applied.softwareCursor && fixture.env.getVar("fps").valueAsLong == 144 &&
      !fixture.env.getVar("softwareCursor").valueAsBool,
    "new display preferences round trip through runtime and environment");
  configuration.fpsCap = -1;
  testTrue(g,
           !CanvasSceneTestAccess::applyConfiguration(fixture.module,
                                                         configuration) &&
             fixture.env.getVar("fps").valueAsLong == 144,
           "invalid FPS cap leaves applied preferences intact");
  menu->open(applied);
  // End lands on the Exit footer button.
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::End, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.016);
  ExitConfirmDialog* confirm =
    CanvasSceneTestAccess::getExitConfirmDialog(fixture.module);
  testTrue(g,
           !fixture.window.closeRequested && menu->isOpen() &&
             confirm != nullptr && confirm->isOpen(),
           "Exit menu action asks for confirmation first");
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Y, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           fixture.window.closeRequested && !confirm->isOpen(),
           "confirming exit requests normal application shutdown");
}

static void
openPaintDrawer(CellGameFixture& fixture)
{
  fixture.module.update(0.016);
  testTrue(g,
           !CanvasSceneTestAccess::isPaintPaletteExpanded(fixture.module),
           "drawer starts closed as the peeking bubble");
  fixture.window.mouseX = static_cast<double>(fixture.window.width) * 0.5;
  fixture.window.mouseY = static_cast<double>(fixture.window.height) - 1.0;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  testTrue(g,
           !CanvasSceneTestAccess::isPaintPaletteExpanded(fixture.module),
           "footer click cannot open the palette");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
  const float scale =
    fixture.renderer.getUiScale() *
    CanvasSceneTestAccess::getPaintPaletteVisual(fixture.module)
      .getTransform()
      .scaleX;
  fixture.window.mouseX = static_cast<double>(fixture.window.width) * 0.5;
  fixture.window.mouseY =
    static_cast<double>(fixture.window.height -
                        CanvasSceneTestAccess::getCellContext(fixture.module)
                          ->getCanvasView()
                          ->getBottomInsetPixels()) -
    16.0 * scale;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  testTrue(g,
           CanvasSceneTestAccess::isPaintPaletteExpanded(fixture.module),
           "the peeking bubble opens the drawer");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
}

static void
pointAtPaintCard(CellGameFixture& fixture, int stateCount, int state)
{
  const float scale =
    fixture.renderer.getUiScale() *
    CanvasSceneTestAccess::getPaintPaletteVisual(fixture.module)
      .getTransform()
      .scaleX;
  const float width = static_cast<float>(stateCount) * 132.0f + 24.0f;
  fixture.window.mouseX =
    static_cast<double>(fixture.window.width) * 0.5 +
    (-width * 0.5f + 74.0f + static_cast<float>(state) * 132.0f) * scale;
  fixture.window.mouseY =
    static_cast<double>(fixture.window.height -
                        CanvasSceneTestAccess::getCellContext(fixture.module)
                          ->getCanvasView()
                          ->getBottomInsetPixels()) -
    94.0f * scale;
}
static void
testPaintPalette()
{
  CellGameFixture fixture;
  fixture.env.setVar("reducedUiMotion", true);
  openPaintDrawer(fixture);
  SparseCellGrid* grid =
    CanvasSceneTestAccess::getCellContext(fixture.module)->getGrid();
  const std::uint64_t revision = grid->getRevision();
  pointAtPaintCard(fixture, 2, 1);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  testEqInt(g,
            CanvasSceneTestAccess::getPaintBrush(fixture.module),
            1,
            "dead swatch selects the erase brush");
  testTrue(g,
           grid->getRevision() == revision,
           "swatch click does not mutate the world");
  fixture.window.mouseX = 400.0;
  fixture.window.mouseY = 200.0;
  fixture.module.update(0.016);
  testTrue(
    g, grid->getRevision() == revision, "dragging off palette stays captured");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);

  fixture.env.setVar("ModeString", "BRIANS_BRAIN");
  fixture.module.update(0.016);
  testEqInt(g,
            CanvasSceneTestAccess::getPaintBrush(fixture.module),
            0,
            "ruleset change resets generic brush");
  pointAtPaintCard(fixture, 3, 2);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  testEqInt(g,
            CanvasSceneTestAccess::getPaintBrush(fixture.module),
            2,
            "Brian's Brain exposes dying cells");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
  fixture.window.mouseX = 400.0;
  fixture.window.mouseY = 200.0;
  const glm::dvec2 world =
    fixture.camera.ScreenToWorldPrecise({ 400.0, 200.0 });
  std::int64_t cellX = 0;
  std::int64_t cellY = 0;
  testTrue(g,
           CanvasCoordinatePolicy::tryWorldToCell(world.x, &cellX) &&
             CanvasCoordinatePolicy::tryWorldToCell(world.y, &cellY),
           "paint target converts to cell coordinates");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  testEqInt(g,
            grid->getCell({ cellX, cellY }),
            2,
            "selected dying brush paints the canvas");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseRight, InputAction::Press);
  fixture.module.update(0.016);
  testEqInt(g,
            grid->getCell({ cellX, cellY }),
            1,
            "right mouse still erases regardless of brush");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseRight, InputAction::Release);

  fixture.env.setVar("ModeString", "WIREWORLD");
  fixture.module.update(0.016);
  pointAtPaintCard(fixture, 4, 0);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  testEqInt(g,
            CanvasSceneTestAccess::getWireworldBrush(fixture.module),
            0,
            "Wireworld swatch updates its existing brush");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
  fixture.env.setVar("reducedUiMotion", false);
  fixture.window.mouseX = 320.0;
  fixture.window.mouseY =
    326.0 - CanvasSceneTestAccess::getCellContext(fixture.module)
              ->getCanvasView()
              ->getBottomInsetPixels();
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  const float reveal =
    CanvasSceneTestAccess::getPaintPaletteReveal(fixture.module);
  testTrue(g,
           !CanvasSceneTestAccess::isPaintPaletteExpanded(fixture.module) &&
             reveal > 0.0f && reveal < 1.0f,
           "header starts an eased collapse");
  fixture.module.update(0.016);
  testTrue(g,
           !CanvasSceneTestAccess::isPaintPaletteExpanded(fixture.module),
           "held header click does not toggle repeatedly");
  fixture.env.setVar("reducedUiMotion", true);
  fixture.module.update(0.016);
  testTrue(g,
           CanvasSceneTestAccess::getPaintPaletteReveal(fixture.module) ==
             0.0f,
           "reduced motion snaps the collapsed panel");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
  fixture.console.isOpen = true;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  testTrue(g,
           !CanvasSceneTestAccess::getPaintPaletteVisual(fixture.module)
               .isVisible() &&
             !CanvasSceneTestAccess::isPaintPaletteExpanded(fixture.module),
           "console owns input and hides palette");
}

static void
testPaintPaletteSounds()
{
  testSection("CanvasScene: the paint drawer voices expand and collapse");
  CellGameFixture fixture;
  fixture.env.setVar("reducedUiMotion", true);
  CSimSounds::resetCounts();
  openPaintDrawer(fixture);
  testTrue(g,
           CSimSounds::playCount(CSimSound::CanvasPaintMenuExpand) == 1 &&
             CSimSounds::playCount(CSimSound::CanvasPaintMenuCollapse) == 0,
           "opening the drawer plays the expand cue once");
  const std::uint64_t hoversBefore =
    CSimSounds::playCount(CSimSound::MenuHover);
  fixture.window.mouseX = 320.0;
  fixture.window.mouseY =
    326.0 - CanvasSceneTestAccess::getCellContext(fixture.module)
              ->getCanvasView()
              ->getBottomInsetPixels();
  fixture.module.update(0.016);
  fixture.module.update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == hoversBefore + 1,
           "pointing at the collapse header plays one hover cue");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  fixture.module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
  testTrue(g,
           !CanvasSceneTestAccess::isPaintPaletteExpanded(fixture.module) &&
             CSimSounds::playCount(CSimSound::CanvasPaintMenuCollapse) == 1 &&
             CSimSounds::playCount(CSimSound::CanvasPaintMenuExpand) == 1,
           "a held header click collapses it with one collapse cue");
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == hoversBefore + 1,
           "clicking does not add a hover cue");
  fixture.window.mouseX = 0.0;
  fixture.window.mouseY = 0.0;
  fixture.module.update(0.016);
  fixture.window.mouseX = static_cast<double>(fixture.window.width) * 0.5;
  fixture.window.mouseY =
    static_cast<double>(fixture.window.height -
                        CanvasSceneTestAccess::getCellContext(fixture.module)
                          ->getCanvasView()
                          ->getBottomInsetPixels()) -
    16.0 * fixture.renderer.getUiScale() *
      CanvasSceneTestAccess::getPaintPaletteVisual(fixture.module)
        .getTransform()
        .scaleX;
  fixture.module.update(0.016);
  fixture.module.update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == hoversBefore + 2,
           "returning to the expand bubble plays one more hover cue");
  CSimSounds::resetCounts();
}

static void
testPaintPaletteCardSounds()
{
  testSection("CanvasScene: paint cards voice hover, pick and browse");
  CellGameFixture fixture;
  fixture.env.setVar("reducedUiMotion", true);
  // Five states: four cards show and the wheel can move one step.
  fixture.env.setVar("ModeString", "QUADLIFE");
  fixture.module.update(0.016);
  openPaintDrawer(fixture);
  CSimSounds::resetCounts();
  pointAtPaintCard(fixture, 4, 0);
  fixture.module.update(0.016);
  fixture.module.update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 1,
           "pointing at a card plays one hover cue");
  pointAtPaintCard(fixture, 4, 2);
  fixture.module.update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 2,
           "moving to another card plays another");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  fixture.module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
  testTrue(g,
           CanvasSceneTestAccess::getPaintBrush(fixture.module) == 2 &&
             CSimSounds::playCount(CSimSound::MenuSelect) == 1 &&
             CSimSounds::playCount(CSimSound::MenuHover) == 2,
           "picking a card plays one select cue and no hover");
  *fixture.input.getMouseScrollOffset() = -1.0;
  fixture.module.update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 3,
           "a wheel step that moves the cards ticks once");
  *fixture.input.getMouseScrollOffset() = -1.0;
  fixture.module.update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 3,
           "the wheel stays quiet at the end of the states");
  CSimSounds::resetCounts();
}

static void
testPaintPaletteBubbleMorph()
{
  testSection("CanvasScene: the paint bubble morphs into the drawer");
  CellGameFixture fixture;
  fixture.env.setVar("reducedUiMotion", false);
  fixture.module.update(0.016);
  GameVisual& visual =
    CanvasSceneTestAccess::getPaintPaletteVisual(fixture.module);
  testTrue(g,
           visual.isVisible() && visual.textCount() == 0u,
           "the collapsed palette is a wordless bubble");
  const float scale =
    fixture.renderer.getUiScale() * visual.getTransform().scaleX;
  const double bottom =
    static_cast<double>(fixture.window.height -
                        CanvasSceneTestAccess::getCellContext(fixture.module)
                          ->getCanvasView()
                          ->getBottomInsetPixels());
  // The bubble's bounding corner lies outside the circle.
  fixture.window.mouseX =
    static_cast<double>(fixture.window.width) * 0.5 + 26.0 * scale;
  fixture.window.mouseY = bottom - 38.0 * scale;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
  testTrue(g,
           !CanvasSceneTestAccess::isPaintPaletteExpanded(fixture.module),
           "the bubble is round: its bounding corner does not open it");

  fixture.window.mouseX = static_cast<double>(fixture.window.width) * 0.5;
  fixture.window.mouseY = bottom - 16.0 * scale;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
  testTrue(
    g,
    CanvasSceneTestAccess::isPaintPaletteExpanded(fixture.module) &&
      CanvasSceneTestAccess::getPaintPaletteWidthMorph(fixture.module) >
        CanvasSceneTestAccess::getPaintPaletteReveal(fixture.module),
    "opening stretches the bubble wide before it rises");
  testEqSize(g, visual.textCount(), 0u, "labels wait for the drawer to form");
  float peak = 0.0f;
  for (int frame = 0; frame < 90; ++frame) {
    fixture.module.update(1.0 / 60.0);
    peak = std::max(
      peak, CanvasSceneTestAccess::getPaintPaletteReveal(fixture.module));
  }
  testTrue(g,
           peak > 1.02f && CanvasSceneTestAccess::getPaintPaletteReveal(
                             fixture.module) == 1.0f,
           "the drawer bounces past open and settles exactly");
  testTrue(g,
           visual.textCount() > 0u,
           "the drawer's labels fade in once it has formed");
}

static void
testModeBadge()
{
  testSection("ModeBadge: drops in, stretches into a pill, melts away");
  ModeBadge badge;
  testTrue(g, !badge.isVisible(), "the badge starts hidden");
  badge.show("EDIT", ColorRgba{ 255, 204, 102, 255 }, false);
  testTrue(g,
           badge.isVisible() && badge.top() < 0.0f &&
             std::abs(badge.width() - ModeBadge::kHeight) < 0.01f,
           "a new badge starts as a bead above the screen");
  float lowestTop = badge.top();
  float widest = 0.0f;
  for (int frame = 0; frame < 60; ++frame) {
    badge.tick(1.0f / 60.0f, false);
    lowestTop = std::max(lowestTop, badge.top());
    widest = std::max(widest, badge.width());
  }
  const float restingWidth = badge.width();
  testTrue(g,
           badge.top() == ModeBadge::kMargin && lowestTop > ModeBadge::kMargin,
           "it drops past its margin, bounces and rests at the margin");
  testTrue(g,
           restingWidth > ModeBadge::kHeight + 20.0f &&
             widest > restingWidth + 1.0f &&
             badge.getVisual().textCount() == 1u,
           "it stretches past its pill width, settles, and shows its label");

  badge.show("NORMAL", UiTheme::accentCool(), true);
  badge.tick(1.0f / 60.0f, false);
  testTrue(g,
           badge.isVisible() && badge.label() == "NORMAL" &&
             badge.top() > ModeBadge::kMargin - 1.0f,
           "a mode change while showing keeps the badge in place");
  for (int frame = 0; frame < 240; ++frame) {
    badge.tick(1.0f / 60.0f, false);
  }
  testTrue(g, !badge.isVisible(), "after its hold it melts away and hides");

  badge.show("EDIT", ColorRgba{ 255, 204, 102, 255 }, false);
  badge.tick(0.01f, true);
  testTrue(g,
           badge.top() == ModeBadge::kMargin && badge.width() > 40.0f,
           "reduced motion shows the finished pill at once");
  for (int frame = 0; frame < 20; ++frame) {
    badge.tick(0.1f, true);
  }
  testTrue(g, !badge.isVisible(), "reduced motion hides it at once too");

  CellGameFixture fixture;
  fixture.module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Press);
  fixture.module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Release);
  ModeBadge& moduleBadge =
    CanvasSceneTestAccess::getModeBadge(fixture.module);
  testTrue(g,
           moduleBadge.isVisible() && !moduleBadge.label().empty(),
           "toggling the mode shows the corner badge");
  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  bool dispatched = false;
  for (DrawableBase* drawable : fixture.scene.drawablesIn(RenderLayerId::UI)) {
    dispatched = dispatched || drawable == &moduleBadge.getVisual();
  }
  testTrue(g, dispatched, "the badge is drawn in the UI layer");
}

static void
testSoftwareCursor()
{
  testSection("SoftwareCursor: exact tip, swaying body, afterimage, press");
  SoftwareCursor cursor;
  testTrue(g, !cursor.isVisible(), "the software cursor starts hidden");
  cursor.update(1.0f / 60.0f, 100.0f, 100.0f, true, false, false);
  testTrue(g,
           cursor.isVisible() && cursor.tipX() == 100.0f &&
             cursor.tipY() == 100.0f && cursor.lean() == 0.0f,
           "it appears still, with its tip on the pointer");
  float furthestLean = 0.0f;
  for (int frame = 1; frame <= 12; ++frame) {
    cursor.update(1.0f / 60.0f,
                  100.0f + 12.0f * static_cast<float>(frame),
                  100.0f,
                  true,
                  false,
                  false);
    furthestLean = std::max(furthestLean, cursor.lean());
  }
  testTrue(g,
           furthestLean > 0.1f && cursor.tipX() == 244.0f,
           "moving right swings the body while the tip tracks exactly");
  const std::size_t movingShapes = cursor.getVisual().shapeCount();
  float swingBack = 0.0f;
  for (int frame = 0; frame < 90; ++frame) {
    cursor.update(1.0f / 60.0f, 244.0f, 100.0f, true, false, false);
    swingBack = std::min(swingBack, cursor.lean());
  }
  testTrue(g,
           swingBack < 0.0f && std::abs(cursor.lean()) < 0.001f,
           "stopping swings it past upright before it settles");
  const std::size_t stillShapes = cursor.getVisual().shapeCount();
  testTrue(g,
           movingShapes > stillShapes,
           "moving leaves a holographic afterimage that a still pointer drops");
  cursor.update(1.0f / 60.0f, 244.0f, 100.0f, true, true, false);
  for (int frame = 0; frame < 20; ++frame) {
    cursor.update(1.0f / 60.0f, 244.0f, 100.0f, true, true, false);
  }
  const float pressed = cursor.pressScale();
  float rebound = 0.0f;
  for (int frame = 0; frame < 30; ++frame) {
    cursor.update(1.0f / 60.0f, 244.0f, 100.0f, true, false, false);
    rebound = std::max(rebound, cursor.pressScale());
  }
  testTrue(g,
           pressed < 0.9f && rebound > 1.0f,
           "a press squishes it and releasing boings it back");
  cursor.update(1.0f / 60.0f, 244.0f, 100.0f, false, false, false);
  testTrue(g,
           !cursor.isVisible() && cursor.lean() == 0.0f,
           "hiding clears it and forgets its motion");
  cursor.update(1.0f / 60.0f, 50.0f, 50.0f, true, false, true);
  cursor.update(1.0f / 60.0f, 400.0f, 50.0f, true, false, true);
  testTrue(g,
           cursor.lean() == 0.0f && cursor.tipX() == 400.0f &&
             cursor.getVisual().shapeCount() <= stillShapes,
           "reduced motion keeps it upright, without an afterimage");
}

static void
testPaintPaletteFittedInput()
{
  CellGameFixture fixture;
  fixture.env.setVar("reducedUiMotion", true);
  fixture.env.setVar("ModeString", "WIREWORLD");
  fixture.window.handleResize(200, 240);
  openPaintDrawer(fixture);
  GameVisual& visual =
    CanvasSceneTestAccess::getPaintPaletteVisual(fixture.module);
  const float fit = visual.getTransform().scaleX;
  testTrue(g, fit > 0.0f && fit < 1.0f, "small window fits the entire palette");
  const float scale = fixture.renderer.getUiScale() * fit;
  pointAtPaintCard(fixture, 4, 2);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  testEqInt(g,
            CanvasSceneTestAccess::getWireworldBrush(fixture.module),
            2,
            "fitted coordinates select the visible tail swatch");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
  fixture.window.mouseX = 100.0;
  fixture.window.mouseY =
    240.0 -
    CanvasSceneTestAccess::getCellContext(fixture.module)
      ->getCanvasView()
      ->getBottomInsetPixels() -
    154.0 * scale;
  const SparseCellGrid* grid =
    CanvasSceneTestAccess::getCellContext(fixture.module)->getGrid();
  const std::uint64_t beforeClose = grid->getRevision();
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  testTrue(g,
           !CanvasSceneTestAccess::isPaintPaletteExpanded(fixture.module) &&
             visual.textCount() == 0u,
           "fitted header collapses into the wordless bubble");
  testTrue(g,
           grid->getRevision() == beforeClose,
           "snapping the tab closed cannot paint through its former position");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
  fixture.window.mouseY =
    240.0 -
    CanvasSceneTestAccess::getCellContext(fixture.module)
      ->getCanvasView()
      ->getBottomInsetPixels() -
    16.0 * scale;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  bool hasStateLabels = true;
  const RuleSet* wireworld =
    CanvasSceneTestAccess::getCellContext(fixture.module)->getRuleSet();
  GameVisual& labels =
    CanvasSceneTestAccess::getPaintPaletteTopVisual(fixture.module);
  for (unsigned char state = 0u; state < 4u; ++state) {
    bool found = false;
    for (std::size_t index = 0u; index < labels.textCount(); ++index) {
      found = found ||
              labels.getText(index)->content == wireworld->getStateName(state);
    }
    hasStateLabels = hasStateLabels && found;
  }
  testTrue(g,
           CanvasSceneTestAccess::isPaintPaletteExpanded(fixture.module) &&
             hasStateLabels,
           "second header click restores all state labels and help");
  const float localPanelWidth = 4.0f * 132.0f + 24.0f;
  const float localScreenWidth = static_cast<float>(fixture.window.width) /
                                 (fixture.renderer.getUiScale() * fit);
  const float panelLeft = (localScreenWidth - localPanelWidth) * 0.5f;
  const float panelRight = panelLeft + localPanelWidth;
  const std::shared_ptr<Font> font = Font::getDefaultFont();
  bool textInside = visual.textCount() > 0u;
  for (std::size_t index = 0u; index < visual.textCount(); ++index) {
    const TextPrimitive* text = visual.getText(index);
    const float textWidth =
      font != nullptr ? font->measureText(text->content, text->sizePt).width
                      : GuiKit::estimateTextWidth(text->content, text->sizePt);
    textInside = textInside && text->x >= panelLeft + 10.0f &&
                 text->x + textWidth <= panelRight - 10.0f;
  }
  testTrue(g, textInside, "every drawer label stays inside the drawer surface");
  testEqInt(g,
            CanvasSceneTestAccess::getWireworldBrush(fixture.module),
            2,
            "collapse preserves the selected brush");
  fixture.scene.ClearDrawables();
  fixture.scene.AddDrawable(&visual, RenderLayerId::UI);
  fixture.mock.resetCounters();
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&fixture.scene, &fixture.camera);
  fixture.renderer.EndFrame();
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) > 0u,
           "fitted palette emits backend-neutral draw commands");
}

static void
testHamburgerMenuButton()
{
  testSection("CanvasScene: hamburger icon toggles settings menu");
  CellGameFixture fixture;
  ConfigurationMenu* menu =
    CanvasSceneTestAccess::getConfigurationMenu(fixture.module);
  testTrue(g, menu != nullptr && !menu->isOpen(), "settings start closed");

  // Initial update establishes hamburger button placement and dimensions
  fixture.module.update(0.016);
  GameVisual* hamburger =
    CanvasSceneTestAccess::getHamburgerVisual(fixture.module);
  testTrue(g,
           hamburger != nullptr && hamburger->isVisible(),
           "hamburger button is visible in default state");

  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  const std::vector<DrawableBase*> uiDrawables =
    fixture.scene.drawablesIn(RenderLayerId::UI);
  testTrue(g,
           std::find(uiDrawables.begin(), uiDrawables.end(), hamburger) !=
             uiDrawables.end(),
           "hamburger button is on the UI layer");

  const float hx = CanvasSceneTestAccess::getHamburgerX(fixture.module);
  const float hy = CanvasSceneTestAccess::getHamburgerY(fixture.module);
  const float hsize =
    CanvasSceneTestAccess::getHamburgerSize(fixture.module);
  testTrue(g,
           hx > 0.0f && hy > 0.0f && hsize > 0.0f,
           "hamburger button has valid bounds");

  // Move mouse outside hamburger -> not hovered
  fixture.window.mouseX = 0.0;
  fixture.window.mouseY = 0.0;
  fixture.module.update(0.016);
  testTrue(g,
           !CanvasSceneTestAccess::isHamburgerHovered(fixture.module),
           "hamburger is not hovered when mouse is away");

  // Move mouse inside hamburger -> hovered
  CSimSounds::resetCounts();
  fixture.window.mouseX = static_cast<double>(hx + hsize * 0.5f);
  fixture.window.mouseY = static_cast<double>(hy + hsize * 0.5f);
  fixture.module.update(0.016);
  testTrue(g,
           CanvasSceneTestAccess::isHamburgerHovered(fixture.module),
           "hamburger is hovered when mouse is over it");
  fixture.module.update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 1,
           "hovering the hamburger plays the hover cue once");

  // Click hamburger -> toggles settings open
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  testTrue(g, menu->isOpen(), "clicking hamburger opens settings menu");
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuSelect) == 1,
           "clicking hamburger plays the select cue once");
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 1,
           "the click adds no hover cue");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);

  // When settings are open, hamburger is hidden
  testTrue(g,
           !hamburger->isVisible(),
           "hamburger is hidden while settings menu is open");

  // Press Escape to close settings
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g, !menu->isOpen(), "settings menu closed with Escape");
  testTrue(g,
           hamburger->isVisible(),
           "hamburger reappears after settings menu closes");
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuSelect) == 1,
           "holding and closing do not repeat the select cue");
  CSimSounds::resetCounts();
}

static void
testExitConfirmationFromQ()
{
  testSection("CanvasScene: Q asks before exiting");
  CellGameFixture fixture;
  ExitConfirmDialog* confirm =
    CanvasSceneTestAccess::getExitConfirmDialog(fixture.module);
  testTrue(
    g, confirm != nullptr && !confirm->isOpen(), "confirm starts closed");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Q, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           !fixture.window.closeRequested && confirm->isOpen(),
           "Q opens the exit confirmation instead of closing immediately");

  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  testEqSize(g,
             fixture.scene.drawableCount(),
             3u,
             "exit confirmation renders above the canvas and entrance veil");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           !fixture.window.closeRequested && !confirm->isOpen(),
           "Escape cancels the exit confirmation");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Q, InputAction::Press, 0 });
  fixture.module.update(0.016);
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Y, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           fixture.window.closeRequested && !confirm->isOpen(),
           "Y confirms Q-initiated exit");
}

static void
testLoadRejectsInvalidFiles()
{
  testSection("CanvasScene: invalid save validation");
  CellGameFixture fixture;
  testTrue(g,
           !CanvasSceneTestAccess::load(fixture.module, ""),
           "empty load path rejected");
  testTrue(g,
           !CanvasSceneTestAccess::load(fixture.module, "missing.illumo"),
           "missing file rejected");

  std::ofstream("short.illumo", std::ios::binary).write("short", 5);
  testTrue(g,
           !CanvasSceneTestAccess::load(fixture.module, "short.illumo"),
           "truncated header rejected");

  const char sparseMagic[8] = { 'I', 'L', 'L', 'U', 'M', 'O', '2', '\0' };
  const std::uint32_t sparseVersion = 2;
  char sparseTag[MAX_RULETAG_SIZE] = {};
  std::memcpy(sparseTag, "SEEDS", 5);
  const double sparseCameraX = 0.0;
  const double sparseCameraY = 0.0;
  const double sparseZoom = 1.0;
  const std::uint64_t sparseChunks = 1;
  {
    std::ofstream output("short-v2.illumo", std::ios::binary | std::ios::trunc);
    output.write(sparseMagic, sizeof(sparseMagic));
    output.write(reinterpret_cast<const char*>(&sparseVersion),
                 sizeof(sparseVersion));
    output.write(sparseTag, sizeof(sparseTag));
    output.write(reinterpret_cast<const char*>(&sparseCameraX),
                 sizeof(sparseCameraX));
    output.write(reinterpret_cast<const char*>(&sparseCameraY),
                 sizeof(sparseCameraY));
    output.write(reinterpret_cast<const char*>(&sparseZoom),
                 sizeof(sparseZoom));
    output.write(reinterpret_cast<const char*>(&sparseChunks),
                 sizeof(sparseChunks));
  }
  CellContext* liveContext =
    CanvasSceneTestAccess::getCellContext(fixture.module);
  liveContext->getGrid()->setCell(CellAddress{ 77, 88 }, 0);
  testTrue(g,
           !CanvasSceneTestAccess::load(fixture.module, "short-v2.illumo"),
           "truncated sparse record rejected");
  testEqUChar(g,
              liveContext->getGrid()->getCell(CellAddress{ 77, 88 }),
              0,
              "malformed sparse load does not mutate live state");

  const char sparseMagicV3[8] = { 'I', 'L', 'L', 'U', 'M', 'O', '3', '\0' };
  const std::uint32_t sparseVersionV3 = 3;
  const std::int64_t invalidWorldWidth = 0;
  const std::int64_t invalidWorldHeight = 2;
  const std::uint64_t noSparseChunks = 0;
  fixture.camera.SetPositionPrecise(10.5, 20.5);
  fixture.camera.SetZoom(2.5f);
  for (const std::uint32_t version : { 2u, 3u }) {
    for (const double invalidCoordinate :
         { 1e300, -1e300, std::ldexp(1.0, 67), -std::ldexp(1.0, 67) }) {
      for (const bool invalidX : { false, true }) {
        const double savedX = invalidX ? invalidCoordinate : 0.0;
        const double savedY = invalidX ? 0.0 : invalidCoordinate;
        {
          std::ofstream output("invalid-camera.illumo",
                               std::ios::binary | std::ios::trunc);
          output.write(version == 2 ? sparseMagic : sparseMagicV3, 8);
          output.write(reinterpret_cast<const char*>(&version),
                       sizeof(version));
          output.write(sparseTag, sizeof(sparseTag));
          output.write(reinterpret_cast<const char*>(&savedX), sizeof(savedX));
          output.write(reinterpret_cast<const char*>(&savedY), sizeof(savedY));
          output.write(reinterpret_cast<const char*>(&sparseZoom),
                       sizeof(sparseZoom));
          if (version == 3) {
            const std::int64_t finiteSize = 2;
            output.write(reinterpret_cast<const char*>(&finiteSize),
                         sizeof(finiteSize));
            output.write(reinterpret_cast<const char*>(&finiteSize),
                         sizeof(finiteSize));
          }
          output.write(reinterpret_cast<const char*>(&noSparseChunks),
                       sizeof(noSparseChunks));
        }
        testTrue(g,
                 !CanvasSceneTestAccess::load(fixture.module,
                                                 "invalid-camera.illumo"),
                 "otherwise valid sparse metadata rejects unsafe camera");
        testTrue(g,
                 liveContext->getGrid()->getCell(CellAddress{ 77, 88 }) == 0 &&
                   fixture.camera.GetPositionPrecise() ==
                     glm::dvec2(10.5, 20.5) &&
                   fixture.camera.GetZoom() == 2.5f,
                 "unsafe metadata preserves live cells and camera");
      }
    }
  }
  {
    std::ofstream output("invalid-v3-topology.illumo",
                         std::ios::binary | std::ios::trunc);
    output.write(sparseMagicV3, sizeof(sparseMagicV3));
    output.write(reinterpret_cast<const char*>(&sparseVersionV3),
                 sizeof(sparseVersionV3));
    output.write(sparseTag, sizeof(sparseTag));
    output.write(reinterpret_cast<const char*>(&sparseCameraX),
                 sizeof(sparseCameraX));
    output.write(reinterpret_cast<const char*>(&sparseCameraY),
                 sizeof(sparseCameraY));
    output.write(reinterpret_cast<const char*>(&sparseZoom),
                 sizeof(sparseZoom));
    output.write(reinterpret_cast<const char*>(&invalidWorldWidth),
                 sizeof(invalidWorldWidth));
    output.write(reinterpret_cast<const char*>(&invalidWorldHeight),
                 sizeof(invalidWorldHeight));
    output.write(reinterpret_cast<const char*>(&noSparseChunks),
                 sizeof(noSparseChunks));
  }
  testTrue(g,
           !CanvasSceneTestAccess::load(fixture.module,
                                           "invalid-v3-topology.illumo"),
           "mixed finite and infinite topology metadata is rejected");
  testEqUChar(g,
              liveContext->getGrid()->getCell(CellAddress{ 77, 88 }),
              0,
              "invalid v3 metadata does not mutate live state");

  writeSaveFile("unknown-rule.illumo", "NOT_A_RULE", 2, 2, { 1, 1, 1, 1 });
  testTrue(
    g,
    !CanvasSceneTestAccess::load(fixture.module, "unknown-rule.illumo"),
    "unknown ruleset rejected");

  writeSaveFile("invalid-size.illumo", "SEEDS", 0, 4, {});
  testTrue(
    g,
    !CanvasSceneTestAccess::load(fixture.module, "invalid-size.illumo"),
    "zero dimension rejected");
  writeSaveFile("oversized.illumo", "SEEDS", 100000001, 1, {});
  testTrue(g,
           !CanvasSceneTestAccess::load(fixture.module, "oversized.illumo"),
           "oversized canvas rejected before allocation");
  writeSaveFile("short-cells.illumo", "SEEDS", 2, 2, { 0, 1, 0 });
  testTrue(
    g,
    !CanvasSceneTestAccess::load(fixture.module, "short-cells.illumo"),
    "truncated cell data rejected");

  testTrue(g,
           !CanvasSceneTestAccess::save(fixture.module, ""),
           "empty save path rejected");
  testTrue(g,
           !CanvasSceneTestAccess::save(fixture.module, "."),
           "directory cannot be opened as a save file");
}

static void
testLoadCopiesOverlap()
{
  testSection("CanvasScene: different-size save overlap");
  CellGameFixture fixture(4, 3);
  writeSaveFile("small.illumo", "SEEDS", 2, 2, { 0, 1, 1, 0 });
  testTrue(g,
           CanvasSceneTestAccess::load(fixture.module, "small.illumo"),
           "different-size valid save loads");
  CanvasView* canvas =
    CanvasSceneTestAccess::getCellContext(fixture.module)->getCanvasView();
  testEqUChar(
    g, canvas->getCanvasPixel(-1, -1), 0, "legacy origin row zero copied");
  testEqUChar(
    g, canvas->getCanvasPixel(0, -1), 1, "legacy row zero width preserved");
  testEqUChar(g, canvas->getCanvasPixel(0, 0), 0, "legacy row one copied");
  testEqUChar(
    g, canvas->getCanvasPixel(3, 2), 1, "outside overlap remains empty");
  testTrue(g,
           CanvasSceneTestAccess::getCellContext(fixture.module)
               ->getGrid()
               ->getAllocatedChunkCount() == 2,
           "legacy cells are imported sparsely");
}

static void
testConsoleSimulationCommands()
{
  testSection("CanvasScene: simulation console commands");
  CellGameFixture fixture(6, 6);
  fixture.execute("ruleset", { "SEEDS" });
  testTrue(
    g,
    CanvasSceneTestAccess::getCellContext(fixture.module)->getModeString() ==
      "SEEDS",
    "ruleset command switches mode");
  fixture.execute("mode", { "not_real" });
  testTrue(g,
           historyContains(fixture.console, "Unknown ruleset"),
           "unknown mode is reported");

  fixture.execute("tps", { "120" });
  testEqInt(g,
            static_cast<int>(fixture.env.getVar("tps").valueAsLong),
            120,
            "TPS command updates simulator timing");
  fixture.execute("tps", { "0" });
  testEqInt(g,
            static_cast<int>(fixture.env.getVar("tps").valueAsLong),
            120,
            "invalid TPS is rejected");
  fixture.execute("speed", { "2.5" });
  testTrue(g,
           fixture.env.getVar("speedFactor").value == "2.5",
           "speed command updates simulator multiplier");
  fixture.execute("fade", { "12.25" });
  testTrue(g,
           fixture.env.getVar("cellFadeSpeed").value == "12.25",
           "fade command updates simulator presentation");

  fixture.execute("setcell", { "1", "2", "0" });
  CanvasView* canvas =
    CanvasSceneTestAccess::getCellContext(fixture.module)->getCanvasView();
  testEqUChar(
    g, canvas->getCanvasPixel(1, 2), 0, "setcell writes a valid cell");
  fixture.execute("setcell", { "-99", "2", "0" });
  fixture.execute("setcell", { "1", "2", "999" });
  testEqUChar(g, canvas->getCanvasPixel(-99, 2), 0, "far setcell is accepted");
  testTrue(g,
           historyContains(fixture.console, "Usage: setcell"),
           "setcell state validation is reported");

  // Confirm clearing is on by default: the command asks first.
  const unsigned char beforeClear = canvas->getCanvasPixel(1, 2);
  fixture.execute("clear_canvas");
  ExitConfirmDialog* clearConfirm =
    CanvasSceneTestAccess::getExitConfirmDialog(fixture.module);
  testTrue(g,
           clearConfirm != nullptr && clearConfirm->isOpen() &&
             canvas->getCanvasPixel(1, 2) == beforeClear,
           "clear command asks before emptying cells");
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Y, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           !clearConfirm->isOpen() && canvas->getCanvasPixel(1, 2) == 1,
           "confirming the dialog empties cells");
  fixture.execute("setcell", { "1", "2", "0" });
  fixture.execute("clear_canvas", { "yes" });
  testEqUChar(g,
              canvas->getCanvasPixel(1, 2),
              1,
              "clear_canvas yes empties cells without asking");
  fixture.env.setVar("confirmClear", false);
  fixture.execute("setcell", { "1", "2", "0" });
  fixture.execute("clear_canvas");
  testTrue(g,
           !clearConfirm->isOpen() && canvas->getCanvasPixel(1, 2) == 1,
           "with confirmation off the command clears at once");
  fixture.execute("randomize", { "100" });
  const CellAddress visibleOrigin = canvas->getVisibleCell(0, 0);
  testEqUChar(g,
              canvas->getCanvasPixel(visibleOrigin.x, visibleOrigin.y),
              0,
              "100 percent binary randomize fills alive cells");
  fixture.execute("randomize", { "0" });
  testEqUChar(g,
              canvas->getCanvasPixel(visibleOrigin.x, visibleOrigin.y),
              1,
              "zero percent randomize empties cells");
  fixture.camera.SetZoom(0.1f);
  canvas->syncVisibleRegion();
  fixture.execute("randomize", { "100" });
  const CellAddress farFirstCell = canvas->getVisibleFirstCell();
  const CellAddress farLastCell{
    farFirstCell.x + canvas->getVisibleCellWidth() - 1,
    farFirstCell.y - canvas->getVisibleCellHeight() + 1
  };
  testTrue(g,
           canvas->getVisibleCellWidth() > canvas->getViewWidth() ||
             canvas->getVisibleCellHeight() > canvas->getViewHeight(),
           "far view aggregates source cells into fewer display texels");
  testEqUChar(g,
              canvas->getCanvasPixel(farLastCell.x, farLastCell.y),
              0,
              "far-view randomize reaches the full source region");
  fixture.execute("randomize", { "101" });
  testTrue(g,
           historyContains(fixture.console, "percentage from 0 to 100"),
           "randomize validates density");
  canvas->setCanvasPixel(0, 0, 0);
  fixture.execute("randomize", { "nan" });
  fixture.execute("randomize", { "inf" });
  testEqUChar(
    g, canvas->getCanvasPixel(0, 0), 0, "randomize rejects non-finite density");

  fixture.execute("pause");
  testTrue(g,
           CanvasSceneTestAccess::getState(fixture.module) ==
             CellState::EDIT,
           "pause enters edit state");
  fixture.execute("step", { "2" });
  testTrue(g,
           historyContains(fixture.console, "Advanced 2 generations"),
           "step advances requested generations");
  fixture.execute("step", { "0" });
  testTrue(g,
           historyContains(fixture.console, "integer from 1 to 1000"),
           "step validates generation count");
  fixture.execute("run");
  testTrue(g,
           CanvasSceneTestAccess::getState(fixture.module) ==
             CellState::NORMAL,
           "run enters normal state");
  fixture.executeThroughConsole("status");
  testTrue(g,
           historyContains(fixture.console, "State: RUNNING"),
           "status dispatches through the console to report simulation state");
}

static void
testConsoleCameraAndFiles()
{
  testSection("CanvasScene: camera and file console commands");
  CellGameFixture fixture(4, 4);
  fixture.execute("camera", { "10.5", "20.5", "2.5" });
  testTrue(g,
           fixture.camera.GetPosition() == glm::vec2(10.5f, 20.5f),
           "camera command updates position");
  testTrue(g, fixture.camera.GetZoom() == 2.5f, "camera command updates zoom");
  fixture.execute("camera", { "bad", "20" });
  testTrue(g,
           historyContains(fixture.console, "Usage: camera"),
           "camera validates numeric arguments");
  fixture.execute("camera", { "nan", "20" });
  testTrue(g,
           fixture.camera.GetPosition() == glm::vec2(10.5f, 20.5f),
           "camera rejects non-finite position");
  for (const char* invalid : { "1e300",
                               "-1e300",
                               "1.47573952589676412928e20",
                               "-1.47573952589676412928e20" }) {
    fixture.execute("camera", { invalid, "20", "3" });
    fixture.execute("camera", { "10", invalid, "4" });
    testTrue(g,
             fixture.camera.GetPositionPrecise() == glm::dvec2(10.5, 20.5) &&
               fixture.camera.GetZoom() == 2.5f,
             "unsafe camera coordinates preserve position and zoom");
  }
  fixture.execute("camera_reset");
  testTrue(g,
           historyContains(fixture.console, "Camera reset"),
           "camera reset command reports success");

  fixture.execute("save", { "console-save" });
  testTrue(g,
           std::filesystem::exists("console-save.csim"),
           "save command adds extension");
  fixture.execute("load", { "console-save" });
  testTrue(
    g,
    historyContains(fixture.console, "Loaded canvas from console-save.csim"),
    "load command falls back to extension");
  std::filesystem::copy_file("console-save.csim",
                             "legacy-console-save.illumo",
                             std::filesystem::copy_options::overwrite_existing);
  fixture.execute("load", { "legacy-console-save" });
  testTrue(g,
           historyContains(fixture.console,
                           "Loaded canvas from legacy-console-save.illumo"),
           "load command retains legacy .illumo fallback");
  fixture.execute("save", {});
  fixture.execute("load", {});
  testTrue(g,
           historyContains(fixture.console, "Usage: save"),
           "save command validates arguments");
  testTrue(g,
           historyContains(fixture.console, "Usage: load"),
           "load command validates arguments");

  gSaveDialogResult.clear();
  gLoadDialogResult.clear();
  fixture.execute("save_dialog");
  fixture.execute("load_dialog");
  testTrue(g,
           historyContains(fixture.console, "Save cancelled"),
           "cancelled save dialog is reported");
  testTrue(g,
           historyContains(fixture.console, "Load cancelled"),
           "cancelled load dialog is reported");

  gSaveDialogResult = "dialog-save";
  fixture.execute("save_dialog");
  testTrue(g,
           std::filesystem::exists("dialog-save.csim"),
           "save dialog path gains extension");
  gLoadDialogResult = "dialog-save.csim";
  fixture.execute("load_dialog");
  testTrue(
    g,
    historyContains(fixture.console, "Loaded canvas from dialog-save.csim"),
    "load dialog uses selected path");
}

// Presses and releases the left button over a toolbar button.
static bool
clickToolbarButton(CellGameFixture& fixture, CanvasEditAction action)
{
  float x = 0.0f;
  float y = 0.0f;
  if (!CanvasSceneTestAccess::getActionBar(fixture.module)
         .buttonCenter(action, &x, &y)) {
    return false;
  }
  fixture.window.mouseX = x;
  fixture.window.mouseY = y;
  fixture.module.update(0.0);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.0);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.0);
  return true;
}

static void
testToolbarSaveLoad()
{
  testSection("CanvasScene: the toolbar saves and loads through the pickers");
  WorkingDirectoryFixture directory;
  testTrue(g, directory.isReady(), "temporary working directory is ready");
  if (!directory.isReady()) {
    return;
  }
  CellGameFixture fixture;
  fixture.env.setVar("reducedUiMotion", true);
  SparseCellGrid* grid =
    CanvasSceneTestAccess::getCellContext(fixture.module)->getGrid();
  CanvasView* canvas =
    CanvasSceneTestAccess::getCellContext(fixture.module)->getCanvasView();
  canvas->clearCanvas();
  canvas->setCanvasPixel(3, 4, 0);
  fixture.module.update(0.0);

  gSaveDialogResult = "toolbar-save";
  testTrue(g,
           clickToolbarButton(fixture, CanvasEditAction::Save),
           "Save is on the toolbar");
  testTrue(g,
           std::filesystem::exists("toolbar-save.csim"),
           "Save writes the picked file");

  canvas->clearCanvas();
  fixture.module.update(0.0);
  gLoadDialogResult = "toolbar-save.csim";
  testTrue(g,
           clickToolbarButton(fixture, CanvasEditAction::Load),
           "Load is on the toolbar");
  testTrue(g,
           grid->getCell({ 3, 4 }) == 0 && grid->getStoredCellCount() == 1u,
           "Load restores the picked world");
  testTrue(
    g,
    historyContains(fixture.console, "Loaded canvas from toolbar-save.csim"),
    "the load is announced");

  gLoadDialogResult.clear();
  clickToolbarButton(fixture, CanvasEditAction::Load);
  testTrue(g,
           historyContains(fixture.console, "Load cancelled") &&
             grid->getStoredCellCount() == 1u,
           "a cancelled pick keeps the world");
}

static void
testUpdateStateAndTiming()
{
  testSection("CanvasScene: update state and timing");
  CellGameFixture fixture(5, 5);
  CellContext* cellContext =
    CanvasSceneTestAccess::getCellContext(fixture.module);
  CanvasView* canvas = cellContext->getCanvasView();
  canvas->clearCanvas();
  canvas->setCanvasPixel(1, 2, 0);
  canvas->setCanvasPixel(2, 2, 0);
  canvas->setCanvasPixel(3, 2, 0);

  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Press);
  fixture.module.update(0.0);
  testTrue(g,
           CanvasSceneTestAccess::getState(fixture.module) ==
             CellState::NORMAL,
           "toggle action enters normal state");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::None);
  fixture.module.update(0.04);
  bool publishedGeneration = false;
  for (int attempt = 0; attempt < 10000 && !publishedGeneration; ++attempt) {
    std::this_thread::yield();
    fixture.module.update(0.0);
    publishedGeneration =
      CanvasSceneTestAccess::getLastSimulationSteps(fixture.module) == 1;
  }
  testTrue(g, publishedGeneration, "normal update publishes async generation");
  testEqUChar(g,
              canvas->getCanvasPixel(2, 1),
              0,
              "normal update advances simulation at configured tps");

  fixture.env.setVar("tps", 100000);
  fixture.env.setVar("speedFactor", 1000.0);
  fixture.env.setVar("cellFadeSpeed", -2.0);
  fixture.module.update(1.0);
  CanvasSceneTestAccess::drainSimulation(fixture.module);
  testTrue(g,
           CanvasSceneTestAccess::getState(fixture.module) ==
             CellState::NORMAL,
           "large delta and rate remain bounded");
  testTrue(g,
           CanvasSceneTestAccess::getLastSimulationSteps(fixture.module) <=
             1,
           "normal update publishes at most one generation per frame");
  testTrue(g,
           CanvasSceneTestAccess::getSimulationDebtDropped(fixture.module),
           "normal update drops excessive catch-up debt");

  InputManager::scrollCallback(nullptr, 0.0, 1.0);
  const float oldZoom = fixture.camera.GetZoom();
  fixture.module.update(-1.0);
  fixture.camera.Update(1.0f);
  testTrue(
    g, fixture.camera.GetZoom() > oldZoom, "scroll input updates camera zoom");

  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Press);
  fixture.module.update(0.0);
  testTrue(g,
           CanvasSceneTestAccess::getState(fixture.module) ==
             CellState::EDIT,
           "toggle action returns to edit state");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::None);
  fixture.module.update(0.016);
}

static void
testCameraInputBounds()
{
  testSection("CanvasScene: camera input bounds");
  CellGameFixture fixture;
  const glm::dvec2 boundary(CanvasCoordinatePolicy::kMaximumWorld, 0.0);
  fixture.camera.SetPositionPrecise(boundary.x, boundary.y);
  fixture.camera.SetZoom(1.0f);
  fixture.window.mouseX = -1e9;
  fixture.window.mouseY = 240.0;
  InputManager::scrollCallback(nullptr, 0.0, -1.0);
  fixture.module.update(0.0);
  testTrue(g,
           fixture.camera.GetTargetPositionPrecise() == boundary &&
             fixture.camera.GetTargetZoom() == 1.0f,
           "zoom out rejects a pending target beyond coordinate boundary");
  fixture.camera.Update(1.0f);
  testTrue(g,
           fixture.camera.GetPositionPrecise() == boundary &&
             fixture.camera.GetZoom() == 1.0f,
           "later interpolation cannot apply rejected zoom");
  fixture.camera.SetPositionPrecise(0.0, 0.0);
  testTrue(g,
           CanvasSceneTestAccess::save(fixture.module, "camera-save.illumo"),
           "valid camera saves");
  const std::vector<char> saved = readFileBytes("camera-save.illumo");
  fixture.camera.SetPositionPrecise(1e300, 0.0);
  testTrue(
    g,
    !CanvasSceneTestAccess::save(fixture.module, "camera-save.illumo"),
    "unsafe direct camera cannot produce unloadable save");
  testTrue(g,
           readFileBytes("camera-save.illumo") == saved,
           "invalid camera save preserves destination");
}

static void
testSimulationFailureReporting()
{
  testSection(
    "CanvasScene: failed generations do not count and remain retryable");
  {
    CellGameFixture fixture;
    fixture.execute("ruleset", { "RULE_90" });
    SparseCellGrid* grid =
      CanvasSceneTestAccess::getCellContext(fixture.module)->getGrid();
    grid->clear();
    grid->setCell(
      CellAddress{ 0, std::numeric_limits<std::int64_t>::max() - 1 }, 0);
    fixture.execute("step", { "3" });
    testTrue(
      g,
      CanvasSceneTestAccess::getSimulationGeneration(fixture.module) == 1 &&
        CanvasSceneTestAccess::isSimulationRetryPending(fixture.module),
      "manual batch counts only its successful first generation");
    testTrue(g,
             historyContains(fixture.console, "failed after 1 of 3"),
             "manual failure reports requested and completed count");
    grid->clear();
    grid->setCell(CellAddress{ 0, 0 }, 0);
    fixture.execute("run");
    fixture.module.update(0.0);
    testTrue(g,
             CanvasSceneTestAccess::isSimulationBusy(fixture.module),
             "run retries failed work without waiting another time step");
    CanvasSceneTestAccess::drainSimulation(fixture.module);
    testTrue(
      g,
      CanvasSceneTestAccess::getSimulationGeneration(fixture.module) == 2,
      "successful retry counts once");
  }
  {
    CellGameFixture fixture;
    fixture.execute("ruleset", { "RULE_90" });
    SparseCellGrid* grid =
      CanvasSceneTestAccess::getCellContext(fixture.module)->getGrid();
    grid->clear();
    // The last row has no row below it, so the first generation fails.
    grid->setCell(CellAddress{ 0, std::numeric_limits<std::int64_t>::max() },
                  0);
    const std::uint64_t revision = grid->getRevision();
    fixture.execute("run");
    fixture.module.update(0.04);
    CanvasSceneTestAccess::drainSimulation(fixture.module);
    testTrue(
      g,
      CanvasSceneTestAccess::getSimulationGeneration(fixture.module) == 0 &&
        grid->getRevision() == revision &&
        CanvasSceneTestAccess::getState(fixture.module) == CellState::EDIT &&
        CanvasSceneTestAccess::isSimulationRetryPending(fixture.module),
      "async failure preserves published world and pauses with pending retry");
    testTrue(g,
             historyContains(fixture.console, "paused without publication"),
             "async failure has actionable console diagnostic");
    fixture.module.update(0.25);
    testTrue(g,
             !CanvasSceneTestAccess::isSimulationBusy(fixture.module),
             "failure does not create an automatic retry loop");
    grid->clear();
    grid->setCell(CellAddress{ 0, 0 }, 0);
    fixture.execute("run");
    fixture.module.update(0.0);
    CanvasSceneTestAccess::drainSimulation(fixture.module);
    testTrue(
      g,
      CanvasSceneTestAccess::getSimulationGeneration(fixture.module) == 1,
      "explicit async retry succeeds and counts once");
  }
}

static void
testFrameSimulationBudget()
{
  testSection("CanvasScene: asynchronous simulation budget");
  CellGameFixture fixture(5, 5);
  fixture.env.setVar("tps", 30);
  fixture.env.setVar("speedFactor", 1.0);

  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Press);
  fixture.module.update(0.0);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::None);

  fixture.module.update(0.25);
  testEqInt(g,
            CanvasSceneTestAccess::getLastSimulationSteps(fixture.module),
            0,
            "scheduled generation does not block its render frame");
  testTrue(g,
           CanvasSceneTestAccess::getSimulationDebtDropped(fixture.module),
           "in-flight scheduling drops excess catch-up debt");
  CanvasSceneTestAccess::drainSimulation(fixture.module);
  testEqInt(g,
            CanvasSceneTestAccess::getLastSimulationSteps(fixture.module),
            1,
            "drain publishes the single in-flight generation");
  testTrue(g,
           CanvasSceneTestAccess::getLastSimulationFrameMilliseconds(
             fixture.module) >= 0.0,
           "published generations expose measured worker time");
  fixture.executeThroughConsole("status");
  testTrue(g,
           historyContains(fixture.console, "achieved="),
           "status reports achieved simulation rate separately");
  testTrue(g,
           historyContains(fixture.console, "step p50/p95/max"),
           "status reports rolling worker-generation latency");
}

static void
testAsyncTransitionDraining()
{
  testSection("CanvasScene: async state transitions drain safely");
  CellGameFixture fixture(16, 12);
  const std::string savePath = "async-transition.csim";

  fixture.execute("run");
  fixture.module.update(0.25);
  testTrue(g,
           CanvasSceneTestAccess::isSimulationBusy(fixture.module),
           "running update leaves one generation in flight or completed");
  fixture.execute("pause");
  testTrue(g,
           !CanvasSceneTestAccess::isSimulationBusy(fixture.module) &&
             CanvasSceneTestAccess::getState(fixture.module) ==
               CellState::EDIT,
           "pause publishes and drains before entering edit mode");

  fixture.execute("run");
  fixture.module.update(0.25);
  fixture.execute("save", { savePath });
  testTrue(g,
           !CanvasSceneTestAccess::isSimulationBusy(fixture.module) &&
             std::filesystem::exists(savePath),
           "save drains before reading the published grid");

  fixture.module.update(0.25);
  fixture.execute("ruleset", { "SEEDS" });
  CellContext* context =
    CanvasSceneTestAccess::getCellContext(fixture.module);
  testTrue(g,
           !CanvasSceneTestAccess::isSimulationBusy(fixture.module) &&
             context->getModeString() == "SEEDS",
           "ruleset change drains before replacing the transition table");

  fixture.module.update(0.25);
  fixture.execute("step", { "2" });
  testTrue(g,
           !CanvasSceneTestAccess::isSimulationBusy(fixture.module) &&
             CanvasSceneTestAccess::getState(fixture.module) ==
               CellState::EDIT,
           "manual stepping drains and returns to edit mode");

  fixture.execute("run");
  fixture.module.update(0.25);
  fixture.execute("load", { savePath });
  testTrue(g,
           !CanvasSceneTestAccess::isSimulationBusy(fixture.module),
           "load drains before replacing published sparse state");
}

static int
runCanvasSceneCase(void (*testFunction)())
{
  g.failures = 0;
  gSaveDialogResult.clear();
  gLoadDialogResult.clear();
  testFunction();
  return g.failures;
}

static void
testInputRegistrationLifetime()
{
  CellGameFixture fixture;
  fixture.module.stop();
  fixture.started = false;
  for (int i = 0; i < NUM_INPUT_CONTEXTS * 2; ++i) {
    CanvasScene module;
    testTrue(
      g, module.start(fixture.context), "re-entry retains input capacity");
    testTrue(g,
             fixture.input.getActiveInputContext()->getActions().contains(
               "PaintCanvas"),
             "new module owns active bindings");
    module.stop();
    module.stop();
    testTrue(
      g, !fixture.input.isActionActive("PaintCanvas"), "exit retires bindings");
  }
  for (int i = 0; i < NUM_INPUT_CONTEXTS; ++i) {
    testTrue(g,
             fixture.input.registerInputContext(InputContext{}) >= 0,
             "all slots available after repeated exit");
  }
  InputContext* selected = fixture.input.getActiveInputContext();
  CanvasScene rejected;
  testTrue(
    g, !rejected.start(fixture.context), "full registry rejects startup");
  testTrue(g,
           CanvasSceneTestAccess::getCellContext(rejected) == nullptr,
           "failed startup allocates no domain state");
  testTrue(g,
           fixture.input.getActiveInputContext() == selected,
           "failed startup preserves selection");
}

static void
testRulesetWorkshopF2Draft()
{
  CellGameFixture fixture;
  RulesetWorkshopMenu* menu =
    CanvasSceneTestAccess::getRulesetWorkshopMenu(fixture.module);
  const RuleSetDefinition* original =
    RuleSetRegistry::instance().getRuleSetDefinition("GAME_OF_LIFE");
  testTrue(g,
           menu != nullptr && original != nullptr,
           "rule workshop and built-in definition are available");
  fixture.input.getKeyQueue().push({ KeyCode::F2, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g, menu->isOpen(), "F2 opens the separate rule workshop");
  bool hasWorkshopTitle = false;
  bool hasReadableRowLabel = false;
  bool hasReadableStepperValue = false;
  bool hasReadableControlHelp = false;
  for (std::size_t index = 0u; index < menu->getVisual().textCount(); ++index) {
    TextPrimitive* text = menu->getVisual().getText(index);
    hasWorkshopTitle = hasWorkshopTitle ||
                       (text != nullptr && text->content == "RULESET WORKSHOP");
    hasReadableRowLabel = hasReadableRowLabel ||
                          (text != nullptr && text->content == "Starter rule" &&
                           text->sizePt >= 15.0f);
    hasReadableStepperValue =
      hasReadableStepperValue ||
      (text != nullptr && text->content == "Conway's Game of Life" &&
       text->sizePt >= 12.0f);
    hasReadableControlHelp =
      hasReadableControlHelp ||
      (text != nullptr && text->content.find("ARROWS / W,S MOVE") == 0u &&
       text->sizePt >= 12.0f);
  }
  testTrue(g, hasWorkshopTitle, "the menu presents the Ruleset Workshop name");
  testTrue(g,
           hasReadableRowLabel && hasReadableStepperValue &&
             hasReadableControlHelp,
           "workshop labels, values, and controls use readable type sizes");
  testTrue(g,
           menu->getAnimationProgressForTesting() > 0.0f &&
             menu->getAnimationProgressForTesting() < 1.0f,
           "workshop panel reveal advances smoothly");
  testTrue(g,
           menu->getDraft().id.rfind("CUSTOM_GAME_OF_LIFE", 0u) == 0u,
           "editing a built-in stages a custom stable ID");
  testEqStr(g,
            menu->getPreviewText(),
            "Alive + 3 live neighbors -> Alive",
            "workshop opens with a readable transition preview");
  testTrue(g,
           menu->getSelectedControlForTesting() == "Starter rule",
           "workshop focuses the starter rule on open");

  *fixture.input.getMouseScrollOffset() = -1.0;
  fixture.module.update(0.016);
  testTrue(g,
           menu->getFirstVisibleRowForTesting() == 1 &&
             menu->getSelectedControlForTesting() == "Starter rule" &&
             *fixture.input.getMouseScrollOffset() == 0.0,
           "wheel scrolls the workshop view without moving selection");

  testTrue(g,
           focusWorkshopControl(*menu, fixture.input, "Ruleset name") &&
             hasWorkshopTextCaret(*menu),
           "focused workshop text field renders a visible caret");
  menu->tick(0.6f);
  menu->update(&fixture.input);
  testTrue(g,
           !hasWorkshopTextCaret(*menu),
           "workshop text caret alternates off during its blink cycle");

  const bool birthFocused =
    focusWorkshopControl(*menu, fixture.input, "Birth counts");
  testTrue(
    g, birthFocused, "keyboard focus reaches the family-specific birth chips");
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           menu->getValuePulseForTesting() > 0.0f,
           "editing a rule briefly highlights the changed value");
  testTrue(g,
           (menu->getDraft().birthMask & (1u << 3u)) == 0u,
           "enter toggles the selected birth count in the staged draft");
  const float selectionBefore = menu->getSelectionPositionForTesting();
  fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  fixture.module.update(0.016);
  fixture.module.update(0.07);
  testTrue(
    g,
    menu->getSelectionPositionForTesting() > selectionBefore &&
      menu->getSelectionPositionForTesting() <
        static_cast<float>(menu->getControlIndexForTesting("Survival counts")),
    "workshop selection highlight glides between rows");
  fixture.input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g, !menu->isOpen(), "Escape discards the staged draft");
  testTrue(g,
           original->birthMask == (1u << 3u),
           "discard leaves the active catalog definition unchanged");

  CellGameFixture reducedMotionFixture;
  reducedMotionFixture.env.setVar("reducedUiMotion", true);
  RulesetWorkshopMenu* reducedMenu =
    CanvasSceneTestAccess::getRulesetWorkshopMenu(
      reducedMotionFixture.module);
  reducedMotionFixture.input.getKeyQueue().push(
    { KeyCode::F2, InputAction::Press, 0 });
  reducedMotionFixture.module.update(0.016);
  testTrue(g,
           reducedMenu != nullptr &&
             reducedMenu->getAnimationProgressForTesting() == 1.0f &&
             reducedMenu->getValuePulseForTesting() == 0.0f,
           "reduced motion snaps the workshop reveal and disables pulses");
  testTrue(g,
           focusWorkshopControl(
             *reducedMenu, reducedMotionFixture.input, "Ruleset name"),
           "reduced-motion workshop can focus a text field");
  reducedMenu->tick(0.6f);
  reducedMenu->update(&reducedMotionFixture.input);
  testTrue(g,
           hasWorkshopTextCaret(*reducedMenu),
           "reduced motion keeps the focused text caret steadily visible");
}

static void
testRulesetWorkshopFamilyControls()
{
  CellGameFixture fixture;
  fixture.env.setVar("reducedUiMotion", true);
  RulesetWorkshopMenu* menu =
    CanvasSceneTestAccess::getRulesetWorkshopMenu(fixture.module);
  const RuleSetDefinition* life =
    RuleSetRegistry::instance().getRuleSetDefinition("GAME_OF_LIFE");
  const RuleSetDefinition* generations =
    RuleSetRegistry::instance().getRuleSetDefinition("BRIANS_BRAIN");
  const RuleSetDefinition* table =
    RuleSetRegistry::instance().getRuleSetDefinition("WIREWORLD");
  const RuleSetDefinition* elementary =
    RuleSetRegistry::instance().getRuleSetDefinition("RULE_90");
  const RuleSetDefinition* cyclic =
    RuleSetRegistry::instance().getRuleSetDefinition("PRISM_RUSH");
  testTrue(g,
           menu != nullptr && life != nullptr && generations != nullptr &&
             table != nullptr && elementary != nullptr && cyclic != nullptr,
           "built-in definitions cover every workshop rule family");
  if (menu == nullptr || life == nullptr || generations == nullptr ||
      table == nullptr || elementary == nullptr || cyclic == nullptr) {
    return;
  }

  testTrue(g,
           openWorkshop(*menu, *life, true) &&
             menu->hasControlForTesting("Starter rule") &&
             menu->hasControlForTesting("Cell family") &&
             menu->hasControlForTesting("Ruleset name") &&
             menu->getFamilyDraft().kind == RuleFamily::LifeLike,
           "ruleset identity and behavior family have separate controls");
  const std::string originalId = menu->getDraft().id;
  const std::string originalName = menu->getDraft().name;
  testTrue(g,
           focusWorkshopControl(*menu, fixture.input, "Cell family"),
           "family can be selected independently from the starter rule");
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0 });
  menu->update(&fixture.input);
  testTrue(
    g,
    menu->getFamilyDraft().kind == RuleFamily::Generations &&
      menu->getDraft().id == originalId &&
      menu->getDraft().name == originalName &&
      menu->hasControlForTesting("Number of states"),
    "family change preserves ruleset identity and loads family controls");

  testTrue(g,
           openWorkshop(*menu, *life, true) &&
             menu->hasControlForTesting("Birth counts") &&
             menu->hasControlForTesting("Survival counts") &&
             !menu->hasControlForTesting("Number of states") &&
             !menu->hasControlForTesting("Wolfram rule number") &&
             !menu->hasControlForTesting("Transition table"),
           "Life-like family shows only its B/S rule controls");
  testTrue(g,
           focusWorkshopControl(*menu, fixture.input, "Live neighbors (0-8)"),
           "preview inputs are independently keyboard navigable");
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0 });
  menu->update(&fixture.input);
  testTrue(g,
           menu->getPreviewText().find("4 live neighbors") != std::string::npos,
           "changing a preview input refreshes its example transition");
  testTrue(g,
           focusWorkshopControl(*menu, fixture.input, "Birth counts"),
           "birth chips are reachable after changing the preview");
  TextPrimitive* birthLabel = nullptr;
  for (std::size_t index = 0u; index < menu->getVisual().textCount(); ++index) {
    TextPrimitive* text = menu->getVisual().getText(index);
    if (text != nullptr && text->content == "Birth counts") {
      birthLabel = text;
      break;
    }
  }
  testTrue(g, birthLabel != nullptr, "focused birth chips are visible");
  if (birthLabel != nullptr) {
    fixture.window.mouseX = 440.0;
    fixture.window.mouseY = birthLabel->y + birthLabel->sizePt * 0.5f;
    menu->update(&fixture.input);
    InputManagerTestAccess::setAction(
      fixture.input, KeyCode::MouseLeft, InputAction::Press);
    menu->update(&fixture.input);
    InputManagerTestAccess::setAction(
      fixture.input, KeyCode::MouseLeft, InputAction::Release);
    menu->update(&fixture.input);
    testTrue(g,
             (menu->getDraft().birthMask & (1u << 3u)) == 0u,
             "clicking the visible birth count chip toggles that count");
  }
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  menu->update(&fixture.input);
  testTrue(g,
           (menu->getDraft().birthMask & (1u << 3u)) != 0u,
           "Enter toggles the selected B/S count chip");

  testTrue(g,
           openWorkshop(*menu, *generations, true) &&
             menu->hasControlForTesting("Birth counts") &&
             menu->hasControlForTesting("Survival counts") &&
             menu->hasControlForTesting("Number of states") &&
             !menu->hasControlForTesting("Wolfram rule number"),
           "Generations form adds its state-count control to B/S");
  testTrue(g,
           focusWorkshopControl(*menu, fixture.input, "Number of states"),
           "Generations state count is keyboard navigable");
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0 });
  menu->update(&fixture.input);
  testTrue(g,
           menu->getFamilyDraft().stateCount ==
             RuleSetRegistry::instance()
                 .getFamilyDefinition(generations->familyId)
                 ->stateCount +
               1u,
           "state-count stepper updates the staged definition");

  testTrue(
    g,
    openWorkshop(*menu, *cyclic, true) &&
      menu->hasControlForTesting("Successor threshold") &&
      menu->hasControlForTesting("Cycle step") &&
      !menu->hasControlForTesting("Birth counts") &&
      !menu->hasControlForTesting("Transition table"),
    "cyclic form exposes interaction controls without count-table fields");
  testTrue(g,
           focusWorkshopControl(*menu, fixture.input, "Successor threshold"),
           "cyclic successor threshold is keyboard navigable");
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0 });
  menu->update(&fixture.input);
  testTrue(g,
           menu->getDraft().cyclicThreshold == 2u,
           "threshold stepper changes the staged cyclic rule");
  testTrue(g,
           focusWorkshopControl(*menu, fixture.input, "Cycle step"),
           "cyclic cycle step is keyboard navigable");
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0 });
  menu->update(&fixture.input);
  testTrue(g,
           menu->getDraft().cyclicStep == 5u,
           "cycle step skips values that would exclude declared states");
  testTrue(g,
           focusWorkshopControl(*menu, fixture.input, "Live neighbors (0-8)"),
           "cyclic preview successor count is navigable");
  testTrue(
    g,
    menu->getPreviewText().find("successor neighbors") != std::string::npos,
    "cyclic preview explains that neighbors are matched by successor state");

  testTrue(g,
           openWorkshop(*menu, *table, true) &&
             menu->hasControlForTesting("Transition table") &&
             !menu->hasControlForTesting("Birth counts") &&
             !menu->hasControlForTesting("Survival counts") &&
             !menu->hasControlForTesting("Wolfram rule number"),
           "Moore-table form explains JSON editing without irrelevant fields");

  testTrue(g,
           openWorkshop(*menu, *elementary, true) &&
             menu->hasControlForTesting("Wolfram rule number") &&
             menu->hasControlForTesting("Example neighborhood") &&
             !menu->hasControlForTesting("Birth counts") &&
             !menu->hasControlForTesting("Survival counts") &&
             !menu->hasControlForTesting("Live neighbors (0-8)"),
           "elementary form shows its rule number and 1D preview pattern");
  testTrue(g,
           focusWorkshopControl(*menu, fixture.input, "Example neighborhood"),
           "elementary preview neighborhood is keyboard navigable");
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0 });
  menu->update(&fixture.input);
  testTrue(g,
           menu->getPreviewText().find("011 ->") == 0u,
           "elementary preview shows the selected left-center-right pattern");

  fixture.window.handleResize(320, 240);
  menu->update(&fixture.input);
  bool hasApply = false;
  bool hasDiscard = false;
  for (std::size_t index = 0u; index < menu->getVisual().textCount(); ++index) {
    TextPrimitive* text = menu->getVisual().getText(index);
    if (text != nullptr && text->content == "SAVE & APPLY") {
      hasApply = true;
    } else if (text != nullptr && text->content == "DISCARD") {
      hasDiscard = true;
    }
  }
  testTrue(
    g,
    hasApply && hasDiscard &&
      menu->getBodyBottomForTesting() <= menu->getFooterTopForTesting(),
    "compact layout keeps both actions visible below the scroll viewport");
  menu->close();
}

static void
testRulesetWorkshopPointerNavigation()
{
  CellGameFixture fixture;
  fixture.env.setVar("reducedUiMotion", true);
  RulesetWorkshopMenu* menu =
    CanvasSceneTestAccess::getRulesetWorkshopMenu(fixture.module);
  const RuleSetDefinition* gameOfLife =
    RuleSetRegistry::instance().getRuleSetDefinition("GAME_OF_LIFE");
  testTrue(g,
           menu != nullptr && gameOfLife != nullptr &&
             openWorkshop(*menu, *gameOfLife, true),
           "workshop opens for pointer navigation");
  if (menu == nullptr || gameOfLife == nullptr || !menu->isOpen()) {
    return;
  }

  menu->update(&fixture.input);
  const std::string startingId = menu->getDraft().id;
  bool foundStarterRule = false;
  float templateCenterY = 0.0f;
  GameVisual& visual = menu->getVisual();
  for (std::size_t index = 0; index < visual.textCount(); ++index) {
    TextPrimitive* text = visual.getText(index);
    if (text != nullptr && text->content == "Starter rule") {
      templateCenterY = text->y + text->sizePt * 0.5f;
      foundStarterRule = true;
      break;
    }
  }
  testTrue(
    g, foundStarterRule, "starter-rule row is visible for pointer focus");
  if (!foundStarterRule) {
    return;
  }

  fixture.window.mouseX = 350.0;
  fixture.window.mouseY = templateCenterY;
  menu->update(&fixture.input);
  testTrue(g,
           menu->getSelectedControlForTesting() == "Starter rule",
           "pointer motion focuses the named template control");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  menu->update(&fixture.input);
  testTrue(g,
           menu->getSelectedControlForTesting() == "Starter rule" &&
             menu->getDraft().id != startingId,
           "clicking the visible minus control changes the starting template");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  menu->update(&fixture.input);

  fixture.window.mouseX = 580.0;
  menu->update(&fixture.input);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  menu->update(&fixture.input);
  testTrue(g,
           menu->getDraft().id == startingId,
           "clicking the right value area cycles the template forward");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  menu->update(&fixture.input);

  fixture.input.getKeyQueue().push({ KeyCode::End, InputAction::Press, 0 });
  menu->update(&fixture.input);
  testTrue(g,
           menu->getSelectedControlForTesting() == "Discard",
           "End focuses the persistent discard action");
  fixture.window.mouseX = 500.0;
  fixture.window.mouseY = 443.0;
  menu->update(&fixture.input);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  testTrue(g,
           menu->update(&fixture.input) == RulesetWorkshopAction::Cancel,
           "clicking the pinned discard button cancels the draft");
}

static void
testRulesetWorkshopNavigation()
{
  CellGameFixture fixture;
  fixture.env.setVar("reducedUiMotion", true);
  RulesetWorkshopMenu* menu =
    CanvasSceneTestAccess::getRulesetWorkshopMenu(fixture.module);
  const RuleSetDefinition* gameOfLife =
    RuleSetRegistry::instance().getRuleSetDefinition("GAME_OF_LIFE");
  testTrue(g,
           menu != nullptr && gameOfLife != nullptr &&
             openWorkshop(*menu, *gameOfLife, true),
           "workshop opens for input navigation");
  if (menu == nullptr || gameOfLife == nullptr || !menu->isOpen()) {
    return;
  }

  GameVisual& visual = menu->getVisual();
  TextPrimitive* birthLabel = nullptr;
  for (std::size_t index = 0; index < visual.textCount(); ++index) {
    TextPrimitive* text = visual.getText(index);
    if (text != nullptr && text->content == "Birth counts") {
      birthLabel = text;
      break;
    }
  }
  testTrue(g, birthLabel != nullptr, "visible rules expose hoverable rows");
  if (birthLabel != nullptr) {
    fixture.window.mouseX = 320.0;
    fixture.window.mouseY = birthLabel->y + birthLabel->sizePt * 0.5f;
    menu->update(&fixture.input);
    testTrue(g,
             menu->getSelectedControlForTesting() == "Birth counts",
             "moving the pointer focuses the row under it");
  }

  *fixture.input.getMouseScrollOffset() = -1.0;
  menu->update(&fixture.input);
  testTrue(g,
           menu->getFirstVisibleRowForTesting() == 1 &&
             menu->getSelectedControlForTesting() == "Birth counts" &&
             *fixture.input.getMouseScrollOffset() == 0.0,
           "wheel scrolls the workshop viewport without stealing focus");

  fixture.input.getKeyQueue().push({ KeyCode::W, InputAction::Press, 0 });
  menu->update(&fixture.input);
  testTrue(g,
           menu->getSelectedControlForTesting() == "Ruleset name",
           "W moves focus upward to the rule name");
  fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  menu->update(&fixture.input);
  fixture.input.getKeyQueue().push({ KeyCode::S, InputAction::Press, 0 });
  menu->update(&fixture.input);
  testTrue(g,
           menu->getSelectedControlForTesting() == "Survival counts",
           "S moves focus down across actionable controls");
  fixture.input.getKeyQueue().push({ KeyCode::Tab, InputAction::Press, 0 });
  menu->update(&fixture.input);
  testTrue(g,
           menu->getSelectedControlForTesting() == "Example cell state",
           "Tab advances to the next actionable control");
  InputManagerTestAccess::setModifierFlags(fixture.input, 1);
  fixture.input.getKeyQueue().push({ KeyCode::Tab, InputAction::Press, 1 });
  menu->update(&fixture.input);
  InputManagerTestAccess::setModifierFlags(fixture.input, 0);
  testTrue(g,
           menu->getSelectedControlForTesting() == "Survival counts",
           "Shift+Tab moves focus backward");

  fixture.input.getKeyQueue().push({ KeyCode::Home, InputAction::Press, 0 });
  menu->update(&fixture.input);
  fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::W, InputAction::Press, 0 });
  fixture.input.getCharQueue().push('W');
  fixture.input.getKeyQueue().push({ KeyCode::Space, InputAction::Press, 0 });
  fixture.input.getCharQueue().push(' ');
  menu->update(&fixture.input);
  testTrue(g,
           menu->getSelectedControlForTesting() == "Ruleset name" &&
             menu->getDraft().name.size() >= 2u &&
             menu->getDraft().name.substr(menu->getDraft().name.size() - 2u) ==
               "W ",
           "text fields keep W, S, and Space available for typing");

  fixture.input.getKeyQueue().push({ KeyCode::End, InputAction::Press, 0 });
  menu->update(&fixture.input);
  testTrue(g,
           menu->getSelectedControlForTesting() == "Discard",
           "End reaches the final pinned action");
  fixture.input.getKeyQueue().push({ KeyCode::PageUp, InputAction::Press, 0 });
  menu->update(&fixture.input);
  testTrue(g,
           menu->getSelectedControlForTesting() != "Discard" &&
             menu->getFirstVisibleRowForTesting() <
               menu->getControlIndexForTesting("Import rules from JSON"),
           "PageUp navigates a page and keeps focus visible");
  fixture.input.getKeyQueue().push(
    { KeyCode::PageDown, InputAction::Press, 0 });
  menu->update(&fixture.input);
  testTrue(g,
           menu->getSelectedControlForTesting() == "Discard",
           "PageDown returns to the final workshop action");
  fixture.input.getKeyQueue().push({ KeyCode::Space, InputAction::Press, 0 });
  testTrue(g,
           menu->update(&fixture.input) == RulesetWorkshopAction::Cancel,
           "Space activates the focused discard action");
}

static void
testRulesetWorkshopRejectsRemovingLiveStates()
{
  CellGameFixture fixture;
  fixture.execute("ruleset", { "WIREWORLD" });
  fixture.module.update(0.016);
  CellContext* context =
    CanvasSceneTestAccess::getCellContext(fixture.module);
  testTrue(g,
           context != nullptr && context->getRuleSet()->getStateCount() == 4u,
           "Wireworld is active before testing a smaller custom rule");
  context->getGrid()->setCell(CellAddress{ 5, 5 }, 3u);

  RulesetWorkshopMenu* menu =
    CanvasSceneTestAccess::getRulesetWorkshopMenu(fixture.module);
  const RuleSetDefinition* wireworld =
    RuleSetRegistry::instance().getRuleSetDefinition("WIREWORLD");
  testTrue(g,
           menu != nullptr && wireworld != nullptr,
           "workshop and Wireworld definition are available");
  fixture.input.getKeyQueue().push({ KeyCode::F2, InputAction::Press, 0 });
  fixture.module.update(0.016);
  RuleSetDefinition reduced = *wireworld;
  RuleFamilyDefinition reducedFamily =
    *RuleSetRegistry::instance().getFamilyDefinition(wireworld->familyId);
  reducedFamily.id = "CUSTOM_WIREWORLD_TWO_STATE";
  reducedFamily.name = "Two-state Wireworld test";
  reducedFamily.builtIn = false;
  reducedFamily.stateCount = 2u;
  reducedFamily.stateNames.resize(2u);
  reducedFamily.stateColors.resize(2u);
  reduced.transitionTable.fill(1u);
  reduced.transitionTableStateCount = 2u;
  reduced.familyId = reducedFamily.id;
  reduced.builtIn = false;
  menu->setDraft(reducedFamily, reduced);
  fixture.input.getKeyQueue().push({ KeyCode::End, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Up, InputAction::Press, 0 });
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.016);

  testTrue(g,
           menu->isOpen() && !menu->getError().empty(),
           "Apply rejects a state-count reduction that invalidates the world");
  testTrue(g,
           context->getModeString() == "WIREWORLD" &&
             context->getGrid()->getCell(CellAddress{ 5, 5 }) == 3u,
           "rejected Apply preserves the active rule and live state");
}

static void
testRulesetWorkshopApplyPersistsAndActivates()
{
  RuleSetRegistry baseCatalog;
  {
    CellGameFixture fixture;
    baseCatalog = RuleSetRegistry::instance();
    WorkingDirectoryFixture workingDirectory;
    testTrue(g,
             workingDirectory.isReady(),
             "isolated user catalog directory is available");
    if (workingDirectory.isReady()) {
      RulesetWorkshopMenu* menu =
        CanvasSceneTestAccess::getRulesetWorkshopMenu(fixture.module);
      const RuleSetDefinition* gameOfLife =
        RuleSetRegistry::instance().getRuleSetDefinition("GAME_OF_LIFE");
      testTrue(g,
               menu != nullptr && gameOfLife != nullptr,
               "workshop and source definition are available");
      fixture.input.getKeyQueue().push({ KeyCode::F2, InputAction::Press, 0 });
      fixture.module.update(0.016);

      RuleSetDefinition custom = *gameOfLife;
      custom.id = "CUSTOM_F2_APPLY";
      custom.name = "Workshop Apply Test";
      custom.builtIn = false;
      custom.rule = "B2/S";
      custom.birthMask = 1u << 2u;
      custom.surviveMask = 0u;
      setWorkshopDraft(*menu, custom);
      testTrue(g,
               focusWorkshopControl(*menu, fixture.input, "Family name"),
               "family appearance can be edited in the staged draft");
      fixture.input.getCharQueue().push('X');
      menu->update(&fixture.input);
      fixture.input.getKeyQueue().push({ KeyCode::End, InputAction::Press, 0 });
      fixture.input.getKeyQueue().push({ KeyCode::Up, InputAction::Press, 0 });
      fixture.input.getKeyQueue().push(
        { KeyCode::Enter, InputAction::Press, 0 });
      fixture.module.update(0.016);

      CellContext* activeContext =
        CanvasSceneTestAccess::getCellContext(fixture.module);
      const bool firstApplySucceeded =
        activeContext != nullptr && !menu->isOpen() &&
        activeContext->getModeString() == "CUSTOM_F2_APPLY";
      testTrue(g,
               firstApplySucceeded,
               (menu->getError().empty()
                  ? "Save & Apply activates the validated custom ID"
                  : ("Save & Apply failed: " + menu->getError()).c_str()));
      if (firstApplySucceeded) {
        testTrue(g,
                 activeContext->getRuleSet()->nextState(1u, 2u) == 0u,
                 "first custom definition controls the active transition");
        fixture.input.getKeyQueue().push(
          { KeyCode::F2, InputAction::Press, 0 });
        fixture.module.update(0.016);
        const RuleSetDefinition* activeDefinition =
          RuleSetRegistry::instance().getRuleSetDefinition("CUSTOM_F2_APPLY");
        if (menu->isOpen() && activeDefinition != nullptr) {
          RuleSetDefinition updated = *activeDefinition;
          updated.rule = "B3/S23";
          updated.birthMask = 1u << 3u;
          updated.surviveMask = (1u << 2u) | (1u << 3u);
          setWorkshopDraft(*menu, updated);
          testTrue(g,
                   menu->getDraft().id == "CUSTOM_F2_APPLY",
                   "re-editing retains the active custom ID");
          fixture.input.getKeyQueue().push(
            { KeyCode::End, InputAction::Press, 0 });
          fixture.input.getKeyQueue().push(
            { KeyCode::Up, InputAction::Press, 0 });
          fixture.input.getKeyQueue().push(
            { KeyCode::Enter, InputAction::Press, 0 });
          fixture.module.update(0.016);
        } else {
          testTrue(g, false, "F2 reopens the active custom rule");
        }
        testTrue(g,
                 !menu->isOpen() &&
                   activeContext->getRuleSet()->nextState(1u, 2u) == 1u,
                 "Save & Apply recompiles edits to the active custom ID");
      }
      RuleSetRegistry imported;
      testTrue(
        g,
        RuleCatalogLoader::loadFromDefaultLocations(imported) &&
          imported.isKnownRule("CUSTOM_F2_APPLY") &&
          imported.getRuleSetDefinition("CUSTOM_F2_APPLY")
              ->familyId.rfind("CUSTOM_FAMILY_", 0u) == 0u &&
          imported
              .getFamilyDefinition(
                imported.getRuleSetDefinition("CUSTOM_F2_APPLY")->familyId)
              ->name.find("Custom ") == 0u,
        "Save & Apply persists the family and rule overlays");
      testTrue(g,
               imported.getRuleSetDefinition("CUSTOM_F2_APPLY") != nullptr &&
                 imported.getRuleSetDefinition("CUSTOM_F2_APPLY")->rule ==
                   "B3/S23",
               "re-edit persists the replacement rule definition");
    }
  }
  RuleSetRegistry::instance() = baseCatalog;
}

static void
testRulesetWorkshopImportRejectsReducingActiveStateRange()
{
  RuleSetRegistry baseCatalog;
  gLoadDialogResult.clear();
  {
    CellGameFixture fixture;
    baseCatalog = RuleSetRegistry::instance();
    WorkingDirectoryFixture workingDirectory;
    testTrue(
      g, workingDirectory.isReady(), "isolated import directory is available");
    if (workingDirectory.isReady()) {
      RuleSetRegistry& registry = RuleSetRegistry::instance();
      const RuleFamilyDefinition* generations =
        registry.getFamilyDefinition("GENERATIONS_3_STATE");
      const RuleSetDefinition* briansBrain =
        registry.getRuleSetDefinition("BRIANS_BRAIN");
      RulesetWorkshopMenu* menu =
        CanvasSceneTestAccess::getRulesetWorkshopMenu(fixture.module);
      CellContext* context =
        CanvasSceneTestAccess::getCellContext(fixture.module);
      testTrue(g,
               generations != nullptr && briansBrain != nullptr &&
                 menu != nullptr && context != nullptr,
               "catalog, workshop, and live context are available");
      if (generations != nullptr && briansBrain != nullptr && menu != nullptr &&
          context != nullptr) {
        RuleFamilyDefinition expandedFamily = *generations;
        expandedFamily.id = "CUSTOM_IMPORT_GENERATIONS";
        expandedFamily.name = "Six-state import test";
        expandedFamily.stateCount = 6u;
        expandedFamily.builtIn = false;
        for (unsigned int state = 3u; state < expandedFamily.stateCount;
             ++state) {
          expandedFamily.stateNames.push_back("State " + std::to_string(state));
          expandedFamily.stateColors.push_back({ 160u, 160u, 160u });
        }
        RuleSetDefinition expandedRule = *briansBrain;
        expandedRule.id = "CUSTOM_IMPORT_GENERATIONS_RULE";
        expandedRule.name = "Six-state import rule";
        expandedRule.familyId = expandedFamily.id;
        expandedRule.builtIn = false;
        const bool registered = registry.registerFamily(expandedFamily) &&
                                registry.registerRule(expandedRule);
        testTrue(g, registered, "six-state custom family and rule register");
        if (registered &&
            context->setRuleSet(expandedFamily.id, expandedRule.id)) {
          context->getGrid()->setCell(CellAddress{ 5, 5 }, 5u);
          RuleFamilyDefinition reducedFamily = expandedFamily;
          reducedFamily.stateCount = 3u;
          reducedFamily.stateNames.resize(3u);
          reducedFamily.stateColors.resize(3u);
          const RuleSetDefinition* registeredRule =
            registry.getRuleSetDefinition(expandedRule.id);
          const std::filesystem::path catalogPath =
            workingDirectory.getDirectory() / "reduced-family.json";
          std::ofstream catalogFile(catalogPath, std::ios::binary);
          if (registeredRule != nullptr) {
            catalogFile << RuleSetRegistry::serializeRulePackage(
              reducedFamily, *registeredRule);
          }
          catalogFile.close();
          const bool catalogWritten =
            registeredRule != nullptr && static_cast<bool>(catalogFile);
          testTrue(
            g, catalogWritten, "reduced family package is written for import");
          if (catalogWritten) {
            gLoadDialogResult = catalogPath.string();
            fixture.input.getKeyQueue().push(
              { KeyCode::F2, InputAction::Press, 0 });
            fixture.module.update(0.016);
            testTrue(g,
                     focusWorkshopControl(
                       *menu, fixture.input, "Import rules from JSON"),
                     "workshop import control can be selected");
            fixture.input.getKeyQueue().push(
              { KeyCode::Enter, InputAction::Press, 0 });
            fixture.module.update(0.016);
            const RuleFamilyDefinition* unchangedFamily =
              registry.getFamilyDefinition(expandedFamily.id);
            testTrue(g,
                     menu->isOpen() && menu->getError().find(
                                         "active ruleset") != std::string::npos,
                     "import reports the active ruleset state-range conflict");
            testTrue(g,
                     unchangedFamily != nullptr &&
                       unchangedFamily->stateCount == 6u &&
                       context->getRuleSet()->getStateCount() == 6u &&
                       context->getGrid()->getCell(CellAddress{ 5, 5 }) == 5u,
                     "rejected import preserves registry and live world");
            testTrue(
              g,
              !std::filesystem::exists(workingDirectory.getDirectory() /
                                       "families.user.json") &&
                !std::filesystem::exists(workingDirectory.getDirectory() /
                                         "rulesets.user.json"),
              "rejected import does not persist partial overlays");

            context->getGrid()->setCell(CellAddress{ 5, 5 }, 2u);
            testTrue(g,
                     focusWorkshopControl(
                       *menu, fixture.input, "Import rules from JSON"),
                     "workshop import can be selected again after rejection");
            fixture.input.getKeyQueue().push(
              { KeyCode::Enter, InputAction::Press, 0 });
            fixture.module.update(0.016);
            unchangedFamily = registry.getFamilyDefinition(expandedFamily.id);
            testTrue(
              g,
              menu->isOpen() &&
                menu->getError().find("active ruleset") != std::string::npos,
              "import rejects shrinking states the active rule can produce");
            testTrue(
              g,
              unchangedFamily != nullptr && unchangedFamily->stateCount == 6u &&
                context->getRuleSet()->getStateCount() == 6u &&
                context->getGrid()->getCell(CellAddress{ 5, 5 }) == 2u,
              "state-range rejection preserves valid live cells and registry");
            testTrue(
              g,
              !std::filesystem::exists(workingDirectory.getDirectory() /
                                       "families.user.json") &&
                !std::filesystem::exists(workingDirectory.getDirectory() /
                                         "rulesets.user.json"),
              "state-range rejection does not persist partial overlays");
          }
        } else {
          testTrue(
            g, false, "custom family and rule activate before import test");
        }
      }
    }
  }
  gLoadDialogResult.clear();
  RuleSetRegistry::instance() = baseCatalog;
}

// The program's director for a fixture: returning to the title adds the
// title scene through CSimScenes.
struct CanvasReturnScenes
{
  explicit CanvasReturnScenes(IllumoContext& context)
    : director(context)
  {
    context.scenes = &director;
  }
  // Title switches requested: CSimScenes never queues a second.
  int requests() const
  {
    return director.has(CSimScenes::kTitle) && director.hasPendingSwitch() ? 1
                                                                           : 0;
  }
  SceneDirector director;
};

// Summary of the canvas veil: how many shapes are fully opaque, full-size
// cells (the covered part of the screen), and the veil's overall extent.
struct VeilSummary
{
  std::size_t shapes = 0u;
  std::size_t coveredCells = 0u;
  bool allOpaque = true;
  float right = 0.0f;
  float bottom = 0.0f;
  float cellWidth = 0.0f;
  float cellHeight = 0.0f;
};

static VeilSummary
summarizeVeil(GameVisual& veil, float cellWidth, float cellHeight)
{
  VeilSummary summary;
  summary.shapes = veil.shapeCount();
  summary.cellWidth = cellWidth;
  summary.cellHeight = cellHeight;
  for (std::size_t index = 0u; index < veil.shapeCount(); ++index) {
    const ShapePrimitive* shape = veil.getShape(index);
    summary.allOpaque = summary.allOpaque && shape->color.a == 255;
    summary.right = std::max(summary.right, shape->rect.x + shape->rect.w);
    summary.bottom = std::max(summary.bottom, shape->rect.y + shape->rect.h);
    if (shape->color.a == 255 && std::abs(shape->rect.w - cellWidth) < 0.01f &&
        std::abs(shape->rect.h - cellHeight) < 0.01f) {
      ++summary.coveredCells;
    }
  }
  return summary;
}

// Firing halos are drawn first, so find the top-left cell by position.
static bool
hasCoveredCornerCell(GameVisual& veil, float cellWidth, float cellHeight)
{
  for (std::size_t index = 0u; index < veil.shapeCount(); ++index) {
    const ShapePrimitive* shape = veil.getShape(index);
    if (shape->color.a == 255 && shape->rect.x == 0.0f &&
        shape->rect.y == 0.0f && std::abs(shape->rect.w - cellWidth) < 0.01f &&
        std::abs(shape->rect.h - cellHeight) < 0.01f) {
      return true;
    }
  }
  return false;
}

static void
testCanvasReturn()
{
  CSimSounds::resetCounts();
  CellGameFixture fixture;
  testTrue(g,
           CSimSounds::playCount(CSimSound::CanvasEnter) == 1,
           "starting the canvas plays the enter cue");
  CanvasReturnScenes host(fixture.context);
  GameVisual& veil =
    CanvasSceneTestAccess::getCanvasEntranceVisual(fixture.module);
  // The first frame is fully covered: every shape is one full, opaque cell.
  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  const float cellWidth = veil.getShape(0)->rect.w;
  const float cellHeight = veil.getShape(0)->rect.h;
  const std::size_t cellCount = veil.shapeCount();
  CanvasSceneTestAccess::advanceCanvasEntrance(fixture.module, 1.0);
  fixture.input.getKeyQueue().push({ KeyCode::Q, InputAction::Press, 0 });
  fixture.module.update(0.0);
  fixture.input.getKeyQueue().push({ KeyCode::M, InputAction::Press, 0 });
  fixture.module.update(0.0);
  testEqInt(
    g, host.requests(), 0, "Main Menu waits for the canvas exit animation");
  testTrue(g,
           CSimSounds::playCount(CSimSound::CanvasExit) == 1,
           "leaving for the main menu plays the exit cue at once");
  fixture.module.update(0.24);
  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  const VeilSummary closing = summarizeVeil(veil, cellWidth, cellHeight);
  testTrue(g,
           veil.isVisible() && closing.shapes > 0u &&
             closing.coveredCells > 0u && closing.coveredCells < cellCount,
           "canvas veil closes gradually before returning");
  fixture.execute("menu");
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.25);
  testEqInt(g,
            host.requests(),
            1,
            "repeated returns do not restart or duplicate the transition");
  testTrue(g,
           fixture.input.getKeyQueue().empty(),
           "exit input does not leak into the main menu");
  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  const VeilSummary covered = summarizeVeil(veil, cellWidth, cellHeight);
  testTrue(g,
           covered.allOpaque && covered.coveredCells == cellCount &&
             covered.shapes == cellCount,
           "canvas is covered when the return to the title is requested");
  fixture.module.update(1.0);
  testEqInt(g, host.requests(), 1, "completed exit submits only once");
  testTrue(g,
           CSimSounds::playCount(CSimSound::CanvasExit) == 1,
           "repeated return requests voice the exit once");
  CSimSounds::resetCounts();
}

static void
pressModeToggle(CellGameFixture& fixture)
{
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Press);
  fixture.module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::None);
  fixture.module.update(0.016);
}

static void
testCanvasModeSwitchSound()
{
  CellGameFixture fixture;
  CSimSounds::resetCounts();
  fixture.module.update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::CanvasModeSwitch) == 0,
           "entering the canvas in EDIT plays no mode cue");
  pressModeToggle(fixture);
  testTrue(g,
           CSimSounds::playCount(CSimSound::CanvasModeSwitch) == 1,
           "E from EDIT to NORMAL plays the mode cue");
  pressModeToggle(fixture);
  testTrue(g,
           CSimSounds::playCount(CSimSound::CanvasModeSwitch) == 2,
           "E back to EDIT plays it again");
  fixture.execute("pause");
  fixture.execute("step", { "1" });
  testTrue(g,
           CSimSounds::playCount(CSimSound::CanvasModeSwitch) == 2,
           "pause or step while already editing stays quiet");
  fixture.execute("run");
  testTrue(g,
           CSimSounds::playCount(CSimSound::CanvasModeSwitch) == 3,
           "the run command voices the switch to NORMAL");
  fixture.execute("step", { "1" });
  testTrue(g,
           CSimSounds::playCount(CSimSound::CanvasModeSwitch) == 4,
           "stepping from a running canvas voices the return to EDIT");
  CSimSounds::resetCounts();
}

static void
testCanvasEditMusic()
{
  {
    CellGameFixture fixture;
    testTrue(g,
             CSimSounds::musicPlaying(CSimMusic::CanvasEdit),
             "a canvas opening in EDIT loops the edit music");
    pressModeToggle(fixture);
    testTrue(g,
             !CSimSounds::musicPlaying(CSimMusic::CanvasEdit),
             "E to NORMAL fades the edit music out");
    pressModeToggle(fixture);
    testTrue(g,
             CSimSounds::musicPlaying(CSimMusic::CanvasEdit),
             "E back to EDIT starts it again");
    fixture.execute("run");
    testTrue(g,
             !CSimSounds::musicPlaying(CSimMusic::CanvasEdit),
             "the run command stops it");
    fixture.execute("step", { "1" });
    testTrue(g,
             CSimSounds::musicPlaying(CSimMusic::CanvasEdit),
             "stepping back into EDIT starts it");
    fixture.module.leave();
    testTrue(g,
             !CSimSounds::musicPlaying(CSimMusic::CanvasEdit),
             "leaving the canvas for the title stops it");
    fixture.module.enter();
    testTrue(g,
             CSimSounds::musicPlaying(CSimMusic::CanvasEdit),
             "resuming the canvas in EDIT starts it again");
    CanvasReturnScenes host(fixture.context);
    fixture.execute("menu");
    testTrue(g,
             !CSimSounds::musicPlaying(CSimMusic::CanvasEdit),
             "returning to the main menu fades it out at once");
  }
  {
    CellGameFixture fixture;
    testTrue(g,
             CSimSounds::musicPlaying(CSimMusic::CanvasEdit),
             "a second canvas plays it");
  }
  testTrue(g,
           !CSimSounds::musicPlaying(CSimMusic::CanvasEdit),
           "closing the canvas stops it");
  CSimSounds::resetCounts();
}

static void
testReducedCanvasReturn()
{
  CellGameFixture fixture;
  CanvasReturnScenes host(fixture.context);
  fixture.env.setVar("reducedUiMotion", true);
  fixture.execute("menu");
  testEqInt(g,
            host.requests(),
            1,
            "reduced motion returns immediately through the console path");
  fixture.execute("menu");
  testEqInt(g, host.requests(), 1, "immediate return remains idempotent");
}

static void
testCanvasEntrance()
{
  CellGameFixture fixture;
  GameVisual& veil =
    CanvasSceneTestAccess::getCanvasEntranceVisual(fixture.module);
  fixture.module.dispatch(fixture.scene);
  testTrue(g,
           veil.isVisible() && veil.shapeCount() > 0,
           "entering a canvas starts a visible reveal");
  const float cellWidth = veil.getShape(0)->rect.w;
  const float cellHeight = veil.getShape(0)->rect.h;
  const std::size_t cellCount = veil.shapeCount();
  const VeilSummary start = summarizeVeil(veil, cellWidth, cellHeight);
  const std::array<int, 2> dimensions = fixture.window.getWindowDimensions();
  testTrue(g,
           start.allOpaque && start.coveredCells == cellCount &&
             std::abs(start.right - static_cast<float>(dimensions[0])) <
               0.01f &&
             std::abs(start.bottom - static_cast<float>(dimensions[1])) < 0.01f,
           "entrance initially covers the canvas with opaque cells");
  CanvasSceneTestAccess::advanceCanvasEntrance(fixture.module, 0.24);
  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  const VeilSummary partial = summarizeVeil(veil, cellWidth, cellHeight);
  testTrue(g,
           partial.coveredCells > 0u && partial.coveredCells < cellCount,
           "entrance veil dissolves over time");
  testTrue(g,
           hasCoveredCornerCell(veil, cellWidth, cellHeight),
           "the center reveals before the outer edge");
  bool centerChanged = false;
  const float centerX = static_cast<float>(dimensions[0]) * 0.5f;
  const float centerY = static_cast<float>(dimensions[1]) * 0.5f;
  for (std::size_t index = 0u; index < veil.shapeCount(); ++index) {
    const ShapePrimitive* shape = veil.getShape(index);
    const float shapeX = shape->rect.x + shape->rect.w * 0.5f;
    const float shapeY = shape->rect.y + shape->rect.h * 0.5f;
    if (std::abs(shapeX - centerX) < cellWidth &&
        std::abs(shapeY - centerY) < cellHeight &&
        (shape->color.a < 255 || shape->rect.w < cellWidth - 0.01f)) {
      centerChanged = true;
    }
  }
  testTrue(g, centerChanged, "center cells fire and shrink first");
  const std::size_t shapesBefore = veil.shapeCount();
  const std::size_t coveredBefore = partial.coveredCells;
  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  testTrue(g,
           veil.shapeCount() == shapesBefore &&
             summarizeVeil(veil, cellWidth, cellHeight).coveredCells ==
               coveredBefore,
           "drawing does not advance entrance time");
  fixture.env.setVar("uiScale", 4);
  fixture.window.handleResize(800, 600);
  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  // At 800x600 and 4x UI scale the veil spans a 200x150 virtual viewport.
  const VeilSummary scaled = summarizeVeil(veil, 0.0f, 0.0f);
  bool cornerAtOrigin = false;
  for (std::size_t index = 0u; index < veil.shapeCount(); ++index) {
    const ShapePrimitive* shape = veil.getShape(index);
    cornerAtOrigin =
      cornerAtOrigin ||
      (shape->color.a == 255 && shape->rect.x == 0.0f && shape->rect.y == 0.0f);
  }
  testTrue(g,
           cornerAtOrigin && std::abs(scaled.right * 4.0f - 800.0f) < 0.05f &&
             std::abs(scaled.bottom * 4.0f - 600.0f) < 0.05f,
           "entrance coverage follows viewport size and UI scale");
  CanvasSceneTestAccess::advanceCanvasEntrance(fixture.module, 1.0);
  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);
  const std::vector<DrawableBase*>& ui =
    fixture.scene.drawablesIn(RenderLayerId::UI);
  testTrue(g,
           !veil.isVisible() &&
             std::find(ui.begin(), ui.end(), &veil) == ui.end(),
           "completed entrance is no longer submitted");
  fixture.module.stop();
  fixture.env.setVar("reducedUiMotion", true);
  fixture.started = fixture.module.start(fixture.context);
  testTrue(g,
           fixture.started && !veil.isVisible(),
           "reduced motion skips entrance from the first frame");
}

void
registerCanvasSceneTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.CanvasScene.CanvasReturn",
               []() { return runCanvasSceneCase(testCanvasReturn); });
  registry.add("IllumoGame.CellGame.PaintPaletteSounds",
               []() { return runCanvasSceneCase(testPaintPaletteSounds); });
  registry.add("IllumoGame.CellGame.PaintPaletteCardSounds", []() {
    return runCanvasSceneCase(testPaintPaletteCardSounds);
  });
  registry.add("IllumoGame.CanvasScene.ModeSwitchSound", []() {
    return runCanvasSceneCase(testCanvasModeSwitchSound);
  });
  registry.add("IllumoGame.CanvasScene.ResetCanvas",
               []() { return runCanvasSceneCase(testResetCanvas); });
  registry.add("IllumoGame.CanvasScene.EditMusic",
               []() { return runCanvasSceneCase(testCanvasEditMusic); });
  registry.add("IllumoGame.CanvasScene.ReducedCanvasReturn",
               []() { return runCanvasSceneCase(testReducedCanvasReturn); });

  registry.add("IllumoGame.CanvasScene.CanvasEntrance",
               []() { return runCanvasSceneCase(testCanvasEntrance); });
  registry.add("IllumoGame.CanvasScene.ToolbarSaveLoad",
               []() { return runCanvasSceneCase(testToolbarSaveLoad); });

  registry.add("IllumoGame.CellGame.InputRegistrationLifetime", []() {
    return runCanvasSceneCase(testInputRegistrationLifetime);
  });
  registry.add("IllumoGame.CellGame.RulesetWorkshopF2", []() {
    return runCanvasSceneCase(testRulesetWorkshopF2Draft);
  });
  registry.add("IllumoGame.CellGame.RulesetWorkshopFamilyControls", []() {
    return runCanvasSceneCase(testRulesetWorkshopFamilyControls);
  });
  registry.add("IllumoGame.CellGame.RulesetWorkshopPointerNavigation", []() {
    return runCanvasSceneCase(testRulesetWorkshopPointerNavigation);
  });
  registry.add("IllumoGame.CellGame.RulesetWorkshopNavigation", []() {
    return runCanvasSceneCase(testRulesetWorkshopNavigation);
  });
  registry.add("IllumoGame.CellGame.RulesetWorkshopStateGuard", []() {
    return runCanvasSceneCase(testRulesetWorkshopRejectsRemovingLiveStates);
  });
  registry.add("IllumoGame.CellGame.RulesetWorkshopApply", []() {
    return runCanvasSceneCase(testRulesetWorkshopApplyPersistsAndActivates);
  });
  registry.add("IllumoGame.CellGame.RulesetWorkshopImportStateGuard", []() {
    return runCanvasSceneCase(
      testRulesetWorkshopImportRejectsReducingActiveStateRange);
  });
  registry.add("IllumoGame.CellGame.StartAndRegistration", []() {
    return runCanvasSceneCase(testStartRegistersGameFeatures);
  });
  registry.add("IllumoGame.CellGame.Preferences",
               []() { return runCanvasSceneCase(testCanvasPreferences); });
  registry.add("IllumoGame.CellGame.RestartPrompt",
               []() { return runCanvasSceneCase(testRestartPrompt); });
  registry.add("IllumoGame.CellGame.InvalidContext", []() {
    return runCanvasSceneCase(testInvalidContextStartIsContained);
  });
  registry.add("IllumoGame.CellGame.Render3dTestFlag",
               []() { return runCanvasSceneCase(testRender3dTestFlag); });
  registry.add("IllumoGame.CellGame.WireworldSeedAndBrush", []() {
    return runCanvasSceneCase(testWireworldSeedAndBrush);
  });
  registry.add("IllumoGame.CellGame.CyclicMultistateSeed", []() {
    return runCanvasSceneCase(testCyclicMultistateSeed);
  });
  registry.add("IllumoGame.CellGame.EveryShippedRuleStarts", []() {
    return runCanvasSceneCase(testEveryShippedRuleStarts);
  });
  registry.add("IllumoGame.CellGame.ResearchedStarterSeeds", []() {
    return runCanvasSceneCase(testResearchedStarterSeeds);
  });
  registry.add("IllumoGame.CellGame.SaveLoadRoundTrip",
               []() { return runCanvasSceneCase(testSaveLoadRoundTrip); });
  registry.add("IllumoGame.CellGame.SparseV2Compatibility", []() {
    return runCanvasSceneCase(testSparseV2Compatibility);
  });
  registry.add("IllumoGame.CellGame.ReleaseConfiguration", []() {
    return runCanvasSceneCase(testReleaseConfigurationWorkflow);
  });
  registry.add("IllumoGame.CellGame.PaintPalette",
               []() { return runCanvasSceneCase(testPaintPalette); });
  registry.add("IllumoGame.CellGame.SoftwareCursor",
               []() { return runCanvasSceneCase(testSoftwareCursor); });
  registry.add("IllumoGame.CellGame.ModeBadge",
               []() { return runCanvasSceneCase(testModeBadge); });
  registry.add("IllumoGame.CellGame.PaintPaletteBubbleMorph", []() {
    return runCanvasSceneCase(testPaintPaletteBubbleMorph);
  });
  registry.add("IllumoGame.CellGame.PaintPaletteFittedInput", []() {
    return runCanvasSceneCase(testPaintPaletteFittedInput);
  });
  registry.add("IllumoGame.CellGame.HamburgerMenu",
               []() { return runCanvasSceneCase(testHamburgerMenuButton); });
  registry.add("IllumoGame.CellGame.SettingsYieldToConsole", []() {
    return runCanvasSceneCase(testSettingsYieldToConsole);
  });
  registry.add("IllumoGame.CellGame.ExitConfirmation", []() {
    return runCanvasSceneCase(testExitConfirmationFromQ);
  });
  registry.add("IllumoGame.CellGame.InvalidSaveFiles", []() {
    return runCanvasSceneCase(testLoadRejectsInvalidFiles);
  });
  registry.add("IllumoGame.CellGame.LoadOverlap",
               []() { return runCanvasSceneCase(testLoadCopiesOverlap); });
  registry.add("IllumoGame.CellGame.SimulationCommands", []() {
    return runCanvasSceneCase(testConsoleSimulationCommands);
  });
  registry.add("IllumoGame.CellGame.CameraAndFileCommands", []() {
    return runCanvasSceneCase(testConsoleCameraAndFiles);
  });
  registry.add("IllumoGame.CellGame.UpdateStateAndTiming", []() {
    return runCanvasSceneCase(testUpdateStateAndTiming);
  });
  registry.add("IllumoGame.CellGame.FrameSimulationBudget", []() {
    return runCanvasSceneCase(testFrameSimulationBudget);
  });
  registry.add("IllumoGame.CellGame.SimulationFailureReporting", []() {
    return runCanvasSceneCase(testSimulationFailureReporting);
  });
  registry.add("IllumoGame.CellGame.CameraInputBounds",
               []() { return runCanvasSceneCase(testCameraInputBounds); });
  registry.add("IllumoGame.CellGame.AsyncTransitionDraining", []() {
    return runCanvasSceneCase(testAsyncTransitionDraining);
  });
}
