#include "Game/CSimSounds.h"
#include "Game/CanvasCoordinatePolicy.h"
#include "Game/CellGameModule.h"
#include "Game/RuleCatalogLoader.h"
#include "Game/SoftwareCursor.h"
#include "Rulesets/RuleSet.h"
#include "Rulesets/RuleSetRegistry.h"
#include "Rulesets/WireworldRuleSet.h"
#include "TestAccess.h"
#include "TestHarness.h"
#include <Illumo/Content/VfsAssetSource.h>
#include <Illumo/Engine/IModuleHost.h>
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
             RuleSetRegistry::instance().getKnownRules().size(),
             "ruleset completion candidates registered");
  testTrue(g,
           CellGameModuleTestAccess::getConfigurationMenu(fixture.module) !=
             nullptr,
           "Release-visible configuration menu is constructed at startup");

  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  testEqSize(g,
             fixture.scene.drawableCount(),
             2,
             "canvas and entrance veil are dispatched while splash is hidden");
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

  SceneInstance* diagnostic =
    CellGameModuleTestAccess::getRender3dScene(fixture.module);
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
    fixture.module.Update(0.25);
    fixture.scene.ClearDrawables();
    fixture.module.DispatchDrawables(&fixture.scene);
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
  fixture.env.setVar("FamilyString", "WIREWORLD_FAMILY");
  fixture.env.setVar("RuleSetString", "WIREWORLD");
  fixture.env.setVar("ModeString", "WIREWORLD");
  // Restart under Wireworld so seedInitialPattern runs for that ruleset.
  fixture.module.Exit();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "WIREWORLD_FAMILY");
  fixture.env.setVar("RuleSetString", "WIREWORLD");
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
testCyclicMultistateSeed()
{
  testSection("CellGameModule: cyclic rules seed many interacting states");
  CellGameFixture fixture(24, 18);
  fixture.module.Exit();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "PRISMATIC_ECOLOGY_12");
  fixture.env.setVar("RuleSetString", "PRISM_RUSH");
  fixture.env.setVar("ModeString", "PRISM_RUSH");
  fixture.started = fixture.module.Start(&fixture.context);
  CellContext* context =
    CellGameModuleTestAccess::getCellContext(fixture.module);
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
  testSection("CellGameModule: every shipped rule starts and advances");
  CellGameFixture fixture(24, 18);
  const std::vector<RuleSetDefinition> definitions =
    RuleSetRegistry::instance().getDefinitions();
  for (const RuleSetDefinition& definition : definitions) {
    fixture.module.Exit();
    fixture.started = false;
    fixture.env.setVar("FamilyString", definition.familyId);
    fixture.env.setVar("RuleSetString", definition.id);
    fixture.env.setVar("ModeString", definition.id);
    fixture.started = fixture.module.Start(&fixture.context);
    CellContext* context =
      CellGameModuleTestAccess::getCellContext(fixture.module);
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
  testSection("CellGameModule: researched rules start with active populations");
  CellGameFixture fixture(24, 18);
  fixture.module.Exit();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "QUADLIFE_4_SPECIES");
  fixture.env.setVar("RuleSetString", "QUADLIFE");
  fixture.env.setVar("ModeString", "QUADLIFE");
  fixture.started = fixture.module.Start(&fixture.context);
  CellContext* context =
    CellGameModuleTestAccess::getCellContext(fixture.module);
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

  fixture.module.Exit();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "GENERATIONS_21_PHASE");
  fixture.env.setVar("RuleSetString", "FIREWORKS");
  fixture.env.setVar("ModeString", "FIREWORKS");
  fixture.started = fixture.module.Start(&fixture.context);
  context = CellGameModuleTestAccess::getCellContext(fixture.module);
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

  fixture.module.Exit();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "HODGEPODGE_101_LEVEL");
  fixture.env.setVar("RuleSetString", "HODGEPODGE_SPIRAL_G28");
  fixture.env.setVar("ModeString", "HODGEPODGE_SPIRAL_G28");
  fixture.started = fixture.module.Start(&fixture.context);
  context = CellGameModuleTestAccess::getCellContext(fixture.module);
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

  fixture.module.Exit();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "TURMITE_3_COLOR");
  fixture.env.setVar("RuleSetString", "TURMITE_RRL");
  fixture.env.setVar("ModeString", "TURMITE_RRL");
  fixture.started = fixture.module.Start(&fixture.context);
  context = CellGameModuleTestAccess::getCellContext(fixture.module);
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

  fixture.module.Exit();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "HPP_LATTICE_GAS_16");
  fixture.env.setVar("RuleSetString", "HPP_GAS");
  fixture.env.setVar("ModeString", "HPP_GAS");
  fixture.started = fixture.module.Start(&fixture.context);
  context = CellGameModuleTestAccess::getCellContext(fixture.module);
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

  fixture.module.Exit();
  fixture.started = false;
  fixture.env.setVar("FamilyString", "RPSLS_5_SPECIES");
  fixture.env.setVar("RuleSetString", "RPSLS_INVASION_T1");
  fixture.env.setVar("ModeString", "RPSLS_INVASION_T1");
  fixture.started = fixture.module.Start(&fixture.context);
  context = CellGameModuleTestAccess::getCellContext(fixture.module);
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
    CellGameModuleTestAccess::save(fixture.module, "roundtrip-again.illumo"),
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
           CellGameModuleTestAccess::load(fixture.module, "valid-v3.illumo") &&
             cellContext->getModeString() == "SEEDS" &&
             cellContext->getFamilyString() == "LIFE_LIKE_BINARY",
           "version 3 derives the family from its ruleset ID");
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
             3u,
             "settings render above the canvas and entrance veil");

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
  configuration.fpsCap = 144;
  configuration.showInspector = true;
  configuration.reducedUiMotion = true;
  configuration.softwareCursor = false;
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

  const SimulatorConfiguration applied =
    CellGameModuleTestAccess::currentConfiguration(fixture.module);
  testTrue(
    g,
    applied.fpsCap == 144 && applied.showInspector && applied.reducedUiMotion &&
      !applied.softwareCursor && fixture.env.getVar("fps").valueAsLong == 144 &&
      !fixture.env.getVar("softwareCursor").valueAsBool,
    "new display preferences round trip through runtime and environment");
  configuration.fpsCap = -1;
  testTrue(g,
           !CellGameModuleTestAccess::applyConfiguration(fixture.module,
                                                         configuration) &&
             fixture.env.getVar("fps").valueAsLong == 144,
           "invalid FPS cap leaves applied preferences intact");
  menu->open(applied);
  for (int row = 0; row < 19; ++row) {
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
openPaintDrawer(CellGameFixture& fixture)
{
  fixture.module.Update(0.016);
  testTrue(g,
           !CellGameModuleTestAccess::isPaintPaletteExpanded(fixture.module),
           "drawer starts closed as the peeking bubble");
  fixture.window.mouseX = static_cast<double>(fixture.window.width) * 0.5;
  fixture.window.mouseY = static_cast<double>(fixture.window.height) - 1.0;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  testTrue(g,
           !CellGameModuleTestAccess::isPaintPaletteExpanded(fixture.module),
           "footer click cannot open the palette");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
  const float scale =
    fixture.renderer.getUiScale() *
    CellGameModuleTestAccess::getPaintPaletteVisual(fixture.module)
      .getTransform()
      .scaleX;
  fixture.window.mouseX = static_cast<double>(fixture.window.width) * 0.5;
  fixture.window.mouseY =
    static_cast<double>(fixture.window.height -
                        CellGameModuleTestAccess::getCellContext(fixture.module)
                          ->getCanvasView()
                          ->getBottomInsetPixels()) -
    16.0 * scale;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  testTrue(g,
           CellGameModuleTestAccess::isPaintPaletteExpanded(fixture.module),
           "the peeking bubble opens the drawer");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
}

static void
pointAtPaintCard(CellGameFixture& fixture, int stateCount, int state)
{
  const float scale =
    fixture.renderer.getUiScale() *
    CellGameModuleTestAccess::getPaintPaletteVisual(fixture.module)
      .getTransform()
      .scaleX;
  const float width = static_cast<float>(stateCount) * 132.0f + 24.0f;
  fixture.window.mouseX =
    static_cast<double>(fixture.window.width) * 0.5 +
    (-width * 0.5f + 74.0f + static_cast<float>(state) * 132.0f) * scale;
  fixture.window.mouseY =
    static_cast<double>(fixture.window.height -
                        CellGameModuleTestAccess::getCellContext(fixture.module)
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
    CellGameModuleTestAccess::getCellContext(fixture.module)->getGrid();
  const std::uint64_t revision = grid->getRevision();
  pointAtPaintCard(fixture, 2, 1);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  testEqInt(g,
            CellGameModuleTestAccess::getPaintBrush(fixture.module),
            1,
            "dead swatch selects the erase brush");
  testTrue(g,
           grid->getRevision() == revision,
           "swatch click does not mutate the world");
  fixture.window.mouseX = 400.0;
  fixture.window.mouseY = 200.0;
  fixture.module.Update(0.016);
  testTrue(
    g, grid->getRevision() == revision, "dragging off palette stays captured");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);

  fixture.env.setVar("ModeString", "BRIANS_BRAIN");
  fixture.module.Update(0.016);
  testEqInt(g,
            CellGameModuleTestAccess::getPaintBrush(fixture.module),
            0,
            "ruleset change resets generic brush");
  pointAtPaintCard(fixture, 3, 2);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  testEqInt(g,
            CellGameModuleTestAccess::getPaintBrush(fixture.module),
            2,
            "Brian's Brain exposes dying cells");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
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
  fixture.module.Update(0.016);
  testEqInt(g,
            grid->getCell({ cellX, cellY }),
            2,
            "selected dying brush paints the canvas");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseRight, InputAction::Press);
  fixture.module.Update(0.016);
  testEqInt(g,
            grid->getCell({ cellX, cellY }),
            1,
            "right mouse still erases regardless of brush");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseRight, InputAction::Release);

  fixture.env.setVar("ModeString", "WIREWORLD");
  fixture.module.Update(0.016);
  pointAtPaintCard(fixture, 4, 0);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  testEqInt(g,
            CellGameModuleTestAccess::getWireworldBrush(fixture.module),
            0,
            "Wireworld swatch updates its existing brush");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
  fixture.env.setVar("reducedUiMotion", false);
  fixture.window.mouseX = 320.0;
  fixture.window.mouseY =
    326.0 - CellGameModuleTestAccess::getCellContext(fixture.module)
              ->getCanvasView()
              ->getBottomInsetPixels();
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  const float reveal =
    CellGameModuleTestAccess::getPaintPaletteReveal(fixture.module);
  testTrue(g,
           !CellGameModuleTestAccess::isPaintPaletteExpanded(fixture.module) &&
             reveal > 0.0f && reveal < 1.0f,
           "header starts an eased collapse");
  fixture.module.Update(0.016);
  testTrue(g,
           !CellGameModuleTestAccess::isPaintPaletteExpanded(fixture.module),
           "held header click does not toggle repeatedly");
  fixture.env.setVar("reducedUiMotion", true);
  fixture.module.Update(0.016);
  testTrue(g,
           CellGameModuleTestAccess::getPaintPaletteReveal(fixture.module) ==
             0.0f,
           "reduced motion snaps the collapsed panel");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
  fixture.console.isOpen = true;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  testTrue(g,
           !CellGameModuleTestAccess::getPaintPaletteVisual(fixture.module)
               .isVisible() &&
             !CellGameModuleTestAccess::isPaintPaletteExpanded(fixture.module),
           "console owns input and hides palette");
}

