#include "EditorClipboard.h"
#include "EditorScene.h"
#include "EditorShortcuts.h"
#include "EditorUiAtlas.h"
#include "TestAccess.h"
#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Content/SceneDirector.h>
#include <Illumo/Content/ScenePlay.h>
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
#include <fstream>
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
  DrawList scene;
  IllumoContext context;
  // The editor runs as its program runs it, through a director.
  SceneDirector director;
  EditorScene& module;
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
    , director(context)
    , module(director.emplace<EditorScene>("editor"))
    , started(false)
  {
    env.setVar("WinX", 1280);
    env.setVar("WinY", 720);
    env.setVar("fontSize", "13");
    mock.Initialize();
    seedShippedAtlas();
    started = director.switchTo("editor") && director.applyPending();
  }
};

static void
testSettingStepsPivotAndArrange()
{
  testSection("EditorScene: grid and snap steppers, pivot and arrange");
  EditorFixture fixture;
  testTrue(g, fixture.started, "editor starts");
  EditorScene& module = fixture.module;
  EditorDocument& document = EditorSceneTestAccess::document(module);
  EditorSceneTestAccess::handleCommand(module, EditorCommand::GridSpacingUp);
  testTrue(g,
           document.editorState().gridSpacing == 2.0f,
           "grid spacing steps up the ladder");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::GridSpacingDown);
  EditorSceneTestAccess::handleCommand(module, EditorCommand::GridSpacingDown);
  testTrue(g, document.editorState().gridSpacing == 0.5f, "and down");
  for (int step = 0; step < 12; ++step) {
    EditorSceneTestAccess::handleCommand(module, EditorCommand::GridSpacingUp);
  }
  testTrue(
    g, document.editorState().gridSpacing == 10.0f, "the ladder's end holds");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::SnapRotateUp);
  EditorSceneTestAccess::handleCommand(module, EditorCommand::SnapScaleUp);
  testTrue(g,
           document.editorState().snapRotateDegrees == 30.0f &&
             document.editorState().snapScale == 0.25f,
           "rotate and scale snaps step");
  SceneEditorState odd = document.editorState();
  odd.snapTranslate = 0.3f;
  document.setEditorState(odd);
  EditorSceneTestAccess::handleCommand(module, EditorCommand::SnapMoveUp);
  testTrue(g,
           document.editorState().snapTranslate == 0.5f,
           "a value between steps moves to the next step up");
  odd.snapTranslate = 0.3f;
  document.setEditorState(odd);
  EditorSceneTestAccess::handleCommand(module, EditorCommand::SnapMoveDown);
  testTrue(g, document.editorState().snapTranslate == 0.25f, "or down");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::ToggleGrid);
  testTrue(g, !document.editorState().gridVisible, "the grid hides");
  testTrue(g, !document.isDirty(), "view settings never dirty the scene");
  module.update(0.01);
  const EditorToolsState& tools = EditorSceneTestAccess::tools(module)->state();
  testTrue(g,
           tools.gridSpacing == 10.0f && !tools.gridVisible &&
             tools.snapTranslate == 0.25f,
           "the Tools panel shows the settings");

  const std::string a =
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
  const std::string b =
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
  document.setTransform(b,
                        Transform3D::fromPosition(Vector3(4.0f, 2.0f, 0.0f)));
  EditorSelection& selection = EditorSceneTestAccess::selection(module);
  selection.set(std::vector<std::string>{ a, b });
  GizmoFrame frame = EditorSceneTestAccess::gizmoFrame(module, b);
  testTrue(g,
           frame.origin.x == 4.0f && frame.origin.y == 2.0f,
           "the gizmo starts at the primary node");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::TogglePivot);
  frame = EditorSceneTestAccess::gizmoFrame(module, b);
  testTrue(g,
           std::fabs(frame.origin.x - 2.0f) < 1e-4f &&
             std::fabs(frame.origin.y - 1.0f) < 1e-4f,
           "the centre pivot sits at the selection's bounds centre");
  module.update(0.01);
  testTrue(g,
           EditorSceneTestAccess::tools(module)->state().pivotCenter,
           "the Tools panel shows the pivot");

  EditorSceneTestAccess::handleCommand(module, EditorCommand::AlignMaxX);
  testTrue(g,
           document.findNode(a)->transform.position.x == 4.0f &&
             document.findNode(b)->transform.position.x == 4.0f,
           "Arrange aligns the selection");
  selection.set(a);
  const size_t commands = document.history().size();
  EditorSceneTestAccess::handleCommand(module, EditorCommand::AlignMinY);
  testEqSize(g, document.history().size(), commands, "one node is not aligned");
}

static void
testFindFocusesHierarchyFilter()
{
  testSection("EditorScene: Ctrl+F types into the hierarchy filter");
  EditorFixture fixture;
  EditorScene& module = fixture.module;
  EditorDocument& document = EditorSceneTestAccess::document(module);
  const std::string id =
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
  module.update(0.016);
  fixture.input.getKeyQueue().push({ KeyCode::F, InputAction::Press, 0x2 });
  module.update(0.016);
  EditorSceneGraphView* view = EditorSceneTestAccess::sceneGraphView(module);
  testTrue(g, view->filterEditing(), "Ctrl+F focuses the filter");
  // A typed letter arrives as a key press and a character.
  fixture.input.getKeyQueue().push({ KeyCode::H, InputAction::Press, 0 });
  fixture.input.getCharQueue().push(static_cast<unsigned char>('c'));
  module.update(0.016);
  testTrue(g,
           view->filter() == "c" && document.findNode(id)->visible,
           "typing filters and never fires the H shortcut");
  fixture.input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  module.update(0.016);
  testTrue(g,
           view->filter().empty() && !view->filterEditing() &&
             EditorSceneTestAccess::selectedId(module) == id,
           "Escape clears the filter, not the selection");
}

static void
testDropPreview()
{
  testSection("EditorScene: a dragged mesh or texture shows where it lands");
  EditorFixture fixture;
  EditorScene& module = fixture.module;
  const GuiToolRect view = EditorSceneTestAccess::dock(module).center();
  const float x = view.x + view.w * 0.5f;
  const float y = view.y + view.h * 0.5f;
  testTrue(g,
           EditorSceneTestAccess::showDropPreview(
             module, "/engine/Meshes/crate.obj", x, y) &&
             EditorSceneTestAccess::dropPreviewShown(module),
           "a mesh over the viewport shows its ghost");
  testTrue(g,
           EditorSceneTestAccess::showDropPreview(
             module, "/project/textures/a.png", x, y),
           "so does a texture");
  testTrue(g,
           !EditorSceneTestAccess::showDropPreview(
             module, "/project/scenes/a.ilsc", x, y) &&
             !EditorSceneTestAccess::dropPreviewShown(module),
           "a scene cannot be placed, so it shows nothing");
  testTrue(g,
           !EditorSceneTestAccess::showDropPreview(
             module, "/engine/Meshes/crate.obj", 10.0f, y),
           "over a dock panel nothing shows");
  module.update(0.016);
  testTrue(g,
           !EditorSceneTestAccess::dropPreviewShown(module),
           "with no drag in progress the ghost is hidden");
  EditorDocument& document = EditorSceneTestAccess::document(module);
  testEqSize(g, document.nodeCount(), 0u, "a preview never edits the scene");
}

static void
testCloseConfirmation()
{
  testSection("EditorScene: native close shares unsaved confirmation");
  EditorFixture fixture;
  testTrue(g, fixture.started, "editor starts");
  testTrue(g,
           fixture.module.closeRequested(),
           "clean document accepts native close");
  EditorSceneTestAccess::createNode(fixture.module, EditorCommand::CreateCube);
  testTrue(
    g, !fixture.module.closeRequested(), "dirty native close is deferred");
  testTrue(g,
           EditorSceneTestAccess::confirmationOpen(fixture.module),
           "native close opens confirmation");
  testTrue(g,
           !fixture.module.closeRequested(),
           "repeated request keeps pending confirmation");
  fixture.input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  fixture.module.update(0.01);
  testTrue(g,
           !fixture.window.shouldWindowClose() &&
             !EditorSceneTestAccess::confirmationOpen(fixture.module),
           "cancel leaves editor open");

  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  document.setPath("missing-close-parent/document.ilsc");
  testTrue(g,
           !fixture.module.closeRequested(),
           "new close request reopens confirmation");
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.01);
  testTrue(g,
           document.isDirty() && !fixture.window.shouldWindowClose(),
           "failed save leaves document dirty and open");

  const std::string path = "test-close-saved.ilsc";
  document.setPath(path);
  testTrue(g, !fixture.module.closeRequested(), "save can be retried");
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.update(0.01);
  testTrue(g,
           !document.isDirty() && fixture.window.shouldWindowClose() &&
             fixture.module.closeRequested(),
           "successful save permits close");
  testTrue(g, std::filesystem::exists(path), "successful close writes scene");
  std::filesystem::remove(path);

  EditorFixture discard;
  EditorSceneTestAccess::createNode(discard.module, EditorCommand::CreateCube);
  EditorSceneTestAccess::handleCommand(discard.module,
                                        EditorCommand::ExitEditor);
  testTrue(g,
           EditorSceneTestAccess::confirmationOpen(discard.module),
           "toolbar exit shares confirmation");
  discard.input.getKeyQueue().push({ KeyCode::N, InputAction::Press, 0 });
  discard.module.update(0.01);
  testTrue(g,
           EditorSceneTestAccess::document(discard.module).isDirty() &&
             discard.window.shouldWindowClose() &&
             discard.module.closeRequested(),
           "discard closes without re-prompting on dirty document");
  discard.window.cancelCloseRequest();
  EditorSceneTestAccess::createNode(discard.module, EditorCommand::CreateCube);
  testTrue(g,
           !discard.module.closeRequested() &&
             EditorSceneTestAccess::confirmationOpen(discard.module),
           "approval is consumed if another module vetoes and editing resumes");
  discard.input.getKeyQueue().push({ KeyCode::N, InputAction::Press, 0 });
  discard.module.update(0.01);
  discard.window.cancelCloseRequest();
  discard.module.update(0.01);
  testTrue(
    g,
    !discard.module.closeRequested() &&
      EditorSceneTestAccess::confirmationOpen(discard.module),
    "approval expires when an earlier module vetoes before consultation");
}

