#include "Game/CellGameModule.h"
#include "TestAccess.h"
#include "TestHarness.h"
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
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

struct CellGameFixture
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
  IllumoContext context;
  CellGameModule module;
  bool started;

  CellGameFixture(int width = 8, int height = 6)
    : window(640, 480)
    , env()
    , camera(glm::vec2(1.0f, 1.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , registry()
    , console(&env, &registry, &window, &renderer)
    , input(nullptr)
    , scene(&window, &camera)
    , context{ &scene,  &window, &console, &input,   &renderer,
               nullptr, &env,    &camera,  &registry }
    , module()
    , started(false)
  {
    env.setVar("WinX", 640);
    env.setVar("WinY", 480);
    env.setVar("CanvasX", width);
    env.setVar("CanvasY", height);
    env.setVar("ModeString", "GAME_OF_LIFE");
    env.setVar("tps", 30);
    env.setVar("speedFactor", 1.0);
    env.setVar("cellFadeSpeed", 8.0);
    env.setVar("WorldChunksX", 0);
    env.setVar("WorldChunksY", 0);
    env.setVar("vsync", true);
    env.setVar("fullscreen", false);
    env.setVar("render3dTest", false);
    mock.Initialize();
    module.Start(&context);
    started = CellGameModuleTestAccess::getCellContext(module) != nullptr;
  }

  ~CellGameFixture()
  {
    if (started) {
      module.Exit();
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
testStartRegistersGameFeatures()
{
  testSection("CellGameModule: start and command registration");
  CellGameFixture fixture;
  testTrue(g, fixture.started, "valid headless context starts the game module");
  testTrue(g,
           CellGameModuleTestAccess::getState(fixture.module) ==
             CellState::EDIT,
           "module starts in edit mode");
  testEqSize(g,
             fixture.registry.GetCommandNames().size(),
             31,
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
             9,
             "ruleset completion candidates registered");
  testTrue(g,
           CellGameModuleTestAccess::getConfigurationMenu(fixture.module) !=
             nullptr,
           "Release-visible configuration menu is constructed at startup");

  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  testEqSize(g,
             fixture.scene.drawableCount(),
             1,
             "canvas is dispatched while splash is hidden");
  testEqSize(g,
             fixture.scene.drawablesIn(RenderLayerId::World).size(),
             1,
             "canvas is on the World layer");

  fixture.module.Exit();
  fixture.started = false;
  testEqSize(g,
             fixture.registry.GetCommandNames().size(),
             2,
             "Exit unregisters every game command");
}

static void
testInvalidContextStartIsContained()
{
  testSection("CellGameModule: invalid start context");
  CellGameModule module;
  module.Start(nullptr);
  testTrue(g,
           CellGameModuleTestAccess::getCellContext(module) == nullptr,
           "null context does not create game state");
  // Failed Start must not crash on later frame hooks.
  module.Update(0.016);
  module.DispatchDrawables(nullptr);
  module.Exit();

  CellGameFixture fixture;
  IllumoContext incomplete = fixture.context;
  incomplete.commandRegistry = nullptr;
  CellGameModule incompleteModule;
  incompleteModule.Start(&incomplete);
  testTrue(g,
           CellGameModuleTestAccess::getCellContext(incompleteModule) ==
             nullptr,
           "missing service rejects startup");
  incompleteModule.Update(0.016);
  incompleteModule.DispatchDrawables(&fixture.scene);
  incompleteModule.Exit();
}

static void
testRender3dTestFlag()
{
  testSection("CellGameModule: render3dTest diagnostic scene");
  CellGameFixture fixture;
  fixture.env.setVar("render3dTest", true);
  fixture.module.Update(0.5);
  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);

  MeshVisual* staticScene =
    CellGameModuleTestAccess::getRender3dTestStatic(fixture.module);
  MeshVisual* animatedScene =
    CellGameModuleTestAccess::getRender3dTestAnimated(fixture.module);
  MeshVisual* childScene =
    CellGameModuleTestAccess::getRender3dTestChild(fixture.module);
  SceneGraph* render3dGraph =
    CellGameModuleTestAccess::getRender3dSceneGraph(fixture.module);
  testTrue(g,
           staticScene != nullptr && animatedScene != nullptr &&
             childScene != nullptr && render3dGraph != nullptr,
           "flag builds all 3D diagnostic attachments and SceneGraph");
  testEqSize(g,
             fixture.scene.drawablesIn(RenderLayerId::World).size(),
             1u,
             "flag replaces CanvasView with the single SceneGraph drawable");
  testEqSize(g,
             render3dGraph->getNodeCount(),
             3u,
             "SceneGraph holds root, orbit, and child nodes");
  if (render3dGraph != nullptr) {
    testTrue(g,
             fixture.scene.drawablesIn(RenderLayerId::World)[0] ==
               render3dGraph,
             "diagnostic scene registers SceneGraph in World layer");
    const SceneNodeHandle root = render3dGraph->getRoot(0);
    testTrue(g,
             render3dGraph->getRenderAttachment(root) == staticScene,
             "root attaches the axes/grid MeshVisual");
    testTrue(g,
             render3dGraph->getChildCount(root) == 1u &&
               render3dGraph->getRenderAttachment(
                 render3dGraph->getChild(root, 0)) == animatedScene,
             "orbit child attaches the cube MeshVisual");
  }
  testTrue(g,
           fixture.camera.getProjectionType() == ProjectionType::Perspective,
           "flag drives the product camera in perspective");

  fixture.env.setVar("render3dTest", false);
  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  testEqSize(g,
             fixture.scene.drawablesIn(RenderLayerId::World).size(),
             1u,
             "disabling the flag restores CanvasView");
  testTrue(
    g,
    fixture.scene.drawablesIn(RenderLayerId::World)[0] ==
      CellGameModuleTestAccess::getCellContext(fixture.module)->getCanvasView(),
    "normal World presentation is restored");
  testTrue(g,
           fixture.camera.getProjectionType() == ProjectionType::Orthographic,
           "disabling the flag restores the orthographic CA camera");
}

static void
testWireworldSeedAndBrush()
{
  testSection("CellGameModule: Wireworld seed and brush state");
  CellGameFixture fixture(16, 12);
  fixture.env.setVar("ModeString", "WIREWORLD");
  // Restart under Wireworld so seedInitialPattern runs for that ruleset.
  fixture.module.Exit();
  fixture.started = false;
  fixture.env.setVar("ModeString", "WIREWORLD");
  fixture.module.Start(&fixture.context);
  fixture.started =
    CellGameModuleTestAccess::getCellContext(fixture.module) != nullptr;
  testTrue(g, fixture.started, "Wireworld Start succeeds");

  CellContext* cellContext =
    CellGameModuleTestAccess::getCellContext(fixture.module);
  testTrue(g, cellContext != nullptr, "Wireworld cellContext exists");
  testTrue(
    g, cellContext->getModeString() == "WIREWORLD", "ModeString is WIREWORLD");

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
              CellGameModuleTestAccess::getWireworldBrush(fixture.module)),
            static_cast<int>(WireworldRuleSet::CELL_CONDUCTOR),
            "default brush is conductor");
  CellGameModuleTestAccess::setWireworldBrush(fixture.module,
                                              WireworldRuleSet::CELL_HEAD);
  testEqInt(g,
            static_cast<int>(
              CellGameModuleTestAccess::getWireworldBrush(fixture.module)),
            static_cast<int>(WireworldRuleSet::CELL_HEAD),
            "brush can select head for left-paint");
}

static void
testSaveLoadRoundTrip()
{
  testSection("CellGameModule: save/load round trip");
  CellGameFixture fixture(5, 4);
  CellContext* cellContext =
    CellGameModuleTestAccess::getCellContext(fixture.module);
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
           CellGameModuleTestAccess::save(fixture.module, savePath.string()),
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
           CellGameModuleTestAccess::load(fixture.module, savePath.string()),
           "saved canvas loads");
  testTrue(g,
           cellContext->getModeString() == "WIREWORLD",
           "saved ruleset is restored");
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
    CellGameModuleTestAccess::save(fixture.module, "roundtrip-again.illumo"),
    "same sparse state saves again");
  const std::vector<char> secondSave = readFileBytes("roundtrip-again.illumo");
  testTrue(g, firstSave == secondSave, "sparse save output is deterministic");
  testEqSize(g,
             std::filesystem::file_size(savePath),
             static_cast<std::size_t>(188 + 2 * (16 * 16 + 16)),
             "save contains the sparse header and two chunk records");
}

