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
#include <cmath>
#include <filesystem>
#include <string>

static TestCounters g;

static void
seedShippedAtlas()
{
  const std::filesystem::path source =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "Assets" /
    "editor-ui-atlas.jpg";
  const std::filesystem::path destination =
    std::filesystem::current_path() / EditorUiAtlas::relativePath();
  std::error_code error;
  std::filesystem::create_directories(destination.parent_path(), error);
  std::filesystem::copy_file(source,
                             destination,
                             std::filesystem::copy_options::overwrite_existing,
                             error);
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
  const std::filesystem::path path = "module-roundtrip.ilsc";
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

void
registerEditorModuleTests(IllumoTestRegistry& registry)
{
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