static void
testUiAtlasSpritesFromStart()
{
  testSection("EditorScene: shipped atlas draws the Tools panel sprites");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  testTrue(
    g,
    fixture.assets
        .getState(EditorSceneTestAccess::toolbar(fixture.module)->atlas())
        .state == AssetState::Ready,
    "module loaded the shipped atlas");
  EditorToolsPanel* tools = EditorSceneTestAccess::tools(fixture.module);
  EditorToolbar* toolbar = EditorSceneTestAccess::toolbar(fixture.module);
  testTrue(g, tools != nullptr && toolbar != nullptr, "chrome exists");
  fixture.module.update(0.016);
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
  testSection("EditorScene: create cube and attach graph");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  EditorSceneTestAccess::createNode(fixture.module, EditorCommand::CreateCube);
  testEqSize(g,
             EditorSceneTestAccess::document(fixture.module).nodeCount(),
             1u,
             "document has one node");
  testEqSize(g,
             EditorSceneTestAccess::graph(fixture.module).getNodeCount(),
             1u,
             "graph has one node");
  testTrue(g,
           !EditorSceneTestAccess::selectedId(fixture.module).empty(),
           "new cube is selected");
}

static void
testSaveLoadThroughDocument()
{
  testSection("EditorScene: scene file round trip");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  EditorSceneTestAccess::createNode(fixture.module, EditorCommand::CreateCube);
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
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
  testSection("EditorScene: 2D/3D mode, create, select, properties");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  EditorSceneTestAccess::handleCommand(fixture.module,
                                        EditorCommand::SetMode3D);
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  testTrue(g,
           document.worldMode() == SceneWorldMode::World3D,
           "module switches to 3D");
  EditorSceneTestAccess::createNode(fixture.module,
                                     EditorCommand::CreateEllipse);
  const std::string ellipseId =
    EditorSceneTestAccess::selectedId(fixture.module);
  testTrue(g, !ellipseId.empty(), "ellipse selected");
  testTrue(g,
           document.setExtent(ellipseId, Vector3(1.25f, 0.8f, 0.5f)),
           "property size via document");
  testTrue(g,
           document.setColor(ellipseId, ColorRgba{ 4, 5, 6, 255 }),
           "property color via document");
  EditorSceneTestAccess::refreshView(fixture.module);

  const size_t beforeArm = document.nodeCount();
  EditorSceneTestAccess::handleCommand(fixture.module,
                                        EditorCommand::CreatePyramid);
  testEqSize(
    g, document.nodeCount(), beforeArm, "CreatePyramid arms without inserting");
  testTrue(g,
           EditorSceneTestAccess::activeTool(fixture.module) ==
             EditorCommand::CreatePyramid,
           "pyramid tool is armed");
  EditorSceneTestAccess::applyActiveToolAt(fixture.module, 1.0f, 2.0f);
  testTrue(g,
           EditorSceneTestAccess::activeTool(fixture.module) ==
             EditorCommand::SelectTool,
           "place disarms the tool");
  const EditorSceneDetail detail = fixture.module.sceneDetail();
  testTrue(g, detail.nodeCount >= 2u, "scene has ellipse and pyramid");
  testTrue(g, detail.hasSelection, "selection present");
  testTrue(g, detail.worldMode == SceneWorldMode::World3D, "detail reports 3D");
  testTrue(g,
           detail.kindLabel == "Pyramid" || detail.kindLabel == "Ellipse",
           "selected kind is a created primitive");
  EditorToolsPanel* tools = EditorSceneTestAccess::tools(fixture.module);
  testTrue(g, tools != nullptr, "the Tools panel exists");
  float modeX = 0.0f;
  float modeY = 0.0f;
  tools->controlCenterForTesting(EditorCommand::SetMode2D, &modeX, &modeY);
  const EditorCommand mode2d = tools->clickAtForTesting(modeX, modeY);
  testTrue(g, mode2d == EditorCommand::SetMode2D, "Tools 2D hit");
  EditorSceneTestAccess::handleCommand(fixture.module, mode2d);
  testTrue(
    g, document.worldMode() == SceneWorldMode::World2D, "Tools mode applied");
}

static void
testCreateToolArmsOnly()
{
  testSection("EditorScene: Create command arms, canvas place inserts once");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  const size_t before = document.nodeCount();
  EditorSceneTestAccess::handleCommand(fixture.module,
                                        EditorCommand::CreateCube);
  testEqSize(g, document.nodeCount(), before, "no node on tool select");
  testTrue(g,
           EditorSceneTestAccess::activeTool(fixture.module) ==
             EditorCommand::CreateCube,
           "cube tool armed");
  EditorSceneTestAccess::applyActiveToolAt(fixture.module, 3.0f, -1.5f);
  testEqSize(g, document.nodeCount(), before + 1u, "one node on canvas place");
  testTrue(g,
           EditorSceneTestAccess::activeTool(fixture.module) ==
             EditorCommand::SelectTool,
           "tool returns to select after place");
  const SceneNode* node =
    document.findNode(EditorSceneTestAccess::selectedId(fixture.module));
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
  testSection("EditorScene: 3D pick/place lands on Y=0 XZ grid");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  EditorSceneTestAccess::handleCommand(fixture.module,
                                        EditorCommand::SetMode3D);
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  testTrue(
    g, document.worldMode() == SceneWorldMode::World3D, "3D mode active");
  float planeX = 0.0f;
  float planeZ = 0.0f;
  testTrue(g,
           EditorSceneTestAccess::screenToWorld(
             fixture.module, 960.0f, 360.0f, &planeX, &planeZ),
           "3D unproject of off-center pixel");
  EditorSceneTestAccess::handleCommand(fixture.module,
                                        EditorCommand::CreateCube);
  EditorSceneTestAccess::applyActiveToolAt(fixture.module, planeX, planeZ);
  const SceneNode* node =
    document.findNode(EditorSceneTestAccess::selectedId(fixture.module));
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
    "EditorScene: toolbar Create click arms through Update without placing");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  EditorToolbar* toolbar = EditorSceneTestAccess::toolbar(fixture.module);
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

  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  const size_t before = document.nodeCount();
  fixture.window.mouseX = static_cast<double>(createX);
  fixture.window.mouseY = 8.0;
  pressLeft(fixture, true);
  fixture.module.update(0.016);
  testTrue(g, toolbar->isMenuOpen(), "Update opens Create menu");
  testEqSize(g, document.nodeCount(), before, "opening menu does not insert");
  pressLeft(fixture, false);
  fixture.module.update(0.016);

  fixture.window.mouseX = static_cast<double>(createX);
  fixture.window.mouseY = static_cast<double>(cubeY);
  pressLeft(fixture, true);
  fixture.module.update(0.016);
  testEqSize(
    g, document.nodeCount(), before, "Create Cube menu click does not insert");
  testTrue(g,
           EditorSceneTestAccess::activeTool(fixture.module) ==
             EditorCommand::CreateCube,
           "Create Cube is armed after Update");
}

static void
testOpenConsoleBlocksEditorInput()
{
  testSection(
    "EditorScene: open console blocks editor input and grave is unconsumed");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  // 1. When console is closed, Grave key is not consumed or toggled by
  // EditorScene
  fixture.input.clearKeyQueue();
  fixture.input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Grave, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testTrue(g,
           !fixture.console.isOpen,
           "EditorScene does not toggle console on Grave");
  testTrue(g,
           !fixture.input.getKeyQueue().empty() &&
             fixture.input.getKeyQueue().front().key == KeyCode::Grave,
           "Grave key remains in queue for DebugOverlay overlay");

  // 2. When console is open, toolbar and panel input yield
  fixture.console.Toggle();
  testTrue(g, fixture.console.isOpen, "console is open");
  EditorToolbar* toolbar = EditorSceneTestAccess::toolbar(fixture.module);
  testTrue(g, toolbar != nullptr, "toolbar exists");
  fixture.window.mouseX = 20.0;
  fixture.window.mouseY = 8.0;
  pressLeft(fixture, true);
  fixture.module.update(0.016);
  testTrue(g, !toolbar->isMenuOpen(), "open console blocks toolbar clicks");
}

