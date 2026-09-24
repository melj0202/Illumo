#include "MeshViewerModule.h"
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/FakePanelSurfaces.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestAccess.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

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
// A viewer whose host can open panel windows when `windows` is set.
struct ViewerPanelFixture
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
  MeshViewerModule module;
  bool started;

  explicit ViewerPanelFixture(bool windows)
    : window(1280, 720)
    , env(isolatedSettings("meshviewer-panels-settings.json"))
    , camera(glm::vec2(0.0f, 0.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , assets(&renderer, false)
    , registry()
    , console(&env, &registry, &window, &renderer, "IllMeshViewer")
    , input(nullptr)
    , scene(&window, &camera)
    , surfaces(&window, &camera)
    , context{ &scene,  &window, &console, &input,   &renderer,
               &assets, &env,    &camera,  &registry }
    , module()
    , started(false)
  {
    mock.Initialize();
    if (windows) {
      context.panelSurfaces = &surfaces;
    }
    started = module.Start(&context);
  }

  ~ViewerPanelFixture()
  {
    if (started) {
      module.Exit();
    }
  }

  void frame()
  {
    surfaces.step();
    module.Update(0.016);
  }

  // A main-window click at a layout point (UI scale 1).
  void click(float x, float y)
  {
    window.mouseX = x;
    window.mouseY = y;
    InputManagerTestAccess::setAction(
      input, KeyCode::MouseLeft, InputAction::Press);
    module.Update(0.016);
    InputManagerTestAccess::setAction(
      input, KeyCode::MouseLeft, InputAction::None);
    module.Update(0.016);
  }
};

static bool
contains(const std::vector<DrawableBase*>& drawables, const DrawableBase* item)
{
  return std::find(drawables.begin(), drawables.end(), item) != drawables.end();
}

static int
testDisplayEdits()
{
  TestCounters counters;
  ViewerPanelFixture fixture(false);
  testTrue(counters, fixture.started, "the viewer starts");
  fixture.module.Update(0.016);
  MeshViewerDisplayPanel* display = fixture.module.displayPanel();
  testTrue(counters,
           display->placement().visible && display->placement().area.x > 900.0f,
           "Display docks in the right column");

  float x = 0.0f;
  float y = 0.0f;
  display->controlPointForTesting("Grid", 0.0f, &x, &y);
  fixture.click(x, y);
  testTrue(counters,
           !fixture.module.showGrid() &&
             fixture.env.getVar("showGrid").value == "0",
           "the Grid toggle hides the grid and keeps the setting");

  display->controlPointForTesting("Lighting", 0.0f, &x, &y);
  fixture.click(x, y);
  testTrue(counters,
           !fixture.module.meshVisual()->isLightingEnabled() &&
             fixture.env.getVar("lightingEnabled").value == "0",
           "the Lighting toggle edits its env var and applies at once");

  // A slider drag: press on the track, move to its end, release.
  const float yawBefore = fixture.module.cameraController().yaw();
  display->controlPointForTesting("Radius", 0.1f, &x, &y);
  fixture.window.mouseX = x;
  fixture.window.mouseY = y;
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::Press);
  fixture.module.Update(0.016);
  float endX = 0.0f;
  float endY = 0.0f;
  display->controlPointForTesting("Radius", 1.0f, &endX, &endY);
  fixture.window.mouseX = endX + 40.0f;
  fixture.module.Update(0.016);
  InputManagerTestAccess::setAction(
    fixture.input, KeyCode::MouseLeft, InputAction::None);
  fixture.module.Update(0.016);
  testTrue(counters,
           std::fabs(fixture.module.meshVisual()->getShadowRadius() - 20.0f) <
               0.01f &&
             !fixture.env.getVar("shadowRadius").value.empty(),
           "dragging the Radius slider sets the shadow radius");
  testTrue(counters,
           std::fabs(fixture.module.cameraController().yaw() - yawBefore) <
             1.0e-5f,
           "a slider drag never orbits the camera");

  display->controlPointForTesting("Ambient", 0.5f, &x, &y);
  fixture.click(x, y);
  testTrue(counters,
           std::fabs(fixture.module.meshVisual()->getAmbientColor().y - 0.5f) <
             0.02f,
           "the Ambient slider sets the ambient brightness");

  MeshViewerInfoPanel* info = fixture.module.infoPanel();
  testTrue(counters,
           fixture.module.loadMeshFromMemory(
             "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", "tri.obj"),
           "a mesh loads");
  fixture.module.Update(0.016);
  testTrue(counters,
           info->placement().visible && info->getVisual().textCount() >= 10u,
           "the Info panel lists the mesh");
  return counters.failures;
}

static int
testViewerPopOutAndDock()
{
  TestCounters counters;
  ViewerPanelFixture fixture(true);
  GuiPanelDock& dock = fixture.module.dock();
  MeshViewerUi* ui = fixture.module.ui();
  float x = 0.0f;
  float y = 0.0f;
  testTrue(
    counters,
    ui->menuItemCenterForTesting(MeshViewerAction::PopOutDisplayPanel, &x, &y),
    "the View menu offers pop-out");
  fixture.click(x, y);
  fixture.frame();
  MeshViewerDisplayPanel* display = fixture.module.displayPanel();
  testTrue(counters,
           dock.mode("display") == GuiDockMode::Detached &&
             display->placement().surface == 202u,
           "the Display panel pops out into its own window");

  fixture.scene.ClearDrawables();
  fixture.surfaces.clearScenes();
  fixture.module.DispatchDrawables(&fixture.scene);
  FakePanelSurfaces::Window* window = fixture.surfaces.window(202);
  testTrue(counters,
           window != nullptr &&
             contains(window->scene->drawablesIn(RenderLayerId::UI), display) &&
             !contains(fixture.scene.drawablesIn(RenderLayerId::UI), display),
           "a detached panel draws in its window only");
  testTrue(counters,
           dock.view("info").frame.h > 400.0f,
           "Info takes the whole right column");

  // A click in the detached window edits a setting.
  display->controlPointForTesting("Wireframe", 0.0f, &x, &y);
  window->pointer.x = x;
  window->pointer.y = y;
  window->pointer.left = true;
  fixture.module.Update(0.016);
  window->pointer.left = false;
  fixture.module.Update(0.016);
  testTrue(counters,
           fixture.module.showWireframe(),
           "the detached Display panel toggles the wireframe");

  fixture.surfaces.userClose(202);
  fixture.frame();
  testTrue(counters,
           dock.mode("display") == GuiDockMode::Docked &&
             display->placement().surface == 0u,
           "closing the window docks the panel");

  ui->menuItemCenterForTesting(MeshViewerAction::ToggleInfoPanel, &x, &y);
  fixture.click(x, y);
  testTrue(counters,
           dock.mode("info") == GuiDockMode::Hidden &&
             !fixture.module.infoPanel()->placement().visible,
           "View > Info hides the panel");
  const std::string saved = fixture.env.getVar("panelLayout").value;
  testTrue(counters,
           saved.find("panel info hidden") != std::string::npos,
           "the layout is saved in panelLayout");
  return counters.failures;
}

void
registerMeshViewerPanelsTests(IllumoTestRegistry& registry)
{
  registry.add("IllMeshViewer.Panels.DisplayEdits",
               []() { return testDisplayEdits(); });
  registry.add("IllMeshViewer.Panels.PopOutAndDock",
               []() { return testViewerPopOutAndDock(); });
}