static void
testSparseV2Compatibility()
{
  testSection("CellGameModule: sparse v2 compatibility");
  CellGameFixture fixture;
  CellContext* cellContext =
    CellGameModuleTestAccess::getCellContext(fixture.module);
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
           CellGameModuleTestAccess::load(fixture.module, "valid-v2.illumo"),
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
             fixture.camera.GetPositionPrecise() ==
               glm::dvec2(cameraX, cameraY) &&
             fixture.camera.GetZoom() == static_cast<float>(cameraZoom),
           "version 2 ruleset and camera are restored");
}

static void
testSettingsYieldToConsole()
{
  testSection("CellGameModule: open console blocks settings input");
  CellGameFixture fixture;
  ConfigurationMenu* menu =
    CellGameModuleTestAccess::getConfigurationMenu(fixture.module);
  testTrue(g, menu != nullptr && !menu->isOpen(), "settings start closed");
  menu->open(CellGameModuleTestAccess::currentConfiguration(fixture.module));
  testTrue(g, menu->isOpen(), "settings open for console-yield check");

  fixture.console.Toggle();
  testTrue(g, fixture.console.isOpen, "console is open");
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g, menu->isOpen(), "open console blocks settings Escape");
}

static void
testReleaseConfigurationWorkflow()
{
  testSection("CellGameModule: Release configuration workflow");
  CellGameFixture fixture;
  ConfigurationMenu* menu =
    CellGameModuleTestAccess::getConfigurationMenu(fixture.module);
  testTrue(g, menu != nullptr && !menu->isOpen(), "settings start closed");

  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::F1, InputAction::Press);
  fixture.module.Update(0.016);
  testTrue(g, menu != nullptr && menu->isOpen(), "F1 opens settings");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::F1, InputAction::Release);
  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  testEqSize(g,
             fixture.scene.drawableCount(),
             2u,
             "open settings add one UI drawable beside the canvas");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g, !menu->isOpen(), "Escape closes settings without applying");

  CellContext* cellContext =
    CellGameModuleTestAccess::getCellContext(fixture.module);
  cellContext->getGrid()->setCell(CellAddress{ 77, 88 }, 0);
  SimulatorConfiguration configuration =
    CellGameModuleTestAccess::currentConfiguration(fixture.module);
  configuration.ruleSet = "SEEDS";
  configuration.worldChunkWidth = 2;
  configuration.worldChunkHeight = 3;
  configuration.tps = 48;
  configuration.speedFactor = 2.0;
  configuration.fadeSpeed = 4.0;
  configuration.vsync = false;
  configuration.fullscreen = true;
  testTrue(
    g,
    CellGameModuleTestAccess::applyConfiguration(fixture.module, configuration),
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

  menu->open(CellGameModuleTestAccess::currentConfiguration(fixture.module));
  for (int row = 0; row < 12; ++row) {
    fixture.input.getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::Down, InputAction::Press, 0 });
  }
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  ExitConfirmDialog* confirm =
    CellGameModuleTestAccess::getExitConfirmDialog(fixture.module);
  testTrue(g,
           !fixture.window.closeRequested && menu->isOpen() &&
             confirm != nullptr && confirm->isOpen(),
           "Exit menu action asks for confirmation first");
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Y, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g,
           fixture.window.closeRequested && !confirm->isOpen(),
           "confirming exit requests normal application shutdown");
}

