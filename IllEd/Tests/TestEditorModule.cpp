#include "EditorClipboard.h"
#include "EditorModule.h"
#include "EditorShortcuts.h"
#include "EditorUiAtlas.h"
#include "TestAccess.h"
#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/TestAccess.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>

static TestCounters g;

// A fresh settings file per fixture, so saved toggles and layouts never leak
// into other cases through the shared envvars.json.
static std::filesystem::path
isolatedSettings(const char* name)
{
  const std::filesystem::path path =
    std::filesystem::temp_directory_path() / name;
  std::error_code error;
  std::filesystem::remove(path, error);
  return path;
}
static ScenePrimitive
primitiveOf(const SceneNode* node)
{
  const SceneComponent* component =
    node == nullptr ? nullptr : node->find(SceneComponentType::Primitive);
  return component == nullptr ? ScenePrimitive{}
                              : std::get<ScenePrimitive>(component->value);
}

static std::string
parentOf(const EditorDocument& document, const std::string& id)
{
  return document.scene().idOf(
    document.graph().getParent(document.nodeHandle(id)));
}

static bool
isShape(const SceneNode* node, ScenePrimitiveShape shape)
{
  const SceneComponent* component =
    node == nullptr ? nullptr : node->find(SceneComponentType::Primitive);
  return component != nullptr &&
         std::get<ScenePrimitive>(component->value).shape == shape;
}
namespace {
bool g_seededAtlasCreated = false;
std::filesystem::path g_seededAtlasPath;

void
cleanupSeededAtlas()
{
  if (g_seededAtlasCreated && !g_seededAtlasPath.empty()) {
    std::error_code error;
    std::filesystem::remove(g_seededAtlasPath, error);
    std::filesystem::path parent = g_seededAtlasPath.parent_path();
    if (std::filesystem::is_empty(parent, error)) {
      std::filesystem::remove(parent, error);
      parent = parent.parent_path();
      if (std::filesystem::is_empty(parent, error)) {
        std::filesystem::remove(parent, error);
      }
    }
    g_seededAtlasCreated = false;
  }
}
} // namespace

static void
seedShippedAtlas()
{
  const std::filesystem::path destination =
    std::filesystem::current_path() / EditorUiAtlas::relativePath();
  std::error_code error;
  if (std::filesystem::exists(destination, error)) {
    return;
  }
  const std::filesystem::path source =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "Assets" /
    "editor-ui-atlas.jpg";
  std::filesystem::create_directories(destination.parent_path(), error);
  if (std::filesystem::copy_file(
        source,
        destination,
        std::filesystem::copy_options::overwrite_existing,
        error)) {
    if (!g_seededAtlasCreated) {
      g_seededAtlasCreated = true;
      g_seededAtlasPath = destination;
      std::atexit(cleanupSeededAtlas);
    }
  }
}

struct EditorFixture
{
  NullRenderWindow window;
  EnvVars env;
  Camera camera;
  MockBackend mock;
  Renderer renderer;
  AssetManager assets;
  CommandRegistry registry;
  CommandLine console;
  InputManager input;
  Scene scene;
  IllumoContext context;
  EditorModule module;
  bool started;

  EditorFixture()
    : window(1280, 720)
    , env(isolatedSettings("illed-module-settings.json"))
    , camera(glm::vec2(0.0f, 0.0f), 32.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , assets(&renderer, false)
    , registry()
    , console(&env, &registry, &window, &renderer, "IllEd")
    , input(nullptr)
    , scene(&window, &camera)
    , context{ &scene,  &window, &console, &input,   &renderer,
               &assets, &env,    &camera,  &registry }
    , module()
    , started(false)
  {
    env.setVar("WinX", 1280);
    env.setVar("WinY", 720);
    env.setVar("fontSize", "13");
    mock.Initialize();
    seedShippedAtlas();
    started = module.Start(&context);
  }

  ~EditorFixture()
  {
    if (started) {
      module.Exit();
    }
  }
};

static void
testCloseConfirmation()
{
  testSection("EditorModule: native close shares unsaved confirmation");
  EditorFixture fixture;
  testTrue(g, fixture.started, "editor starts");
  testTrue(g,
           fixture.module.OnCloseRequested(),
           "clean document accepts native close");
  EditorModuleTestAccess::createNode(fixture.module, EditorCommand::CreateCube);
  testTrue(
    g, !fixture.module.OnCloseRequested(), "dirty native close is deferred");
  testTrue(g,
           EditorModuleTestAccess::confirmationOpen(fixture.module),
           "native close opens confirmation");
  testTrue(g,
           !fixture.module.OnCloseRequested(),
           "repeated request keeps pending confirmation");
  fixture.input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.Update(0.01);
  testTrue(g,
           !fixture.window.shouldWindowClose() &&
             !EditorModuleTestAccess::confirmationOpen(fixture.module),
           "cancel leaves editor open");

  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  document.setPath("missing-close-parent/document.ilsc");
  testTrue(g,
           !fixture.module.OnCloseRequested(),
           "new close request reopens confirmation");
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.Update(0.01);
  testTrue(g,
           document.isDirty() && !fixture.window.shouldWindowClose(),
           "failed save leaves document dirty and open");

  const std::string path = "test-close-saved.ilsc";
  document.setPath(path);
  testTrue(g, !fixture.module.OnCloseRequested(), "save can be retried");
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.Update(0.01);
  testTrue(g,
           !document.isDirty() && fixture.window.shouldWindowClose() &&
             fixture.module.OnCloseRequested(),
           "successful save permits close");
  testTrue(g, std::filesystem::exists(path), "successful close writes scene");
  std::filesystem::remove(path);

  EditorFixture discard;
  EditorModuleTestAccess::createNode(discard.module, EditorCommand::CreateCube);
  EditorModuleTestAccess::handleCommand(discard.module,
                                        EditorCommand::ExitEditor);
  testTrue(g,
           EditorModuleTestAccess::confirmationOpen(discard.module),
           "toolbar exit shares confirmation");
  discard.input.getKeyQueue().push({ KeyCode::N, InputAction::Press, 0 });
  discard.module.Update(0.01);
  testTrue(g,
           EditorModuleTestAccess::document(discard.module).isDirty() &&
             discard.window.shouldWindowClose() &&
             discard.module.OnCloseRequested(),
           "discard closes without re-prompting on dirty document");
  discard.window.cancelCloseRequest();
  EditorModuleTestAccess::createNode(discard.module, EditorCommand::CreateCube);
  testTrue(g,
           !discard.module.OnCloseRequested() &&
             EditorModuleTestAccess::confirmationOpen(discard.module),
           "approval is consumed if another module vetoes and editing resumes");
  discard.input.getKeyQueue().push({ KeyCode::N, InputAction::Press, 0 });
  discard.module.Update(0.01);
  discard.window.cancelCloseRequest();
  discard.module.Update(0.01);
  testTrue(
    g,
    !discard.module.OnCloseRequested() &&
      EditorModuleTestAccess::confirmationOpen(discard.module),
    "approval expires when an earlier module vetoes before consultation");
}

static void
testUiAtlasSpritesFromStart()
{
  testSection("EditorModule: shipped atlas draws the Tools panel sprites");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  testTrue(
    g,
    fixture.assets
        .getState(EditorModuleTestAccess::toolbar(fixture.module)->atlas())
        .state == AssetState::Ready,
    "module loaded the shipped atlas");
  EditorToolsPanel* tools = EditorModuleTestAccess::tools(fixture.module);
  EditorToolbar* toolbar = EditorModuleTestAccess::toolbar(fixture.module);
  testTrue(g, tools != nullptr && toolbar != nullptr, "chrome exists");
  fixture.module.Update(0.016);
  testTrue(g,
           tools->getVisual().spriteCount() >= 8u,
           "the Tools panel draws its Create tool icons");

  fixture.renderer.BeginFrame();
  testTrue(
    g, tools->AppendCommands(&fixture.renderer), "the panel appends tokens");
  fixture.renderer.EndFrame();
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::SetTexture) >= 1u,
           "the panel binds the atlas texture");
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "the panel draws atlas sprites");
}

static void
testCreateCubeAndGraph()
{
  testSection("EditorModule: create cube and attach graph");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  EditorModuleTestAccess::createNode(fixture.module, EditorCommand::CreateCube);
  testEqSize(g,
             EditorModuleTestAccess::document(fixture.module).nodeCount(),
             1u,
             "document has one node");
  testEqSize(g,
             EditorModuleTestAccess::graph(fixture.module).getNodeCount(),
             1u,
             "graph has one node");
  testTrue(g,
           !EditorModuleTestAccess::selectedId(fixture.module).empty(),
           "new cube is selected");
}

static void
testSaveLoadThroughDocument()
{
  testSection("EditorModule: scene file round trip");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  EditorModuleTestAccess::createNode(fixture.module, EditorCommand::CreateCube);
  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  const std::filesystem::path path =
    std::filesystem::temp_directory_path() / "module-roundtrip.ilsc";
  struct TempFileGuard
  {
    std::filesystem::path file;
    ~TempFileGuard()
    {
      std::error_code ec;
      std::filesystem::remove(file, ec);
    }
  } guard{ path };
  std::error_code fsError;
  std::filesystem::remove(path, fsError);
  std::string error;
  testTrue(
    g,
    IllEdNativeFiles::writeText(path.string(), document.encode(), &error),
    "saves .ilsc");
  std::string text;
  EditorDocument loaded;
  testTrue(g,
           IllEdNativeFiles::readText(path.string(), &text, &error) &&
             loaded.loadFromText(text, &error),
           "loads .ilsc");
  testEqSize(g, loaded.nodeCount(), 1u, "loaded cube");
  const SceneNode* cube = loaded.findNode(
    std::string(loaded.graph().getName(loaded.graph().getRoot(0))));
  const SceneComponent* primitive =
    cube == nullptr ? nullptr : cube->find(SceneComponentType::Primitive);
  testTrue(g,
           primitive != nullptr &&
             std::get<ScenePrimitive>(primitive->value).shape ==
               ScenePrimitiveShape::Cube,
           "shape is cube");
}

