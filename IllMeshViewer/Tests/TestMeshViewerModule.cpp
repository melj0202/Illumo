#include "MeshViewerModule.h"
#include "TestAccess.h"
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>

static TestCounters g;

static const std::string g_testCubeObj = "# Simple Cube OBJ\n"
                                         "v -1.0 -1.0  1.0\n"
                                         "v  1.0 -1.0  1.0\n"
                                         "v -1.0  1.0  1.0\n"
                                         "v  1.0  1.0  1.0\n"
                                         "v -1.0  1.0 -1.0\n"
                                         "v  1.0  1.0 -1.0\n"
                                         "v -1.0 -1.0 -1.0\n"
                                         "v  1.0 -1.0 -1.0\n"
                                         "f 1 2 4 3\n"
                                         "f 3 4 6 5\n"
                                         "f 5 6 8 7\n"
                                         "f 7 8 2 1\n"
                                         "f 2 8 6 4\n"
                                         "f 7 1 3 5\n";

struct ModuleFixture
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
  MeshViewerModule module;
  bool started;

  ModuleFixture()
    : window(1280, 720)
    , env()
    , camera(glm::vec2(0.0f, 0.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , assets(&renderer, false)
    , registry()
    , console(&env, &registry, &window, &renderer, "IllMeshViewer")
    , input(nullptr)
    , scene(&window, &camera)
    , context{ &scene,  &window, &console, &input,   &renderer,
               &assets, &env,    &camera,  &registry }
    , module()
    , started(false)
  {
    env.setVar("WinX", 1280);
    env.setVar("WinY", 720);
    mock.Initialize();
    started = module.Start(&context);
  }

  ~ModuleFixture()
  {
    if (started) {
      module.Exit();
    }
  }
};

static void
testModuleStartupAndLifecycle()
{
  testSection("MeshViewerModule: startup and clean lifecycle");
  ModuleFixture fixture;
  testTrue(g, fixture.started, "module started successfully");
  testTrue(g, fixture.module.showGrid(), "grid enabled by default");
  testTrue(g, !fixture.module.showWireframe(), "wireframe disabled by default");

  fixture.module.Update(0.016);
  fixture.module.DispatchDrawables(&fixture.scene);
  testTrue(
    g, fixture.scene.drawableCount() >= 2u, "grid and UI drawables registered");
}

static void
testModuleMeshLoading()
{
  testSection("MeshViewerModule: load mesh from memory and frame camera");
  ModuleFixture fixture;
  testTrue(g, fixture.started, "module started");

  const bool loaded =
    fixture.module.loadMeshFromMemory(g_testCubeObj, "cube.obj");
  testTrue(g, loaded, "mesh loaded from memory");
  testTrue(g, !fixture.module.meshData().isEmpty(), "mesh data is not empty");
  testEqSize(
    g, fixture.module.meshData().vertices.size(), 8u, "cube has 8 vertices");
  testEqSize(
    g, fixture.module.meshData().indices.size(), 36u, "cube has 36 indices");

  fixture.module.Update(0.016);
  fixture.module.DispatchDrawables(&fixture.scene);
  testTrue(g,
           fixture.scene.drawableCount() >= 3u,
           "grid, mesh, and UI drawables registered");

  // Toggle wireframe
  fixture.module.setShowWireframe(true);
  testTrue(g, fixture.module.showWireframe(), "wireframe enabled");
  fixture.module.DispatchDrawables(&fixture.scene);
  testTrue(g,
           fixture.scene.drawableCount() >= 4u,
           "grid, mesh, wireframe, and UI drawables registered");

  // Reset camera
  fixture.module.resetCamera();
  testTrue(g,
           fixture.module.cameraController().distance() > 0.0f,
           "camera distance valid");
}

static int
runModuleCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerMeshViewerModuleTests(IllumoTestRegistry& registry)
{
  registry.add("IllMeshViewer.Module.StartupAndLifecycle",
               []() { return runModuleCase(testModuleStartupAndLifecycle); });
  registry.add("IllMeshViewer.Module.MeshLoading",
               []() { return runModuleCase(testModuleMeshLoading); });
}
