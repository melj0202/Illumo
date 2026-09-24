#include "MeshViewerModule.h"
#include "TestAccess.h"
#include <Illumo/Content/VfsAssetSource.h>
#include <Illumo/Content/VirtualFileSystem.h>
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
#include <filesystem>
#include <fstream>

static TestCounters g;
static int g_loadDialogCalls = 0;

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
std::string
SaveLoad::GetLoadLocation(const SaveLoadDialogSpec&)
{
  ++g_loadDialogCalls;
  return {};
}

std::string
SaveLoad::GetSaveLocation(const SaveLoadDialogSpec&)
{
  return {};
}

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

  explicit ModuleFixture(IAssetSource* source = nullptr)
    : window(1280, 720)
    , env(isolatedSettings("meshviewer-module-settings.json"))
    , camera(glm::vec2(0.0f, 0.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , assets(&renderer, false, source)
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
  testTrue(g, fixture.module.showSkybox(), "skybox enabled by default");

  fixture.module.setShowSkybox(false);
  testTrue(g, !fixture.module.showSkybox(), "skybox disabled via setter");
  fixture.module.setShowSkybox(true);
  testTrue(g, fixture.module.showSkybox(), "skybox re-enabled via setter");

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

  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&fixture.scene, &fixture.camera);
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

  fixture.scene.ClearDrawables();
  fixture.module.DispatchDrawables(&fixture.scene);
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&fixture.scene, &fixture.camera);
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

static void
sendShortcut(ModuleFixture& fixture, KeyCode key, InputAction action)
{
  fixture.input.getKeyQueue().push({ key, action, 0 });
  fixture.module.Update(0.016);
}

static void
testRepeatedShortcuts()
{
  ModuleFixture fixture;
  fixture.module.cameraController().setSmoothingSpeed(1000.0f);
  g_loadDialogCalls = 0;
  const float resetDistance = fixture.module.cameraController().distance();
  for (int cycle = 0; cycle < 3; ++cycle) {
    const bool grid = fixture.module.showGrid();
    const bool wire = fixture.module.showWireframe();
    const bool sky = fixture.module.showSkybox();
    for (KeyCode key : { KeyCode::G, KeyCode::X, KeyCode::B, KeyCode::O }) {
      sendShortcut(fixture, key, InputAction::Press);
      sendShortcut(fixture, key, InputAction::Hold);
      sendShortcut(fixture, key, InputAction::Release);
    }
    testTrue(g, fixture.module.showGrid() == !grid, "G toggles each press");
    testTrue(
      g, fixture.module.showWireframe() == !wire, "X toggles each press");
    testTrue(g, fixture.module.showSkybox() == !sky, "B toggles each press");
    testEqInt(g, g_loadDialogCalls, cycle + 1, "O opens once per press");
    for (KeyCode key : { KeyCode::F, KeyCode::R }) {
      fixture.module.cameraController().setDistance(resetDistance * 3.0f);
      sendShortcut(fixture, key, InputAction::Press);
      testTrue(g,
               std::abs(fixture.module.cameraController().distance() -
                        resetDistance) < 0.001f,
               "F and R reset repeatedly");
      fixture.module.cameraController().setDistance(resetDistance * 2.0f);
      sendShortcut(fixture, key, InputAction::Hold);
      sendShortcut(fixture, key, InputAction::Release);
      testTrue(g,
               fixture.module.cameraController().distance() > resetDistance,
               "repeat and release do not reset camera");
    }
  }
  fixture.console.isOpen = true;
  const bool grid = fixture.module.showGrid();
  sendShortcut(fixture, KeyCode::G, InputAction::Press);
  testTrue(g, fixture.module.showGrid() == grid, "console suppresses shortcut");
  fixture.input.clearKeyQueue();
  fixture.console.isOpen = false;
  sendShortcut(fixture, KeyCode::G, InputAction::Press);
  testTrue(
    g, fixture.module.showGrid() != grid, "shortcut works after console");
  fixture.input.getKeyQueue().push({ KeyCode::A, InputAction::Press, 0 });
  fixture.module.Update(0.016);
  testEqSize(
    g, fixture.input.getKeyQueue().size(), 1u, "unrelated key preserved");
  ModuleFixture second;
  sendShortcut(second, KeyCode::G, InputAction::Press);
  testTrue(
    g, !second.module.showGrid(), "second instance has independent shortcuts");
}

