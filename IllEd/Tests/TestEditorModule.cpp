#include "EditorAttachment.h"
#include "EditorModule.h"
#include "EditorUiAtlas.h"
#include "TestAccess.h"
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
    , env()
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
testEditorAttachmentForwardsBounds()
{
  testSection("EditorAttachment: forwards visual bounds");
  EditorFixture fixture;
  testTrue(g, fixture.started, "editor fixture starts");

  IlscNode node;
  node.kind = SceneNodeKind::SolidCube;
  node.primitive.extent = Vector3(2.0f, 3.0f, 4.0f);
  EditorAttachment attachment;
  testTrue(g,
           attachment.configure(
             &fixture.renderer, &fixture.camera, node, IlscWorldMode::World3D),
           "bounded editor attachment configures");
  AxisAlignedBounds3 bounds;
  testTrue(g,
           attachment.getSceneLocalBounds(&bounds),
           "editor attachment forwards MeshVisual bounds");
  testTrue(g,
           glm::length(bounds.minimum - Vector3(-2.0f, -3.0f, -4.0f)) <
               0.0001f &&
             glm::length(bounds.maximum - Vector3(2.0f, 3.0f, 4.0f)) < 0.0001f,
           "forwarded bounds cover the configured primitive");
}

static void
testCloseConfirmation()
{
  testSection("EditorModule: native close shares unsaved confirmation");
  EditorFixture fixture;
  testTrue(g, fixture.started, "editor starts");
  testTrue(g,
           fixture.module.OnCloseRequested(),
           "clean document accepts native close");
  EditorModuleTestAccess::createNode(fixture.module, SceneNodeKind::SolidCube);
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
  EditorModuleTestAccess::createNode(discard.module, SceneNodeKind::SolidCube);
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
  EditorModuleTestAccess::createNode(discard.module, SceneNodeKind::SolidCube);
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
  testSection("EditorModule: shipped atlas draws sidebar and toolbar sprites");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  testTrue(
    g,
    fixture.assets
        .getState(EditorModuleTestAccess::toolbar(fixture.module)->atlas())
        .state == AssetState::Ready,
    "module loaded the shipped atlas");
  EditorSidebar* sidebar = EditorModuleTestAccess::sidebar(fixture.module);
  EditorToolbar* toolbar = EditorModuleTestAccess::toolbar(fixture.module);
  testTrue(g, sidebar != nullptr && toolbar != nullptr, "chrome exists");
  testTrue(g,
           sidebar->getVisual().spriteCount() >= 10u,
           "sidebar emits tool and mode sprites");
  testTrue(
    g, toolbar->getVisual().spriteCount() >= 4u, "toolbar emits menu sprites");

  fixture.renderer.BeginFrame();
  testTrue(
    g, sidebar->AppendCommands(&fixture.renderer), "sidebar appends tokens");
  fixture.renderer.EndFrame();
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::SetTexture) >= 1u,
           "sidebar binds the atlas texture");
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "sidebar draws atlas sprites");
}