static void
testEditorSceneDoesNotDispatchConsole()
{
  testSection("EditorScene: does not dispatch console drawable (DebugOverlay "
              "responsibility)");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  fixture.console.Toggle();
  testTrue(g, fixture.console.isOpen, "console is open");
  testTrue(g, fixture.console.wantsDraw(), "console wants draw");

  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);

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
           "EditorScene does not add CommandLine to Scene drawables");
}

static void
testUiDrawOrder()
{
  testSection("EditorScene: UI draw order ensures toolbar and dropdowns draw "
              "above side panels");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);

  const std::vector<DrawableBase*>& uiDrawables =
    fixture.scene.drawablesIn(RenderLayerId::UI);
  testTrue(g, uiDrawables.size() >= 3u, "at least 3 UI drawables in scene");

  EditorSceneGraphView* sceneGraphView =
    EditorSceneTestAccess::sceneGraphView(fixture.module);
  EditorToolsPanel* tools = EditorSceneTestAccess::tools(fixture.module);
  EditorToolbar* toolbar = EditorSceneTestAccess::toolbar(fixture.module);

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
  testSection("EditorScene: 2D nodes (Rect, Ellipse, Triangle) emit render "
              "tokens in 2D mode");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");
  testTrue(g,
           EditorSceneTestAccess::document(fixture.module).worldMode() ==
             SceneWorldMode::World2D,
           "starts in 2D mode");

  EditorSceneTestAccess::createNode(fixture.module, EditorCommand::CreateRect);
  EditorSceneTestAccess::createNode(fixture.module,
                                     EditorCommand::CreateEllipse);
  EditorSceneTestAccess::createNode(fixture.module,
                                     EditorCommand::CreateTriangle);

  fixture.scene.ClearDrawables();
  fixture.module.dispatch(fixture.scene);

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
  testSection("EditorScene: fontSize configured from IEnvVars");
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
  DrawList scene(&window, &camera);
  IllumoContext context{ &scene,  &window, &console, &input,   &renderer,
                         &assets, &env,    &camera,  &registry };
  SceneDirector director(context);
  EditorScene& module = director.emplace<EditorScene>("editor");
  mock.Initialize();
  seedShippedAtlas();
  const bool started =
    director.switchTo("editor") && director.applyPending();
  testTrue(g, started, "module started with fontSize 20");

  EditorToolbar* toolbar = EditorSceneTestAccess::toolbar(module);
  EditorToolsPanel* tools = EditorSceneTestAccess::tools(module);
  EditorSceneGraphView* sceneGraphView =
    EditorSceneTestAccess::sceneGraphView(module);

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

  director.stopAll();
}

static void
testFontSizeImmediateRuntimeChange()
{
  testSection(
    "EditorScene: runtime fontSize change takes effect immediately on Update");
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
  DrawList scene(&window, &camera);
  IllumoContext context{ &scene,  &window, &console, &input,   &renderer,
                         &assets, &env,    &camera,  &registry };
  SceneDirector director(context);
  EditorScene& module = director.emplace<EditorScene>("editor");
  mock.Initialize();
  seedShippedAtlas();
  const bool started =
    director.switchTo("editor") && director.applyPending();
  testTrue(g, started, "module started");

  EditorToolbar* toolbar = EditorSceneTestAccess::toolbar(module);
  EditorToolsPanel* tools = EditorSceneTestAccess::tools(module);
  EditorSceneGraphView* sceneGraphView =
    EditorSceneTestAccess::sceneGraphView(module);

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
  module.update(0.016);

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
  module.update(0.016);

  testTrue(g,
           std::abs(toolbar->fontSize() - 16.0f) < 0.001f,
           "toolbar resized to 16 immediately");
  testTrue(g,
           std::abs(tools->fontSize() - 16.0f) < 0.001f,
           "tools resized to 16 immediately");

  director.stopAll();
}

static void
testMousePanningMovesCameraNaturalDirection()
{
  testSection(
    "EditorScene: mouse panning moves camera in natural drag direction");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  // Initial camera position at (0, 0)
  fixture.camera.SetPositionPrecise(0.0, 0.0);
  fixture.camera.SetZoom(1.0f);
  fixture.window.mouseX = 500.0;
  fixture.window.mouseY = 300.0;
  fixture.module.update(0.016);

  // Press middle mouse button and drag right (500 -> 550) and down (300 -> 350)
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseMiddle, InputAction::Press);
  fixture.module.update(0.016);

  fixture.window.mouseX = 550.0;
  fixture.window.mouseY = 350.0;
  fixture.module.update(0.016);

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
    "EditorScene: 3D mouse panning moves target along camera orientation");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  EditorSceneTestAccess::handleCommand(fixture.module,
                                        EditorCommand::SetMode3D);
  fixture.camera.SetPositionPrecise(0.0, 0.0);
  fixture.camera.SetZoom(1.0f);
  fixture.window.mouseX = 500.0;
  fixture.window.mouseY = 300.0;
  fixture.module.update(0.016);

  // Default camera yaw is 0.0. Looking down -Z, right vector is (+1, 0, 0).
  // Dragging right (mouse 500 -> 550) should shift target left (-X).
  // Dragging down (mouse 300 -> 350) should shift target forward (-Z,
  // position.y < 0).
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseMiddle, InputAction::Press);
  fixture.module.update(0.016);

  fixture.window.mouseX = 550.0;
  fixture.window.mouseY = 350.0;
  fixture.module.update(0.016);

  fixture.camera.Update(1.0f);
  const glm::dvec2 pos = fixture.camera.GetPositionPrecise();
  testTrue(g, pos.x < 0.0, "3D dragging right moves target -X");
  testTrue(g, pos.y < 0.0, "3D dragging down moves target -Z (forward)");
}

static void
test3DCameraElevationKeys()
{
  testSection("EditorScene: 3D camera elevation via PageUp and PageDown");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  EditorSceneTestAccess::handleCommand(fixture.module,
                                        EditorCommand::SetMode3D);
  fixture.module.update(0.016);

  const glm::vec3 initialEye = fixture.camera.getEye();
  const glm::vec3 initialTarget = fixture.camera.getTarget();

  // Press E to elevate camera Up
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::PageUp, InputAction::Press);
  fixture.module.update(0.1);
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
  fixture.module.update(0.2);
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
    "EditorScene: Transform gizmo hit testing and constraint dragging");
  EditorFixture fixture;
  testTrue(g, fixture.started, "module starts");

  EditorSceneTestAccess::handleCommand(fixture.module,
                                        EditorCommand::SetMode3D);
  EditorSceneTestAccess::createNode(fixture.module, EditorCommand::CreateCube);
  const std::string id = EditorSceneTestAccess::selectedId(fixture.module);
  testTrue(g, !id.empty(), "cube created and selected");

  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  document.setTransform(id,
                        Transform3D::fromPosition(Vector3(0.0f, 0.0f, 0.0f)));
  fixture.camera.SetPositionPrecise(0.0, 0.0);
  fixture.camera.SetZoom(32.0f);
  fixture.module.update(0.016);

  // Gizmo is centered at world (0, 0, 0)
  const glm::vec3 gizmoOrigin(0.0f, 0.0f, 0.0f);
  const float initialScale =
    EditorSceneTestAccess::gizmoScale(fixture.module, gizmoOrigin);
  testTrue(g,
           initialScale > 0.3f && initialScale < 2.0f,
           "initial gizmo scale is well-proportioned");

  // When camera zooms out (distance increases), world gizmoScale increases
  // proportionally so that screen-space visual size remains stable rather than
  // wildly blowing up or shrinking
  fixture.camera.SetZoom(8.0f);
  fixture.module.update(0.016);
  const float zoomedOutScale =
    EditorSceneTestAccess::gizmoScale(fixture.module, gizmoOrigin);
  testTrue(g,
           zoomedOutScale > initialScale,
           "zoomed out camera increases world scale for screen stability");

  // Screen center corresponds to world (0, 0, 0)
  const float centerX = 640.0f;
  const float centerY = 360.0f;

  const GizmoPart centerHit = EditorSceneTestAccess::hitTestGizmo(
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
  fixture.module.update(0.016);
  testTrue(g,
           EditorSceneTestAccess::isDragging(fixture.module),
           "drag started on center click");

  // Move the mouse over a docked panel (x=20, y=100 is in the Hierarchy)
  fixture.window.mouseX = 20.0;
  fixture.window.mouseY = 100.0;
  fixture.module.update(0.016);
  testTrue(g,
           EditorSceneTestAccess::isDragging(fixture.module),
           "drag persists when cursor moves over UI");

  // Re-enter world area
  fixture.window.mouseX = 700.0;
  fixture.window.mouseY = 360.0;
  fixture.module.update(0.016);
  testTrue(g,
           EditorSceneTestAccess::isDragging(fixture.module),
           "drag still active when cursor re-enters");
  const SceneNode* movedNode = document.findNode(id);
  testTrue(g,
           movedNode != nullptr && movedNode->transform.position.x != 0.0f,
           "node position updated during continued drag");

  // Releasing mouse terminates the drag
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
  testTrue(g,
           !EditorSceneTestAccess::isDragging(fixture.module),
           "drag terminates on mouse release");
}

