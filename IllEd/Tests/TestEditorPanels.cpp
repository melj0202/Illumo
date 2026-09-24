#include "EditorModule.h"
#include "IllEdPlatform.h"
#include "TestAccess.h"
#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/FakePanelSurfaces.h>
#include <Illumo/Testing/TestAccess.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

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
// An editor whose host can open panel windows (FakePanelSurfaces), with an
// optional saved layout in its settings.
struct PanelFixture
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
  FakePanelSurfaces surfaces;
  IllumoContext context;
  EditorModule module;
  bool started;

  explicit PanelFixture(const std::string& layout = {})
    : window(1280, 720)
    , env(isolatedSettings("illed-panels-settings.json"))
    , camera(glm::vec2(0.0f, 0.0f), 32.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , assets(&renderer, false)
    , registry()
    , console(&env, &registry, &window, &renderer, "IllEd")
    , input(nullptr)
    , scene(&window, &camera)
    , surfaces(&window, &camera)
    , context{ &scene,  &window, &console, &input,   &renderer,
               &assets, &env,    &camera,  &registry }
    , module()
    , started(false)
  {
    env.setVar("fontSize", "13");
    if (!layout.empty()) {
      env.setVar("panelLayout", layout);
    }
    mock.Initialize();
    context.panelSurfaces = &surfaces;
    started = module.Start(&context);
  }

  ~PanelFixture()
  {
    if (started) {
      module.Exit();
    }
  }

  // One host exchange: pending windows open, then the editor runs a frame.
  void frame()
  {
    surfaces.step();
    module.Update(0.016);
  }

  void pressLeft(bool down)
  {
    InputManagerTestAccess::setAction(
      input, KeyCode::MouseLeft, down ? InputAction::Press : InputAction::None);
  }

  GuiPanelDock& dock() { return EditorModuleTestAccess::dock(module); }
};

static bool
contains(const std::vector<DrawableBase*>& drawables, const DrawableBase* item)
{
  return std::find(drawables.begin(), drawables.end(), item) != drawables.end();
}

static int
testPopOutAndDock()
{
  TestCounters counters;
  PanelFixture fixture;
  testTrue(counters, fixture.started, "the editor starts");
  testTrue(
    counters, fixture.dock().canDetach(), "a host with windows offers pop-out");
  EditorInspector* inspector =
    EditorModuleTestAccess::inspector(fixture.module);

  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::PopOutInspectorPanel);
  testTrue(counters,
           fixture.dock().mode("inspector") == GuiDockMode::Opening,
           "pop-out asks the host for a window");
  fixture.frame();
  testTrue(counters,
           fixture.dock().mode("inspector") == GuiDockMode::Detached &&
             inspector->placement().surface == 104u &&
             inspector->placement().area.y == GuiToolStyle::kTitleHeight,
           "the opened window hosts the inspector below its title bar");

  fixture.scene.ClearDrawables();
  fixture.surfaces.clearScenes();
  fixture.module.DispatchDrawables(&fixture.scene);
  FakePanelSurfaces::Window* window = fixture.surfaces.window(104);
  testTrue(
    counters,
    window != nullptr &&
      contains(window->scene->drawablesIn(RenderLayerId::UI), inspector) &&
      !contains(fixture.scene.drawablesIn(RenderLayerId::UI), inspector),
    "a detached panel draws in its own window only");
  testTrue(counters,
           window != nullptr &&
             window->scene->drawablesIn(RenderLayerId::UI).size() == 2u,
           "the window also gets the panel's title bar");

  fixture.surfaces.userClose(104);
  fixture.frame();
  testTrue(counters,
           fixture.dock().mode("inspector") == GuiDockMode::Docked &&
             fixture.surfaces.window(104) == nullptr &&
             inspector->placement().surface == 0u,
           "closing the window docks the panel");

  // Tearing the Hierarchy off by dragging its title past the window's edge.
  const GuiToolRect title = fixture.dock().view("hierarchy").title;
  fixture.window.mouseX = title.x + 30.0;
  fixture.window.mouseY = title.y + 8.0;
  fixture.pressLeft(true);
  fixture.module.Update(0.016);
  testTrue(counters, fixture.dock().dragging(), "a title press drags");
  fixture.window.mouseX = -40.0;
  fixture.window.mouseY = 120.0;
  fixture.module.Update(0.016);
  fixture.pressLeft(false);
  fixture.frame();
  testTrue(counters,
           fixture.dock().mode("hierarchy") == GuiDockMode::Detached &&
             fixture.surfaces.window(101) != nullptr &&
             fixture.surfaces.window(101)->requestedX < 0,
           "a drag past the edge tears the panel off where it was dropped");
  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::PopOutHierarchyPanel);
  testTrue(counters,
           fixture.dock().mode("hierarchy") == GuiDockMode::Docked &&
             fixture.surfaces.window(101) == nullptr,
           "the View menu docks a detached panel");

  fixture.surfaces.refuseNextOpen = true;
  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::PopOutToolsPanel);
  fixture.frame();
  testTrue(counters,
           fixture.dock().mode("tools") == GuiDockMode::Docked,
           "a refused window leaves the panel docked");
  return counters.failures;
}