static void
testPaintPaletteSounds()
{
  testSection("CellGameModule: the paint drawer voices expand and collapse");
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
    326.0 - CellGameModuleTestAccess::getCellContext(fixture.module)
              ->getCanvasView()
              ->getBottomInsetPixels();
  fixture.module.Update(0.016);
  fixture.module.Update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == hoversBefore + 1,
           "pointing at the collapse header plays one hover cue");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  fixture.module.Update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
  testTrue(g,
           !CellGameModuleTestAccess::isPaintPaletteExpanded(fixture.module) &&
             CSimSounds::playCount(CSimSound::CanvasPaintMenuCollapse) == 1 &&
             CSimSounds::playCount(CSimSound::CanvasPaintMenuExpand) == 1,
           "a held header click collapses it with one collapse cue");
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == hoversBefore + 1,
           "clicking does not add a hover cue");
  fixture.window.mouseX = 0.0;
  fixture.window.mouseY = 0.0;
  fixture.module.Update(0.016);
  fixture.window.mouseX = static_cast<double>(fixture.window.width) * 0.5;
  fixture.window.mouseY =
    static_cast<double>(fixture.window.height -
                        CellGameModuleTestAccess::getCellContext(fixture.module)
                          ->getCanvasView()
                          ->getBottomInsetPixels()) -
    16.0 * fixture.renderer.getUiScale() *
      CellGameModuleTestAccess::getPaintPaletteVisual(fixture.module)
        .getTransform()
        .scaleX;
  fixture.module.Update(0.016);
  fixture.module.Update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == hoversBefore + 2,
           "returning to the expand bubble plays one more hover cue");
  CSimSounds::resetCounts();
}