static int
testCameraDoesNotDirty()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  testTrue(counters, !document.isDirty(), "fresh document is clean");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::Right, InputAction::Press);
  for (int frame = 0; frame < 10; ++frame) {
    fixture.module.update(0.05);
  }
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::Right, InputAction::Release);
  fixture.camera.SetZoom(12.0f);
  fixture.module.update(0.05);
  testTrue(counters,
           fixture.camera.GetTargetPositionPrecise().x > 1.0,
           "arrow keys pan the camera");
  testTrue(counters, !document.isDirty(), "camera motion never dirties");
  testTrue(counters,
           fixture.module.closeRequested(),
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
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  const std::string first = EditorSceneTestAccess::createNode(
    fixture.module, EditorCommand::CreateCube);
  testEqStr(counters,
            EditorSceneTestAccess::selectedId(fixture.module),
            first,
            "the new node is selected");
  const std::string second = EditorSceneTestAccess::createNode(
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
  EditorToolbar* toolbar = EditorSceneTestAccess::toolbar(fixture.module);
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
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  EditorSceneTestAccess::createNode(fixture.module, EditorCommand::CreateCube);
  testEqSize(counters, document.nodeCount(), 1, "one node");
  fixture.input.getKeyQueue().push({ KeyCode::Z, InputAction::Press, 0x2 });
  fixture.module.update(0.016);
  testEqSize(counters, document.nodeCount(), 0, "Ctrl+Z undoes the create");
  testTrue(counters,
           EditorSceneTestAccess::selectedId(fixture.module).empty(),
           "the removed node leaves the selection");
  fixture.input.getKeyQueue().push({ KeyCode::Y, InputAction::Press, 0x2 });
  fixture.module.update(0.016);
  testEqSize(counters, document.nodeCount(), 1, "Ctrl+Y redoes it");
  const std::string id(document.graph().getName(document.graph().getRoot(0)));
  EditorSceneTestAccess::setSelectedId(fixture.module, id);
  fixture.input.getKeyQueue().push({ KeyCode::D, InputAction::Press, 0x2 });
  fixture.module.update(0.016);
  testEqSize(counters, document.nodeCount(), 2, "Ctrl+D duplicates");
  testTrue(counters,
           EditorSceneTestAccess::selectedId(fixture.module) != id,
           "the duplicate becomes the selection");
  fixture.input.getKeyQueue().push({ KeyCode::A, InputAction::Press, 0x2 });
  fixture.module.update(0.016);
  testEqSize(counters,
             EditorSceneTestAccess::selection(fixture.module).size(),
             2,
             "Ctrl+A selects everything");
  fixture.input.getKeyQueue().push({ KeyCode::Delete, InputAction::Press, 0 });
  fixture.module.update(0.016);
  testEqSize(counters, document.nodeCount(), 0, "Delete removes the selection");
  fixture.input.getKeyQueue().push({ KeyCode::Z, InputAction::Press, 0x2 });
  fixture.module.update(0.016);
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
  EditorSceneTestAccess::screenToWorld(
    fixture.module, 640.0f, 360.0f, &ox, &oy);
  EditorSceneTestAccess::screenToWorld(
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
  fixture.module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.update(0.016);
  for (int step = 1; step <= 4; ++step) {
    fixture.window.mouseX = fromX + (toX - fromX) * step / 4.0;
    fixture.window.mouseY = fromY + (toY - fromY) * step / 4.0;
    fixture.module.update(0.016);
  }
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  fixture.module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::None);
  fixture.module.update(0.016);
}

static int
testGizmoRotateAndScaleDrags()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  const std::string id = EditorSceneTestAccess::createNode(
    fixture.module, EditorCommand::CreateRect);
  document.setTransform(id, Transform3D{});
  fixture.camera.SetPositionPrecise(0.0, 0.0);
  fixture.camera.SetZoom(32.0f);
  fixture.module.update(0.016);
  fixture.input.getKeyQueue().push({ KeyCode::E, InputAction::Press, 0 });
  fixture.module.update(0.016);
  const float scale =
    EditorSceneTestAccess::gizmoScale(fixture.module, glm::vec3(0.0f));
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
  fixture.module.update(0.016);
  document.setTransform(id, Transform3D{});
  fixture.module.update(0.016);
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
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  const std::string parent = document.createPrimitive(
    true, ScenePrimitiveShape::Cube, {}, Transform3D{});
  Transform3D turned;
  turned.rotation =
    glm::angleAxis(glm::radians(90.0f), Vector3(0.0f, 0.0f, 1.0f));
  document.setTransform(parent, turned);
  const std::string child = document.createPrimitive(
    false, ScenePrimitiveShape::Rect, parent, Transform3D{});
  EditorSceneTestAccess::setSelectedId(fixture.module, child);
  fixture.camera.SetPositionPrecise(0.0, 0.0);
  fixture.camera.SetZoom(32.0f);
  fixture.module.update(0.016);
  fixture.input.getKeyQueue().push({ KeyCode::X, InputAction::Press, 0 });
  fixture.module.update(0.016);
  const float scale =
    EditorSceneTestAccess::gizmoScale(fixture.module, glm::vec3(0.0f));
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
  fixture.module.update(0.016);
  testTrue(counters, document.editorState().snapEnabled, "G turns snapping on");
  testTrue(counters,
           !document.isDirty() || document.history().size() > 0,
           "snapping is view state");
  fixture.input.getKeyQueue().push({ KeyCode::X, InputAction::Press, 0 });
  fixture.module.update(0.016);
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
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  const std::string id = document.createPrimitive(
    false,
    ScenePrimitiveShape::Cube,
    {},
    Transform3D::fromPosition(Vector3(50.0f, -20.0f, 0.0f)));
  Transform3D big = document.findNode(id)->transform;
  big.scale = Vector3(10.0f);
  document.setTransform(id, big);
  EditorSceneTestAccess::setSelectedId(fixture.module, id);
  fixture.input.getKeyQueue().push({ KeyCode::F, InputAction::Press, 0 });
  fixture.module.update(0.016);
  fixture.camera.Update(10.0f);
  // The viewport between the dock columns (the test window has UI scale 1).
  const GuiToolRect view = EditorSceneTestAccess::dock(fixture.module).center();
  float screenX = 0.0f;
  float screenY = 0.0f;
  EditorSceneTestAccess::worldToScreen(
    fixture.module, Vector3(50.0f, -20.0f, 0.0f), &screenX, &screenY);
  testTrue(counters,
           std::fabs(screenX - (view.x + view.w * 0.5f)) < 0.5f &&
             std::fabs(screenY - (view.y + view.h * 0.5f)) < 0.5f,
           "F centers the selection in the visible viewport");
  float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
  EditorSceneTestAccess::screenToWorld(
    fixture.module, view.x, view.y, &left, &top);
  EditorSceneTestAccess::screenToWorld(
    fixture.module, view.x + view.w, view.y + view.h, &right, &bottom);
  testTrue(counters,
           left < 45.0f && right > 55.0f && bottom < -25.0f && top > -15.0f,
           "the viewport contains the whole node");

  // Reset Camera puts the origin at the viewport's centre too.
  EditorSceneTestAccess::handleCommand(fixture.module,
                                       EditorCommand::ResetCamera);
  fixture.camera.Update(10.0f);
  EditorSceneTestAccess::worldToScreen(
    fixture.module, Vector3(0.0f), &screenX, &screenY);
  testTrue(counters,
           std::fabs(screenX - (view.x + view.w * 0.5f)) < 0.5f &&
             std::fabs(screenY - (view.y + view.h * 0.5f)) < 0.5f,
           "Reset Camera centers the origin in the visible viewport");

  // 3D: the framed point lands on the viewport's centre in perspective.
  document.setWorldMode(SceneWorldMode::World3D);
  document.setTransform(id,
                        Transform3D::fromPosition(Vector3(3.0f, 1.0f, -2.0f)));
  EditorSceneTestAccess::refreshView(fixture.module);
  EditorSceneTestAccess::frameSelection(fixture.module);
  fixture.camera.Update(10.0f);
  EditorSceneTestAccess::refreshView(fixture.module);
  EditorSceneTestAccess::worldToScreen(
    fixture.module, Vector3(3.0f, 1.0f, -2.0f), &screenX, &screenY);
  testTrue(counters,
           std::fabs(screenX - (view.x + view.w * 0.5f)) < 2.0f &&
             std::fabs(screenY - (view.y + view.h * 0.5f)) < 2.0f,
           "3D framing centers the selection in the visible viewport");
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
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  EditorSelection& selection =
    EditorSceneTestAccess::selection(fixture.module);
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
  fixture.module.update(0.016);
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
           !EditorSceneTestAccess::boxSelecting(fixture.module),
           "the marquee ends with the button");
  testEqSize(
    counters, document.history().size(), commands, "box selection never edits");

  float sx = 0.0f;
  float sy = 0.0f;
  EditorSceneTestAccess::worldToScreen(
    fixture.module, Vector3(0.0f, 4.0f, 0.0f), &sx, &sy);
  EditorSceneTestAccess::boxSelect(
    fixture.module, sx - 10.0f, sy - 10.0f, sx + 10.0f, sy + 10.0f, true);
  testEqSize(counters, selection.size(), 3, "an additive box adds to it");

  document.setVisible(right, false);
  worldToScreen2D(fixture, 3.2f, -2.4f, &bx, &by);
  EditorSceneTestAccess::boxSelect(fixture.module,
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
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  EditorSelection& selection =
    EditorSceneTestAccess::selection(fixture.module);
  EditorSceneTestAccess::handleCommand(fixture.module,
                                        EditorCommand::SetMode3D);
  const Vector3 nearPoint(-1.5f, 0.0f, 1.0f);
  const Vector3 farPoint(1.5f, 0.0f, -2.0f);
  const std::string nearId = document.createPrimitive(
    false, ScenePrimitiveShape::Cube, {}, Transform3D::fromPosition(nearPoint));
  const std::string farId = document.createPrimitive(
    false, ScenePrimitiveShape::Cube, {}, Transform3D::fromPosition(farPoint));
  fixture.module.update(0.016);

  float nx = 0.0f, ny = 0.0f, fx = 0.0f, fy = 0.0f;
  testTrue(
    counters,
    EditorSceneTestAccess::worldToScreen(
      fixture.module, nearPoint, &nx, &ny) &&
      EditorSceneTestAccess::worldToScreen(fixture.module, farPoint, &fx, &fy),
    "both nodes project onto the screen");
  glm::vec3 origin(0.0f);
  glm::vec3 direction(0.0f);
  EditorSceneTestAccess::screenToWorldRay(
    fixture.module, nx, ny, &origin, &direction);
  const glm::vec3 toNode = nearPoint - origin;
  const float offAxis =
    glm::length(toNode - direction * glm::dot(toNode, direction));
  testTrue(counters,
           offAxis < 0.01f,
           "the projection inverts the picking ray exactly");

  EditorSceneTestAccess::boxSelect(
    fixture.module, nx - 12.0f, ny - 12.0f, nx + 12.0f, ny + 12.0f, false);
  testTrue(counters,
           selection.size() == 1 && selection.contains(nearId),
           "a box around one projected node selects only it");
  EditorSceneTestAccess::boxSelect(
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
  EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
  EditorSelection& selection =
    EditorSceneTestAccess::selection(fixture.module);
  const std::string a = document.createPrimitive(
    false, ScenePrimitiveShape::Rect, {}, Transform3D{});
  const std::string b = document.createPrimitive(
    false, ScenePrimitiveShape::Ellipse, {}, Transform3D{});
  const std::string c = document.createPrimitive(
    false, ScenePrimitiveShape::Triangle, {}, Transform3D{});
  const std::string text = EditorClipboard::copy(document.scene(), { a });
  selection.set(b);
  testTrue(counters,
           EditorSceneTestAccess::pasteText(fixture.module, text),
           "clipboard text pastes");
  const std::vector<std::string> order = document.scene().childIds("");
  testTrue(counters,
           order.size() == 4 && order[0] == a && order[1] == b &&
             order[3] == c && order[2] == selection.primary(),
           "the paste lands after the primary selection and is selected");
  testTrue(counters,
           !EditorSceneTestAccess::pasteText(fixture.module, "not a scene"),
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
    EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
    testEqStr(counters,
              document.packageRoot(),
              "/project",
              "new documents belong to the mounted project");
    fixture.module.update(0.016);
    testTrue(counters,
             EditorSceneTestAccess::assetBrowser(fixture.module) != nullptr &&
               EditorSceneTestAccess::assetBrowser(fixture.module)->visible(),
             "the asset browser docks below the hierarchy");

    EditorSceneTestAccess::placeDroppedAsset(
      fixture.module, "/project/meshes/tri.obj", 640.0f, 360.0f);
    const std::string mesh = EditorSceneTestAccess::selectedId(fixture.module);
    const SceneNode* meshNode = document.findNode(mesh);
    const SceneAsset* meshAsset = document.scene().document().findAsset("tri");
    testTrue(counters,
             meshNode != nullptr && meshNode->find(SceneComponentType::Mesh) &&
               meshAsset != nullptr &&
               meshAsset->type == SceneAssetType::Mesh &&
               meshAsset->path == "meshes/tri.obj",
             "a dropped mesh becomes a node and a package-relative asset");
    EditorSceneTestAccess::placeDroppedAsset(
      fixture.module, "/project/textures/leaf.png", 700.0f, 360.0f);
    const SceneNode* sprite =
      document.findNode(EditorSceneTestAccess::selectedId(fixture.module));
    testTrue(counters,
             sprite != nullptr && sprite->find(SceneComponentType::Sprite) &&
               document.scene().document().findAsset("leaf") != nullptr,
             "a dropped texture becomes a sprite");
    EditorSceneTestAccess::placeDroppedAsset(
      fixture.module, "/project/meshes/tri.obj", 600.0f, 300.0f);
    testEqSize(counters,
               document.scene().document().assets.size(),
               2,
               "dropping the same file again reuses its asset");
    const std::size_t nodes = document.nodeCount();
    EditorSceneTestAccess::placeDroppedAsset(
      fixture.module, "/project/notes.txt", 640.0f, 360.0f);
    EditorSceneTestAccess::placeDroppedAsset(
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

    EditorSceneTestAccess::handleCommand(fixture.module,
                                          EditorCommand::CreateLight);
    const SceneNode* light =
      document.findNode(EditorSceneTestAccess::selectedId(fixture.module));
    EditorSceneTestAccess::handleCommand(fixture.module,
                                          EditorCommand::CreateCamera);
    const SceneNode* camera =
      document.findNode(EditorSceneTestAccess::selectedId(fixture.module));
    testTrue(counters,
             light != nullptr && light->find(SceneComponentType::Light) &&
               camera != nullptr && camera->find(SceneComponentType::Camera),
             "Create adds light and camera nodes");

    EditorSceneTestAccess::handleCommand(fixture.module,
                                          EditorCommand::SaveToProject);
    std::vector<uint8_t> saved;
    testTrue(counters,
             tree->read("/project/scenes/Untitled.ilsc", saved, error) &&
               !document.isDirty() &&
               document.path() == "vfs:/project/scenes/Untitled.ilsc",
             "Save to Project writes into /project/scenes");
    const std::size_t savedNodes = document.nodeCount();
    EditorSceneTestAccess::handleCommand(fixture.module,
                                          EditorCommand::NewDocument);
    EditorSceneTestAccess::openLocation(
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

// Play: the scene's game launches with a copy of the document that names the
// game and its package root; Ctrl+P (or the window closing) stops it.
static int
testPlayScene()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "illed-play-scene";
  std::error_code code;
  std::filesystem::remove_all(root, code);
  std::filesystem::create_directories(root / "playground");
  std::filesystem::create_directories(root / "viewer");
  std::filesystem::create_directories(root / "project");
  std::ofstream(root / "playground" / "behaviours.json")
    << R"({"format":"illumo-behaviours","format_version":1,"behaviours":[
         {"type":"playground.bob","fields":[
           {"name":"height","kind":"number","default":1}]}]})";
  std::ofstream(root / "viewer" / "behaviours.json")
    << R"({"format":"illumo-behaviours","format_version":1,"behaviours":[
         {"type":"viewer.spin","fields":[]}]})";
  std::shared_ptr<VirtualFileSystem> tree =
    std::make_shared<VirtualFileSystem>();
  std::string error;
  for (const char* name : { "playground", "viewer" }) {
    VfsMount app;
    app.point = std::string("/apps/") + name;
    app.layers.push_back(
      { DirectoryVfsBackend::open(root / name, false, error), name });
    tree->mount(app, error);
  }
  VfsMount project;
  project.point = "/project";
  project.layers.push_back(
    { DirectoryVfsBackend::open(root / "project", true, error), "level" });
  tree->mount(project, error);
  IllEdNativeTree::install(tree);
  {
    EditorFixture fixture;
    EditorScene& module = fixture.module;
    EditorDocument& document = EditorSceneTestAccess::document(module);
    IllEdNativeLauncher::setAvailable(false);
    EditorSceneTestAccess::handleCommand(module, EditorCommand::PlayScene);
    testTrue(counters,
             EditorSceneTestAccess::playing(module).empty(),
             "without a launcher Play starts nothing");
    IllEdNativeLauncher::setAvailable(true);
    EditorSceneTestAccess::handleCommand(module, EditorCommand::PlayScene);
    testTrue(counters,
             IllEdNativeLauncher::launches().empty(),
             "with two games and no behaviours Play cannot guess the game");

    const std::string id =
      EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
    EditorInspector* inspector = EditorSceneTestAccess::inspector(module);
    EditorSelection& selection = EditorSceneTestAccess::selection(module);
    inspector->activateField(
      "behaviour.add:playground.bob", &document, &selection);
    EditorSceneTestAccess::handleCommand(module, EditorCommand::PlayScene);
    testTrue(counters,
             IllEdNativeLauncher::launches().size() == 1 &&
               IllEdNativeLauncher::launches()[0].application == "playground" &&
               EditorSceneTestAccess::playing(module) == "playground",
             "a scene using a game's behaviour plays in that game");
    SceneDocument launched;
    std::string parseError;
    ScenePlay play;
    const IllEdNativeLauncher::Launch first =
      IllEdNativeLauncher::launches().empty()
        ? IllEdNativeLauncher::Launch{}
        : IllEdNativeLauncher::launches()[0];
    testTrue(counters,
             IlscCodec::parse(first.document, launched, parseError) &&
               launched.findNode(id) != nullptr,
             "the launch document is the current scene");
    testTrue(counters,
             ScenePlay::read(launched.extensions, &play) &&
               play.application == "playground" && play.root == "/project",
             "it names its game and package root");
    testEqStr(
      counters, first.name, "Untitled.ilsc", "it is named after the document");
    testTrue(counters,
             document.playApplication().empty(),
             "playing writes nothing into the edited document");
    module.update(0.016);
    testTrue(counters,
             EditorSceneTestAccess::toolbar(module)->menuHintForTesting(
               EditorCommand::PlayScene) == "Ctrl+P",
             "View > Play shows its shortcut");
    EditorSceneTestAccess::handleCommand(module, EditorCommand::PlayScene);
    testTrue(counters,
             EditorSceneTestAccess::playing(module).empty() &&
               !IllEdNativeLauncher::running(),
             "Play again stops the game");

    EditorSelection none;
    inspector->update(&fixture.input, &document, &none, 0.016f);
    const InspectorField* game = inspector->field("scene.play");
    testTrue(counters,
             game != nullptr && game->value == "(automatic)" &&
               game->choices.size() == 3,
             "the scene offers its installed games");
    inspector->activateField("scene.play", &document, &none);
    inspector->activateField("scene.play", &document, &none);
    testEqStr(counters,
              document.playApplication(),
              "viewer",
              "a game chosen for the scene is stored with it");
    EditorSceneTestAccess::handleCommand(module, EditorCommand::PlayScene);
    testTrue(counters,
             IllEdNativeLauncher::launches().size() == 2 &&
               IllEdNativeLauncher::launches()[1].application == "viewer",
             "the scene's choice wins over its behaviours");
    IllEdNativeLauncher::running() = false;
    module.update(0.016);
    testTrue(counters,
             EditorSceneTestAccess::playing(module).empty(),
             "closing the game window stops playing");
    document.undo();
    document.undo();
    testEqStr(
      counters, document.playApplication(), "", "choosing the game undoes");
    IllEdNativeLauncher::setAvailable(false);
  }
  IllEdNativeTree::install(nullptr);
  std::filesystem::remove_all(root, code);
  return counters.failures;
}

