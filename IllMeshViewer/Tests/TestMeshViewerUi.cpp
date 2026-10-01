#include "MeshViewerUi.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>

static TestCounters g;

struct UiFixture
{
  NullRenderWindow window;
  EnvVars env;
  Camera camera;
  MockBackend mock;
  Renderer renderer;
  InputManager input;
  MeshViewerUi ui;

  UiFixture()
    : window(1280, 720)
    , env()
    , camera(glm::vec2(0.0f, 0.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , input(nullptr)
    , ui(&window, &renderer)
  {
    mock.Initialize();
  }
};

static void
testUiLayoutAndHits()
{
  testSection("MeshViewerUi: plain menu bar, status bar and empty card");
  UiFixture fixture;

  testTrue(g, fixture.ui.containsScreenPoint(100.0f, 10.0f), "menu bar hit");
  testTrue(g, fixture.ui.containsScreenPoint(100.0f, 710.0f), "status bar hit");
  testTrue(g,
           !fixture.ui.containsScreenPoint(640.0f, 150.0f),
           "the empty viewport passes");
  testTrue(g,
           fixture.ui.containsScreenPoint(640.0f, 330.0f),
           "the empty-state card blocks the viewport under it");

  fixture.ui.update(&fixture.input, 0.016f);
  testEqSize(g, fixture.ui.menuCountForTesting(), 2u, "File and View menus");
  testTrue(g,
           fixture.ui.getVisual().spriteCount() == 0u &&
             fixture.ui.getVisual().shapeCount() > 0u,
           "the plain chrome is flat shapes and text");
}

static void
testUiMenus()
{
  testSection("MeshViewerUi: menus issue actions");
  UiFixture fixture;
  float x = 0.0f;
  float y = 0.0f;
  testTrue(
    g,
    fixture.ui.menuItemCenterForTesting(MeshViewerAction::OpenMesh, &x, &y) &&
      fixture.ui.clickAtForTesting(x, y) == MeshViewerAction::OpenMesh,
    "File > Open");
  testTrue(
    g,
    fixture.ui.menuItemCenterForTesting(MeshViewerAction::ToggleGrid, &x, &y) &&
      fixture.ui.clickAtForTesting(x, y) == MeshViewerAction::ToggleGrid,
    "View > Grid");
  std::vector<MeshViewerPanelMenuEntry> panels;
  MeshViewerPanelMenuEntry display;
  display.title = "Display";
  display.toggle = MeshViewerAction::ToggleDisplayPanel;
  display.popOut = MeshViewerAction::PopOutDisplayPanel;
  panels.push_back(display);
  fixture.ui.setPanels(panels, false);
  testTrue(g,
           fixture.ui.menuItemCenterForTesting(
             MeshViewerAction::PopOutDisplayPanel, &x, &y) &&
             fixture.ui.clickAtForTesting(x, y) == MeshViewerAction::None,
           "pop-out is disabled without window support");
  fixture.ui.closeMenus();
  fixture.ui.setPanels(panels, true);
  testTrue(g,
           fixture.ui.menuItemCenterForTesting(
             MeshViewerAction::PopOutDisplayPanel, &x, &y) &&
             fixture.ui.clickAtForTesting(x, y) ==
               MeshViewerAction::PopOutDisplayPanel,
           "pop-out fires with window support");
}

static void
testUiMetadataAndToast()
{
  testSection("MeshViewerUi: mesh metadata and toast notifications");
  UiFixture fixture;

  MeshMetadata meta;
  meta.hasMesh = true;
  meta.filename = "suzanne.obj";
  meta.vertexCount = 500;
  meta.triangleCount = 968;
  meta.dimensions = glm::vec3(2.0f, 1.5f, 1.8f);
  fixture.ui.setMeshMetadata(meta);

  fixture.ui.showToast("Loaded mesh successfully");
  fixture.ui.update(&fixture.input, 0.016f);

  testTrue(g, fixture.ui.meshMetadata().hasMesh, "metadata registered hasMesh");
  testTrue(g,
           fixture.ui.meshMetadata().filename == "suzanne.obj",
           "metadata filename preserved");
  testTrue(g,
           !fixture.ui.containsScreenPoint(640.0f, 330.0f),
           "with a mesh open the card is gone");
  testEqStr(g,
            fixture.ui.toastForTesting(),
            "Loaded mesh successfully",
            "the toast is shown");
}

static void
testUiSmallWindowLayout()
{
  testSection("MeshViewerUi: small window layout and hit testing");
  NullRenderWindow smallWindow(640, 360);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&smallWindow, &env, &camera, &mock, false);
  InputManager input(nullptr);
  MeshViewerUi ui(&smallWindow, &renderer);
  ui.setViewport(GuiToolRect{ 0.0f, 24.0f, 640.0f, 314.0f });

  ui.update(&input, 0.016f);
  testTrue(g,
           ui.containsScreenPoint(320.0f, 160.0f),
           "center empty card hit-test on 640x360 window");
  testTrue(g, ui.containsScreenPoint(50.0f, 10.0f), "menu bar hit-test");
  testTrue(g, ui.containsScreenPoint(50.0f, 350.0f), "status bar hit-test");
}
static int
runUiCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerMeshViewerUiTests(IllumoTestRegistry& registry)
{
  registry.add("IllMeshViewer.Ui.LayoutAndHits",
               []() { return runUiCase(testUiLayoutAndHits); });
  registry.add("IllMeshViewer.Ui.MetadataAndToast",
               []() { return runUiCase(testUiMetadataAndToast); });
  registry.add("IllMeshViewer.Ui.Menus",
               []() { return runUiCase(testUiMenus); });
  registry.add("IllMeshViewer.Ui.SmallWindowLayout",
               []() { return runUiCase(testUiSmallWindowLayout); });
}