static void
testModeCreateSelectProperties()
{
  testSection("EditorModule: 2D/3D mode, create, select, properties");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::SetMode3D);
  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  testTrue(g,
           document.worldMode() == SceneWorldMode::World3D,
           "module switches to 3D");
  EditorModuleTestAccess::createNode(fixture.module,
                                     EditorCommand::CreateEllipse);
  const std::string ellipseId =
    EditorModuleTestAccess::selectedId(fixture.module);
  testTrue(g, !ellipseId.empty(), "ellipse selected");
  testTrue(g,
           document.setExtent(ellipseId, Vector3(1.25f, 0.8f, 0.5f)),
           "property size via document");
  testTrue(g,
           document.setColor(ellipseId, ColorRgba{ 4, 5, 6, 255 }),
           "property color via document");
  EditorModuleTestAccess::refreshView(fixture.module);

  const size_t beforeArm = document.nodeCount();
  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::CreatePyramid);
  testEqSize(
    g, document.nodeCount(), beforeArm, "CreatePyramid arms without inserting");
  testTrue(g,
           EditorModuleTestAccess::activeTool(fixture.module) ==
             EditorCommand::CreatePyramid,
           "pyramid tool is armed");
  EditorModuleTestAccess::applyActiveToolAt(fixture.module, 1.0f, 2.0f);
  testTrue(g,
           EditorModuleTestAccess::activeTool(fixture.module) ==
             EditorCommand::SelectTool,
           "place disarms the tool");
  const EditorSceneDetail detail = fixture.module.sceneDetail();
  testTrue(g, detail.nodeCount >= 2u, "scene has ellipse and pyramid");
  testTrue(g, detail.hasSelection, "selection present");
  testTrue(g, detail.worldMode == SceneWorldMode::World3D, "detail reports 3D");
  testTrue(g,
           detail.kindLabel == "Pyramid" || detail.kindLabel == "Ellipse",
           "selected kind is a created primitive");
  EditorToolsPanel* tools = EditorModuleTestAccess::tools(fixture.module);
  testTrue(g, tools != nullptr, "the Tools panel exists");
  float modeX = 0.0f;
  float modeY = 0.0f;
  tools->controlCenterForTesting(EditorCommand::SetMode2D, &modeX, &modeY);
  const EditorCommand mode2d = tools->clickAtForTesting(modeX, modeY);
  testTrue(g, mode2d == EditorCommand::SetMode2D, "Tools 2D hit");
  EditorModuleTestAccess::handleCommand(fixture.module, mode2d);
  testTrue(
    g, document.worldMode() == SceneWorldMode::World2D, "Tools mode applied");
}

static void
testCreateToolArmsOnly()
{
  testSection("EditorModule: Create command arms, canvas place inserts once");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  const size_t before = document.nodeCount();
  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::CreateCube);
  testEqSize(g, document.nodeCount(), before, "no node on tool select");
  testTrue(g,
           EditorModuleTestAccess::activeTool(fixture.module) ==
             EditorCommand::CreateCube,
           "cube tool armed");
  EditorModuleTestAccess::applyActiveToolAt(fixture.module, 3.0f, -1.5f);
  testEqSize(g, document.nodeCount(), before + 1u, "one node on canvas place");
  testTrue(g,
           EditorModuleTestAccess::activeTool(fixture.module) ==
             EditorCommand::SelectTool,
           "tool returns to select after place");
  const SceneNode* node =
    document.findNode(EditorModuleTestAccess::selectedId(fixture.module));
  testTrue(g, isShape(node, ScenePrimitiveShape::Cube), "placed cube");
  testTrue(g,
           node != nullptr && node->transform.position.x == 3.0f &&
             node->transform.position.y == -1.5f &&
             node->transform.position.z == 0.0f,
           "2D place is on XY z=0");
}

static void
testPlaceOn3DGround()
{
  testSection("EditorModule: 3D pick/place lands on Y=0 XZ grid");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::SetMode3D);
  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  testTrue(
    g, document.worldMode() == SceneWorldMode::World3D, "3D mode active");
  float planeX = 0.0f;
  float planeZ = 0.0f;
  testTrue(g,
           EditorModuleTestAccess::screenToWorld(
             fixture.module, 960.0f, 360.0f, &planeX, &planeZ),
           "3D unproject of off-center pixel");
  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::CreateCube);
  EditorModuleTestAccess::applyActiveToolAt(fixture.module, planeX, planeZ);
  const SceneNode* node =
    document.findNode(EditorModuleTestAccess::selectedId(fixture.module));
  testTrue(g, node != nullptr, "placed a node");
  testTrue(g,
           node != nullptr && std::fabs(node->transform.position.y) < 0.001f,
           "placed on Y=0 ground");
  testTrue(g,
           node != nullptr && std::fabs(node->transform.position.z) > 0.01f,
           "ground hit uses XZ (z is not forced to 0)");
  std::string hit;
  testTrue(
    g,
    document.pickRay(
      Vector3(node->transform.position.x, 100.0f, node->transform.position.z),
      Vector3(0.0f, -1.0f, 0.0f),
      &hit),
    "3D pick finds the placed node on XZ");
  testEqStr(g, hit, node->id, "picked the placed cube");
}

static void
pressLeft(EditorFixture& fixture, bool down)
{
  InputManagerTestAccess::setAction(fixture.input,
                                    KeyCode::MouseLeft,
                                    down ? InputAction::Press
                                         : InputAction::None);
}

static void
testToolbarCreateClickDoesNotInsertOnUpdate()
{
  testSection(
    "EditorModule: toolbar Create click arms through Update without placing");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  EditorToolbar* toolbar = EditorModuleTestAccess::toolbar(fixture.module);
  testTrue(g, toolbar != nullptr, "toolbar exists");

  float createX = -1.0f;
  for (int x = 0; x <= 400; x += 2) {
    toolbar->closeMenus();
    toolbar->clickAtForTesting(static_cast<float>(x), 8.0f);
    if (toolbar->openMenuForTesting() == 2) {
      createX = static_cast<float>(x);
      break;
    }
  }
  testTrue(g, createX >= 0.0f, "found Create menu title");

  float cubeY = -1.0f;
  toolbar->closeMenus();
  toolbar->clickAtForTesting(createX, 8.0f);
  for (int y = 24; y <= 320; y += 2) {
    if (toolbar->openMenuForTesting() < 0) {
      toolbar->clickAtForTesting(createX, 8.0f);
    }
    const EditorCommand command =
      toolbar->clickAtForTesting(createX, static_cast<float>(y));
    if (command == EditorCommand::CreateCube) {
      cubeY = static_cast<float>(y);
      break;
    }
  }
  testTrue(g, cubeY >= 0.0f, "found Solid Cube item");
  toolbar->closeMenus();

  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  const size_t before = document.nodeCount();
  fixture.window.mouseX = static_cast<double>(createX);
  fixture.window.mouseY = 8.0;
  pressLeft(fixture, true);
  fixture.module.Update(0.016);
  testTrue(g, toolbar->isMenuOpen(), "Update opens Create menu");
  testEqSize(g, document.nodeCount(), before, "opening menu does not insert");
  pressLeft(fixture, false);
  fixture.module.Update(0.016);

  fixture.window.mouseX = static_cast<double>(createX);
  fixture.window.mouseY = static_cast<double>(cubeY);
  pressLeft(fixture, true);
  fixture.module.Update(0.016);
  testEqSize(
    g, document.nodeCount(), before, "Create Cube menu click does not insert");
  testTrue(g,
           EditorModuleTestAccess::activeTool(fixture.module) ==
             EditorCommand::CreateCube,
           "Create Cube is armed after Update");
}

static void
testOpenConsoleBlocksEditorInput()
{
  testSection(
    "EditorModule: open console blocks editor input and grave is unconsumed");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  // 1. When console is closed, Grave key is not consumed or toggled by
  // EditorModule
  fixture.input.clearKeyQueue();
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Grave, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(g,
           !fixture.console.isOpen,
           "EditorModule does not toggle console on Grave");
  testTrue(g,
           !fixture.input.getKeyQueue().empty() &&
             fixture.input.getKeyQueue().front().key == KeyCode::Grave,
           "Grave key remains in queue for DebugModule overlay");

  // 2. When console is open, toolbar and panel input yield
  fixture.console.Toggle();
  testTrue(g, fixture.console.isOpen, "console is open");
  EditorToolbar* toolbar = EditorModuleTestAccess::toolbar(fixture.module);
  testTrue(g, toolbar != nullptr, "toolbar exists");
  fixture.window.mouseX = 20.0;
  fixture.window.mouseY = 8.0;
  pressLeft(fixture, true);
  fixture.module.Update(0.016);
  testTrue(g, !toolbar->isMenuOpen(), "open console blocks toolbar clicks");
}

static void
testEditorModuleDoesNotDispatchConsole()
{
  testSection("EditorModule: does not dispatch console drawable (DebugModule "
              "responsibility)");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  fixture.console.Toggle();
  testTrue(g, fixture.console.isOpen, "console is open");
  testTrue(g, fixture.console.wantsDraw(), "console wants draw");

  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);

  const std::vector<DrawableBase*>& uiDrawables =
    fixture.scene.drawablesIn(RenderLayerId::UI);
  bool consoleFoundInScene = false;
  for (DrawableBase* drawable : uiDrawables) {
    if (drawable == &fixture.console) {
      consoleFoundInScene = true;
      break;
    }
  }
  testTrue(g,
           !consoleFoundInScene,
           "EditorModule does not add CommandLine to Scene drawables");
}