static void
testPaintPaletteCardSounds()
{
  testSection("CellGameModule: paint cards voice hover, pick and browse");
  CellGameFixture fixture;
  fixture.env.setVar("reducedUiMotion", true);
  // Five states: four cards show and the wheel can move one step.
  fixture.env.setVar("ModeString", "QUADLIFE");
  fixture.module.Update(0.016);
  openPaintDrawer(fixture);
  CSimSounds::resetCounts();
  pointAtPaintCard(fixture, 4, 0);
  fixture.module.Update(0.016);
  fixture.module.Update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 1,
           "pointing at a card plays one hover cue");
  pointAtPaintCard(fixture, 4, 2);
  fixture.module.Update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 2,
           "moving to another card plays another");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  fixture.module.Update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
  testTrue(g,
           CellGameModuleTestAccess::getPaintBrush(fixture.module) == 2 &&
             CSimSounds::playCount(CSimSound::MenuSelect) == 1 &&
             CSimSounds::playCount(CSimSound::MenuHover) == 2,
           "picking a card plays one select cue and no hover");
  *fixture.input.getMouseScrollOffset() = -1.0;
  fixture.module.Update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 3,
           "a wheel step that moves the cards ticks once");
  *fixture.input.getMouseScrollOffset() = -1.0;
  fixture.module.Update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 3,
           "the wheel stays quiet at the end of the states");
  CSimSounds::resetCounts();
}

