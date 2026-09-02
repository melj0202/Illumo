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
#include <cstring>

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

static void
testModuleLightingFromEnvVars()
{
  testSection("MeshViewerModule: lighting EnvVars update MeshVisual");
  ModuleFixture fixture;
  testTrue(g, fixture.started, "module started");
  testTrue(g,
           fixture.module.loadMeshFromMemory(g_testCubeObj, "cube.obj"),
           "cube loaded");

  fixture.env.setVar("lightingEnabled", "1");
  fixture.env.setVar("lightDirX", 0);
  fixture.env.setVar("lightDirY", 1);
  fixture.env.setVar("lightDirZ", 0);
  fixture.env.setVar("lightColorR", 0.25);
  fixture.env.setVar("lightColorG", 0.5);
  fixture.env.setVar("lightColorB", 0.75);
  fixture.env.setVar("ambientColorR", 0.1);
  fixture.env.setVar("ambientColorG", 0.2);
  fixture.env.setVar("ambientColorB", 0.3);
  fixture.module.Update(0.016);

  MeshVisual* visual = fixture.module.meshVisual();
  testTrue(g, visual != nullptr, "mesh visual exists");
  testTrue(g, visual->isLightingEnabled(), "lighting remains enabled");
  testTrue(g,
           std::abs(visual->getLightDirection().x) < 0.0001f &&
             std::abs(visual->getLightDirection().y - 1.0f) < 0.0001f &&
             std::abs(visual->getLightDirection().z) < 0.0001f,
           "light direction from EnvVars");
  testTrue(g,
           std::abs(visual->getLightColor().x - 0.25f) < 0.0001f &&
             std::abs(visual->getLightColor().y - 0.5f) < 0.0001f &&
             std::abs(visual->getLightColor().z - 0.75f) < 0.0001f,
           "light color from EnvVars");
  testTrue(g,
           std::abs(visual->getAmbientColor().x - 0.1f) < 0.0001f &&
             std::abs(visual->getAmbientColor().y - 0.2f) < 0.0001f &&
             std::abs(visual->getAmbientColor().z - 0.3f) < 0.0001f,
           "ambient color from EnvVars");

  fixture.renderer.BeginFrame();
  testTrue(g, visual->AppendCommands(&fixture.renderer), "mesh appends tokens");
  fixture.renderer.EndFrame();

  bool sawConfiguredLightDir = false;
  for (size_t i = 0; i < fixture.mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = fixture.mock.getLastNonEmptySubmitted(i);
    if (command.commandType == CommandType::SetUniformVec3 &&
        std::strcmp(command.uniformVec3.name, "uLightDir") == 0 &&
        std::abs(command.uniformVec3.x) < 0.0001f &&
        std::abs(command.uniformVec3.y - 1.0f) < 0.0001f &&
        std::abs(command.uniformVec3.z) < 0.0001f) {
      sawConfiguredLightDir = true;
    }
  }
  testTrue(
    g, sawConfiguredLightDir, "configured light direction reaches uLightDir");

  fixture.env.setVar("lightingEnabled", "0");
  fixture.module.Update(0.016);
  testTrue(
    g, !visual->isLightingEnabled(), "lightingEnabled 0 disables lighting");
}

