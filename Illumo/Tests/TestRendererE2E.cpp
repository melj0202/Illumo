// End-to-end drawable → Renderer → MockBackend token tests (no OpenGL).
// Linked into the current IllumoTests target with TestMockBackend.cpp.

#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Test helpers shared with TestMockBackend.cpp (same TU linkage: each file is
// separate). Duplicate minimal assert helpers — keep this file self-contained.

static int g_e2e_failures = 0;

static void
e2eTrue(bool cond, const char* msg)
{
  if (!cond) {
    std::printf("FAIL: %s\n", msg);
    ++g_e2e_failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

static void
e2eEqSize(size_t a, size_t b, const char* msg)
{
  if (a != b) {
    std::printf("FAIL: %s (got %zu, expected %zu)\n", msg, a, b);
    ++g_e2e_failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

static void
e2eEqInt(int a, int b, const char* msg)
{
  if (a != b) {
    std::printf("FAIL: %s (got %d, expected %d)\n", msg, a, b);
    ++g_e2e_failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

// ---------------------------------------------------------------------------
// Null window: dimensions only; no GLFW/GL
// ---------------------------------------------------------------------------
class E2ENullRenderWindow : public IRenderWindow
{
public:
  int width;
  int height;

  E2ENullRenderWindow(int w, int h)
    : IRenderWindow(w, h, "test", nullptr)
    , width(w)
    , height(h)
  {
  }

  void updateWindow() override {}
  void toggleFullscreen() override {}
  void reinitializeWindow(const int, const int, const std::string&) override {}
  void reinitializeWindow() override {}
  void handleResize(int w, int h) override
  {
    width = w;
    height = h;
  }
  std::array<double, 2> getMouseCoords() override
  {
    return std::array<double, 2>{ 0.0, 0.0 };
  }
  GLFWwindow* getWindowInstance() override { return nullptr; }
  std::array<int, 2> getWindowDimensions() override
  {
    return std::array<int, 2>{ width, height };
  }
  bool shouldWindowClose() override { return false; }
  bool isFramePaced() const override { return false; }
  void swapBuffers() override {}
  void requestClose() override {}
};

// ---------------------------------------------------------------------------
// Minimal token drawable for pure Renderer::RenderScene path
// ---------------------------------------------------------------------------
class TokenQuadDrawable : public DrawableBase
{
public:
  MeshHandle meshHandle{};
  ShaderHandle shaderHandle{};
  TextureHandle textureHandle{};
  bool enrolled = false;
  int appendCallCount = 0;
  int drawCount = 0;

  void enroll(Renderer* renderer)
  {
    float verts[32] = {
      1,  1,  0, 1, 0, 0, 1, 1, 1,  -1, 0, 0, 1, 0, 1, 0,
      -1, -1, 0, 0, 0, 1, 0, 0, -1, 1,  0, 1, 1, 0, 0, 1,
    };
    unsigned int idx[6] = { 0, 1, 2, 0, 2, 3 };
    meshHandle = renderer->enrollMesh(verts, sizeof(verts), idx, sizeof(idx));

    ShaderSources sources;
    sources.vertexSource = "void main(){}";
    sources.fragmentSource = "void main(){}";
    shaderHandle = renderer->enrollShader(sources);

    unsigned char px[4] = { 255, 0, 255, 255 };
    textureHandle = renderer->enrollTexture(px, 1, 1, 4);
    enrolled = true;
  }

  void Draw() override
  {
    // Should not be called if AppendCommands returns true.
    ++drawCount;
  }

  bool AppendCommands(Renderer* renderer) override
  {
    ++appendCallCount;
    if (!enrolled || !renderer || !isVisible()) {
      return true;
    }
    renderer->pushSetShader(shaderHandle);
    renderer->pushSetMesh(meshHandle);
    renderer->pushSetTexture(textureHandle, 0);
    float identity[16] = {
      1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
    renderer->pushUniformMat4("uMVP", identity);
    renderer->pushUniformInt("ourTexture", 0);
    renderer->pushDrawIndexed(6, 0);
    return true;
  }
};

// Immediate-only drawable (returns false from AppendCommands default)
class ImmediateStubDrawable : public DrawableBase
{
public:
  int drawCount = 0;
  void Draw() override { ++drawCount; }
};

// Records AppendCommands order for layer-bucket tests.
class OrderProbeDrawable : public DrawableBase
{
public:
  int id = 0;
  static std::vector<int> appendOrder;

  void Draw() override {}

  bool AppendCommands(Renderer* renderer) override
  {
    (void)renderer;
    appendOrder.push_back(id);
    return true;
  }
};

std::vector<int> OrderProbeDrawable::appendOrder;

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
static void
testRendererInjectsMockBackend()
{
  std::printf("\n--- e2e: Renderer inject MockBackend ---\n");
  E2ENullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();

  Renderer renderer(&window, &env, &camera, &mock, false);
  e2eTrue(renderer.getBackend() == &mock, "getBackend is injected mock");
  e2eTrue(!renderer.ownsBackend(), "does not own injected backend");

  MeshHandle handle = renderer.enrollMesh(nullptr, 64, nullptr, 0);
  e2eEqSize(mock.getCreateCount(), 1u, "enrollMesh hits mock CreateMesh");
  e2eEqSize(
    mock.getCreate(0).slot, handle.slot, "create record matches handle slot");
  e2eEqSize(mock.getCreate(0).generation,
            handle.generation,
            "create record matches handle generation");
}

static void
testRenderSceneLayerOrder()
{
  std::printf("\n--- e2e: Scene World/UI/Debug layer order ---\n");
  E2ENullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  Scene scene(&window, &camera);

  OrderProbeDrawable worldA;
  worldA.id = 1;
  OrderProbeDrawable debugA;
  debugA.id = 3;
  OrderProbeDrawable uiA;
  uiA.id = 2;
  OrderProbeDrawable worldB;
  worldB.id = 10;

  // Intentionally add out of visual order; layer buckets must restore World →
  // UI → Debug, preserving within-layer insertion order.
  scene.AddDrawable(&debugA, RenderLayerId::Debug);
  scene.AddDrawable(&uiA, RenderLayerId::UI);
  scene.AddDrawable(&worldB, RenderLayerId::World);
  scene.AddDrawable(&worldA, RenderLayerId::World);

  e2eEqSize(scene.drawableCount(), 4u, "four drawables across layers");
  e2eEqSize(
    scene.drawablesIn(RenderLayerId::World).size(), 2u, "two World drawables");
  e2eEqSize(scene.drawablesIn(RenderLayerId::UI).size(), 1u, "one UI drawable");
  e2eEqSize(
    scene.drawablesIn(RenderLayerId::Debug).size(), 1u, "one Debug drawable");

  OrderProbeDrawable::appendOrder.clear();
  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();

  e2eEqSize(OrderProbeDrawable::appendOrder.size(), 4u, "all layers visited");
  e2eEqInt(OrderProbeDrawable::appendOrder[0], 10, "World first (insertion)");
  e2eEqInt(OrderProbeDrawable::appendOrder[1], 1, "World second");
  e2eEqInt(OrderProbeDrawable::appendOrder[2], 2, "UI after World");
  e2eEqInt(OrderProbeDrawable::appendOrder[3], 3, "Debug last");

  renderer.ensureBuiltinStyles();
  e2eTrue(renderer.builtinStylesReady(), "builtin styles enroll");
  e2eTrue(renderer.getStyle(RenderStyleId::Canvas) != nullptr, "Canvas style");
  e2eTrue(renderer.getStyle(RenderStyleId::UiText) != nullptr, "UiText style");
  e2eTrue(renderer.getStyle(RenderStyleId::Console) != nullptr,
          "Console style");
  e2eTrue(renderer.getStyle(RenderStyleId::Shape) != nullptr, "Shape style");
  e2eTrue(renderer.getStyle(RenderStyleId::Sprite) != nullptr, "Sprite style");
}

static void
testRenderSceneTokenDrawable()
{
  std::printf("\n--- e2e: RenderScene + TokenQuadDrawable ---\n");
  E2ENullRenderWindow window(800, 600);
  EnvVars env;
  env.setVar("WinX", 800);
  env.setVar("WinY", 600);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();

  Renderer renderer(&window, &env, &camera, &mock, false);
  Scene scene(&window, &camera);

  TokenQuadDrawable quad;
  quad.enroll(&renderer);
  scene.AddDrawable(&quad);

  ImmediateStubDrawable stub;
  scene.AddDrawable(&stub);

  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();

  e2eEqInt(mock.getBeginFrameCount(), 1, "BeginFrame once");
  e2eEqInt(mock.getEndFrameCount(), 1, "EndFrame once");
  // RenderScene submits once (non-empty); EndFrame may submit empty.
  e2eTrue(mock.getSubmitCount() >= 1, "at least one submit");
  e2eTrue(mock.getLastNonEmptySubmittedCount() > 0,
          "non-empty token frame recorded");

  // Frame setup prefix
  const CommandType prefix[] = {
    CommandType::SetViewport,
    CommandType::SetPipelineState,
    CommandType::ClearScreen,
  };
  e2eTrue(mock.nonEmptyStartsWith(prefix, 3), "viewport/pipeline/clear prefix");

  e2eEqSize(mock.countNonEmptyOfType(CommandType::DrawIndexed),
            1u,
            "one DrawIndexed from token drawable");
  e2eEqSize(
    mock.countNonEmptyOfType(CommandType::SetShader), 1u, "one SetShader");
  e2eEqSize(mock.countNonEmptyOfType(CommandType::SetMesh), 1u, "one SetMesh");
  e2eEqSize(
    mock.countNonEmptyOfType(CommandType::SetTexture), 1u, "one SetTexture");

  // Viewport matches null window
  e2eEqInt(
    mock.getLastNonEmptySubmitted(0).viewport.width, 800, "viewport width 800");
  e2eEqInt(mock.getLastNonEmptySubmitted(0).viewport.height,
           600,
           "viewport height 600");

  // Hybrid: immediate stub still Draw()'d after token submit
  e2eEqInt(stub.drawCount, 1, "immediate stub Draw called once");
  e2eEqInt(quad.appendCallCount, 1, "token drawable AppendCommands once");
  e2eEqInt(quad.drawCount, 0, "token drawable Draw not called");

  // Enroll creates: mesh + shader + texture
  e2eTrue(mock.getCreateCount() >= 3u, "at least 3 create records from enroll");
}

static void
testRenderProofQuadOnMock()
{
  std::printf("\n--- e2e: RenderProofQuad via MockBackend ---\n");
  E2ENullRenderWindow window(640, 480);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  renderer.BeginFrame();
  renderer.RenderProofQuad();
  renderer.EndFrame();

  e2eTrue(mock.getLastNonEmptySubmittedCount() >= 8u,
          "proof quad emits several tokens");
  e2eEqSize(mock.countNonEmptyOfType(CommandType::DrawIndexed),
            1u,
            "proof DrawIndexed");
  e2eEqSize(mock.countNonEmptyOfType(CommandType::ClearScreen),
            1u,
            "proof ClearScreen");
  e2eTrue(mock.getCreateCount() >= 3u, "proof enrolls mesh/shader/texture");
}

// Production composition model without OpenGL: heap IBackend + takeOwnership.
// Mirrors the backend ownership transfer completed by Illumo::initialize().
static void
testRendererOwnsInjectedBackend()
{
  std::printf(
    "\n--- e2e: Renderer owns injected IBackend (composition style) ---\n");
  E2ENullRenderWindow window(640, 480);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend* mock = new MockBackend();
  mock->Initialize();

  Renderer* renderer = new Renderer(&window, &env, &camera, mock, true);
  e2eTrue(renderer->getBackend() == mock, "getBackend is owned mock");
  e2eTrue(renderer->ownsBackend(), "ownsBackend true for composition inject");

  MeshHandle handle = renderer->enrollMesh(nullptr, 32, nullptr, 0);
  e2eTrue(handle.isValid(), "owned renderer returns typed mesh handle");
  e2eEqSize(mock->getCreateCount(), 1u, "enroll hits owned mock CreateMesh");

  renderer->BeginFrame();
  renderer->pushClearScreen(0.0f, 0.0f, 0.0f, 1.0f);
  renderer->SubmitOnly();
  e2eEqSize(mock->countSubmittedOfType(CommandType::ClearScreen),
            1u,
            "submit through owned backend");

  // Renderer dtor must Shutdown + delete the owned backend (no OpenGL).
  delete renderer;
}

// Structural gate: backend-neutral Renderer.h must not include concrete GL
// types. Path is derived from this test TU so CTest isolation working dirs
// still work.
static void
testRendererHeaderIsBackendNeutral()
{
  std::printf("\n--- e2e: Renderer.h backend-neutral include surface ---\n");
  const std::filesystem::path thisFile(__FILE__);
  const std::filesystem::path rendererHeader =
    thisFile.parent_path().parent_path() / "Include" / "Illumo" / "Rendering" /
    "Renderer.h";

  std::ifstream input(rendererHeader);
  e2eTrue(input.is_open(), "located shipped Renderer.h via test source path");
  if (!input.is_open()) {
    std::printf("  missing: %s\n", rendererHeader.string().c_str());
    return;
  }

  std::string contents;
  contents.assign(std::istreambuf_iterator<char>(input),
                  std::istreambuf_iterator<char>());
  std::printf("  read: %s\n", rendererHeader.string().c_str());

  e2eTrue(contents.find("OpenGL/GLBackend") == std::string::npos,
          "Renderer.h does not include OpenGL/GLBackend");
  e2eTrue(contents.find("OpenGL/GLShaderProgram") == std::string::npos,
          "Renderer.h does not include OpenGL/GLShaderProgram");
  e2eTrue(contents.find("new GLBackend") == std::string::npos,
          "Renderer.h does not construct GLBackend");
  e2eTrue(contents.find("IBackend") != std::string::npos,
          "Renderer.h still depends on IBackend");
}

static void
testRenderSceneLayerPassPipeline()
{
  std::printf(
    "\n--- e2e: Layer-defined render passes with post-processing ---\n");
  E2ENullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  Scene scene(&window, &camera);

  // Configure World layer to have 2 passes:
  // 1. Geometry pass to offscreen MRT target (color + velocity)
  // 2. Motion blur post-process pass to screen
  RenderPassDesc geomPass;
  geomPass.name = "GeometryAndVelocity";
  geomPass.type = PassType::Draw;
  geomPass.useScreenTarget = false;
  geomPass.pooledTargetName = "WorldColorVelocity";
  geomPass.targetDesc.name = "WorldColorVelocity";
  geomPass.targetDesc.windowRelative = true;

  FramebufferAttachmentDesc color0;
  color0.format = TextureFormat::RGBA8;
  geomPass.targetDesc.colorAttachments.push_back(color0);

  FramebufferAttachmentDesc color1;
  color1.format = TextureFormat::RG16F;
  geomPass.targetDesc.colorAttachments.push_back(color1);

  geomPass.targetDesc.depthStencilFormat = TextureFormat::Depth24;
  geomPass.clear.clearColor = true;
  geomPass.clear.clearColorValue = { 0.2f, 0.3f, 0.4f, 1.0f };
  geomPass.clear.clearDepth = true;

  RenderPassDesc postPass;
  postPass.name = "MotionBlurResolve";
  postPass.type = PassType::PostProcess;
  postPass.useScreenTarget = true;
  postPass.styleHandle =
    renderer.getBuiltinStyleHandle(RenderStyleId::MotionBlur);

  PassUniformFloat blurAmount;
  blurAmount.name = "uMotionBlurAmount";
  blurAmount.value = 0.5f;
  postPass.uniformFloats.push_back(blurAmount);

  std::vector<RenderPassDesc> worldPasses;
  worldPasses.push_back(geomPass);
  worldPasses.push_back(postPass);
  scene.SetLayerPasses(RenderLayerId::World, worldPasses);

  e2eTrue(scene.hasCustomPasses(RenderLayerId::World),
          "World layer has custom passes");
  e2eTrue(!scene.hasCustomPasses(RenderLayerId::UI),
          "UI layer has no custom passes");

  TokenQuadDrawable worldQuad;
  worldQuad.enroll(&renderer);
  TokenQuadDrawable uiQuad;
  uiQuad.enroll(&renderer);

  scene.AddDrawable(&worldQuad, RenderLayerId::World);
  scene.AddDrawable(&uiQuad, RenderLayerId::UI);

  renderer.RenderScene(&scene, &camera);

  // Verification
  e2eTrue(worldQuad.appendCallCount == 1,
          "world quad received appendCommands in geom pass");
  e2eTrue(uiQuad.appendCallCount == 1,
          "UI quad received appendCommands in default UI pass");

  // Verify offscreen target was created in target pool
  PooledRenderTarget pooledTarget =
    renderer.getRenderTarget("WorldColorVelocity");
  e2eTrue(pooledTarget.isValid(), "pooled render target is valid");
  e2eEqInt(pooledTarget.width, 1280, "target width matches window width");
  e2eEqInt(pooledTarget.height, 720, "target height matches window height");
  e2eEqSize(pooledTarget.attachments.colorTextures.size(),
            2u,
            "target has 2 color attachments");

  // Inspect submitted commands
  e2eTrue(mock.getLastSubmittedCount() > 0, "commands submitted");
  e2eTrue(mock.countSubmittedOfType(CommandType::SetFramebuffer) >= 2,
          "at least two SetFramebuffer tokens submitted");
}

static void
testRenderTargetPoolResizing()
{
  std::printf("\n--- e2e: RenderTargetPool automatic resizing ---\n");
  MockBackend mock;
  mock.Initialize();
  RenderTargetPool pool(&mock);

  PooledRenderTargetDesc desc;
  desc.name = "TestResizeTarget";
  desc.windowRelative = true;
  desc.scale = 1.0f;
  FramebufferAttachmentDesc color0;
  color0.format = TextureFormat::RGBA8;
  desc.colorAttachments.push_back(color0);

  // Initial acquire at 1280x720
  PooledRenderTarget target1 = pool.acquire(desc, 1280, 720);
  e2eTrue(target1.isValid(), "target1 is valid");
  e2eEqInt(target1.width, 1280, "target1 width 1280");
  e2eEqInt(target1.height, 720, "target1 height 720");

  // Re-acquire at same size: reuses existing
  PooledRenderTarget targetReused = pool.acquire(desc, 1280, 720);
  e2eTrue(targetReused.fboHandle == target1.fboHandle,
          "target reused when size unchanged");

  // Resize to 1920x1080
  PooledRenderTarget target2 = pool.acquire(desc, 1920, 1080);
  e2eTrue(target2.isValid(), "target2 is valid after resize");
  e2eEqInt(target2.width, 1920, "target2 width resized to 1920");
  e2eEqInt(target2.height, 1080, "target2 height resized to 1080");
  e2eTrue(target2.fboHandle != target1.fboHandle,
          "old target handle was replaced");
  e2eTrue(!mock.IsFramebufferValid(target1.fboHandle), "old FBO was destroyed");
  e2eTrue(mock.IsFramebufferValid(target2.fboHandle), "new FBO is valid");

  pool.releaseAll();
  e2eTrue(!mock.IsFramebufferValid(target2.fboHandle),
          "releaseAll destroys active FBO");
}

static int
runRendererE2ECase(void (*testFunction)())
{
  g_e2e_failures = 0;
  testFunction();
  return g_e2e_failures;
}

void
registerRendererE2ETests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Renderer.InjectsMockBackend", []() {
    return runRendererE2ECase(testRendererInjectsMockBackend);
  });
  registry.add("Illumo.Renderer.OwnsInjectedBackend", []() {
    return runRendererE2ECase(testRendererOwnsInjectedBackend);
  });
  registry.add("Illumo.Renderer.HeaderBackendNeutral", []() {
    return runRendererE2ECase(testRendererHeaderIsBackendNeutral);
  });
  registry.add("Illumo.Renderer.SceneLayerOrder",
               []() { return runRendererE2ECase(testRenderSceneLayerOrder); });
  registry.add("Illumo.Renderer.SceneTokenDrawable", []() {
    return runRendererE2ECase(testRenderSceneTokenDrawable);
  });
  registry.add("Illumo.Renderer.ProofQuad",
               []() { return runRendererE2ECase(testRenderProofQuadOnMock); });
  registry.add("Illumo.Renderer.LayerPassPipeline", []() {
    return runRendererE2ECase(testRenderSceneLayerPassPipeline);
  });
  registry.add("Illumo.Renderer.RenderTargetPoolResizing", []() {
    return runRendererE2ECase(testRenderTargetPoolResizing);
  });
}