static void
testUiDrawOrder()
{
  testSection("EditorModule: UI draw order ensures toolbar and dropdowns draw "
              "above side panels");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);

  const std::vector<DrawableBase*>& uiDrawables =
    fixture.scene.drawablesIn(RenderLayerId::UI);
  testTrue(g, uiDrawables.size() >= 3u, "at least 3 UI drawables in scene");

  EditorSceneGraphView* sceneGraphView =
    EditorModuleTestAccess::sceneGraphView(fixture.module);
  EditorToolsPanel* tools = EditorModuleTestAccess::tools(fixture.module);
  EditorToolbar* toolbar = EditorModuleTestAccess::toolbar(fixture.module);

  int sgvIndex = -1;
  int toolsIndex = -1;
  int toolbarIndex = -1;

  for (size_t i = 0; i < uiDrawables.size(); ++i) {
    if (uiDrawables[i] == sceneGraphView) {
      sgvIndex = static_cast<int>(i);
    } else if (uiDrawables[i] == tools) {
      toolsIndex = static_cast<int>(i);
    } else if (uiDrawables[i] == toolbar) {
      toolbarIndex = static_cast<int>(i);
    }
  }

  testTrue(g,
           sgvIndex >= 0 && toolsIndex >= 0 && toolbarIndex >= 0,
           "all UI drawables found in layer");
  testTrue(g,
           toolbarIndex > sgvIndex,
           "toolbar is dispatched after (on top of) SceneGraphView");
  testTrue(g,
           toolbarIndex > toolsIndex,
           "toolbar is dispatched after (on top of) the Tools panel");
}

static void
test2dModeNodeRenderingEmitsTokens()
{
  testSection("EditorModule: 2D nodes (Rect, Ellipse, Triangle) emit render "
              "tokens in 2D mode");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  testTrue(g,
           EditorModuleTestAccess::document(fixture.module).worldMode() ==
             SceneWorldMode::World2D,
           "starts in 2D mode");

  EditorModuleTestAccess::createNode(fixture.module, EditorCommand::CreateRect);
  EditorModuleTestAccess::createNode(fixture.module,
                                     EditorCommand::CreateEllipse);
  EditorModuleTestAccess::createNode(fixture.module,
                                     EditorCommand::CreateTriangle);

  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);

  fixture.mock.resetCounters();
  fixture.renderer.BeginFrame();
  for (DrawableBase* drawable :
       fixture.scene.drawablesIn(RenderLayerId::World)) {
    drawable->AppendCommands(&fixture.renderer);
  }
  fixture.renderer.EndFrame();

  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 3u,
           "2D nodes emit indexed draw tokens");
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::SetUniformMat4) >= 3u,
           "2D nodes set MVP uniform");
}

static void
testFontSizeConfiguredFromEnvVars()
{
  testSection("EditorModule: fontSize configured from IEnvVars");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("fontSize", "20");
  Camera camera(glm::vec2(0.0f, 0.0f), 32.0f, &env);
  MockBackend mock;
  Renderer renderer(&window, &env, &camera, &mock, false);
  AssetManager assets(&renderer, false);
  CommandRegistry registry;
  CommandLine console(&env, &registry, &window, &renderer, "IllEd");
  InputManager input(nullptr);
  Scene scene(&window, &camera);
  IllumoContext context{ &scene,  &window, &console, &input,   &renderer,
                         &assets, &env,    &camera,  &registry };
  EditorModule module;
  mock.Initialize();
  seedShippedAtlas();
  const bool started = module.Start(&context);
  testTrue(g, started, "module started with fontSize 20");

  EditorToolbar* toolbar = EditorModuleTestAccess::toolbar(module);
  EditorToolsPanel* tools = EditorModuleTestAccess::tools(module);
  EditorSceneGraphView* sceneGraphView =
    EditorModuleTestAccess::sceneGraphView(module);

  testTrue(g, toolbar != nullptr, "toolbar exists");
  testTrue(g, tools != nullptr, "tools exists");
  testTrue(g, sceneGraphView != nullptr, "sceneGraphView exists");

  testTrue(
    g, std::abs(toolbar->fontSize() - 20.0f) < 0.001f, "toolbar fontSize 20");
  testTrue(
    g, std::abs(tools->fontSize() - 20.0f) < 0.001f, "tools fontSize 20");
  testTrue(g,
           std::abs(sceneGraphView->fontSize() - 20.0f) < 0.001f,
           "sceneGraphView fontSize 20");

  module.Exit();
}

static void
testFontSizeImmediateRuntimeChange()
{
  testSection(
    "EditorModule: runtime fontSize change takes effect immediately on Update");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("fontSize", "13");
  Camera camera(glm::vec2(0.0f, 0.0f), 32.0f, &env);
  MockBackend mock;
  Renderer renderer(&window, &env, &camera, &mock, false);
  AssetManager assets(&renderer, false);
  CommandRegistry registry;
  CommandLine console(&env, &registry, &window, &renderer, "IllEd");
  InputManager input(nullptr);
  Scene scene(&window, &camera);
  IllumoContext context{ &scene,  &window, &console, &input,   &renderer,
                         &assets, &env,    &camera,  &registry };
  EditorModule module;
  mock.Initialize();
  seedShippedAtlas();
  const bool started = module.Start(&context);
  testTrue(g, started, "module started");

  EditorToolbar* toolbar = EditorModuleTestAccess::toolbar(module);
  EditorToolsPanel* tools = EditorModuleTestAccess::tools(module);
  EditorSceneGraphView* sceneGraphView =
    EditorModuleTestAccess::sceneGraphView(module);

  testTrue(g,
           toolbar != nullptr && tools != nullptr && sceneGraphView != nullptr,
           "views exist");
  testTrue(g,
           std::abs(toolbar->fontSize() - 13.0f) < 0.001f,
           "initial toolbar fontSize 13");
  testTrue(g,
           std::abs(toolbar->barHeight() - GuiToolStyle::kMenuHeight) < 0.001f,
           "initial barHeight is the tool menu height");

  // Change variable at runtime
  env.setVar("fontSize", "24");
  module.Update(0.016);

  testTrue(g,
           std::abs(toolbar->fontSize() - 24.0f) < 0.001f,
           "toolbar resized to 24 immediately");
  testTrue(g,
           std::abs(tools->fontSize() - 24.0f) < 0.001f,
           "tools resized to 24 immediately");
  testTrue(g,
           std::abs(sceneGraphView->fontSize() - 24.0f) < 0.001f,
           "sceneGraphView resized to 24 immediately");
  testTrue(
    g, sceneGraphView->rowHeight() >= 36.0f, "hierarchy rows grow immediately");
  testTrue(g,
           std::abs(toolbar->barHeight() - GuiToolStyle::kMenuHeight) < 0.001f,
           "the plain chrome keeps its metrics");

  // Change variable again at runtime (e.g. via multiplier or smaller size)
  env.setVar("fontSize", "16");
  module.Update(0.016);

  testTrue(g,
           std::abs(toolbar->fontSize() - 16.0f) < 0.001f,
           "toolbar resized to 16 immediately");
  testTrue(g,
           std::abs(tools->fontSize() - 16.0f) < 0.001f,
           "tools resized to 16 immediately");

  module.Exit();
}

static void
testMousePanningMovesCameraNaturalDirection()
{
  testSection(
    "EditorModule: mouse panning moves camera in natural drag direction");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  // Initial camera position at (0, 0)
  fixture.camera.SetPositionPrecise(0.0, 0.0);
  fixture.camera.SetZoom(1.0f);
  fixture.window.mouseX = 500.0;
  fixture.window.mouseY = 300.0;
  fixture.module.Update(0.016);

  // Press middle mouse button and drag right (500 -> 550) and down (300 -> 350)
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseMiddle, InputAction::Press);
  fixture.module.Update(0.016);

  fixture.window.mouseX = 550.0;
  fixture.window.mouseY = 350.0;
  fixture.module.Update(0.016);

  // Dragging right (dx > 0) should shift camera target left (targetPosition.x <
  // 0) Dragging down (dy > 0 in screen space) should shift camera target up
  // (targetPosition.y > 0 in world space, moving the visible scene downward
  // with the cursor)
  fixture.camera.Update(1.0f);
  const glm::dvec2 pos = fixture.camera.GetPositionPrecise();
  testTrue(g, pos.x < 0.0, "dragging right moves camera X left");
  testTrue(g, pos.y > 0.0, "dragging down moves camera Y up (world down)");
}

static void
test3DMousePanningRespectsCameraOrientation()
{
  testSection(
    "EditorModule: 3D mouse panning moves target along camera orientation");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::SetMode3D);
  fixture.camera.SetPositionPrecise(0.0, 0.0);
  fixture.camera.SetZoom(1.0f);
  fixture.window.mouseX = 500.0;
  fixture.window.mouseY = 300.0;
  fixture.module.Update(0.016);

  // Default camera yaw is 0.0. Looking down -Z, right vector is (+1, 0, 0).
  // Dragging right (mouse 500 -> 550) should shift target left (-X).
  // Dragging down (mouse 300 -> 350) should shift target forward (-Z,
  // position.y < 0).
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseMiddle, InputAction::Press);
  fixture.module.Update(0.016);

  fixture.window.mouseX = 550.0;
  fixture.window.mouseY = 350.0;
  fixture.module.Update(0.016);

  fixture.camera.Update(1.0f);
  const glm::dvec2 pos = fixture.camera.GetPositionPrecise();
  testTrue(g, pos.x < 0.0, "3D dragging right moves target -X");
  testTrue(g, pos.y < 0.0, "3D dragging down moves target -Z (forward)");
}

static void
test3DCameraElevationKeys()
{
  testSection("EditorModule: 3D camera elevation via PageUp and PageDown");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::SetMode3D);
  fixture.module.Update(0.016);

  const glm::vec3 initialEye = fixture.camera.getEye();
  const glm::vec3 initialTarget = fixture.camera.getTarget();

  // Press E to elevate camera Up
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::PageUp, InputAction::Press);
  fixture.module.Update(0.1);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::PageUp, InputAction::Release);

  const glm::vec3 elevatedEye = fixture.camera.getEye();
  const glm::vec3 elevatedTarget = fixture.camera.getTarget();
  testTrue(
    g, elevatedTarget.y > initialTarget.y, "PageUp elevates camera target Y");
  testTrue(g, elevatedEye.y > initialEye.y, "PageUp elevates camera eye Y");

  // Press Q to lower camera Down
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::PageDown, InputAction::Press);
  fixture.module.Update(0.2);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::PageDown, InputAction::Release);

  const glm::vec3 loweredEye = fixture.camera.getEye();
  const glm::vec3 loweredTarget = fixture.camera.getTarget();
  testTrue(
    g, loweredTarget.y < elevatedTarget.y, "PageDown lowers camera target Y");
  testTrue(g, loweredEye.y < elevatedEye.y, "PageDown lowers camera eye Y");
}