static void
testModuleShadowFromEnvVars()
{
  testSection("MeshViewerModule: shadow EnvVars update MeshVisual");
  ModuleFixture fixture;
  testTrue(g, fixture.started, "module started");
  testTrue(g,
           fixture.module.loadMeshFromMemory(g_testCubeObj, "cube.obj"),
           "cube loaded");

  fixture.env.setVar("lightingEnabled", "1");
  fixture.env.setVar("shadowsEnabled", "1");
  fixture.env.setVar("shadowMapSize", 512);
  fixture.env.setVar("shadowRadius", 3.0);
  fixture.env.setVar("lightDistance", 12.0);
  fixture.env.setVar("shadowBias", 0.002);
  fixture.env.setVar("shadowSlopeScale", 0.01);
  fixture.env.setVar("shadowNormalOffset", 0.02);
  fixture.env.setVar("shadowPcf", "0");
  fixture.module.Update(0.016);

  MeshVisual* visual = fixture.module.meshVisual();
  testTrue(g, visual != nullptr, "mesh visual exists");
  testTrue(g, visual->isShadowsEnabled(), "shadows remain enabled");
  testTrue(g, visual->getShadowMapSize() == 512, "shadowMapSize from EnvVars");
  testTrue(g,
           std::abs(visual->getShadowRadius() - 3.0f) < 0.0001f,
           "shadowRadius from EnvVars");
  testTrue(g,
           std::abs(visual->getLightDistance() - 12.0f) < 0.0001f,
           "lightDistance from EnvVars");
  testTrue(g,
           std::abs(visual->getShadowBias() - 0.002f) < 0.0001f,
           "shadowBias from EnvVars");
  testTrue(g,
           std::abs(visual->getShadowSlopeScale() - 0.01f) < 0.0001f,
           "shadowSlopeScale from EnvVars");
  testTrue(g,
           std::abs(visual->getShadowNormalOffset() - 0.02f) < 0.0001f,
           "shadowNormalOffset from EnvVars");
  testTrue(g, !visual->isShadowPcfEnabled(), "shadowPcf 0 disables PCF");

  fixture.renderer.BeginFrame();
  testTrue(g, visual->AppendCommands(&fixture.renderer), "mesh appends tokens");
  fixture.renderer.EndFrame();

  bool sawConfiguredBias = false;
  bool sawShadowViewport = false;
  bool insideShadowTarget = false;
  for (size_t i = 0; i < fixture.mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = fixture.mock.getLastNonEmptySubmitted(i);
    if (command.commandType == CommandType::SetFramebuffer &&
        command.bindFramebuffer.handle.isValid()) {
      insideShadowTarget = true;
    } else if (command.commandType == CommandType::SetFramebuffer &&
               !command.bindFramebuffer.handle.isValid()) {
      insideShadowTarget = false;
    }
    if (insideShadowTarget && command.commandType == CommandType::SetViewport &&
        command.viewport.width == 512 && command.viewport.height == 512) {
      sawShadowViewport = true;
    }
    if (command.commandType == CommandType::SetUniformFloat &&
        std::strcmp(command.uniformFloat.name, "uShadowBias") == 0 &&
        std::abs(command.uniformFloat.value - 0.002f) < 0.000001f) {
      sawConfiguredBias = true;
    }
  }
  testTrue(g, sawShadowViewport, "configured shadow map size reaches viewport");
  testTrue(g, sawConfiguredBias, "configured shadow bias reaches uShadowBias");

  fixture.env.setVar("shadowsEnabled", "0");
  fixture.module.Update(0.016);
  testTrue(g, !visual->isShadowsEnabled(), "shadowsEnabled 0 disables shadows");
}

static void
testModuleMotionBlurFromEnvVars()
{
  testSection("MeshViewerModule: motion blur EnvVars update MeshVisual");
  ModuleFixture fixture;
  testTrue(g, fixture.started, "module started");
  testTrue(g,
           fixture.module.loadMeshFromMemory(g_testCubeObj, "cube.obj"),
           "cube loaded");

  fixture.env.setVar("lightingEnabled", "1");
  fixture.env.setVar("motionBlurEnabled", "1");
  fixture.env.setVar("motionBlurAmount", 0.75);
  fixture.env.setVar("motionBlurMax", 0.15);
  fixture.module.Update(0.016);

  MeshVisual* visual = fixture.module.meshVisual();
  testTrue(g, visual != nullptr, "mesh visual exists");
  testTrue(g, visual->isMotionBlurEnabled(), "motion blur remains enabled");
  testTrue(g,
           std::abs(visual->getMotionBlurAmount() - 0.75f) < 0.0001f,
           "motionBlurAmount from EnvVars");
  testTrue(g,
           std::abs(visual->getMotionBlurMax() - 0.15f) < 0.0001f,
           "motionBlurMax from EnvVars");

  fixture.renderer.BeginFrame();
  testTrue(g, visual->AppendCommands(&fixture.renderer), "mesh appends tokens");
  fixture.renderer.EndFrame();

  bool sawConfiguredAmount = false;
  for (size_t i = 0; i < fixture.mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = fixture.mock.getLastNonEmptySubmitted(i);
    if (command.commandType == CommandType::SetUniformFloat &&
        std::strcmp(command.uniformFloat.name, "uMotionBlurAmount") == 0 &&
        std::abs(command.uniformFloat.value - 0.75f) < 0.000001f) {
      sawConfiguredAmount = true;
    }
  }
  testTrue(g,
           sawConfiguredAmount,
           "configured motion blur amount reaches uMotionBlurAmount");

  fixture.env.setVar("motionBlurEnabled", "0");
  fixture.module.Update(0.016);
  testTrue(g,
           !visual->isMotionBlurEnabled(),
           "motionBlurEnabled 0 disables motion blur");
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
  registry.add("IllMeshViewer.Module.LightingFromEnvVars",
               []() { return runModuleCase(testModuleLightingFromEnvVars); });
  registry.add("IllMeshViewer.Module.ShadowFromEnvVars",
               []() { return runModuleCase(testModuleShadowFromEnvVars); });
  registry.add("IllMeshViewer.Module.MotionBlurFromEnvVars",
               []() { return runModuleCase(testModuleMotionBlurFromEnvVars); });
}