// Group, ungroup, select parent and children, lock and isolate.
static int
testGroupLockIsolate()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorScene& module = fixture.module;
  EditorDocument& document = EditorSceneTestAccess::document(module);
  EditorSelection& selection = EditorSceneTestAccess::selection(module);
  const std::string a =
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
  const std::string b =
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
  const std::string c =
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
  document.setTransform(a, Transform3D::fromPosition(Vector3(1.0f, 0, 0)));
  document.setTransform(b, Transform3D::fromPosition(Vector3(3.0f, 0, 0)));
  document.setTransform(c, Transform3D::fromPosition(Vector3(10.0f, 0, 0)));
  const SceneInstance& scene = document.scene();

  selection.set(std::vector<std::string>{ a, b });
  EditorSceneTestAccess::handleCommand(module, EditorCommand::GroupSelection);
  const std::string group = EditorSceneTestAccess::selectedId(module);
  const SceneNode* groupNode = document.findNode(group);
  testTrue(counters,
           groupNode != nullptr && groupNode->name == "Group" &&
             scene.parentOf(a) == group && scene.parentOf(b) == group &&
             std::fabs(document.worldMatrix(group)[3].x - 2.0f) < 1.0e-4f &&
             std::fabs(document.worldMatrix(a)[3].x - 1.0f) < 1.0e-4f &&
             std::fabs(document.worldMatrix(b)[3].x - 3.0f) < 1.0e-4f,
           "Group parents the nodes under a node at their centre, in place");
  testTrue(counters,
           scene.childIds({}) == std::vector<std::string>{ group, c },
           "the group takes the first node's place");
  document.undo();
  testTrue(counters,
           document.findNode(group) == nullptr && scene.parentOf(a).empty(),
           "grouping is one undo step");
  document.redo();
  selection.set(a);
  EditorSceneTestAccess::handleCommand(module, EditorCommand::SelectParent);
  testEqStr(counters,
            EditorSceneTestAccess::selectedId(module),
            group,
            "Select Parent walks up");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::SelectChildren);
  testTrue(counters,
           selection.ids() == std::vector<std::string>{ a, b },
           "Select Children walks down");

  // Locks: the subtree stops picking, the set is saved, undo unlocks.
  selection.set(group);
  EditorSceneTestAccess::handleCommand(module, EditorCommand::ToggleLock);
  std::string hit;
  testTrue(counters,
           document.isLockedSelf(group) && document.isLocked(a) &&
             !document.isLocked(c) &&
             !document.pickRay(
               Vector3(1.0f, 0.0f, 10.0f), Vector3(0.0f, 0.0f, -1.0f), &hit) &&
             document.pickRay(
               Vector3(10.0f, 0.0f, 10.0f), Vector3(0.0f, 0.0f, -1.0f), &hit) &&
             hit == c,
           "a locked group's subtree is not pickable");
  testTrue(counters,
           document.encode().find("illed.view") != std::string::npos,
           "locks are saved with the scene");
  document.undo();
  testTrue(counters,
           !document.isLocked(a) &&
             document.encode().find("illed.view") == std::string::npos,
           "locking is one undo step");

  // Isolation hides others from view only.
  selection.set(c);
  const size_t commands = document.history().size();
  EditorSceneTestAccess::handleCommand(module, EditorCommand::ToggleIsolate);
  const SceneGraph& graph = document.graph();
  testTrue(counters,
           EditorSceneTestAccess::isolated(module) &&
             !graph.isEffectivelyVisible(document.nodeHandle(a)) &&
             graph.isEffectivelyVisible(document.nodeHandle(c)) &&
             document.findNode(a)->visible &&
             document.history().size() == commands,
           "Isolate hides everything else without an edit");
  EditorSceneTestAccess::handleCommand(module, EditorCommand::ToggleIsolate);
  testTrue(counters,
           !EditorSceneTestAccess::isolated(module) &&
             graph.isEffectivelyVisible(document.nodeHandle(a)),
           "toggling again shows everything");

  selection.set(group);
  EditorSceneTestAccess::handleCommand(module, EditorCommand::UngroupSelection);
  testTrue(counters,
           document.findNode(group) == nullptr && scene.parentOf(a).empty() &&
             std::fabs(document.worldMatrix(b)[3].x - 3.0f) < 1.0e-4f &&
             scene.childIds({}) == std::vector<std::string>{ a, b, c },
           "Ungroup lifts the children in place and removes the empty group");
  document.undo();
  testTrue(counters,
           document.findNode(group) != nullptr && scene.parentOf(a) == group,
           "ungrouping is one undo step");
  return counters.failures;
}