static void
testCreateCubeAndGraph()
{
  testSection("EditorModule: create cube and attach graph");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  EditorModuleTestAccess::createNode(fixture.module, SceneNodeKind::SolidCube);
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
  EditorModuleTestAccess::createNode(fixture.module, SceneNodeKind::SolidCube);
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
  testTrue(g, document.saveToFile(path.string(), &error), "saves .ilsc");
  EditorDocument loaded;
  testTrue(g, loaded.loadFromFile(path.string(), &error), "loads .ilsc");
  testEqSize(g, loaded.nodeCount(), 1u, "loaded cube");
  testTrue(g,
           loaded.nodeAt(0)->kind == SceneNodeKind::SolidCube,
           "kind is solid_cube");
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
  testTrue(
    g, document.worldMode() == IlscWorldMode::World3D, "module switches to 3D");
  EditorModuleTestAccess::createNode(fixture.module,
                                     SceneNodeKind::FilledEllipse);
  const std::string ellipseId =
    EditorModuleTestAccess::selectedId(fixture.module);
  testTrue(g, !ellipseId.empty(), "ellipse selected");
  testTrue(g,
           document.setExtent(ellipseId, Vector3(1.25f, 0.8f, 0.5f)),
           "property size via document");
  testTrue(g,
           document.setColor(ellipseId, ColorRgba{ 4, 5, 6, 255 }),
           "property color via document");
  testTrue(g,
           EditorModuleTestAccess::rebuildGraph(fixture.module),
           "rebuild after property edit");
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
  testTrue(g, detail.worldMode == IlscWorldMode::World3D, "detail reports 3D");
  testTrue(g,
           detail.selectedKind == SceneNodeKind::SolidPyramid ||
             detail.selectedKind == SceneNodeKind::FilledEllipse,
           "selected kind is a created primitive");
  EditorSidebar* sidebar = EditorModuleTestAccess::sidebar(fixture.module);
  testTrue(g, sidebar != nullptr, "sidebar exists");
  const EditorCommand mode2d = sidebar->clickAtForTesting(
    sidebar->sidebarX() + 20.0f, 28.0f + 24.0f + 8.0f);
  testTrue(g, mode2d == EditorCommand::SetMode2D, "sidebar 2D hit");
  EditorModuleTestAccess::handleCommand(fixture.module, mode2d);
  testTrue(
    g, document.worldMode() == IlscWorldMode::World2D, "sidebar mode applied");
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
  const IlscNode* node =
    document.findNode(EditorModuleTestAccess::selectedId(fixture.module));
  testTrue(g,
           node != nullptr && node->kind == SceneNodeKind::SolidCube,
           "placed cube");
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
  testTrue(g, document.worldMode() == IlscWorldMode::World3D, "3D mode active");
  float planeX = 0.0f;
  float planeZ = 0.0f;
  testTrue(g,
           EditorModuleTestAccess::screenToWorld(
             fixture.module, 960.0f, 360.0f, &planeX, &planeZ),
           "3D unproject of off-center pixel");
  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::CreateCube);
  EditorModuleTestAccess::applyActiveToolAt(fixture.module, planeX, planeZ);
  const IlscNode* node =
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
    document.pick(node->transform.position.x, node->transform.position.z, &hit),
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
  for (int y = 28; y <= 220; y += 2) {
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

  // 2. When console is open, toolbar and sidebar input yield
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
  EditorSidebar* sidebar = EditorModuleTestAccess::sidebar(fixture.module);
  EditorToolbar* toolbar = EditorModuleTestAccess::toolbar(fixture.module);

  int sgvIndex = -1;
  int sidebarIndex = -1;
  int toolbarIndex = -1;

  for (size_t i = 0; i < uiDrawables.size(); ++i) {
    if (uiDrawables[i] == sceneGraphView) {
      sgvIndex = static_cast<int>(i);
    } else if (uiDrawables[i] == sidebar) {
      sidebarIndex = static_cast<int>(i);
    } else if (uiDrawables[i] == toolbar) {
      toolbarIndex = static_cast<int>(i);
    }
  }

  testTrue(g,
           sgvIndex >= 0 && sidebarIndex >= 0 && toolbarIndex >= 0,
           "all UI drawables found in layer");
  testTrue(g,
           toolbarIndex > sgvIndex,
           "toolbar is dispatched after (on top of) SceneGraphView");
  testTrue(g,
           toolbarIndex > sidebarIndex,
           "toolbar is dispatched after (on top of) sidebar");
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
             IlscWorldMode::World2D,
           "starts in 2D mode");

  EditorModuleTestAccess::createNode(fixture.module, SceneNodeKind::FilledRect);
  EditorModuleTestAccess::createNode(fixture.module,
                                     SceneNodeKind::FilledEllipse);
  EditorModuleTestAccess::createNode(fixture.module,
                                     SceneNodeKind::FilledTriangle);

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
  EditorSidebar* sidebar = EditorModuleTestAccess::sidebar(module);
  EditorSceneGraphView* sceneGraphView =
    EditorModuleTestAccess::sceneGraphView(module);

  testTrue(g, toolbar != nullptr, "toolbar exists");
  testTrue(g, sidebar != nullptr, "sidebar exists");
  testTrue(g, sceneGraphView != nullptr, "sceneGraphView exists");

  testTrue(
    g, std::abs(toolbar->fontSize() - 20.0f) < 0.001f, "toolbar fontSize 20");
  testTrue(
    g, std::abs(sidebar->fontSize() - 20.0f) < 0.001f, "sidebar fontSize 20");
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
  EditorSidebar* sidebar = EditorModuleTestAccess::sidebar(module);
  EditorSceneGraphView* sceneGraphView =
    EditorModuleTestAccess::sceneGraphView(module);

  testTrue(g,
           toolbar != nullptr && sidebar != nullptr &&
             sceneGraphView != nullptr,
           "views exist");
  testTrue(g,
           std::abs(toolbar->fontSize() - 13.0f) < 0.001f,
           "initial toolbar fontSize 13");
  testTrue(
    g, std::abs(toolbar->barHeight() - 28.0f) < 0.001f, "initial barHeight 28");

  // Change variable at runtime
  env.setVar("fontSize", "24");
  module.Update(0.016);

  testTrue(g,
           std::abs(toolbar->fontSize() - 24.0f) < 0.001f,
           "toolbar resized to 24 immediately");
  testTrue(g,
           std::abs(sidebar->fontSize() - 24.0f) < 0.001f,
           "sidebar resized to 24 immediately");
  testTrue(g,
           std::abs(sceneGraphView->fontSize() - 24.0f) < 0.001f,
           "sceneGraphView resized to 24 immediately");
  testTrue(
    g, toolbar->barHeight() >= 48.0f, "toolbar height updated immediately");
  testTrue(
    g, sidebar->panelWidth() >= 360.0f, "sidebar width updated immediately");

  // Change variable again at runtime (e.g. via multiplier or smaller size)
  env.setVar("fontSize", "16");
  module.Update(0.016);

  testTrue(g,
           std::abs(toolbar->fontSize() - 16.0f) < 0.001f,
           "toolbar resized to 16 immediately");
  testTrue(g,
           std::abs(sidebar->fontSize() - 16.0f) < 0.001f,
           "sidebar resized to 16 immediately");

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
  testSection("EditorModule: 3D camera elevation via E (Up) and Q (Down)");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::SetMode3D);
  fixture.module.Update(0.016);

  const glm::vec3 initialEye = fixture.camera.getEye();
  const glm::vec3 initialTarget = fixture.camera.getTarget();

  // Press E to elevate camera Up
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Press);
  fixture.module.Update(0.1);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::E, InputAction::Release);

  const glm::vec3 elevatedEye = fixture.camera.getEye();
  const glm::vec3 elevatedTarget = fixture.camera.getTarget();
  testTrue(g,
           elevatedTarget.y > initialTarget.y,
           "pressing E elevates camera target Y");
  testTrue(g, elevatedEye.y > initialEye.y, "pressing E elevates camera eye Y");

  // Press Q to lower camera Down
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::Q, InputAction::Press);
  fixture.module.Update(0.2);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::Q, InputAction::Release);

  const glm::vec3 loweredEye = fixture.camera.getEye();
  const glm::vec3 loweredTarget = fixture.camera.getTarget();
  testTrue(
    g, loweredTarget.y < elevatedTarget.y, "pressing Q lowers camera target Y");
  testTrue(g, loweredEye.y < elevatedEye.y, "pressing Q lowers camera eye Y");
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
  EditorModuleTestAccess::createNode(fixture.module, SceneNodeKind::SolidCube);
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
  const IlscNode* node = document.findNode(id);
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

  // Move mouse over sidebar / off normal world (e.g. x=20, y=100 which is
  // inside sidebar UI)
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
  const IlscNode* movedNode = document.findNode(id);
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