static const char* const kDemoScene = R"({
  "format": "ilsc",
  "format_version": [2, 0],
  "metadata": { "title": "Demo" },
  "settings": { "world_mode": "3d" },
  "assets": [ { "id": "cube", "type": "mesh", "path": "meshes/cube.obj" } ],
  "nodes": [
    { "id": "root", "name": "Root" },
    { "id": "floor", "parent": "root",
      "transform": { "position": [0, -1, 0] },
      "components": [ { "type": "primitive", "shape": "cube",
                        "extent": [4, 0.1, 4], "color": [90, 110, 140, 255] } ] },
    { "id": "model", "parent": "root",
      "transform": { "position": [3, 0, 0] },
      "components": [ { "type": "mesh", "asset": "cube" } ] }
  ]
})";

static void
writeText(const std::filesystem::path& path, const std::string& text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file << text;
}

// A loose package mounted at /packages/demo, as the runtime mounts one found
// in packages/ or named by --mount.
static std::shared_ptr<VirtualFileSystem>
mountDemoPackage(const std::filesystem::path& root)
{
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  writeText(root / "scenes" / "demo.ilsc", kDemoScene);
  writeText(root / "meshes" / "cube.obj", g_testCubeObj);
  std::shared_ptr<VirtualFileSystem> tree =
    std::make_shared<VirtualFileSystem>();
  std::string error;
  std::shared_ptr<DirectoryVfsBackend> backend =
    DirectoryVfsBackend::open(root, false, error);
  testTrue(g, backend != nullptr, "demo package directory opens");
  if (backend) {
    testTrue(
      g,
      tree->mount({ "/packages/demo", { { backend, "demo" } }, {} }, error),
      "demo package mounts");
  }
  return tree;
}

static bool
drawsLayerDrawable(Scene& scene, const DrawableBase* drawable)
{
  for (const DrawableBase* candidate :
       scene.drawablesIn(RenderLayerId::World)) {
    if (candidate == drawable) {
      return true;
    }
  }
  return false;
}