static void
testHamburgerMenuButton()
{
  testSection("CellGameModule: hamburger icon toggles settings menu");
  CellGameFixture fixture;
  ConfigurationMenu* menu =
    CellGameModuleTestAccess::getConfigurationMenu(fixture.module);
  testTrue(g, menu != nullptr && !menu->isOpen(), "settings start closed");

  // Initial update establishes hamburger button placement and dimensions
  fixture.module.Update(0.016);
  GameVisual* hamburger =
    CellGameModuleTestAccess::getHamburgerVisual(fixture.module);
  testTrue(g,
           hamburger != nullptr && hamburger->isVisible(),
           "hamburger button is visible in default state");

  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  const std::vector<DrawableBase*> uiDrawables =
    fixture.scene.drawablesIn(RenderLayerId::UI);
  testTrue(g,
           std::find(uiDrawables.begin(), uiDrawables.end(), hamburger) !=
             uiDrawables.end(),
           "hamburger button is on the UI layer");

  const float hx = CellGameModuleTestAccess::getHamburgerX(fixture.module);
  const float hy = CellGameModuleTestAccess::getHamburgerY(fixture.module);
  const float hsize =
    CellGameModuleTestAccess::getHamburgerSize(fixture.module);
  testTrue(g,
           hx > 0.0f && hy > 0.0f && hsize > 0.0f,
           "hamburger button has valid bounds");

  // Move mouse outside hamburger -> not hovered
  fixture.window.mouseX = 0.0;
  fixture.window.mouseY = 0.0;
  fixture.module.Update(0.016);
  testTrue(g,
           !CellGameModuleTestAccess::isHamburgerHovered(fixture.module),
           "hamburger is not hovered when mouse is away");

  // Move mouse inside hamburger -> hovered
  fixture.window.mouseX = static_cast<double>(hx + hsize * 0.5f);
  fixture.window.mouseY = static_cast<double>(hy + hsize * 0.5f);
  fixture.module.Update(0.016);
  testTrue(g,
           CellGameModuleTestAccess::isHamburgerHovered(fixture.module),
           "hamburger is hovered when mouse is over it");

  // Click hamburger -> toggles settings open
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  testTrue(g, menu->isOpen(), "clicking hamburger opens settings menu");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);

  // When settings are open, hamburger is hidden
  testTrue(g,
           !hamburger->isVisible(),
           "hamburger is hidden while settings menu is open");

  // Press Escape to close settings
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g, !menu->isOpen(), "settings menu closed with Escape");
  testTrue(g,
           hamburger->isVisible(),
           "hamburger reappears after settings menu closes");
}