void
registerEditorModuleTests(IllumoTestRegistry& registry)
{
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
        selected = document.createNode(SceneNodeKind::SolidCube, {});
        document.setTransform(
          selected,
          Transform3D::fromPosition(Vector3(
            static_cast<float>(i % 50) * 2, static_cast<float>(row) * 2, 0)));
      }
      std::string error;
      document.saveToFile("scene-v2-benchmark.ilsc", &error);
      document.loadFromFile("scene-v2-benchmark.ilsc", &error);
      EditorModuleTestAccess::setSelectedId(fixture.module, selected);
      EditorModuleTestAccess::rebuildGraph(fixture.module);
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
                                       SceneNodeKind::SolidCube);
    const std::string id = EditorModuleTestAccess::selectedId(fixture.module);
    SceneGraph& graph = document.graph();
    const SceneNodeHandle retained = document.nodeHandle(id);
    ISceneRenderAttachment* visual = graph.getAttachment(retained, 1);
    testTrue(
      g,
      visual != nullptr,
      "geometry has a persistent render attachment beside its picking proxy");
    const uint64_t structure = graph.getStructuralRevision();
    const SceneSnapshotView snapshot = graph.extract(nullptr);
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::CycleColor);
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::NudgeExtent);
    testTrue(g,
             document.nodeHandle(id) == retained &&
               graph.getAttachment(retained, 1) == visual &&
               graph.getStructuralRevision() == structure,
             "recolor and extent edits preserve node and visual identities");
    testTrue(
      g, !snapshot.get(), "visual reconfiguration retires old snapshots");
    document.translate(id, Vector3(1, 2, 3));
    EditorModuleTestAccess::rebuildGraph(fixture.module);
    Matrix4 world(1.0f);
    graph.getWorldTransform(retained, &world);
    testTrue(g,
             graph.getAttachment(retained, 1) == visual &&
               world[3][1] == document.findNode(id)->transform.position.y,
             "translation synchronizes without rebuild");
    for (size_t i = 0; i < 4200; ++i) {
      document.setColor(id, ColorRgba{ 80, 90, 100, 255 });
    }
    EditorModuleTestAccess::rebuildGraph(fixture.module);
    testTrue(g,
             document.nodeHandle(id) == retained &&
               graph.getAttachment(retained, 1) == visual,
             "journal overflow resync preserves graph and visual identity");
    fixture.module.Exit();
    testTrue(g,
             document.nodeHandle(id) == retained &&
               graph.getAttachmentCount(retained) == 1,
             "stop preserves document nodes and retires render bindings");
    testTrue(g,
             fixture.module.Start(&fixture.context) &&
               document.nodeHandle(id) == retained &&
               graph.getAttachmentCount(retained) == 2,
             "restart reconstructs only render bindings");
    EditorModuleTestAccess::deleteSelection(fixture.module);
    testTrue(g,
             !graph.isNodeValid(retained),
             "deletion invalidates the original generational handle");
    return g.failures;
  });
  registry.add("IllEd.Module.AttachmentBounds", []() {
    g = {};
    testEditorAttachmentForwardsBounds();
    return g.failures;
  });
  registry.add("IllEd.Module.PropertyCommands", []() {
    g = {};
    EditorFixture fixture;
    testTrue(g, fixture.started, "editor starts");
    EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
    EditorModuleTestAccess::createNode(fixture.module, SceneNodeKind::Empty);
    const std::string parent =
      EditorModuleTestAccess::selectedId(fixture.module);
    EditorModuleTestAccess::createNode(fixture.module,
                                       SceneNodeKind::SolidCube);
    const std::string child =
      EditorModuleTestAccess::selectedId(fixture.module);
    const Vector3 extent = document.findNode(child)->primitive.extent;
    document.setColor(child, ColorRgba{ 210, 90, 70, 255 });
    testTrue(g,
             document.loadFromText(document.encode(), nullptr),
             "reload clean property baseline");
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::NudgeExtent);
    testTrue(g,
             glm::length(document.findNode(child)->primitive.extent -
                         extent * 1.15f) < 0.0001f,
             "size command scales all selected extents");
    testTrue(g, document.isDirty(), "property command marks document dirty");
    const ColorRgba colors[] = { { 80, 180, 90, 255 },
                                 { 70, 140, 220, 255 },
                                 { 210, 90, 70, 255 } };
    for (const ColorRgba& expected : colors) {
      EditorModuleTestAccess::handleCommand(fixture.module,
                                            EditorCommand::CycleColor);
      const ColorRgba actual = document.findNode(child)->primitive.color;
      testTrue(g,
               actual.r == expected.r && actual.g == expected.g &&
                 actual.b == expected.b && actual.a == expected.a,
               "color command advances through the full palette cycle");
    }
    testEqStr(
      g, document.findNode(child)->parentId, parent, "child starts parented");
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::UnparentNode);
    testTrue(g,
             document.findNode(child)->parentId.empty(),
             "unparent command detaches child");
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
    EditorSceneGraphView* graphView =
      EditorModuleTestAccess::sceneGraphView(fixture.module);
    EditorSidebar* sidebar = EditorModuleTestAccess::sidebar(fixture.module);
    const bool graphCollapsed = graphView->isCollapsed();
    const bool sidebarCollapsed = sidebar->isCollapsed();
    const std::string document =
      EditorModuleTestAccess::document(fixture.module).encode();
    for (int iteration = 0; iteration < 2; ++iteration) {
      EditorModuleTestAccess::handleCommand(fixture.module,
                                            EditorCommand::ToggleSceneGraph);
      EditorModuleTestAccess::handleCommand(fixture.module,
                                            EditorCommand::ToggleSidebar);
      testTrue(g,
               graphView->isCollapsed() ==
                 (iteration == 0 ? !graphCollapsed : graphCollapsed),
               "scene graph toggle is reversible");
      testTrue(g,
               sidebar->isCollapsed() ==
                 (iteration == 0 ? !sidebarCollapsed : sidebarCollapsed),
               "inspector toggle is reversible");
    }
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
                                       SceneNodeKind::SolidCube);
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
      IlscCameraState cameraState = document.camera();
      cameraState.pitch = pitch;
      document.setCamera(cameraState);
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
      const std::string nearId =
        document.createNode(SceneNodeKind::SolidCube, {});
      const std::string farId =
        document.createNode(SceneNodeKind::SolidCube, {});
      Transform3D nearTransform = Transform3D::fromEuler(0.3f, 0.7f, 0.2f);
      nearTransform.position = origin + direction * 5.0f;
      document.setTransform(nearId, nearTransform);
      document.setTransform(
        farId, Transform3D::fromPosition(origin + direction * 8.0f));
      EditorModuleTestAccess::rebuildGraph(fixture.module);
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
                                         world3D ? SceneNodeKind::SolidCube
                                                 : SceneNodeKind::FilledRect);
      const std::string id = EditorModuleTestAccess::selectedId(fixture.module);
      EditorDocument& document =
        EditorModuleTestAccess::document(fixture.module);
      document.setTransform(id, Transform3D::fromPosition(Vector3(0.0f)));
      document.setExtent(id, Vector3(10.0f));
      std::string error;
      testTrue(g,
               document.loadFromText(document.encode(), &error),
               "reload clean scene");
      EditorModuleTestAccess::rebuildGraph(fixture.module);
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