static void
testPaintPaletteBubbleMorph()
{
  testSection("CellGameModule: the paint bubble morphs into the drawer");
  CellGameFixture fixture;
  fixture.env.setVar("reducedUiMotion", false);
  fixture.module.Update(0.016);
  GameVisual& visual =
    CellGameModuleTestAccess::getPaintPaletteVisual(fixture.module);
  testTrue(g,
           visual.isVisible() && visual.textCount() == 0u,
           "the collapsed palette is a wordless bubble");
  const float scale =
    fixture.renderer.getUiScale() * visual.getTransform().scaleX;
  const double bottom =
    static_cast<double>(fixture.window.height -
                        CellGameModuleTestAccess::getCellContext(fixture.module)
                          ->getCanvasView()
                          ->getBottomInsetPixels());
  // The bubble's bounding corner lies outside the circle.
  fixture.window.mouseX =
    static_cast<double>(fixture.window.width) * 0.5 + 26.0 * scale;
  fixture.window.mouseY = bottom - 38.0 * scale;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
  testTrue(g,
           !CellGameModuleTestAccess::isPaintPaletteExpanded(fixture.module),
           "the bubble is round: its bounding corner does not open it");

  fixture.window.mouseX = static_cast<double>(fixture.window.width) * 0.5;
  fixture.window.mouseY = bottom - 16.0 * scale;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
  testTrue(
    g,
    CellGameModuleTestAccess::isPaintPaletteExpanded(fixture.module) &&
      CellGameModuleTestAccess::getPaintPaletteWidthMorph(fixture.module) >
        CellGameModuleTestAccess::getPaintPaletteReveal(fixture.module),
    "opening stretches the bubble wide before it rises");
  testEqSize(g, visual.textCount(), 0u, "labels wait for the drawer to form");
  float peak = 0.0f;
  for (int frame = 0; frame < 90; ++frame) {
    fixture.module.Update(1.0 / 60.0);
    peak = std::max(
      peak, CellGameModuleTestAccess::getPaintPaletteReveal(fixture.module));
  }
  testTrue(g,
           peak > 1.02f && CellGameModuleTestAccess::getPaintPaletteReveal(
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
  fixture.module.Update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Press);
  fixture.module.Update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Release);
  ModeBadge& moduleBadge =
    CellGameModuleTestAccess::getModeBadge(fixture.module);
  testTrue(g,
           moduleBadge.isVisible() && !moduleBadge.label().empty(),
           "toggling the mode shows the corner badge");
  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
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
    CellGameModuleTestAccess::getPaintPaletteVisual(fixture.module);
  const float fit = visual.getTransform().scaleX;
  testTrue(g, fit > 0.0f && fit < 1.0f, "small window fits the entire palette");
  const float scale = fixture.renderer.getUiScale() * fit;
  pointAtPaintCard(fixture, 4, 2);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  testEqInt(g,
            CellGameModuleTestAccess::getWireworldBrush(fixture.module),
            2,
            "fitted coordinates select the visible tail swatch");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
  fixture.window.mouseX = 100.0;
  fixture.window.mouseY =
    240.0 -
    CellGameModuleTestAccess::getCellContext(fixture.module)
      ->getCanvasView()
      ->getBottomInsetPixels() -
    154.0 * scale;
  const SparseCellGrid* grid =
    CellGameModuleTestAccess::getCellContext(fixture.module)->getGrid();
  const std::uint64_t beforeClose = grid->getRevision();
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  testTrue(g,
           !CellGameModuleTestAccess::isPaintPaletteExpanded(fixture.module) &&
             visual.textCount() == 0u,
           "fitted header collapses into the wordless bubble");
  testTrue(g,
           grid->getRevision() == beforeClose,
           "snapping the tab closed cannot paint through its former position");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
  fixture.window.mouseY =
    240.0 -
    CellGameModuleTestAccess::getCellContext(fixture.module)
      ->getCanvasView()
      ->getBottomInsetPixels() -
    16.0 * scale;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  bool hasStateLabels = true;
  const RuleSet* wireworld =
    CellGameModuleTestAccess::getCellContext(fixture.module)->getRuleSet();
  for (unsigned char state = 0u; state < 4u; ++state) {
    bool found = false;
    for (std::size_t index = 0u; index < visual.textCount(); ++index) {
      found = found ||
              visual.getText(index)->content == wireworld->getStateName(state);
    }
    hasStateLabels = hasStateLabels && found;
  }
  testTrue(g,
           CellGameModuleTestAccess::isPaintPaletteExpanded(fixture.module) &&
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
            CellGameModuleTestAccess::getWireworldBrush(fixture.module),
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
  CSimSounds::resetCounts();
  fixture.window.mouseX = static_cast<double>(hx + hsize * 0.5f);
  fixture.window.mouseY = static_cast<double>(hy + hsize * 0.5f);
  fixture.module.Update(0.016);
  testTrue(g,
           CellGameModuleTestAccess::isHamburgerHovered(fixture.module),
           "hamburger is hovered when mouse is over it");
  fixture.module.Update(0.016);
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 1,
           "hovering the hamburger plays the hover cue once");

  // Click hamburger -> toggles settings open
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  testTrue(g, menu->isOpen(), "clicking hamburger opens settings menu");
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuSelect) == 1,
           "clicking hamburger plays the select cue once");
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuHover) == 1,
           "the click adds no hover cue");
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
  testTrue(g,
           CSimSounds::playCount(CSimSound::MenuSelect) == 1,
           "holding and closing do not repeat the select cue");
  CSimSounds::resetCounts();
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
             3u,
             "exit confirmation renders above the canvas and entrance veil");

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
                 !CellGameModuleTestAccess::load(fixture.module,
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
testCameraInputBounds()
{
  testSection("CellGameModule: camera input bounds");
  CellGameFixture fixture;
  const glm::dvec2 boundary(CanvasCoordinatePolicy::kMaximumWorld, 0.0);
  fixture.camera.SetPositionPrecise(boundary.x, boundary.y);
  fixture.camera.SetZoom(1.0f);
  fixture.window.mouseX = -1e9;
  fixture.window.mouseY = 240.0;
  InputManager::scrollCallback(nullptr, 0.0, -1.0);
  fixture.module.Update(0.0);
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
           CellGameModuleTestAccess::save(fixture.module, "camera-save.illumo"),
           "valid camera saves");
  const std::vector<char> saved = readFileBytes("camera-save.illumo");
  fixture.camera.SetPositionPrecise(1e300, 0.0);
  testTrue(
    g,
    !CellGameModuleTestAccess::save(fixture.module, "camera-save.illumo"),
    "unsafe direct camera cannot produce unloadable save");
  testTrue(g,
           readFileBytes("camera-save.illumo") == saved,
           "invalid camera save preserves destination");
}