static void
testExitConfirmationFromQ()
{
  testSection("CellGameModule: Q asks before exiting");
  CellGameFixture fixture;
  ExitConfirmDialog* confirm =
    CellGameModuleTestAccess::getExitConfirmDialog(fixture.module);
  testTrue(
    g, confirm != nullptr && !confirm->isOpen(), "confirm starts closed");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Q, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g,
           !fixture.window.closeRequested && confirm->isOpen(),
           "Q opens the exit confirmation instead of closing immediately");

  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  testEqSize(g,
             fixture.scene.drawableCount(),
             2u,
             "exit confirmation adds one UI drawable beside the canvas");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g,
           !fixture.window.closeRequested && !confirm->isOpen(),
           "Escape cancels the exit confirmation");

  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Q, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Y, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g,
           fixture.window.closeRequested && !confirm->isOpen(),
           "Y confirms Q-initiated exit");
}

static void
testLoadRejectsInvalidFiles()
{
  testSection("CellGameModule: invalid save validation");
  CellGameFixture fixture;
  testTrue(g,
           !CellGameModuleTestAccess::load(fixture.module, ""),
           "empty load path rejected");
  testTrue(g,
           !CellGameModuleTestAccess::load(fixture.module, "missing.illumo"),
           "missing file rejected");

  std::ofstream("short.illumo", std::ios::binary).write("short", 5);
  testTrue(g,
           !CellGameModuleTestAccess::load(fixture.module, "short.illumo"),
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
    CellGameModuleTestAccess::getCellContext(fixture.module);
  liveContext->getGrid()->setCell(CellAddress{ 77, 88 }, 0);
  testTrue(g,
           !CellGameModuleTestAccess::load(fixture.module, "short-v2.illumo"),
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
           !CellGameModuleTestAccess::load(fixture.module,
                                           "invalid-v3-topology.illumo"),
           "mixed finite and infinite topology metadata is rejected");
  testEqUChar(g,
              liveContext->getGrid()->getCell(CellAddress{ 77, 88 }),
              0,
              "invalid v3 metadata does not mutate live state");

  writeSaveFile("unknown-rule.illumo", "NOT_A_RULE", 2, 2, { 1, 1, 1, 1 });
  testTrue(
    g,
    !CellGameModuleTestAccess::load(fixture.module, "unknown-rule.illumo"),
    "unknown ruleset rejected");

  writeSaveFile("invalid-size.illumo", "SEEDS", 0, 4, {});
  testTrue(
    g,
    !CellGameModuleTestAccess::load(fixture.module, "invalid-size.illumo"),
    "zero dimension rejected");
  writeSaveFile("oversized.illumo", "SEEDS", 100000001, 1, {});
  testTrue(g,
           !CellGameModuleTestAccess::load(fixture.module, "oversized.illumo"),
           "oversized canvas rejected before allocation");
  writeSaveFile("short-cells.illumo", "SEEDS", 2, 2, { 0, 1, 0 });
  testTrue(
    g,
    !CellGameModuleTestAccess::load(fixture.module, "short-cells.illumo"),
    "truncated cell data rejected");

  testTrue(g,
           !CellGameModuleTestAccess::save(fixture.module, ""),
           "empty save path rejected");
  testTrue(g,
           !CellGameModuleTestAccess::save(fixture.module, "."),
           "directory cannot be opened as a save file");
}

static void
testLoadCopiesOverlap()
{
  testSection("CellGameModule: different-size save overlap");
  CellGameFixture fixture(4, 3);
  writeSaveFile("small.illumo", "SEEDS", 2, 2, { 0, 1, 1, 0 });
  testTrue(g,
           CellGameModuleTestAccess::load(fixture.module, "small.illumo"),
           "different-size valid save loads");
  CanvasView* canvas =
    CellGameModuleTestAccess::getCellContext(fixture.module)->getCanvasView();
  testEqUChar(
    g, canvas->getCanvasPixel(-1, -1), 0, "legacy origin row zero copied");
  testEqUChar(
    g, canvas->getCanvasPixel(0, -1), 1, "legacy row zero width preserved");
  testEqUChar(g, canvas->getCanvasPixel(0, 0), 0, "legacy row one copied");
  testEqUChar(
    g, canvas->getCanvasPixel(3, 2), 1, "outside overlap remains empty");
  testTrue(g,
           CellGameModuleTestAccess::getCellContext(fixture.module)
               ->getGrid()
               ->getAllocatedChunkCount() == 2,
           "legacy cells are imported sparsely");
}

static void
testConsoleSimulationCommands()
{
  testSection("CellGameModule: simulation console commands");
  CellGameFixture fixture(6, 6);
  fixture.execute("ruleset", { "SEEDS" });
  testTrue(
    g,
    CellGameModuleTestAccess::getCellContext(fixture.module)->getModeString() ==
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
    CellGameModuleTestAccess::getCellContext(fixture.module)->getCanvasView();
  testEqUChar(
    g, canvas->getCanvasPixel(1, 2), 0, "setcell writes a valid cell");
  fixture.execute("setcell", { "-99", "2", "0" });
  fixture.execute("setcell", { "1", "2", "999" });
  testEqUChar(g, canvas->getCanvasPixel(-99, 2), 0, "far setcell is accepted");
  testTrue(g,
           historyContains(fixture.console, "Usage: setcell"),
           "setcell state validation is reported");

  fixture.execute("clear_canvas");
  testEqUChar(
    g, canvas->getCanvasPixel(1, 2), 1, "clear command empties cells");
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
           CellGameModuleTestAccess::getState(fixture.module) ==
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
           CellGameModuleTestAccess::getState(fixture.module) ==
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
  testSection("CellGameModule: camera and file console commands");
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
  fixture.execute("camera_reset");
  testTrue(g,
           historyContains(fixture.console, "Camera reset"),
           "camera reset command reports success");

  fixture.execute("save", { "console-save" });
  testTrue(g,
           std::filesystem::exists("console-save.illumo"),
           "save command adds extension");
  fixture.execute("load", { "console-save" });
  testTrue(
    g,
    historyContains(fixture.console, "Loaded canvas from console-save.illumo"),
    "load command falls back to extension");
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
           std::filesystem::exists("dialog-save.illumo"),
           "save dialog path gains extension");
  gLoadDialogResult = "dialog-save.illumo";
  fixture.execute("load_dialog");
  testTrue(
    g,
    historyContains(fixture.console, "Loaded canvas from dialog-save.illumo"),
    "load dialog uses selected path");
}

static void
testUpdateStateAndTiming()
{
  testSection("CellGameModule: update state and timing");
  CellGameFixture fixture(5, 5);
  CellContext* cellContext =
    CellGameModuleTestAccess::getCellContext(fixture.module);
  CanvasView* canvas = cellContext->getCanvasView();
  canvas->clearCanvas();
  canvas->setCanvasPixel(1, 2, 0);
  canvas->setCanvasPixel(2, 2, 0);
  canvas->setCanvasPixel(3, 2, 0);

  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Press);
  fixture.module.Update(0.0);
  testTrue(g,
           CellGameModuleTestAccess::getState(fixture.module) ==
             CellState::NORMAL,
           "toggle action enters normal state");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::None);
  fixture.module.Update(0.04);
  bool publishedGeneration = false;
  for (int attempt = 0; attempt < 10000 && !publishedGeneration; ++attempt) {
    std::this_thread::yield();
    fixture.module.Update(0.0);
    publishedGeneration =
      CellGameModuleTestAccess::getLastSimulationSteps(fixture.module) == 1;
  }
  testTrue(g, publishedGeneration, "normal update publishes async generation");
  testEqUChar(g,
              canvas->getCanvasPixel(2, 1),
              0,
              "normal update advances simulation at configured tps");

  fixture.env.setVar("tps", 100000);
  fixture.env.setVar("speedFactor", 1000.0);
  fixture.env.setVar("cellFadeSpeed", -2.0);
  fixture.module.Update(1.0);
  CellGameModuleTestAccess::drainSimulation(fixture.module);
  testTrue(g,
           CellGameModuleTestAccess::getState(fixture.module) ==
             CellState::NORMAL,
           "large delta and rate remain bounded");
  testTrue(g,
           CellGameModuleTestAccess::getLastSimulationSteps(fixture.module) <=
             1,
           "normal update publishes at most one generation per frame");
  testTrue(g,
           CellGameModuleTestAccess::getSimulationDebtDropped(fixture.module),
           "normal update drops excessive catch-up debt");

  InputManager::scrollCallback(nullptr, 0.0, 1.0);
  const float oldZoom = fixture.camera.GetZoom();
  fixture.module.Update(-1.0);
  fixture.camera.Update(1.0f);
  testTrue(
    g, fixture.camera.GetZoom() > oldZoom, "scroll input updates camera zoom");

  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Press);
  fixture.module.Update(0.0);
  testTrue(g,
           CellGameModuleTestAccess::getState(fixture.module) ==
             CellState::EDIT,
           "toggle action returns to edit state");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::None);
  fixture.module.Update(0.016);
}