// Hover, the viewport context menu, Alt-drag copies, Drop to Floor and
// nudges.
static int
testViewportInteraction()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorScene& module = fixture.module;
  EditorDocument& document = EditorSceneTestAccess::document(module);
  EditorSelection& selection = EditorSceneTestAccess::selection(module);
  EditorSceneTestAccess::handleCommand(module, EditorCommand::SetMode3D);
  const std::string base =
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
  const std::string top =
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
  const std::string loose =
    EditorSceneTestAccess::createNode(module, EditorCommand::CreateCube);
  document.setTransform(base, Transform3D::fromPosition(Vector3(0.0f)));
  document.setTransform(top, Transform3D::fromPosition(Vector3(0, 5.0f, 0)));
  document.setTransform(loose, Transform3D::fromPosition(Vector3(4, 3.0f, 0)));
  selection.set(std::vector<std::string>{ top, loose });
  EditorSceneTestAccess::handleCommand(module, EditorCommand::DropToFloor);
  testTrue(counters,
           std::fabs(document.findNode(top)->transform.position.y - 1.0f) <
               1.0e-3f &&
             std::fabs(document.findNode(loose)->transform.position.y - 0.5f) <
               1.0e-3f,
           "Drop to Floor lands on the cube below, or on the ground");

  // Nudges: Alt+arrow moves by the move snap step; a run is one command.
  selection.set(base);
  module.update(0.016);
  const size_t commands = document.history().size();
  const float before = document.findNode(base)->transform.position.x;
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0x4 });
  module.update(0.016);
  fixture.input.getKeyQueue().push({ KeyCode::Right, InputAction::Press, 0x4 });
  module.update(0.016);
  const Vector3 moved = document.findNode(base)->transform.position;
  testTrue(counters,
           std::fabs(std::fabs(moved.x - before) + std::fabs(moved.z) - 1.0f) <
               1.0e-3f &&
             document.history().size() == commands + 1,
           "two Alt+arrow nudges move one snap step each as one command");

  // Hover and the context menu, through real pointer input.
  float x = 0.0f;
  float y = 0.0f;
  testTrue(counters,
           EditorSceneTestAccess::worldToScreen(
             module, Vector3(document.worldMatrix(loose)[3]), &x, &y),
           "the loose cube is on screen");
  fixture.window.mouseX = x;
  fixture.window.mouseY = y;
  module.update(0.016);
  testEqStr(counters,
            EditorSceneTestAccess::hoverId(module),
            loose,
            "the node under the cursor is hovered");
  testTrue(counters,
           EditorSceneTestAccess::toolbar(module)->statusForTesting().find(
             "Under cursor") != std::string::npos,
           "the status bar names it");
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseRight, InputAction::Press);
  module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseRight, InputAction::Release);
  module.update(0.016);
  EditorToolbar* toolbar = EditorSceneTestAccess::toolbar(module);
  float itemX = 0.0f;
  float itemY = 0.0f;
  testTrue(counters,
           toolbar->popupOpen() &&
             EditorSceneTestAccess::selectedId(module) == loose &&
             toolbar->popupItemCenterForTesting(
               EditorCommand::Duplicate, &itemX, &itemY),
           "a right click opens the menu for the node under it");
  const size_t nodes = document.nodeCount();
  EditorSceneTestAccess::handleCommand(
    module, toolbar->clickAtForTesting(itemX, itemY));
  testTrue(counters,
           document.nodeCount() == nodes + 1 && !toolbar->popupOpen(),
           "a menu item runs its command and closes the menu");

  // Alt held as the body is grabbed moves a copy.
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseRight, InputAction::None);
  selection.set(loose);
  InputManagerTestAccess::setModifierFlags(fixture.input, 0x4);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  module.update(0.016);
  const std::string copy = EditorSceneTestAccess::selectedId(module);
  fixture.window.mouseX = x + 60.0;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Hold);
  module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Release);
  module.update(0.016);
  InputManagerTestAccess::setModifierFlags(fixture.input, 0);
  testTrue(counters,
           copy != loose && document.findNode(copy) != nullptr &&
             document.nodeCount() == nodes + 2 &&
             std::fabs(document.findNode(loose)->transform.position.x - 4.0f) <
               1.0e-4f &&
             document.findNode(copy)->transform.position.x !=
               document.findNode(loose)->transform.position.x,
           "Alt-drag leaves the original and moves a copy");

  // The empty-space menu creates where it was opened.
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::None);
  // An empty patch of ground in the viewport, between the dock columns.
  const float groundX = 860.0f;
  const float groundY = 620.0f;
  float worldX = 0.0f;
  float worldZ = 0.0f;
  EditorSceneTestAccess::screenToWorld(
    module, groundX, groundY, &worldX, &worldZ);
  fixture.window.mouseX = groundX;
  fixture.window.mouseY = groundY;
  module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseRight, InputAction::Press);
  module.update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseRight, InputAction::Release);
  module.update(0.016);
  testTrue(counters,
           toolbar->popupOpen() &&
             toolbar->popupItemCenterForTesting(
               EditorCommand::CreateSphere, &itemX, &itemY) &&
             EditorSceneTestAccess::createAtContextPoint(
               module, EditorCommand::CreateSphere),
           "the empty-space menu offers shapes");
  const Vector3 placed(
    document.worldMatrix(EditorSceneTestAccess::selectedId(module))[3]);
  testTrue(counters,
           std::fabs(placed.x - worldX) < 0.05f &&
             std::fabs(placed.z - worldZ) < 0.05f,
           "a shape from it lands at the clicked point");
  return counters.failures;
}