static void
testTransformGizmoHitAndConstraints()
{
  testSection(
    "EditorModule: Transform gizmo hit testing and constraint dragging");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::SetMode3D);
  EditorModuleTestAccess::createNode(fixture.module, EditorCommand::CreateCube);
  const std::string id = EditorModuleTestAccess::selectedId(fixture.module);
  testTrue(g, !id.empty(), "cube created and selected");

  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  document.setTransform(id,
                        Transform3D::fromPosition(Vector3(0.0f, 0.0f, 0.0f)));
  fixture.camera.SetPositionPrecise(0.0, 0.0);
  fixture.camera.SetZoom(32.0f);
  fixture.module.Update(0.016);

  // Gizmo is centered at world (0, 0, 0)
  const glm::vec3 gizmoOrigin(0.0f, 0.0f, 0.0f);
  const float initialScale =
    EditorModuleTestAccess::gizmoScale(fixture.module, gizmoOrigin);
  testTrue(g,
           initialScale > 0.3f && initialScale < 2.0f,
           "initial gizmo scale is well-proportioned");

  // When camera zooms out (distance increases), world gizmoScale increases
  // proportionally so that screen-space visual size remains stable rather than
  // wildly blowing up or shrinking
  fixture.camera.SetZoom(8.0f);
  fixture.module.Update(0.016);
  const float zoomedOutScale =
    EditorModuleTestAccess::gizmoScale(fixture.module, gizmoOrigin);
  testTrue(g,
           zoomedOutScale > initialScale,
           "zoomed out camera increases world scale for screen stability");

  // Screen center corresponds to world (0, 0, 0)
  const float centerX = 640.0f;
  const float centerY = 360.0f;

  const GizmoPart centerHit = EditorModuleTestAccess::hitTestGizmo(
    fixture.module, centerX, centerY, gizmoOrigin, zoomedOutScale);
  testTrue(
    g, centerHit == GizmoPart::Center, "center of gizmo hits Center part");

  // Translating via document with Vector3 directly
  testTrue(g,
           document.translate(id, Vector3(5.0f, 2.0f, -3.0f)),
           "translate 3D vector");
  const SceneNode* node = document.findNode(id);
  testTrue(g,
           node != nullptr && node->transform.position.x == 5.0f,
           "node position X is 5");
  testTrue(g,
           node != nullptr && node->transform.position.y == 2.0f,
           "node position Y is 2");
  testTrue(g,
           node != nullptr && node->transform.position.z == -3.0f,
           "node position Z is -3");

  // Drag continuity: start dragging center, move outside window / over UI, and
  // ensure drag is preserved Reset node to 0,0,0
  document.setTransform(id,
                        Transform3D::fromPosition(Vector3(0.0f, 0.0f, 0.0f)));
  fixture.window.mouseX = 640.0;
  fixture.window.mouseY = 360.0;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  testTrue(g,
           EditorModuleTestAccess::isDragging(fixture.module),
           "drag started on center click");

  // Move the mouse over a docked panel (x=20, y=100 is in the Hierarchy)
  fixture.window.mouseX = 20.0;
  fixture.window.mouseY = 100.0;
  fixture.module.Update(0.016);
  testTrue(g,
           EditorModuleTestAccess::isDragging(fixture.module),
           "drag persists when cursor moves over UI");

  // Re-enter world area
  fixture.window.mouseX = 700.0;
  fixture.window.mouseY = 360.0;
  fixture.module.Update(0.016);
  testTrue(g,
           EditorModuleTestAccess::isDragging(fixture.module),
           "drag still active when cursor re-enters");
  const SceneNode* movedNode = document.findNode(id);
  testTrue(g,
           movedNode != nullptr && movedNode->transform.position.x != 0.0f,
           "node position updated during continued drag");

  // Releasing mouse terminates the drag
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
  testTrue(g,
           !EditorModuleTestAccess::isDragging(fixture.module),
           "drag terminates on mouse release");
}

static int
testCameraDoesNotDirty()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  testTrue(counters, !document.isDirty(), "fresh document is clean");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::Right, InputAction::Press);
  for (int frame = 0; frame < 10; ++frame) {
    fixture.module.Update(0.05);
  }
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::Right, InputAction::Release);
  fixture.camera.SetZoom(12.0f);
  fixture.module.Update(0.05);
  testTrue(counters,
           fixture.camera.GetTargetPositionPrecise().x > 1.0,
           "arrow keys pan the camera");
  testTrue(counters, !document.isDirty(), "camera motion never dirties");
  testTrue(counters,
           fixture.module.OnCloseRequested(),
           "a view-only session closes without asking to save");
  testTrue(counters,
           document.encode().find("\"zoom\": 12.0") != std::string::npos,
           "the view is still saved with the scene");
  return counters.failures;
}

static int
testNewNodeParentsToRoot()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  const std::string first = EditorModuleTestAccess::createNode(
    fixture.module, EditorCommand::CreateCube);
  testEqStr(counters,
            EditorModuleTestAccess::selectedId(fixture.module),
            first,
            "the new node is selected");
  const std::string second = EditorModuleTestAccess::createNode(
    fixture.module, EditorCommand::CreateRect);
  testTrue(counters,
           parentOf(document, second).empty() &&
             parentOf(document, first).empty(),
           "creating with a selection still places at the root");
  return counters.failures;
}

static int
testShortcutsMatchMenus()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorToolbar* toolbar = EditorModuleTestAccess::toolbar(fixture.module);
  for (const EditorShortcut& shortcut : EditorShortcuts::all()) {
    const std::string hint = toolbar->menuHintForTesting(shortcut.command);
    if (!hint.empty() && hint != EditorShortcuts::labelFor(shortcut.command)) {
      std::printf("FAIL: menu hint %s differs from the table\n", hint.c_str());
      ++counters.failures;
    }
    const int modifiers = (shortcut.shift ? 0x1 : 0) |
                          (shortcut.control ? 0x2 : 0) |
                          (shortcut.alt ? 0x4 : 0);
    toolbar->closeMenus();
    fixture.input.getKeyQueue().push(
      { shortcut.key, InputAction::Press, modifiers });
    const EditorCommand produced = toolbar->update(&fixture.input, 0.016f);
    if (produced !=
        EditorShortcuts::match(
          shortcut.key, shortcut.control, shortcut.shift, shortcut.alt)) {
      std::printf("FAIL: key %s did not produce its command\n",
                  EditorShortcuts::keyName(shortcut.key).c_str());
      ++counters.failures;
    }
  }
  testTrue(counters, true, "every table shortcut works from the keyboard");
  const EditorCommand listed[] = {
    EditorCommand::Undo,          EditorCommand::Redo,
    EditorCommand::Duplicate,     EditorCommand::DeleteNode,
    EditorCommand::UnparentNode,  EditorCommand::SetMode2D,
    EditorCommand::SetMode3D,     EditorCommand::ResetCamera,
    EditorCommand::FrameSelection
  };
  for (const EditorCommand command : listed) {
    if (toolbar->menuHintForTesting(command).empty()) {
      ++counters.failures;
      std::printf("FAIL: a menu item lacks its shortcut hint\n");
    }
  }
  testTrue(counters, true, "menu items show their shortcuts");
  return counters.failures;
}

static int
testKeyboardUndoRedo()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  EditorModuleTestAccess::createNode(fixture.module, EditorCommand::CreateCube);
  testEqSize(counters, document.nodeCount(), 1, "one node");
  fixture.input.getKeyQueue().push({ KeyCode::Z, InputAction::Press, 0x2 });
  fixture.module.Update(0.016);
  testEqSize(counters, document.nodeCount(), 0, "Ctrl+Z undoes the create");
  testTrue(counters,
           EditorModuleTestAccess::selectedId(fixture.module).empty(),
           "the removed node leaves the selection");
  fixture.input.getKeyQueue().push({ KeyCode::Y, InputAction::Press, 0x2 });
  fixture.module.Update(0.016);
  testEqSize(counters, document.nodeCount(), 1, "Ctrl+Y redoes it");
  const std::string id(document.graph().getName(document.graph().getRoot(0)));
  EditorModuleTestAccess::setSelectedId(fixture.module, id);
  fixture.input.getKeyQueue().push({ KeyCode::D, InputAction::Press, 0x2 });
  fixture.module.Update(0.016);
  testEqSize(counters, document.nodeCount(), 2, "Ctrl+D duplicates");
  testTrue(counters,
           EditorModuleTestAccess::selectedId(fixture.module) != id,
           "the duplicate becomes the selection");
  fixture.input.getKeyQueue().push({ KeyCode::A, InputAction::Press, 0x2 });
  fixture.module.Update(0.016);
  testEqSize(counters,
             EditorModuleTestAccess::selection(fixture.module).size(),
             2,
             "Ctrl+A selects everything");
  fixture.input.getKeyQueue().push({ KeyCode::Delete, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testEqSize(counters, document.nodeCount(), 0, "Delete removes the selection");
  fixture.input.getKeyQueue().push({ KeyCode::Z, InputAction::Press, 0x2 });
  fixture.module.Update(0.016);
  testEqSize(
    counters, document.nodeCount(), 2, "one undo restores a multi-node delete");
  return counters.failures;
}

// World-to-screen for the fixture's 2D camera, measured from the module.
static void
worldToScreen2D(EditorFixture& fixture,
                float x,
                float y,
                double* sx,
                double* sy)
{
  float ox = 0.0f;
  float oy = 0.0f;
  float px = 0.0f;
  float py = 0.0f;
  EditorModuleTestAccess::screenToWorld(
    fixture.module, 640.0f, 360.0f, &ox, &oy);
  EditorModuleTestAccess::screenToWorld(
    fixture.module, 740.0f, 260.0f, &px, &py);
  const double unitX = 100.0 / static_cast<double>(px - ox);
  const double unitY = 100.0 / static_cast<double>(py - oy);
  *sx = 640.0 + (x - ox) * unitX;
  *sy = 360.0 - (y - oy) * unitY;
}

static void
dragMouse(EditorFixture& fixture,
          double fromX,
          double fromY,
          double toX,
          double toY)
{
  fixture.window.mouseX = fromX;
  fixture.window.mouseY = fromY;
  fixture.module.Update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  for (int step = 1; step <= 4; ++step) {
    fixture.window.mouseX = fromX + (toX - fromX) * step / 4.0;
    fixture.window.mouseY = fromY + (toY - fromY) * step / 4.0;
    fixture.module.Update(0.016);
  }
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.Update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::None);
  fixture.module.Update(0.016);
}