static void
testFrameSimulationBudget()
{
  testSection("CellGameModule: asynchronous simulation budget");
  CellGameFixture fixture(5, 5);
  fixture.env.setVar("tps", 30);
  fixture.env.setVar("speedFactor", 1.0);

  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Press);
  fixture.module.Update(0.0);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::None);

  fixture.module.Update(0.25);
  testEqInt(g,
            CellGameModuleTestAccess::getLastSimulationSteps(fixture.module),
            0,
            "scheduled generation does not block its render frame");
  testTrue(g,
           CellGameModuleTestAccess::getSimulationDebtDropped(fixture.module),
           "in-flight scheduling drops excess catch-up debt");
  CellGameModuleTestAccess::drainSimulation(fixture.module);
  testEqInt(g,
            CellGameModuleTestAccess::getLastSimulationSteps(fixture.module),
            1,
            "drain publishes the single in-flight generation");
  testTrue(g,
           CellGameModuleTestAccess::getLastSimulationFrameMilliseconds(
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
  testSection("CellGameModule: async state transitions drain safely");
  CellGameFixture fixture(16, 12);
  const std::string savePath = "async-transition.illumo";

  fixture.execute("run");
  fixture.module.Update(0.25);
  testTrue(g,
           CellGameModuleTestAccess::isSimulationBusy(fixture.module),
           "running update leaves one generation in flight or completed");
  fixture.execute("pause");
  testTrue(g,
           !CellGameModuleTestAccess::isSimulationBusy(fixture.module) &&
             CellGameModuleTestAccess::getState(fixture.module) ==
               CellState::EDIT,
           "pause publishes and drains before entering edit mode");

  fixture.execute("run");
  fixture.module.Update(0.25);
  fixture.execute("save", { savePath });
  testTrue(g,
           !CellGameModuleTestAccess::isSimulationBusy(fixture.module) &&
             std::filesystem::exists(savePath),
           "save drains before reading the published grid");

  fixture.module.Update(0.25);
  fixture.execute("ruleset", { "SEEDS" });
  CellContext* context =
    CellGameModuleTestAccess::getCellContext(fixture.module);
  testTrue(g,
           !CellGameModuleTestAccess::isSimulationBusy(fixture.module) &&
             context->getModeString() == "SEEDS",
           "ruleset change drains before replacing the transition table");

  fixture.module.Update(0.25);
  fixture.execute("step", { "2" });
  testTrue(g,
           !CellGameModuleTestAccess::isSimulationBusy(fixture.module) &&
             CellGameModuleTestAccess::getState(fixture.module) ==
               CellState::EDIT,
           "manual stepping drains and returns to edit mode");

  fixture.execute("run");
  fixture.module.Update(0.25);
  fixture.execute("load", { savePath });
  testTrue(g,
           !CellGameModuleTestAccess::isSimulationBusy(fixture.module),
           "load drains before replacing published sparse state");
}

static int
runCellGameModuleCase(void (*testFunction)())
{
  g.failures = 0;
  gSaveDialogResult.clear();
  gLoadDialogResult.clear();
  testFunction();
  return g.failures;
}

void
registerCellGameModuleTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.CellGame.StartAndRegistration", []() {
    return runCellGameModuleCase(testStartRegistersGameFeatures);
  });
  registry.add("IllumoGame.CellGame.InvalidContext", []() {
    return runCellGameModuleCase(testInvalidContextStartIsContained);
  });
  registry.add("IllumoGame.CellGame.Render3dTestFlag",
               []() { return runCellGameModuleCase(testRender3dTestFlag); });
  registry.add("IllumoGame.CellGame.WireworldSeedAndBrush", []() {
    return runCellGameModuleCase(testWireworldSeedAndBrush);
  });
  registry.add("IllumoGame.CellGame.SaveLoadRoundTrip",
               []() { return runCellGameModuleCase(testSaveLoadRoundTrip); });
  registry.add("IllumoGame.CellGame.SparseV2Compatibility", []() {
    return runCellGameModuleCase(testSparseV2Compatibility);
  });
  registry.add("IllumoGame.CellGame.ReleaseConfiguration", []() {
    return runCellGameModuleCase(testReleaseConfigurationWorkflow);
  });
  registry.add("IllumoGame.CellGame.HamburgerMenu",
               []() { return runCellGameModuleCase(testHamburgerMenuButton); });
  registry.add("IllumoGame.CellGame.SettingsYieldToConsole", []() {
    return runCellGameModuleCase(testSettingsYieldToConsole);
  });
  registry.add("IllumoGame.CellGame.ExitConfirmation", []() {
    return runCellGameModuleCase(testExitConfirmationFromQ);
  });
  registry.add("IllumoGame.CellGame.InvalidSaveFiles", []() {
    return runCellGameModuleCase(testLoadRejectsInvalidFiles);
  });
  registry.add("IllumoGame.CellGame.LoadOverlap",
               []() { return runCellGameModuleCase(testLoadCopiesOverlap); });
  registry.add("IllumoGame.CellGame.SimulationCommands", []() {
    return runCellGameModuleCase(testConsoleSimulationCommands);
  });
  registry.add("IllumoGame.CellGame.CameraAndFileCommands", []() {
    return runCellGameModuleCase(testConsoleCameraAndFiles);
  });
  registry.add("IllumoGame.CellGame.UpdateStateAndTiming", []() {
    return runCellGameModuleCase(testUpdateStateAndTiming);
  });
  registry.add("IllumoGame.CellGame.FrameSimulationBudget", []() {
    return runCellGameModuleCase(testFrameSimulationBudget);
  });
  registry.add("IllumoGame.CellGame.AsyncTransitionDraining", []() {
    return runCellGameModuleCase(testAsyncTransitionDraining);
  });
}