static void
testSimulationFailureReporting()
{
  testSection(
    "CellGameModule: failed generations do not count and remain retryable");
  {
    CellGameFixture fixture;
    fixture.execute("ruleset", { "RULE_90" });
    SparseCellGrid* grid =
      CellGameModuleTestAccess::getCellContext(fixture.module)->getGrid();
    grid->clear();
    grid->setCell(
      CellAddress{ 0, std::numeric_limits<std::int64_t>::max() - 1 }, 0);
    fixture.execute("step", { "3" });
    testTrue(
      g,
      CellGameModuleTestAccess::getSimulationGeneration(fixture.module) == 1 &&
        CellGameModuleTestAccess::isSimulationRetryPending(fixture.module),
      "manual batch counts only its successful first generation");
    testTrue(g,
             historyContains(fixture.console, "failed after 1 of 3"),
             "manual failure reports requested and completed count");
    grid->clear();
    grid->setCell(CellAddress{ 0, 0 }, 0);
    fixture.execute("run");
    fixture.module.Update(0.0);
    testTrue(g,
             CellGameModuleTestAccess::isSimulationBusy(fixture.module),
             "run retries failed work without waiting another time step");
    CellGameModuleTestAccess::drainSimulation(fixture.module);
    testTrue(
      g,
      CellGameModuleTestAccess::getSimulationGeneration(fixture.module) == 2,
      "successful retry counts once");
  }
  {
    CellGameFixture fixture;
    fixture.execute("ruleset", { "RULE_90" });
    SparseCellGrid* grid =
      CellGameModuleTestAccess::getCellContext(fixture.module)->getGrid();
    grid->clear();
    grid->setCell(CellAddress{ 0, 0 }, 0);
    CellGameModuleTestAccess::getCellContext(fixture.module)
      ->getSpareGrid()
      ->setElementaryWriteFailureForTesting(1);
    const std::uint64_t revision = grid->getRevision();
    fixture.execute("run");
    fixture.module.Update(0.04);
    CellGameModuleTestAccess::drainSimulation(fixture.module);
    testTrue(
      g,
      CellGameModuleTestAccess::getSimulationGeneration(fixture.module) == 0 &&
        grid->getRevision() == revision &&
        CellGameModuleTestAccess::getState(fixture.module) == CellState::EDIT &&
        CellGameModuleTestAccess::isSimulationRetryPending(fixture.module),
      "async failure preserves published world and pauses with pending retry");
    testTrue(g,
             historyContains(fixture.console, "paused without publication"),
             "async failure has actionable console diagnostic");
    fixture.module.Update(0.25);
    testTrue(g,
             !CellGameModuleTestAccess::isSimulationBusy(fixture.module),
             "failure does not create an automatic retry loop");
    grid->clear();
    grid->setCell(CellAddress{ 0, 0 }, 0);
    fixture.execute("run");
    fixture.module.Update(0.0);
    CellGameModuleTestAccess::drainSimulation(fixture.module);
    testTrue(
      g,
      CellGameModuleTestAccess::getSimulationGeneration(fixture.module) == 1,
      "explicit async retry succeeds and counts once");
  }
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
  const std::string savePath = "async-transition.csim";

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

static void
testInputRegistrationLifetime()
{
  CellGameFixture fixture;
  fixture.module.Exit();
  fixture.started = false;
  for (int i = 0; i < NUM_INPUT_CONTEXTS * 2; ++i) {
    CellGameModule module;
    testTrue(
      g, module.Start(&fixture.context), "re-entry retains input capacity");
    testTrue(g,
             fixture.input.getActiveInputContext()->getActions().contains(
               "PaintCanvas"),
             "new module owns active bindings");
    module.Exit();
    module.Exit();
    testTrue(
      g, !fixture.input.isActionActive("PaintCanvas"), "exit retires bindings");
  }
  for (int i = 0; i < NUM_INPUT_CONTEXTS; ++i) {
    testTrue(g,
             fixture.input.registerInputContext(InputContext{}) >= 0,
             "all slots available after repeated exit");
  }
  InputContext* selected = fixture.input.getActiveInputContext();
  CellGameModule rejected;
  testTrue(
    g, !rejected.Start(&fixture.context), "full registry rejects startup");
  testTrue(g,
           CellGameModuleTestAccess::getCellContext(rejected) == nullptr,
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
    CellGameModuleTestAccess::getRulesetWorkshopMenu(fixture.module);
  const RuleSetDefinition* original =
    RuleSetRegistry::instance().getRuleSetDefinition("GAME_OF_LIFE");
  testTrue(g,
           menu != nullptr && original != nullptr,
           "rule workshop and built-in definition are available");
  fixture.input.getKeyQueue().push({ KeyCode::F2, InputAction::Press, 0 });
  fixture.module.Update(0.016);
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
  fixture.module.Update(0.016);
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
  fixture.module.Update(0.016);
  testTrue(g,
           menu->getValuePulseForTesting() > 0.0f,
           "editing a rule briefly highlights the changed value");
  testTrue(g,
           (menu->getDraft().birthMask & (1u << 3u)) == 0u,
           "enter toggles the selected birth count in the staged draft");
  const float selectionBefore = menu->getSelectionPositionForTesting();
  fixture.input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  fixture.module.Update(0.07);
  testTrue(
    g,
    menu->getSelectionPositionForTesting() > selectionBefore &&
      menu->getSelectionPositionForTesting() <
        static_cast<float>(menu->getControlIndexForTesting("Survival counts")),
    "workshop selection highlight glides between rows");
  fixture.input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g, !menu->isOpen(), "Escape discards the staged draft");
  testTrue(g,
           original->birthMask == (1u << 3u),
           "discard leaves the active catalog definition unchanged");

  CellGameFixture reducedMotionFixture;
  reducedMotionFixture.env.setVar("reducedUiMotion", true);
  RulesetWorkshopMenu* reducedMenu =
    CellGameModuleTestAccess::getRulesetWorkshopMenu(
      reducedMotionFixture.module);
  reducedMotionFixture.input.getKeyQueue().push(
    { KeyCode::F2, InputAction::Press, 0 });
  reducedMotionFixture.module.Update(0.016);
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
    CellGameModuleTestAccess::getRulesetWorkshopMenu(fixture.module);
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
    CellGameModuleTestAccess::getRulesetWorkshopMenu(fixture.module);
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
    CellGameModuleTestAccess::getRulesetWorkshopMenu(fixture.module);
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
  fixture.module.Update(0.016);
  CellContext* context =
    CellGameModuleTestAccess::getCellContext(fixture.module);
  testTrue(g,
           context != nullptr && context->getRuleSet()->getStateCount() == 4u,
           "Wireworld is active before testing a smaller custom rule");
  context->getGrid()->setCell(CellAddress{ 5, 5 }, 3u);

  RulesetWorkshopMenu* menu =
    CellGameModuleTestAccess::getRulesetWorkshopMenu(fixture.module);
  const RuleSetDefinition* wireworld =
    RuleSetRegistry::instance().getRuleSetDefinition("WIREWORLD");
  testTrue(g,
           menu != nullptr && wireworld != nullptr,
           "workshop and Wireworld definition are available");
  fixture.input.getKeyQueue().push({ KeyCode::F2, InputAction::Press, 0 });
  fixture.module.Update(0.016);
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
  fixture.module.Update(0.016);

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
        CellGameModuleTestAccess::getRulesetWorkshopMenu(fixture.module);
      const RuleSetDefinition* gameOfLife =
        RuleSetRegistry::instance().getRuleSetDefinition("GAME_OF_LIFE");
      testTrue(g,
               menu != nullptr && gameOfLife != nullptr,
               "workshop and source definition are available");
      fixture.input.getKeyQueue().push({ KeyCode::F2, InputAction::Press, 0 });
      fixture.module.Update(0.016);

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
      fixture.module.Update(0.016);

      CellContext* activeContext =
        CellGameModuleTestAccess::getCellContext(fixture.module);
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
        fixture.module.Update(0.016);
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
          fixture.module.Update(0.016);
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
        CellGameModuleTestAccess::getRulesetWorkshopMenu(fixture.module);
      CellContext* context =
        CellGameModuleTestAccess::getCellContext(fixture.module);
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
            fixture.module.Update(0.016);
            testTrue(g,
                     focusWorkshopControl(
                       *menu, fixture.input, "Import rules from JSON"),
                     "workshop import control can be selected");
            fixture.input.getKeyQueue().push(
              { KeyCode::Enter, InputAction::Press, 0 });
            fixture.module.Update(0.016);
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
            fixture.module.Update(0.016);
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

class CanvasReturnHost : public IModuleHost
{
public:
  int requests = 0;
  std::unique_ptr<IModule> next;
  void RequestTransition(std::unique_ptr<IModule> module) override
  {
    ++requests;
    next = std::move(module);
  }
  bool HasPendingTransition() const override { return next != nullptr; }
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
  CanvasReturnHost host;
  CellGameFixture fixture;
  testTrue(g,
           CSimSounds::playCount(CSimSound::CanvasEnter) == 1,
           "starting the canvas plays the enter cue");
  fixture.context.moduleHost = &host;
  GameVisual& veil =
    CellGameModuleTestAccess::getCanvasEntranceVisual(fixture.module);
  // The first frame is fully covered: every shape is one full, opaque cell.
  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  const float cellWidth = veil.getShape(0)->rect.w;
  const float cellHeight = veil.getShape(0)->rect.h;
  const std::size_t cellCount = veil.shapeCount();
  CellGameModuleTestAccess::advanceCanvasEntrance(fixture.module, 1.0);
  fixture.input.getKeyQueue().push({ KeyCode::Q, InputAction::Press, 0 });
  fixture.module.Update(0.0);
  fixture.input.getKeyQueue().push({ KeyCode::M, InputAction::Press, 0 });
  fixture.module.Update(0.0);
  testEqInt(
    g, host.requests, 0, "Main Menu waits for the canvas exit animation");
  testTrue(g,
           CSimSounds::playCount(CSimSound::CanvasExit) == 1,
           "leaving for the main menu plays the exit cue at once");
  fixture.module.Update(0.24);
  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  const VeilSummary closing = summarizeVeil(veil, cellWidth, cellHeight);
  testTrue(g,
           veil.isVisible() && closing.shapes > 0u &&
             closing.coveredCells > 0u && closing.coveredCells < cellCount,
           "canvas veil closes gradually before returning");
  fixture.execute("menu");
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.Update(0.25);
  testEqInt(g,
            host.requests,
            1,
            "repeated returns do not restart or duplicate the transition");
  testTrue(g,
           fixture.input.getKeyQueue().empty(),
           "exit input does not leak into the main menu");
  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  const VeilSummary covered = summarizeVeil(veil, cellWidth, cellHeight);
  testTrue(g,
           covered.allOpaque && covered.coveredCells == cellCount &&
             covered.shapes == cellCount,
           "canvas is covered when the module transition is requested");
  fixture.module.Update(1.0);
  testEqInt(g, host.requests, 1, "completed exit submits only once");
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
  fixture.module.Update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::None);
  fixture.module.Update(0.016);
}

static void
testCanvasModeSwitchSound()
{
  CellGameFixture fixture;
  CSimSounds::resetCounts();
  fixture.module.Update(0.016);
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
testReducedCanvasReturn()
{
  CanvasReturnHost host;
  CellGameFixture fixture;
  fixture.context.moduleHost = &host;
  fixture.env.setVar("reducedUiMotion", true);
  fixture.execute("menu");
  testEqInt(g,
            host.requests,
            1,
            "reduced motion returns immediately through the console path");
  fixture.execute("menu");
  testEqInt(g, host.requests, 1, "immediate return remains idempotent");
}

static void
testCanvasEntrance()
{
  CellGameFixture fixture;
  GameVisual& veil =
    CellGameModuleTestAccess::getCanvasEntranceVisual(fixture.module);
  fixture.module.DispatchDrawables(&fixture.scene);
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
  CellGameModuleTestAccess::advanceCanvasEntrance(fixture.module, 0.24);
  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
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
  fixture.module.DispatchDrawables(&fixture.scene);
  testTrue(g,
           veil.shapeCount() == shapesBefore &&
             summarizeVeil(veil, cellWidth, cellHeight).coveredCells ==
               coveredBefore,
           "drawing does not advance entrance time");
  fixture.env.setVar("uiScale", 4);
  fixture.window.handleResize(800, 600);
  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
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
  CellGameModuleTestAccess::advanceCanvasEntrance(fixture.module, 1.0);
  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  const std::vector<DrawableBase*>& ui =
    fixture.scene.drawablesIn(RenderLayerId::UI);
  testTrue(g,
           !veil.isVisible() &&
             std::find(ui.begin(), ui.end(), &veil) == ui.end(),
           "completed entrance is no longer submitted");
  fixture.module.Exit();
  fixture.env.setVar("reducedUiMotion", true);
  fixture.started = fixture.module.Start(&fixture.context);
  testTrue(g,
           fixture.started && !veil.isVisible(),
           "reduced motion skips entrance from the first frame");
}

void
registerCellGameModuleTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.CellGameModule.CanvasReturn",
               []() { return runCellGameModuleCase(testCanvasReturn); });
  registry.add("IllumoGame.CellGame.PaintPaletteSounds",
               []() { return runCellGameModuleCase(testPaintPaletteSounds); });
  registry.add("IllumoGame.CellGame.PaintPaletteCardSounds", []() {
    return runCellGameModuleCase(testPaintPaletteCardSounds);
  });
  registry.add("IllumoGame.CellGameModule.ModeSwitchSound", []() {
    return runCellGameModuleCase(testCanvasModeSwitchSound);
  });
  registry.add("IllumoGame.CellGameModule.ReducedCanvasReturn",
               []() { return runCellGameModuleCase(testReducedCanvasReturn); });

  registry.add("IllumoGame.CellGameModule.CanvasEntrance",
               []() { return runCellGameModuleCase(testCanvasEntrance); });

  registry.add("IllumoGame.CellGame.InputRegistrationLifetime", []() {
    return runCellGameModuleCase(testInputRegistrationLifetime);
  });
  registry.add("IllumoGame.CellGame.RulesetWorkshopF2", []() {
    return runCellGameModuleCase(testRulesetWorkshopF2Draft);
  });
  registry.add("IllumoGame.CellGame.RulesetWorkshopFamilyControls", []() {
    return runCellGameModuleCase(testRulesetWorkshopFamilyControls);
  });
  registry.add("IllumoGame.CellGame.RulesetWorkshopPointerNavigation", []() {
    return runCellGameModuleCase(testRulesetWorkshopPointerNavigation);
  });
  registry.add("IllumoGame.CellGame.RulesetWorkshopNavigation", []() {
    return runCellGameModuleCase(testRulesetWorkshopNavigation);
  });
  registry.add("IllumoGame.CellGame.RulesetWorkshopStateGuard", []() {
    return runCellGameModuleCase(testRulesetWorkshopRejectsRemovingLiveStates);
  });
  registry.add("IllumoGame.CellGame.RulesetWorkshopApply", []() {
    return runCellGameModuleCase(testRulesetWorkshopApplyPersistsAndActivates);
  });
  registry.add("IllumoGame.CellGame.RulesetWorkshopImportStateGuard", []() {
    return runCellGameModuleCase(
      testRulesetWorkshopImportRejectsReducingActiveStateRange);
  });
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
  registry.add("IllumoGame.CellGame.CyclicMultistateSeed", []() {
    return runCellGameModuleCase(testCyclicMultistateSeed);
  });
  registry.add("IllumoGame.CellGame.EveryShippedRuleStarts", []() {
    return runCellGameModuleCase(testEveryShippedRuleStarts);
  });
  registry.add("IllumoGame.CellGame.ResearchedStarterSeeds", []() {
    return runCellGameModuleCase(testResearchedStarterSeeds);
  });
  registry.add("IllumoGame.CellGame.SaveLoadRoundTrip",
               []() { return runCellGameModuleCase(testSaveLoadRoundTrip); });
  registry.add("IllumoGame.CellGame.SparseV2Compatibility", []() {
    return runCellGameModuleCase(testSparseV2Compatibility);
  });
  registry.add("IllumoGame.CellGame.ReleaseConfiguration", []() {
    return runCellGameModuleCase(testReleaseConfigurationWorkflow);
  });
  registry.add("IllumoGame.CellGame.PaintPalette",
               []() { return runCellGameModuleCase(testPaintPalette); });
  registry.add("IllumoGame.CellGame.SoftwareCursor",
               []() { return runCellGameModuleCase(testSoftwareCursor); });
  registry.add("IllumoGame.CellGame.ModeBadge",
               []() { return runCellGameModuleCase(testModeBadge); });
  registry.add("IllumoGame.CellGame.PaintPaletteBubbleMorph", []() {
    return runCellGameModuleCase(testPaintPaletteBubbleMorph);
  });
  registry.add("IllumoGame.CellGame.PaintPaletteFittedInput", []() {
    return runCellGameModuleCase(testPaintPaletteFittedInput);
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
  registry.add("IllumoGame.CellGame.SimulationFailureReporting", []() {
    return runCellGameModuleCase(testSimulationFailureReporting);
  });
  registry.add("IllumoGame.CellGame.CameraInputBounds",
               []() { return runCellGameModuleCase(testCameraInputBounds); });
  registry.add("IllumoGame.CellGame.AsyncTransitionDraining", []() {
    return runCellGameModuleCase(testAsyncTransitionDraining);
  });
}