static int
testDetachedTextEntry()
{
  TestCounters counters;
  PanelFixture fixture;
  const std::string id = EditorModuleTestAccess::createNode(
    fixture.module, EditorCommand::CreateCube);
  EditorModuleTestAccess::handleCommand(fixture.module,
                                        EditorCommand::PopOutInspectorPanel);
  fixture.frame();
  fixture.module.Update(0.016);
  EditorInspector* inspector =
    EditorModuleTestAccess::inspector(fixture.module);
  const InspectorField* name = inspector->field("name");
  FakePanelSurfaces::Window* window = fixture.surfaces.window(104);
  testTrue(counters,
           name != nullptr && window != nullptr,
           "the detached inspector shows the selection");
  if (name == nullptr || window == nullptr) {
    return counters.failures;
  }
  // Scale 1: the window's pixels are its layout units.
  window->pointer.x = name->x + name->width * 0.5f;
  window->pointer.y = name->y + 8.0f;
  window->pointer.left = true;
  fixture.module.Update(0.016);
  window->pointer.left = false;
  fixture.module.Update(0.016);
  testTrue(counters,
           inspector->editing() && inspector->editingKey() == "name",
           "a click in the detached window focuses the field");
  for (const char character : std::string("Crate")) {
    fixture.input.getCharQueue().push(static_cast<unsigned char>(character));
  }
  // R is also the Scale shortcut; a focused field takes the key first.
  fixture.input.getKeyQueue().push({ KeyCode::R, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  fixture.input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
  testEqStr(counters,
            document.findNode(id)->name,
            "Crate",
            "typing in the detached inspector renames the node");
  testTrue(counters,
           EditorModuleTestAccess::toolbar(fixture.module)
               ->statusForTesting()
               .find("Scale") == std::string::npos,
           "shortcut keys wait while a field has focus");
  return counters.failures;
}

static int
testCrossWindowAssetDrop()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "illed-panels-drop";
  std::error_code code;
  std::filesystem::remove_all(root, code);
  std::filesystem::create_directories(root / "meshes");
  std::ofstream(root / "illumo.json")
    << R"({"format":"ilpk","format_version":1,"id":"drop","kind":"content"})";
  std::ofstream(root / "meshes" / "tri.obj")
    << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
  std::shared_ptr<VirtualFileSystem> tree =
    std::make_shared<VirtualFileSystem>();
  std::string error;
  VfsMount project;
  project.point = "/project";
  project.layers.push_back(
    { DirectoryVfsBackend::open(root, true, error), "drop" });
  tree->mount(project, error);
  IllEdNativeTree::install(tree);
  {
    PanelFixture fixture;
    EditorModuleTestAccess::handleCommand(fixture.module,
                                          EditorCommand::PopOutAssetsPanel);
    fixture.frame();
    fixture.module.Update(0.016);
    EditorAssetBrowser* browser =
      EditorModuleTestAccess::assetBrowser(fixture.module);
    testTrue(counters,
             browser->placement().surface == 102u && browser->visible(),
             "the Assets panel is in its own window");
    browser->clickRowForTesting(0);
    fixture.module.Update(0.016);
    browser->clickRowForTesting(1);
    fixture.module.Update(0.016);
    const std::vector<GuiFileTreeRow> rows = browser->rowsForTesting();
    float rowX = 0.0f;
    float rowY = 0.0f;
    testTrue(counters,
             rows.size() >= 3u && rows[2].path == "/project/meshes/tri.obj" &&
               browser->rowCenterForTesting(2, &rowX, &rowY),
             "the mesh is listed in the detached browser");

    FakePanelSurfaces::Window* window = fixture.surfaces.window(102);
    const std::array<int, 2> origin = window->origin;
    window->pointer.x = rowX;
    window->pointer.y = rowY;
    window->pointer.left = true;
    fixture.module.Update(0.016);
    // Released over the main viewport's (640, 360), in this window's pixels.
    window->pointer.x =
      640.0 + fixture.surfaces.mainOrigin[0] - static_cast<double>(origin[0]);
    window->pointer.y =
      360.0 + fixture.surfaces.mainOrigin[1] - static_cast<double>(origin[1]);
    fixture.module.Update(0.016);
    testEqStr(counters,
              browser->dragging(),
              "/project/meshes/tri.obj",
              "dragging past the window's edge carries the file");
    const std::size_t before =
      EditorModuleTestAccess::document(fixture.module).nodeCount();
    window->pointer.left = false;
    fixture.module.Update(0.016);
    EditorDocument& document = EditorModuleTestAccess::document(fixture.module);
    const SceneNode* placed =
      document.findNode(EditorModuleTestAccess::selectedId(fixture.module));
    testTrue(counters,
             document.nodeCount() == before + 1u && placed != nullptr &&
               placed->find(SceneComponentType::Mesh) != nullptr,
             "a drop over the main viewport places the mesh");
    float expectedX = 0.0f;
    float expectedY = 0.0f;
    EditorModuleTestAccess::screenToWorld(
      fixture.module, 640.0f, 360.0f, &expectedX, &expectedY);
    testTrue(counters,
             placed != nullptr &&
               std::fabs(placed->transform.position.x - expectedX) < 0.01f,
             "it lands where the pointer was released");
  }
  IllEdNativeTree::install(nullptr);
  std::filesystem::remove_all(root, code);
  return counters.failures;
}

static int
testLayoutPersists()
{
  TestCounters counters;
  std::string saved;
  {
    PanelFixture first;
    EditorModuleTestAccess::handleCommand(first.module,
                                          EditorCommand::ToggleToolsPanel);
    EditorModuleTestAccess::handleCommand(first.module,
                                          EditorCommand::PopOutHierarchyPanel);
    first.dock().setColumnWidth(GuiDockSide::Left, 300.0f);
    first.frame();
    first.frame();
    saved = first.env.getVar("panelLayout").value;
    testTrue(counters,
             saved.find("panel tools hidden") != std::string::npos &&
               saved.find("panel hierarchy detached") != std::string::npos &&
               saved.find('\n') == std::string::npos,
             "the layout is saved as one settings line");
  }
  {
    PanelFixture second(saved);
    testTrue(
      counters,
      second.dock().mode("tools") == GuiDockMode::Hidden &&
        std::fabs(second.dock().columnWidth(GuiDockSide::Left) - 300.0f) < 0.5f,
      "hidden panels and column sizes come back");
    second.frame();
    testTrue(counters,
             second.dock().mode("hierarchy") == GuiDockMode::Detached &&
               second.surfaces.window(101) != nullptr,
             "a detached panel reopens its window on the next start");
  }
  {
    PanelFixture third("not a layout");
    third.frame();
    testTrue(counters,
             third.dock().mode("tools") == GuiDockMode::Docked &&
               third.dock().mode("hierarchy") == GuiDockMode::Docked,
             "an unreadable layout falls back to the default");
  }
  return counters.failures;
}

void
registerEditorPanelsTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Panels.PopOutAndDock",
               []() { return testPopOutAndDock(); });
  registry.add("IllEd.Panels.DetachedTextEntry",
               []() { return testDetachedTextEntry(); });
  registry.add("IllEd.Panels.CrossWindowAssetDrop",
               []() { return testCrossWindowAssetDrop(); });
  registry.add("IllEd.Panels.LayoutPersists",
               []() { return testLayoutPersists(); });
}