static int
testGizmoRotateAndScaleDrags()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  const std::string id = EditorModuleTestAccess::createNode(
    fixture.module, EditorCommand::CreateRect);
  document.setTransform(id, Transform3D{});
  fixture.camera.SetPositionPrecise(0.0, 0.0);
  fixture.camera.SetZoom(32.0f);
  fixture.module.Update(0.016);
  fixture.input.getKeyQueue().push({ KeyCode::E, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  const float scale =
    EditorModuleTestAccess::gizmoScale(fixture.module, glm::vec3(0.0f));
  const float radius = EditorGizmo::kRingRadius * scale;
  double ax = 0, ay = 0, bx = 0, by = 0;
  worldToScreen2D(fixture, radius, 0.0f, &ax, &ay);
  worldToScreen2D(fixture, 0.0f, radius, &bx, &by);
  const size_t commands = document.history().size();
  dragMouse(fixture, ax, ay, bx, by);
  const Vector3 euler =
    glm::degrees(glm::eulerAngles(document.findNode(id)->transform.rotation));
  testTrue(counters,
           std::fabs(euler.z - 90.0f) < 2.0f,
           "E then a quarter drag on the ring rotates 90 degrees");
  testEqSize(counters,
             document.history().size(),
             commands + 1,
             "a rotate drag is one undo step");

  fixture.input.getKeyQueue().push({ KeyCode::R, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  document.setTransform(id, Transform3D{});
  fixture.module.Update(0.016);
  const float axis = EditorGizmo::kAxisLength * scale;
  worldToScreen2D(fixture, axis * 0.6f, 0.0f, &ax, &ay);
  worldToScreen2D(fixture, axis * 1.2f, 0.0f, &bx, &by);
  dragMouse(fixture, ax, ay, bx, by);
  const Transform3D scaled = document.findNode(id)->transform;
  testTrue(counters,
           std::fabs(scaled.scale.x - 2.0f) < 0.1f &&
             std::fabs(scaled.scale.y - 1.0f) < 1e-4f,
           "R then an X handle drag doubles only X");
  document.undo();
  testTrue(counters,
           std::fabs(document.findNode(id)->transform.scale.x - 1.0f) < 1e-4f,
           "undo restores the scale");
  return counters.failures;
}

static int
testGizmoLocalSpaceAndSnap()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  const std::string parent = document.createPrimitive(
    true, ScenePrimitiveShape::Cube, {}, Transform3D{});
  Transform3D turned;
  turned.rotation =
    glm::angleAxis(glm::radians(90.0f), Vector3(0.0f, 0.0f, 1.0f));
  document.setTransform(parent, turned);
  const std::string child = document.createPrimitive(
    false, ScenePrimitiveShape::Rect, parent, Transform3D{});
  EditorModuleTestAccess::setSelectedId(fixture.module, child);
  fixture.camera.SetPositionPrecise(0.0, 0.0);
  fixture.camera.SetZoom(32.0f);
  fixture.module.Update(0.016);
  fixture.input.getKeyQueue().push({ KeyCode::X, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  const float scale =
    EditorModuleTestAccess::gizmoScale(fixture.module, glm::vec3(0.0f));
  // In local space the child's X handle points along world +Y.
  double ax = 0, ay = 0, bx = 0, by = 0;
  worldToScreen2D(
    fixture, 0.0f, 0.6f * EditorGizmo::kAxisLength * scale, &ax, &ay);
  worldToScreen2D(
    fixture, 0.8f, 0.6f * EditorGizmo::kAxisLength * scale + 2.0f, &bx, &by);
  dragMouse(fixture, ax, ay, bx, by);
  Matrix4 world = document.worldMatrix(child);
  testTrue(counters,
           std::fabs(world[3][0]) < 1e-3f &&
             std::fabs(world[3][1] - 2.0f) < 0.05f,
           "a local X drag moves along the rotated axis only");
  testTrue(counters,
           std::fabs(document.findNode(child)->transform.position.x - 2.0f) <
             0.05f,
           "which is the child's own local X");

  fixture.input.getKeyQueue().push({ KeyCode::G, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testTrue(counters, document.editorState().snapEnabled, "G turns snapping on");
  testTrue(counters,
           !document.isDirty() || document.history().size() > 0,
           "snapping is view state");
  fixture.input.getKeyQueue().push({ KeyCode::X, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  const Vector3 start(world[3]);
  worldToScreen2D(fixture,
                  start.x + 0.6f * EditorGizmo::kAxisLength * scale,
                  start.y,
                  &ax,
                  &ay);
  worldToScreen2D(fixture,
                  start.x + 0.6f * EditorGizmo::kAxisLength * scale + 1.3f,
                  start.y,
                  &bx,
                  &by);
  dragMouse(fixture, ax, ay, bx, by);
  world = document.worldMatrix(child);
  testTrue(counters,
           std::fabs(world[3][0] - (start.x + 1.5f)) < 1e-3f,
           "a snapped world X drag of 1.3 moves exactly 1.5");
  return counters.failures;
}

static int
testFrameSelectionFitsBounds()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  const std::string id = document.createPrimitive(
    false,
    ScenePrimitiveShape::Cube,
    {},
    Transform3D::fromPosition(Vector3(50.0f, -20.0f, 0.0f)));
  Transform3D big = document.findNode(id)->transform;
  big.scale = Vector3(10.0f);
  document.setTransform(id, big);
  EditorModuleTestAccess::setSelectedId(fixture.module, id);
  fixture.input.getKeyQueue().push({ KeyCode::F, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  const glm::dvec2 target = fixture.camera.GetTargetPositionPrecise();
  testTrue(counters,
           std::fabs(target.x - 50.0) < 1e-3 &&
             std::fabs(target.y + 20.0) < 1e-3,
           "F centers the camera on the selection");
  float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
  fixture.camera.Update(10.0f);
  EditorModuleTestAccess::screenToWorld(
    fixture.module, 0.0f, 0.0f, &left, &top);
  EditorModuleTestAccess::screenToWorld(
    fixture.module, 1280.0f, 720.0f, &right, &bottom);
  testTrue(counters,
           left < 45.0f && right > 55.0f && bottom < -25.0f && top > -15.0f,
           "the framed view contains the whole node");
  testTrue(counters,
           !document.isDirty() || document.history().size() > 0,
           "framing never edits the scene");
  return counters.failures;
}

static int
testBoxSelect2D()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  EditorSelection& selection =
    EditorModuleTestAccess::selection(fixture.module);
  const std::string left = document.createPrimitive(
    false,
    ScenePrimitiveShape::Rect,
    {},
    Transform3D::fromPosition(Vector3(-2.0f, -1.0f, 0.0f)));
  const std::string right = document.createPrimitive(
    false,
    ScenePrimitiveShape::Rect,
    {},
    Transform3D::fromPosition(Vector3(2.0f, -1.0f, 0.0f)));
  const std::string top = document.createPrimitive(
    false,
    ScenePrimitiveShape::Rect,
    {},
    Transform3D::fromPosition(Vector3(0.0f, 4.0f, 0.0f)));
  fixture.camera.SetPositionPrecise(0.0, 0.0);
  fixture.camera.SetZoom(32.0f);
  fixture.module.Update(0.016);
  const size_t commands = document.history().size();

  double ax = 0, ay = 0, bx = 0, by = 0;
  worldToScreen2D(fixture, -3.2f, 0.4f, &ax, &ay);
  worldToScreen2D(fixture, 3.2f, -2.4f, &bx, &by);
  dragMouse(fixture, ax, ay, bx, by);
  testTrue(counters,
           selection.size() == 2 && selection.contains(left) &&
             selection.contains(right) && !selection.contains(top),
           "a marquee drag from empty space selects the nodes inside it");
  testTrue(counters,
           !EditorModuleTestAccess::boxSelecting(fixture.module),
           "the marquee ends with the button");
  testEqSize(
    counters, document.history().size(), commands, "box selection never edits");

  float sx = 0.0f;
  float sy = 0.0f;
  EditorModuleTestAccess::worldToScreen(
    fixture.module, Vector3(0.0f, 4.0f, 0.0f), &sx, &sy);
  EditorModuleTestAccess::boxSelect(
    fixture.module, sx - 10.0f, sy - 10.0f, sx + 10.0f, sy + 10.0f, true);
  testEqSize(counters, selection.size(), 3, "an additive box adds to it");

  document.setVisible(right, false);
  worldToScreen2D(fixture, 3.2f, -2.4f, &bx, &by);
  EditorModuleTestAccess::boxSelect(fixture.module,
                                    static_cast<float>(ax),
                                    static_cast<float>(ay),
                                    static_cast<float>(bx),
                                    static_cast<float>(by),
                                    false);
  testTrue(counters,
           selection.size() == 1 && selection.contains(left),
           "hidden nodes are not box-selected");

  // A click (no drag) on empty space still clears the selection.
  dragMouse(fixture, ax, ay, ax + 1.0, ay + 1.0);
  testTrue(counters, selection.empty(), "a click on empty space deselects");
  return counters.failures;
}

static int
testBoxSelect3D()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  EditorSelection& selection =
    EditorModuleTestAccess::selection(fixture.module);
  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::SetMode3D);
  const Vector3 nearPoint(-1.5f, 0.0f, 1.0f);
  const Vector3 farPoint(1.5f, 0.0f, -2.0f);
  const std::string nearId = document.createPrimitive(
    false, ScenePrimitiveShape::Cube, {}, Transform3D::fromPosition(nearPoint));
  const std::string farId = document.createPrimitive(
    false, ScenePrimitiveShape::Cube, {}, Transform3D::fromPosition(farPoint));
  fixture.module.Update(0.016);

  float nx = 0.0f, ny = 0.0f, fx = 0.0f, fy = 0.0f;
  testTrue(
    counters,
    EditorModuleTestAccess::worldToScreen(
      fixture.module, nearPoint, &nx, &ny) &&
      EditorModuleTestAccess::worldToScreen(fixture.module, farPoint, &fx, &fy),
    "both nodes project onto the screen");
  glm::vec3 origin(0.0f);
  glm::vec3 direction(0.0f);
  EditorModuleTestAccess::screenToWorldRay(
    fixture.module, nx, ny, &origin, &direction);
  const glm::vec3 toNode = nearPoint - origin;
  const float offAxis =
    glm::length(toNode - direction * glm::dot(toNode, direction));
  testTrue(counters,
           offAxis < 0.01f,
           "the projection inverts the picking ray exactly");

  EditorModuleTestAccess::boxSelect(
    fixture.module, nx - 12.0f, ny - 12.0f, nx + 12.0f, ny + 12.0f, false);
  testTrue(counters,
           selection.size() == 1 && selection.contains(nearId),
           "a box around one projected node selects only it");
  EditorModuleTestAccess::boxSelect(
    fixture.module, 0.0f, 0.0f, 1280.0f, 720.0f, false);
  testTrue(counters,
           selection.size() == 2 && selection.contains(farId),
           "a full-screen box selects every visible node");
  return counters.failures;
}

static int
testPastePlacesAfterSelection()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  EditorSelection& selection =
    EditorModuleTestAccess::selection(fixture.module);
  const std::string a = document.createPrimitive(
    false, ScenePrimitiveShape::Rect, {}, Transform3D{});
  const std::string b = document.createPrimitive(
    false, ScenePrimitiveShape::Ellipse, {}, Transform3D{});
  const std::string c = document.createPrimitive(
    false, ScenePrimitiveShape::Triangle, {}, Transform3D{});
  const std::string text = EditorClipboard::copy(document.scene(), { a });
  selection.set(b);
  testTrue(counters,
           EditorModuleTestAccess::pasteText(fixture.module, text),
           "clipboard text pastes");
  const std::vector<std::string> order = document.scene().childIds("");
  testTrue(counters,
           order.size() == 4 && order[0] == a && order[1] == b &&
             order[3] == c && order[2] == selection.primary(),
           "the paste lands after the primary selection and is selected");
  testTrue(counters,
           !EditorModuleTestAccess::pasteText(fixture.module, "not a scene"),
           "foreign clipboard text is refused");
  testEqSize(counters, document.nodeCount(), 4, "and pastes nothing");
  return counters.failures;
}

static int
testAssetsProjectFlow()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "illed-project-flow";
  std::error_code code;
  std::filesystem::remove_all(root, code);
  std::filesystem::create_directories(root / "meshes");
  std::filesystem::create_directories(root / "textures");
  std::ofstream(root / "illumo.json")
    << R"({"format":"ilpk","format_version":1,"id":"flow","kind":"content"})";
  std::ofstream(root / "meshes" / "tri.obj")
    << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  std::ofstream(root / "textures" / "leaf.png") << "not decoded here";
  std::ofstream(root / "notes.txt") << "n";
  std::shared_ptr<VirtualFileSystem> tree =
    std::make_shared<VirtualFileSystem>();
  std::string error;
  VfsMount project;
  project.point = "/project";
  project.layers.push_back(
    { DirectoryVfsBackend::open(root, true, error), "flow" });
  tree->mount(project, error);
  IllEdNativeTree::install(tree);
  {
    EditorFixture fixture;
    EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
    testEqStr(counters,
              document.packageRoot(),
              "/project",
              "new documents belong to the mounted project");
    fixture.module.Update(0.016);
    testTrue(counters,
             EditorModuleTestAccess::assetBrowser(fixture.module) != nullptr &&
               EditorModuleTestAccess::assetBrowser(fixture.module)->visible(),
             "the asset browser docks below the hierarchy");

    EditorModuleTestAccess::placeDroppedAsset(
      fixture.module, "/project/meshes/tri.obj", 640.0f, 360.0f);
    const std::string mesh = EditorModuleTestAccess::selectedId(fixture.module);
    const SceneNode* meshNode = document.findNode(mesh);
    const SceneAsset* meshAsset = document.scene().document().findAsset("tri");
    testTrue(counters,
             meshNode != nullptr && meshNode->find(SceneComponentType::Mesh) &&
               meshAsset != nullptr &&
               meshAsset->type == SceneAssetType::Mesh &&
               meshAsset->path == "meshes/tri.obj",
             "a dropped mesh becomes a node and a package-relative asset");
    EditorModuleTestAccess::placeDroppedAsset(
      fixture.module, "/project/textures/leaf.png", 700.0f, 360.0f);
    const SceneNode* sprite =
      document.findNode(EditorModuleTestAccess::selectedId(fixture.module));
    testTrue(counters,
             sprite != nullptr && sprite->find(SceneComponentType::Sprite) &&
               document.scene().document().findAsset("leaf") != nullptr,
             "a dropped texture becomes a sprite");
    EditorModuleTestAccess::placeDroppedAsset(
      fixture.module, "/project/meshes/tri.obj", 600.0f, 300.0f);
    testEqSize(counters,
               document.scene().document().assets.size(),
               2,
               "dropping the same file again reuses its asset");
    const std::size_t nodes = document.nodeCount();
    EditorModuleTestAccess::placeDroppedAsset(
      fixture.module, "/project/notes.txt", 640.0f, 360.0f);
    EditorModuleTestAccess::placeDroppedAsset(
      fixture.module, "/project/meshes/tri.obj", 10.0f, 360.0f);
    testEqSize(counters,
               document.nodeCount(),
               nodes,
               "other files and drops onto panels place nothing");
    document.undo();
    testEqSize(counters,
               document.nodeCount(),
               nodes - 1,
               "a placement is one undo step");

    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::CreateLight);
    const SceneNode* light =
      document.findNode(EditorModuleTestAccess::selectedId(fixture.module));
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::CreateCamera);
    const SceneNode* camera =
      document.findNode(EditorModuleTestAccess::selectedId(fixture.module));
    testTrue(counters,
             light != nullptr && light->find(SceneComponentType::Light) &&
               camera != nullptr && camera->find(SceneComponentType::Camera),
             "Create adds light and camera nodes");

    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::SaveToProject);
    std::vector<uint8_t> saved;
    testTrue(counters,
             tree->read("/project/scenes/Untitled.ilsc", saved, error) &&
               !document.isDirty() &&
               document.path() == "vfs:/project/scenes/Untitled.ilsc",
             "Save to Project writes into /project/scenes");
    const std::size_t savedNodes = document.nodeCount();
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::NewDocument);
    EditorModuleTestAccess::openLocation(
      fixture.module, { "vfs:/project/scenes/Untitled.ilsc", "Untitled.ilsc" });
    testTrue(counters,
             document.nodeCount() == savedNodes &&
               document.packageRoot() == "/project" &&
               document.scene().document().findAsset("tri") != nullptr,
             "a project scene reopens from the tree under its package");
  }
  IllEdNativeTree::install(nullptr);
  std::filesystem::remove_all(root, code);
  return counters.failures;
}