static void
testSceneFromTree()
{
  testSection("MeshViewerModule: opens an .ilsc scene from the file tree");
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "IllMeshViewerSceneTest";
  std::shared_ptr<VirtualFileSystem> tree = mountDemoPackage(root);
  MeshViewerNativeTree::install(tree);
  VfsAssetSource source(tree);
  {
    ModuleFixture fixture(&source);
    testTrue(g, fixture.started, "module started over the tree");
    fixture.module.loadMeshFromMemory(g_testCubeObj, "cube.obj");
    for (int step = 0; step < 60; ++step) {
      fixture.module.Update(0.1);
    }
    const float meshDistance = fixture.module.cameraController().distance();

    testTrue(g,
             fixture.registry.QueueCommand(
               "viewer_open", { "/packages/demo/scenes/demo.ilsc" }),
             "viewer_open is registered");
    fixture.registry.ExecuteQueue();
    SceneInstance* opened = fixture.module.sceneInstance();
    testTrue(g, opened != nullptr, "viewer_open instantiates the scene");
    testTrue(g,
             fixture.module.meshData().isEmpty(),
             "opening a scene replaces the loose mesh");
    if (opened != nullptr) {
      testEqSize(g, opened->nodeCount(), 3u, "every node instantiates");
      testTrue(g,
               opened->warnings().empty(),
               "the package-relative mesh resolves against /packages/demo");
      testTrue(g,
               opened->packageRoot() == "/packages/demo",
               "the scene's package root is its mount");
      const MeshMetadata& meta =
        MeshViewerModuleTestAccess::ui(fixture.module)->meshMetadata();
      testTrue(g, meta.isScene && meta.hasMesh, "the card describes a scene");
      testEqSize(g, meta.nodeCount, 3u, "the card counts nodes");
      testEqSize(g, meta.assetCount, 1u, "the card counts assets");
      testEqSize(g, meta.missingCount, 0u, "no asset is missing");
      testTrue(g,
               meta.dimensions.x > 4.0f,
               "bounds span the floor and the offset model");
      // The orbit camera eases toward its framing target.
      for (int step = 0; step < 60; ++step) {
        fixture.module.Update(0.1);
      }
      testTrue(g,
               fixture.module.cameraController().distance() >
                 meshDistance * 2.0f,
               "the camera frames the larger scene bounds");

      fixture.module.setShowWireframe(true);
      fixture.module.Update(0.016);
      fixture.scene.ClearDrawables();
      fixture.module.DispatchDrawables(&fixture.scene);
      testTrue(g,
               drawsLayerDrawable(fixture.scene, &opened->drawable()),
               "the scene draws in the World layer");
      testTrue(g,
               !drawsLayerDrawable(
                 fixture.scene,
                 MeshViewerModuleTestAccess::meshVisual(fixture.module)),
               "the empty mesh visual is not drawn");
      testTrue(g,
               drawsLayerDrawable(
                 fixture.scene,
                 MeshViewerModuleTestAccess::wireframeVisual(fixture.module)),
               "wireframe outlines the scene bounds");
    }

    fixture.module.loadMeshFromMemory(g_testCubeObj, "cube.obj");
    testTrue(g,
             fixture.module.sceneInstance() == nullptr,
             "loading a mesh closes the scene");

    fixture.registry.QueueCommand("viewer_open",
                                  { "/packages/demo/scenes/absent.ilsc" });
    fixture.registry.ExecuteQueue();
    testTrue(g,
             fixture.module.sceneInstance() == nullptr &&
               !fixture.module.meshData().isEmpty(),
             "a missing scene leaves the current mesh");
    fixture.registry.QueueCommand("viewer_open", { "relative/scene.ilsc" });
    fixture.registry.ExecuteQueue();
    testTrue(g,
             fixture.module.sceneInstance() == nullptr,
             "viewer_open refuses a relative path");

    // A dialog pick has no package: relative references cannot resolve, so
    // the scene opens with a placeholder instead of failing.
    fixture.module.openLocation(
      { (root / "scenes" / "demo.ilsc").string(), "demo.ilsc" });
    opened = fixture.module.sceneInstance();
    testTrue(g, opened != nullptr, "a picked scene file opens");
    if (opened != nullptr) {
      testTrue(g,
               opened->packageRoot() == "/local",
               "a picked scene resolves against /local");
      testTrue(g,
               !opened->warnings().empty(),
               "its package-relative mesh falls back to a placeholder");
    }
  }
  MeshViewerNativeTree::install(nullptr);
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
}

static void
testSceneFromTextRejectsInvalid()
{
  testSection("MeshViewerModule: invalid scene text keeps the current view");
  ModuleFixture fixture;
  fixture.module.loadMeshFromMemory(g_testCubeObj, "cube.obj");
  std::string error;
  testTrue(g,
           !fixture.module.loadSceneFromText(
             R"({"format":"ilsc","format_version":[1,0],"nodes":[]})",
             "old.ilsc",
             "/local",
             {},
             &error),
           "a format 1 scene is refused");
  testTrue(g, !error.empty(), "the refusal explains itself");
  testTrue(g,
           fixture.module.sceneInstance() == nullptr &&
             !fixture.module.meshData().isEmpty(),
           "the mesh stays open");
  testTrue(g,
           MeshViewerModule::isSceneLocation({ "x", "Forest.ILSC" }) &&
             !MeshViewerModule::isSceneLocation({ "x", "forest.obj" }) &&
             MeshViewerModule::isSceneLocation({ "vfs:/app/a.ilsc", "" }),
           "scene locations are recognized by extension");
}

void
registerMeshViewerModuleTests(IllumoTestRegistry& registry)
{
  registry.add("IllMeshViewer.Module.SceneFromTree",
               []() { return runModuleCase(testSceneFromTree); });
  registry.add("IllMeshViewer.Module.SceneRejectsInvalid",
               []() { return runModuleCase(testSceneFromTextRejectsInvalid); });
  registry.add("IllMeshViewer.Module.RepeatedShortcuts",
               []() { return runModuleCase(testRepeatedShortcuts); });
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