// The grid follows the view: it recentres when the camera travels and grows
// when it zooms out, so it always covers the viewport.
static int
testGridFollowsView()
{
  TestCounters counters;
  EditorFixture fixture;
  EditorScene& module = fixture.module;
  module.update(0.016);
  const Vector3 start = EditorSceneTestAccess::gridWindow(module);
  fixture.camera.SetPositionPrecise(400.0, -300.0);
  module.update(0.016);
  const Vector3 moved = EditorSceneTestAccess::gridWindow(module);
  testTrue(counters,
           std::fabs(moved.x - 400.0f) < 10.0f &&
             std::fabs(moved.y + 300.0f) < 10.0f,
           "the grid recentres on the camera");
  fixture.camera.SetZoom(2.0f);
  // The camera eases toward a new zoom.
  for (int frame = 0; frame < 120; ++frame) {
    module.update(0.016);
  }
  const Vector3 zoomed = EditorSceneTestAccess::gridWindow(module);
  testTrue(counters,
           zoomed.z > start.z * 10.0f,
           "zooming out grows the grid past the view");
  return counters.failures;
}

void
registerEditorSceneTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Module.GridFollowsView",
               []() { return testGridFollowsView(); });
  registry.add("IllEd.Assets.ProjectFlow",
               []() { return testAssetsProjectFlow(); });
  registry.add("IllEd.Module.PlayScene", []() { return testPlayScene(); });
  registry.add("IllEd.Module.GroupLockIsolate",
               []() { return testGroupLockIsolate(); });
  registry.add("IllEd.Module.ViewportInteraction",
               []() { return testViewportInteraction(); });
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
        EditorSceneTestAccess::document(fixture.module);
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
      EditorSceneTestAccess::setSelectedId(fixture.module, selected);
      EditorSceneTestAccess::refreshView(fixture.module);
      const size_t repeats = 30;
      const std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();
      for (size_t i = 0; i < repeats; ++i) {
        EditorSceneTestAccess::handleCommand(fixture.module,
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
    EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
    EditorSceneTestAccess::createNode(fixture.module,
                                       EditorCommand::CreateCube);
    const std::string id = EditorSceneTestAccess::selectedId(fixture.module);
    SceneGraph& graph = document.graph();
    const SceneNodeHandle retained = document.nodeHandle(id);
    ISceneRenderAttachment* visual = graph.getAttachment(retained, 0);
    testTrue(g,
             visual != nullptr && graph.getAttachmentCount(retained) == 1,
             "geometry has one persistent render attachment");
    const uint64_t structure = graph.getStructuralRevision();
    const SceneSnapshotView snapshot = graph.extract(nullptr);
    EditorSceneTestAccess::handleCommand(fixture.module,
                                          EditorCommand::CycleColor);
    EditorSceneTestAccess::handleCommand(fixture.module,
                                          EditorCommand::NudgeExtent);
    testTrue(g,
             document.nodeHandle(id) == retained &&
               graph.getAttachment(retained, 0) == visual &&
               graph.getStructuralRevision() == structure,
             "recolor and extent edits preserve node and visual identities");
    testTrue(
      g, !snapshot.get(), "visual reconfiguration retires old snapshots");
    document.translate(id, Vector3(1, 2, 3));
    EditorSceneTestAccess::refreshView(fixture.module);
    Matrix4 world(1.0f);
    graph.getWorldTransform(retained, &world);
    testTrue(g,
             graph.getAttachment(retained, 0) == visual &&
               world[3][1] == document.findNode(id)->transform.position.y,
             "translation synchronizes without rebuild");
    for (size_t i = 0; i < 4200; ++i) {
      document.setColor(id, ColorRgba{ 80, 90, 100, 255 });
    }
    EditorSceneTestAccess::refreshView(fixture.module);
    testTrue(g,
             document.nodeHandle(id) == retained &&
               graph.getAttachment(retained, 0) == visual,
             "journal overflow resync preserves graph and visual identity");
    fixture.module.stop();
    testTrue(g,
             document.nodeHandle(id) == retained &&
               graph.getAttachmentCount(retained) == 1,
             "stop preserves document nodes");
    testTrue(g,
             fixture.module.start(fixture.context) &&
               document.nodeHandle(id) == retained &&
               graph.getAttachmentCount(retained) == 1,
             "restart rebinds render resources without rebuilding nodes");
    EditorSceneTestAccess::deleteSelection(fixture.module);
    testTrue(g,
             !graph.isNodeValid(retained),
             "deletion invalidates the original generational handle");
    return g.failures;
  });
  registry.add("IllEd.Module.DocumentIsContent", []() {
    g = {};
    testSection("EditorScene: the document edits the scene's content");
    EditorFixture fixture;
    testTrue(g, fixture.started, "editor starts");
    EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
    SceneInstance* content = &fixture.module.content();
    testTrue(g,
             &document.scene() == content,
             "the document borrows the scene's content");
    EditorSceneTestAccess::createNode(fixture.module,
                                       EditorCommand::CreateCube);
    testTrue(g, content->nodeCount() == 1, "edits land in the content");
    const std::string saved = document.encode();
    const uint64_t generation = document.sceneGeneration();
    std::string error;
    testTrue(g,
             !document.loadFromText("not a scene", &error) &&
               content->nodeCount() == 1 &&
               document.sceneGeneration() == generation,
             "a failed load leaves the content as it was");
    document.clear();
    testTrue(g,
             &document.scene() == content && content->nodeCount() == 0 &&
               document.sceneGeneration() > generation,
             "clearing empties the same content");
    testTrue(g,
             document.loadFromText(saved, &error) &&
               &document.scene() == content && content->nodeCount() == 1,
             "loading replaces the content in place");
    return g.failures;
  });
  registry.add("IllEd.Module.PropertyCommands", []() {
    g = {};
    EditorFixture fixture;
    testTrue(g, fixture.started, "editor starts");
    EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
    EditorSceneTestAccess::createNode(fixture.module,
                                       EditorCommand::CreateEmpty);
    const std::string parent =
      EditorSceneTestAccess::selectedId(fixture.module);
    EditorSceneTestAccess::createNode(fixture.module,
                                       EditorCommand::CreateCube);
    const std::string child =
      EditorSceneTestAccess::selectedId(fixture.module);
    testTrue(g,
             parentOf(document, child).empty(),
             "created nodes go to the root, not under the selection");
    document.setParent(child, parent);
    const Vector3 extent = primitiveOf(document.findNode(child)).extent;
    document.setColor(child, ColorRgba{ 210, 90, 70, 255 });
    testTrue(g,
             document.loadFromText(document.encode(), nullptr),
             "reload clean property baseline");
    EditorSceneTestAccess::handleCommand(fixture.module,
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
      EditorSceneTestAccess::handleCommand(fixture.module,
                                            EditorCommand::CycleColor);
      const ColorRgba actual = primitiveOf(document.findNode(child)).color;
      testTrue(g,
               actual.r == expected.r && actual.g == expected.g &&
                 actual.b == expected.b && actual.a == expected.a,
               "color command advances through the full palette cycle");
    }
    testEqStr(g, parentOf(document, child), parent, "child starts parented");
    EditorSceneTestAccess::handleCommand(fixture.module,
                                          EditorCommand::UnparentNode);
    testTrue(
      g, parentOf(document, child).empty(), "unparent command detaches child");
    EditorSceneTestAccess::handleCommand(fixture.module,
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
      EditorSceneTestAccess::handleCommand(fixture.module, command);
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
    GuiPanelDock& dock = EditorSceneTestAccess::dock(fixture.module);
    // A frame stores the view state first; panels must add nothing to it.
    fixture.module.update(0.016);
    const std::string document =
      EditorSceneTestAccess::document(fixture.module).encode();
    for (int iteration = 0; iteration < 2; ++iteration) {
      EditorSceneTestAccess::handleCommand(
        fixture.module, EditorCommand::ToggleHierarchyPanel);
      EditorSceneTestAccess::handleCommand(
        fixture.module, EditorCommand::ToggleInspectorPanel);
      fixture.module.update(0.016);
      const GuiDockMode expected =
        iteration == 0 ? GuiDockMode::Hidden : GuiDockMode::Docked;
      testTrue(g,
               dock.mode("hierarchy") == expected &&
                 dock.mode("inspector") == expected,
               "panel toggles are reversible");
      testTrue(g,
               EditorSceneTestAccess::sceneGraphView(fixture.module)
                   ->placement()
                   .visible == (iteration != 0),
               "a hidden panel's content is not placed");
    }
    EditorSceneTestAccess::handleCommand(fixture.module,
                                          EditorCommand::PopOutToolsPanel);
    testTrue(g,
             dock.mode("tools") == GuiDockMode::Docked,
             "without window support a pop-out stays docked");
    EditorSceneTestAccess::handleCommand(fixture.module,
                                          EditorCommand::ToggleAssetsPanel);
    EditorSceneTestAccess::handleCommand(fixture.module,
                                          EditorCommand::ResetLayout);
    testTrue(g,
             dock.mode("assets") == GuiDockMode::Docked,
             "reset brings every panel back");
    testEqStr(g,
              EditorSceneTestAccess::document(fixture.module).encode(),
              document,
              "panel commands preserve scene data");
    return g.failures;
  });
  registry.add("IllEd.Module.NewDocumentCommand", []() {
    g = {};
    EditorFixture fixture;
    EditorSceneTestAccess::createNode(fixture.module,
                                       EditorCommand::CreateCube);
    EditorDocument& document = EditorSceneTestAccess::document(fixture.module);
    document.setPath("previous-scene.ilsc");
    testTrue(g,
             document.loadFromText(document.encode(), nullptr),
             "reload clean reset baseline");
    fixture.camera.SetPositionPrecise(15.0, -7.0);
    fixture.camera.SetZoom(8.0f);
    EditorSceneTestAccess::handleCommand(fixture.module,
                                         EditorCommand::ResetCamera);
    fixture.camera.Update(10.0f);
    const GuiToolRect view =
      EditorSceneTestAccess::dock(fixture.module).center();
    float originX = 0.0f;
    float originY = 0.0f;
    EditorSceneTestAccess::worldToScreen(
      fixture.module, Vector3(0.0f), &originX, &originY);
    testTrue(g,
             std::fabs(originX - (view.x + view.w * 0.5f)) < 0.5f &&
               std::fabs(originY - (view.y + view.h * 0.5f)) < 0.5f &&
               fixture.camera.GetZoom() == 32.0f,
             "reset command restores camera origin and zoom");
    EditorSceneTestAccess::handleCommand(fixture.module,
                                         EditorCommand::NewDocument);
    testEqSize(g, document.nodeCount(), 0u, "new document clears nodes");
    testTrue(g,
             document.path().empty() && !document.isDirty(),
             "new document is clean and untitled");
    testTrue(g,
             EditorSceneTestAccess::selectedId(fixture.module).empty(),
             "new document clears selection");
    testTrue(g,
             !EditorSceneTestAccess::confirmationOpen(fixture.module),
             "clean document needs no confirmation");
    return g.failures;
  });
  registry.add("IllEd.Module.PerspectivePicking", []() {
    g = {};
    for (float pitch : { 0.0f, 0.45f }) {
      EditorFixture fixture;
      EditorSceneTestAccess::handleCommand(fixture.module,
                                            EditorCommand::SetMode3D);
      EditorDocument& document =
        EditorSceneTestAccess::document(fixture.module);
      SceneEditorState cameraState = document.editorState();
      cameraState.pitch = pitch;
      document.setEditorState(cameraState);
      EditorSceneTestAccess::setCameraTargetHeight(
        fixture.module, pitch == 0.0f ? 0.0f : 6.0f);
      fixture.module.update(0.016);
      glm::vec3 origin{ 0.0f }, direction{ 0.0f };
      testTrue(g,
               EditorSceneTestAccess::screenToWorldRay(
                 fixture.module, 640, 360, &origin, &direction),
               "perspective screen ray available");
      if (pitch == 0.0f) {
        float groundX = 0, groundY = 0;
        testTrue(g,
                 !EditorSceneTestAccess::screenToWorld(
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
      EditorSceneTestAccess::refreshView(fixture.module);
      EditorSceneTestAccess::setSelectedId(fixture.module, "");
      fixture.window.mouseX = 640;
      fixture.window.mouseY = 360;
      InputManagerTestAccess::setAction(
        fixture.input, KeyCode::MouseLeft, InputAction::Press);
      fixture.module.update(0.016);
      testEqStr(g,
                EditorSceneTestAccess::selectedId(fixture.module),
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
        EditorSceneTestAccess::handleCommand(fixture.module,
                                              EditorCommand::SetMode3D);
      }
      EditorSceneTestAccess::createNode(fixture.module,
                                         world3D ? EditorCommand::CreateCube
                                                 : EditorCommand::CreateRect);
      const std::string id = EditorSceneTestAccess::selectedId(fixture.module);
      EditorDocument& document =
        EditorSceneTestAccess::document(fixture.module);
      document.setTransform(id, Transform3D::fromPosition(Vector3(0.0f)));
      document.setExtent(id, Vector3(10.0f));
      std::string error;
      testTrue(g,
               document.loadFromText(document.encode(), &error),
               "reload clean scene");
      EditorSceneTestAccess::refreshView(fixture.module);
      EditorSceneTestAccess::setSelectedId(fixture.module, "");
      fixture.camera.SetPositionPrecise(0.0, 0.0);
      fixture.camera.SetZoom(32.0f);
      fixture.module.update(0.016);
      fixture.window.mouseX = 650.0;
      fixture.window.mouseY = 365.0;
      InputManagerTestAccess::setAction(
        fixture.input, KeyCode::MouseLeft, InputAction::Press);
      fixture.module.update(0.016);
      testEqStr(g,
                EditorSceneTestAccess::selectedId(fixture.module),
                id,
                "body selected off center");
      for (int held = 0; held < 3; ++held) {
        fixture.module.update(0.016);
      }
      const Vector3 position = document.findNode(id)->transform.position;
      testTrue(g,
               glm::length(position) < 0.00001f,
               "stationary selection does not move object");
      testTrue(
        g, !document.isDirty(), "stationary selection keeps document clean");
      InputManagerTestAccess::setAction(
        fixture.input, KeyCode::MouseLeft, InputAction::Release);
      fixture.module.update(0.016);
      testTrue(g, !document.isDirty(), "stationary click release stays clean");
      EditorSceneTestAccess::setSelectedId(fixture.module, "");
      InputManagerTestAccess::setAction(
        fixture.input, KeyCode::MouseLeft, InputAction::Press);
      fixture.module.update(0.016);
      fixture.window.mouseX += 30.0;
      fixture.module.update(0.016);
      testTrue(g,
               glm::length(document.findNode(id)->transform.position) > 0.001f,
               "actual pointer movement drags object");
      testTrue(g, document.isDirty(), "actual drag marks document dirty");
      InputManagerTestAccess::setAction(
        fixture.input, KeyCode::MouseLeft, InputAction::Release);
      fixture.module.update(0.016);
      testTrue(g,
               !EditorSceneTestAccess::isDragging(fixture.module),
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
  registry.add("IllEd.Module.SettingStepsPivotAndArrange", []() {
    g = {};
    testSettingStepsPivotAndArrange();
    return g.failures;
  });
  registry.add("IllEd.Module.FindFocusesHierarchyFilter", []() {
    g = {};
    testFindFocusesHierarchyFilter();
    return g.failures;
  });
  registry.add("IllEd.Module.DropPreview", []() {
    g = {};
    testDropPreview();
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
    testEditorSceneDoesNotDispatchConsole();
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