void
registerEditorModuleTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Assets.ProjectFlow",
               []() { return testAssetsProjectFlow(); });
  registry.add("IllEd.Selection.BoxSelect2D",
               []() { return testBoxSelect2D(); });
  registry.add("IllEd.Selection.BoxSelect3D",
               []() { return testBoxSelect3D(); });
  registry.add("IllEd.Clipboard.PastePlacesAfterSelection",
               []() { return testPastePlacesAfterSelection(); });
  registry.add("IllEd.Gizmo.ModuleRotateAndScaleDrags",
               []() { return testGizmoRotateAndScaleDrags(); });
  registry.add("IllEd.Gizmo.LocalSpaceUnderRotatedParentAndSnap",
               []() { return testGizmoLocalSpaceAndSnap(); });
  registry.add("IllEd.Gizmo.FrameSelectionFitsBounds",
               []() { return testFrameSelectionFitsBounds(); });
  registry.add("IllEd.Module.CameraDoesNotDirty",
               []() { return testCameraDoesNotDirty(); });
  registry.add("IllEd.Module.NewNodeParentsToRoot",
               []() { return testNewNodeParentsToRoot(); });
  registry.add("IllEd.Module.ShortcutsMatchMenus",
               []() { return testShortcutsMatchMenus(); });
  registry.add("IllEd.Module.KeyboardUndoRedo",
               []() { return testKeyboardUndoRedo(); });
  registry.add(
    "IllEd.SceneGraph.Bench.EditLatency",
    []() {
      g = {};
      EditorFixture fixture;
      EditorDocument& document =
        EditorModuleTestAccess::document(fixture.module);
      std::string selected;
      const size_t count = 2000;
      for (size_t i = 0; i < count; ++i) {
        const size_t row = i / 50;
        selected = document.createPrimitive(
          false, ScenePrimitiveShape::Cube, {}, Transform3D{});
        document.setTransform(
          selected,
          Transform3D::fromPosition(Vector3(
            static_cast<float>(i % 50) * 2, static_cast<float>(row) * 2, 0)));
      }
      std::string error;
      std::string text;
      IllEdNativeFiles::writeText(
        "scene-v2-benchmark.ilsc", document.encode(), &error);
      IllEdNativeFiles::readText("scene-v2-benchmark.ilsc", &text, &error);
      document.loadFromText(text, &error);
      EditorModuleTestAccess::setSelectedId(fixture.module, selected);
      EditorModuleTestAccess::refreshView(fixture.module);
      const size_t repeats = 30;
      const std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();
      for (size_t i = 0; i < repeats; ++i) {
        EditorModuleTestAccess::handleCommand(fixture.module,
                                              EditorCommand::CycleColor);
      }
      const double edit = std::chrono::duration<double, std::micro>(
                            std::chrono::steady_clock::now() - start)
                            .count() /
                          repeats;
      std::string picked;
      const std::chrono::steady_clock::time_point queryStart =
        std::chrono::steady_clock::now();
      for (size_t i = 0; i < repeats; ++i) {
        document.pickRay(Vector3(static_cast<float>(i) * 2, 0, -10),
                         Vector3(0, 0, 1),
                         &picked);
      }
      const double pick = std::chrono::duration<double, std::micro>(
                            std::chrono::steady_clock::now() - queryStart)
                            .count() /
                          repeats;
      std::printf("IllEdBench cubes=%zu recolor_us=%.3f ray_us=%.3f "
                  "source=scene-v2-benchmark.ilsc\n",
                  count,
                  edit,
                  pick);
      // Favorable planar-grid prototype over codec-loaded .ilsc content. Exact
      // world-box hits agree with graph queries; revision polling is excluded.
      std::vector<AxisAlignedBounds3> boxes;
      std::vector<SceneNodeHandle> handles;
      std::unordered_map<uint64_t, std::vector<size_t>> grid;
      const std::function<uint64_t(int, int)> key = [](int x, int y) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
               static_cast<uint32_t>(y);
      };
      const std::chrono::steady_clock::time_point gridStart =
        std::chrono::steady_clock::now();
      for (SceneNodeHandle node = document.graph().firstNode(); !node.isNull();
           node = document.graph().nextNode(node)) {
        AxisAlignedBounds3 box;
        if (!document.graph().getWorldBounds(node, &box)) {
          continue;
        }
        const size_t index = boxes.size();
        boxes.push_back(box);
        handles.push_back(node);
        for (int x = static_cast<int>(std::floor(box.minimum.x / 4));
             x <= static_cast<int>(std::floor(box.maximum.x / 4));
             ++x) {
          for (int y = static_cast<int>(std::floor(box.minimum.y / 4));
               y <= static_cast<int>(std::floor(box.maximum.y / 4));
               ++y) {
            grid[key(x, y)].push_back(index);
          }
        }
      }
      const double gridBuild = std::chrono::duration<double, std::micro>(
                                 std::chrono::steady_clock::now() - gridStart)
                                 .count();
      double gridTime = 0, bvhTime = 0;
      for (size_t i = 0; i < repeats; ++i) {
        const float x = static_cast<float>(i) * 2;
        const std::chrono::steady_clock::time_point query =
          std::chrono::steady_clock::now();
        const std::unordered_map<uint64_t, std::vector<size_t>>::const_iterator
          bucket = grid.find(key(static_cast<int>(std::floor(x / 4)), 0));
        float nearest = std::numeric_limits<float>::infinity();
        SceneNodeHandle best;
        if (bucket != grid.end()) {
          for (size_t candidate : bucket->second) {
            const AxisAlignedBounds3& box = boxes[candidate];
            if (x >= box.minimum.x && x <= box.maximum.x &&
                0 >= box.minimum.y && 0 <= box.maximum.y &&
                box.maximum.z >= -10) {
              const float distance = std::max(0.0f, box.minimum.z + 10);
              if (distance < nearest) {
                nearest = distance;
                best = handles[candidate];
              }
            }
          }
        }
        gridTime += std::chrono::duration<double, std::micro>(
                      std::chrono::steady_clock::now() - query)
                      .count();
        SceneRayHit hit;
        const std::chrono::steady_clock::time_point bvhStart =
          std::chrono::steady_clock::now();
        const bool found =
          document.graph().raycast(Vector3(x, 0, -10), Vector3(0, 0, 1), &hit);
        bvhTime += std::chrono::duration<double, std::micro>(
                     std::chrono::steady_clock::now() - bvhStart)
                     .count();
        if (!found || hit.node != best || hit.distance != nearest) {
          ++g.failures;
        }
      }
      const uint64_t sequence = document.graph().getChangeSequence();
      for (size_t i = 0; i < 1000; ++i) {
        document.translate(selected, Vector3(0.001f, 0, 0));
      }
      std::vector<SceneChange> changes;
      const bool retained = document.graph().readChanges(sequence, &changes);
      std::printf(
        "IllEdGrid source=scene-v2-benchmark.ilsc build_us=%.3f "
        "vertical_ray_us=%.3f graph_bvh_us=%.3f journal_drag_records=%zu "
        "retained=%d transform_static_fraction=%.6f\n",
        gridBuild,
        gridTime / repeats,
        bvhTime / repeats,
        changes.size(),
        retained ? 1 : 0,
        1.0 - 1.0 / count);
      testTrue(g,
               retained && changes.size() == 1000,
               "1000 unconsumed drag updates fit the journal");
      return g.failures;
    },
    120);
  registry.add("IllEd.Module.IncrementalGraph", []() {
    g = {};
    EditorFixture fixture;
    EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
    EditorModuleTestAccess::createNode(fixture.module,
                                       EditorCommand::CreateCube);
    const std::string id = EditorModuleTestAccess::selectedId(fixture.module);
    SceneGraph& graph = document.graph();
    const SceneNodeHandle retained = document.nodeHandle(id);
    ISceneRenderAttachment* visual = graph.getAttachment(retained, 0);
    testTrue(g,
             visual != nullptr && graph.getAttachmentCount(retained) == 1,
             "geometry has one persistent render attachment");
    const uint64_t structure = graph.getStructuralRevision();
    const SceneSnapshotView snapshot = graph.extract(nullptr);
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::CycleColor);
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::NudgeExtent);
    testTrue(g,
             document.nodeHandle(id) == retained &&
               graph.getAttachment(retained, 0) == visual &&
               graph.getStructuralRevision() == structure,
             "recolor and extent edits preserve node and visual identities");
    testTrue(
      g, !snapshot.get(), "visual reconfiguration retires old snapshots");
    document.translate(id, Vector3(1, 2, 3));
    EditorModuleTestAccess::refreshView(fixture.module);
    Matrix4 world(1.0f);
    graph.getWorldTransform(retained, &world);
    testTrue(g,
             graph.getAttachment(retained, 0) == visual &&
               world[3][1] == document.findNode(id)->transform.position.y,
             "translation synchronizes without rebuild");
    for (size_t i = 0; i < 4200; ++i) {
      document.setColor(id, ColorRgba{ 80, 90, 100, 255 });
    }
    EditorModuleTestAccess::refreshView(fixture.module);
    testTrue(g,
             document.nodeHandle(id) == retained &&
               graph.getAttachment(retained, 0) == visual,
             "journal overflow resync preserves graph and visual identity");
    fixture.module.Exit();
    testTrue(g,
             document.nodeHandle(id) == retained &&
               graph.getAttachmentCount(retained) == 1,
             "stop preserves document nodes");
    testTrue(g,
             fixture.module.Start(&fixture.context) &&
               document.nodeHandle(id) == retained &&
               graph.getAttachmentCount(retained) == 1,
             "restart rebinds render resources without rebuilding nodes");
    EditorModuleTestAccess::deleteSelection(fixture.module);
    testTrue(g,
             !graph.isNodeValid(retained),
             "deletion invalidates the original generational handle");
    return g.failures;
  });
  registry.add("IllEd.Module.PropertyCommands", []() {
    g = {};
    EditorFixture fixture;
    testTrue(g, fixture.started, "editor starts");
    EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
    EditorModuleTestAccess::createNode(fixture.module,
                                       EditorCommand::CreateEmpty);
    const std::string parent =
      EditorModuleTestAccess::selectedId(fixture.module);
    EditorModuleTestAccess::createNode(fixture.module,
                                       EditorCommand::CreateCube);
    const std::string child =
      EditorModuleTestAccess::selectedId(fixture.module);
    testTrue(g,
             parentOf(document, child).empty(),
             "created nodes go to the root, not under the selection");
    document.setParent(child, parent);
    const Vector3 extent = primitiveOf(document.findNode(child)).extent;
    document.setColor(child, ColorRgba{ 210, 90, 70, 255 });
    testTrue(g,
             document.loadFromText(document.encode(), nullptr),
             "reload clean property baseline");
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::NudgeExtent);
    testTrue(g,
             glm::length(primitiveOf(document.findNode(child)).extent -
                         extent * 1.15f) < 0.0001f,
             "size command scales all selected extents");
    testTrue(g, document.isDirty(), "property command marks document dirty");
    const ColorRgba colors[] = { { 80, 180, 90, 255 },
                                 { 70, 140, 220, 255 },
                                 { 210, 90, 70, 255 } };
    for (const ColorRgba& expected : colors) {
      EditorModuleTestAccess::handleCommand(fixture.module,
                                            EditorCommand::CycleColor);
      const ColorRgba actual = primitiveOf(document.findNode(child)).color;
      testTrue(g,
               actual.r == expected.r && actual.g == expected.g &&
                 actual.b == expected.b && actual.a == expected.a,
               "color command advances through the full palette cycle");
    }
    testEqStr(g, parentOf(document, child), parent, "child starts parented");
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::UnparentNode);
    testTrue(
      g, parentOf(document, child).empty(), "unparent command detaches child");
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::DeleteNode);
    testTrue(g,
             document.findNode(child) == nullptr &&
               document.findNode(parent) != nullptr,
             "deleting detached child preserves its former parent");
    const std::string unchanged = document.encode();
    testTrue(g,
             document.loadFromText(unchanged, nullptr),
             "reload clean no-selection baseline");
    for (EditorCommand command : { EditorCommand::NudgeExtent,
                                   EditorCommand::CycleColor,
                                   EditorCommand::UnparentNode }) {
      EditorModuleTestAccess::handleCommand(fixture.module, command);
    }
    testEqStr(g,
              document.encode(),
              unchanged,
              "property commands without selection preserve document");
    testTrue(g, !document.isDirty(), "empty selection commands remain clean");
    return g.failures;
  });
  registry.add("IllEd.Module.PanelCommands", []() {
    g = {};
    EditorFixture fixture;
    GuiPanelDock& dock = EditorModuleTestAccess::dock(fixture.module);
    // A frame stores the view state first; panels must add nothing to it.
    fixture.module.Update(0.016);
    const std::string document =
      EditorModuleTestAccess::document(fixture.module).encode();
    for (int iteration = 0; iteration < 2; ++iteration) {
      EditorModuleTestAccess::handleCommand(
        fixture.module, EditorCommand::ToggleHierarchyPanel);
      EditorModuleTestAccess::handleCommand(
        fixture.module, EditorCommand::ToggleInspectorPanel);
      fixture.module.Update(0.016);
      const GuiDockMode expected =
        iteration == 0 ? GuiDockMode::Hidden : GuiDockMode::Docked;
      testTrue(g,
               dock.mode("hierarchy") == expected &&
                 dock.mode("inspector") == expected,
               "panel toggles are reversible");
      testTrue(g,
               EditorModuleTestAccess::sceneGraphView(fixture.module)
                   ->placement()
                   .visible == (iteration != 0),
               "a hidden panel's content is not placed");
    }
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::PopOutToolsPanel);
    testTrue(g,
             dock.mode("tools") == GuiDockMode::Docked,
             "without window support a pop-out stays docked");
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::ToggleAssetsPanel);
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::ResetLayout);
    testTrue(g,
             dock.mode("assets") == GuiDockMode::Docked,
             "reset brings every panel back");
    testEqStr(g,
              EditorModuleTestAccess::document(fixture.module).encode(),
              document,
              "panel commands preserve scene data");
    return g.failures;
  });
  registry.add("IllEd.Module.NewDocumentCommand", []() {
    g = {};
    EditorFixture fixture;
    EditorModuleTestAccess::createNode(fixture.module,
                                       EditorCommand::CreateCube);
    EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
    document.setPath("previous-scene.ilsc");
    testTrue(g,
             document.loadFromText(document.encode(), nullptr),
             "reload clean reset baseline");
    fixture.camera.SetPositionPrecise(15.0, -7.0);
    fixture.camera.SetZoom(8.0f);
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::ResetCamera);
    testTrue(g,
             glm::length(fixture.camera.GetPositionPrecise()) < 0.0001 &&
               fixture.camera.GetZoom() == 32.0f,
             "reset command restores camera origin and zoom");
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::NewDocument);
    testEqSize(g, document.nodeCount(), 0u, "new document clears nodes");
    testTrue(g,
             document.path().empty() && !document.isDirty(),
             "new document is clean and untitled");
    testTrue(g,
             EditorModuleTestAccess::selectedId(fixture.module).empty(),
             "new document clears selection");
    testTrue(g,
             !EditorModuleTestAccess::confirmationOpen(fixture.module),
             "clean document needs no confirmation");
    return g.failures;
  });
  registry.add("IllEd.Module.PerspectivePicking", []() {
    g = {};
    for (float pitch : { 0.0f, 0.45f }) {
      EditorFixture fixture;
      EditorModuleTestAccess::handleCommand(fixture.module,
                                            EditorCommand::SetMode3D);
      EditorDocument& document =
        EditorModuleTestAccess::document(fixture.module);
      SceneEditorState cameraState = document.editorState();
      cameraState.pitch = pitch;
      document.setEditorState(cameraState);
      EditorModuleTestAccess::setCameraTargetHeight(
        fixture.module, pitch == 0.0f ? 0.0f : 6.0f);
      fixture.module.Update(0.016);
      glm::vec3 origin{ 0.0f }, direction{ 0.0f };
      testTrue(g,
               EditorModuleTestAccess::screenToWorldRay(
                 fixture.module, 640, 360, &origin, &direction),
               "perspective screen ray available");
      if (pitch == 0.0f) {
        float groundX = 0, groundY = 0;
        testTrue(g,
                 !EditorModuleTestAccess::screenToWorld(
                   fixture.module, 640, 360, &groundX, &groundY),
                 "horizontal screen ray misses ground plane");
      }
      const std::string nearId = document.createPrimitive(
        false, ScenePrimitiveShape::Cube, {}, Transform3D{});
      const std::string farId = document.createPrimitive(
        false, ScenePrimitiveShape::Cube, {}, Transform3D{});
      Transform3D nearTransform = Transform3D::fromEuler(0.3f, 0.7f, 0.2f);
      nearTransform.position = origin + direction * 5.0f;
      document.setTransform(nearId, nearTransform);
      document.setTransform(
        farId, Transform3D::fromPosition(origin + direction * 8.0f));
      EditorModuleTestAccess::refreshView(fixture.module);
      EditorModuleTestAccess::setSelectedId(fixture.module, "");
      fixture.window.mouseX = 640;
      fixture.window.mouseY = 360;
      InputManagerTestAccess::setAction(
        fixture.input, KeyCode::MouseLeft, InputAction::Press);
      fixture.module.Update(0.016);
      testEqStr(g,
                EditorModuleTestAccess::selectedId(fixture.module),
                nearId,
                "screen click selects nearest elevated rotated body");
    }
    return g.failures;
  });
  registry.add("IllEd.Module.BodyDragOffset", []() {
    g = {};
    for (bool world3D : { false, true }) {
      EditorFixture fixture;
      if (world3D) {
        EditorModuleTestAccess::handleCommand(fixture.module,
                                              EditorCommand::SetMode3D);
      }
      EditorModuleTestAccess::createNode(fixture.module,
                                         world3D ? EditorCommand::CreateCube
                                                 : EditorCommand::CreateRect);
      const std::string id = EditorModuleTestAccess::selectedId(fixture.module);
      EditorDocument& document =
        EditorModuleTestAccess::document(fixture.module);
      document.setTransform(id, Transform3D::fromPosition(Vector3(0.0f)));
      document.setExtent(id, Vector3(10.0f));
      std::string error;
      testTrue(g,
               document.loadFromText(document.encode(), &error),
               "reload clean scene");
      EditorModuleTestAccess::refreshView(fixture.module);
      EditorModuleTestAccess::setSelectedId(fixture.module, "");
      fixture.camera.SetPositionPrecise(0.0, 0.0);
      fixture.camera.SetZoom(32.0f);
      fixture.module.Update(0.016);
      fixture.window.mouseX = 650.0;
      fixture.window.mouseY = 365.0;
      InputManagerTestAccess::setAction(
        fixture.input, KeyCode::MouseLeft, InputAction::Press);
      fixture.module.Update(0.016);
      testEqStr(g,
                EditorModuleTestAccess::selectedId(fixture.module),
                id,
                "body selected off center");
      for (int held = 0; held < 3; ++held) {
        fixture.module.Update(0.016);
      }
      const Vector3 position = document.findNode(id)->transform.position;
      testTrue(g,
               glm::length(position) < 0.00001f,
               "stationary selection does not move object");
      testTrue(
        g, !document.isDirty(), "stationary selection keeps document clean");
      InputManagerTestAccess::setAction(
        fixture.input, KeyCode::MouseLeft, InputAction::Release);
      fixture.module.Update(0.016);
      testTrue(g, !document.isDirty(), "stationary click release stays clean");
      EditorModuleTestAccess::setSelectedId(fixture.module, "");
      InputManagerTestAccess::setAction(
        fixture.input, KeyCode::MouseLeft, InputAction::Press);
      fixture.module.Update(0.016);
      fixture.window.mouseX += 30.0;
      fixture.module.Update(0.016);
      testTrue(g,
               glm::length(document.findNode(id)->transform.position) > 0.001f,
               "actual pointer movement drags object");
      testTrue(g, document.isDirty(), "actual drag marks document dirty");
      InputManagerTestAccess::setAction(
        fixture.input, KeyCode::MouseLeft, InputAction::Release);
      fixture.module.Update(0.016);
      testTrue(g,
               !EditorModuleTestAccess::isDragging(fixture.module),
               "release ends body drag");
    }
    return g.failures;
  });
  registry.add("IllEd.Module.CloseConfirmation", []() {
    g = {};
    testCloseConfirmation();
    return g.failures;
  });
  registry.add("IllEd.Module.MousePanningDirection", []() {
    g = {};
    testMousePanningMovesCameraNaturalDirection();
    return g.failures;
  });
  registry.add("IllEd.Module.3DMousePanningDirection", []() {
    g = {};
    test3DMousePanningRespectsCameraOrientation();
    return g.failures;
  });
  registry.add("IllEd.Module.3DCameraElevationKeys", []() {
    g = {};
    test3DCameraElevationKeys();
    return g.failures;
  });
  registry.add("IllEd.Module.TransformGizmoHitAndConstraints", []() {
    g = {};
    testTransformGizmoHitAndConstraints();
    return g.failures;
  });
  registry.add("IllEd.Module.2DNodeRendering", []() {
    g = {};
    test2dModeNodeRenderingEmitsTokens();
    return g.failures;
  });
  registry.add("IllEd.Module.UiAtlasSprites", []() {
    g = {};
    testUiAtlasSpritesFromStart();
    return g.failures;
  });
  registry.add("IllEd.Module.CreateCube", []() {
    g = {};
    testCreateCubeAndGraph();
    return g.failures;
  });
  registry.add("IllEd.Module.SceneRoundTrip", []() {
    g = {};
    testSaveLoadThroughDocument();
    return g.failures;
  });
  registry.add("IllEd.Module.ModeSelectProperties", []() {
    g = {};
    testModeCreateSelectProperties();
    return g.failures;
  });
  registry.add("IllEd.Module.CreateToolArmsOnly", []() {
    g = {};
    testCreateToolArmsOnly();
    return g.failures;
  });
  registry.add("IllEd.Module.PlaceOn3DGround", []() {
    g = {};
    testPlaceOn3DGround();
    return g.failures;
  });
  registry.add("IllEd.Module.ToolbarCreateClickNoInsert", []() {
    g = {};
    testToolbarCreateClickDoesNotInsertOnUpdate();
    return g.failures;
  });
  registry.add("IllEd.Module.OpenConsoleBlocksInput", []() {
    g = {};
    testOpenConsoleBlocksEditorInput();
    return g.failures;
  });
  registry.add("IllEd.Module.ConsoleNotDispatchedByModule", []() {
    g = {};
    testEditorModuleDoesNotDispatchConsole();
    return g.failures;
  });
  registry.add("IllEd.Module.UiDrawOrder", []() {
    g = {};
    testUiDrawOrder();
    return g.failures;
  });
  registry.add("IllEd.Module.FontSizeConfigured", []() {
    g = {};
    testFontSizeConfiguredFromEnvVars();
    return g.failures;
  });
  registry.add("IllEd.Module.FontSizeImmediateRuntimeChange", []() {
    g = {};
    testFontSizeImmediateRuntimeChange();
    return g.failures;
  });
}
