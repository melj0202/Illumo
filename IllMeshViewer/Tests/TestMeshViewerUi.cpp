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
  testSection("MeshViewerUi: layout, hit-testing, and buttons");
  UiFixture fixture;

  testTrue(g, fixture.ui.containsScreenPoint(100.0f, 15.0f), "header hit");
  testTrue(g, fixture.ui.containsScreenPoint(100.0f, 710.0f), "status bar hit");
  testTrue(g,
           !fixture.ui.containsScreenPoint(640.0f, 150.0f),
           "empty 3D center passes");

  fixture.ui.update(&fixture.input, 0.016f);
  testTrue(
    g, fixture.ui.buttonCountForTesting() >= 5u, "header buttons created");
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

  fixture.ui.showToast("Loaded mesh successfully",
                       ColorRgba{ 60, 220, 120, 255 });
  fixture.ui.update(&fixture.input, 0.016f);

  testTrue(g, fixture.ui.meshMetadata().hasMesh, "metadata registered hasMesh");
  testTrue(g,
           fixture.ui.meshMetadata().filename == "suzanne.obj",
           "metadata filename preserved");
}

static void
testUiFontSizeScaling()
{
  testSection("MeshViewerUi: font size scaling and dynamic button widths");
  UiFixture fixture;

  fixture.ui.setFontSize(18.0f);
  fixture.ui.update(&fixture.input, 0.016f);
  testTrue(
    g, fixture.ui.buttonCountForTesting() >= 5u, "buttons with large font");

  // Empty state card bounds should scale properly
  testTrue(g,
           fixture.ui.containsScreenPoint(640.0f, 320.0f),
           "center empty card hit-test with scaled font");
}

static void
testUiSmallWindowLayout()
{
  testSection("MeshViewerUi: responsive small window layout and hit testing");
  NullRenderWindow smallWindow(640, 360);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&smallWindow, &env, &camera, &mock, false);
  InputManager input(nullptr);
  MeshViewerUi ui(&smallWindow, &renderer);

  ui.update(&input, 0.016f);
  testTrue(g, ui.buttonCountForTesting() >= 5u, "buttons on small window");
  testTrue(g,
           ui.containsScreenPoint(320.0f, 160.0f),
           "center empty card hit-test on 640x360 window");
  testTrue(g, ui.containsScreenPoint(50.0f, 10.0f), "header hit-test");
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
  registry.add("IllMeshViewer.Ui.FontSizeScaling",
               []() { return runUiCase(testUiFontSizeScaling); });
  registry.add("IllMeshViewer.Ui.SmallWindowLayout",
               []() { return runUiCase(testUiSmallWindowLayout); });
}
