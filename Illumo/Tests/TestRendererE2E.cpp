// End-to-end drawable → Renderer → MockBackend token tests (no OpenGL).
// Linked into the current IllumoTests target with TestMockBackend.cpp.

#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/Primitives/SkyboxVisual.h>
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
  int getRefreshRate() const override { return 60; }
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
    CommandType::SetFramebuffer,
    CommandType::SetViewport,
    CommandType::SetPipelineState,
    CommandType::ClearScreen,
  };
  e2eTrue(mock.nonEmptyStartsWith(prefix, 4),
          "target/viewport/pipeline/clear prefix");
  e2eTrue(!mock.getLastNonEmptySubmitted(0).bindFramebuffer.handle.isValid(),
          "frame setup explicitly selects screen target");

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
    mock.getLastNonEmptySubmitted(1).viewport.width, 800, "viewport width 800");
  e2eEqInt(mock.getLastNonEmptySubmitted(1).viewport.height,
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

  PassInputTargetBinding colorBinding;
  colorBinding.targetName = "WorldColorVelocity";
  colorBinding.attachmentIndex = 0;
  colorBinding.slot = 0;
  colorBinding.samplerUniformName = "uColorTexture";
  postPass.inputTargetTextures.push_back(colorBinding);

  PassInputTargetBinding velBinding;
  velBinding.targetName = "WorldColorVelocity";
  velBinding.attachmentIndex = 1;
  velBinding.slot = 1;
  velBinding.samplerUniformName = "uVelocityTexture";
  postPass.inputTargetTextures.push_back(velBinding);

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
  e2eTrue(mock.countSubmittedOfType(CommandType::SetTexture) >= 2,
          "color and velocity textures bound for post-process pass");
}

static void
testOrdinaryLayerTargetRestore()
{
  E2ENullRenderWindow window(1280, 720);
  Camera camera;
  MockBackend backend;
  backend.Initialize();
  Renderer renderer(&window, nullptr, &camera, &backend, false);
  Scene scene(&window, &camera);
  RenderPassDesc pass;
  pass.useScreenTarget = false;
  pass.pooledTargetName = "small-world";
  pass.targetDesc.name = pass.pooledTargetName;
  pass.targetDesc.windowRelative = false;
  pass.targetDesc.fixedWidth = 32;
  pass.targetDesc.fixedHeight = 24;
  pass.targetDesc.colorAttachments.push_back(FramebufferAttachmentDesc{});
  pass.customViewport = true;
  pass.viewportX = 2;
  pass.viewportY = 3;
  pass.viewportWidth = 20;
  pass.viewportHeight = 15;
  scene.SetLayerPasses(RenderLayerId::World, { pass });
  TokenQuadDrawable world;
  TokenQuadDrawable ui;
  TokenQuadDrawable debug;
  world.enroll(&renderer);
  ui.enroll(&renderer);
  debug.enroll(&renderer);
  scene.AddDrawable(&world, RenderLayerId::World);
  scene.AddDrawable(&ui, RenderLayerId::UI);
  scene.AddDrawable(&debug, RenderLayerId::Debug);
  renderer.RenderScene(&scene, &camera);
  bool targetKnown = false;
  FramebufferHandle target;
  std::array<int, 4> viewport{};
  int draws = 0;
  bool sawClear = false;
  for (size_t i = 0; i < backend.getLastSubmittedCount(); ++i) {
    const RenderCommand& command = backend.getLastSubmitted(i);
    if (command.commandType == CommandType::SetFramebuffer) {
      target = command.bindFramebuffer.handle;
      targetKnown = true;
    } else if (command.commandType == CommandType::SetViewport) {
      viewport = { command.viewport.x,
                   command.viewport.y,
                   command.viewport.width,
                   command.viewport.height };
    } else if (command.commandType == CommandType::ClearScreen && !sawClear) {
      e2eTrue(targetKnown && !target.isValid() &&
                viewport == std::array<int, 4>{ 0, 0, 1280, 720 },
              "initial clear explicitly targets the full screen");
      sawClear = true;
    } else if (command.commandType == CommandType::DrawIndexed) {
      e2eTrue(draws == 0 ? target.isValid() &&
                             viewport == std::array<int, 4>{ 2, 3, 20, 15 }
                         : targetKnown && !target.isValid() &&
                             viewport == std::array<int, 4>{ 0, 0, 1280, 720 },
              "each layer draws to its intended framebuffer and viewport");
      ++draws;
    }
  }
  e2eTrue(sawClear && draws == 3, "clear and all three layers inspected");
}

static void
testPassClearMasks()
{
  E2ENullRenderWindow window(128, 128);
  Camera camera;
  MockBackend backend;
  backend.Initialize();
  Renderer renderer(&window, nullptr, &camera, &backend, false);
  for (int mask = 0; mask < 4; ++mask) {
    Scene scene(&window, &camera);
    RenderPassDesc pass;
    pass.clear.clearColor = (mask & 1) != 0;
    pass.clear.clearDepth = (mask & 2) != 0;
    pass.clear.clearDepthValue = 0.25f;
    scene.SetLayerPasses(RenderLayerId::World, { pass });
    renderer.RenderScene(&scene, &camera);
    e2eEqSize(backend.countSubmittedOfType(CommandType::ClearScreen),
              1,
              "only initial frame setup clears both buffers");
    e2eEqSize(backend.countSubmittedOfType(CommandType::ClearColorBuffer),
              (mask & 1) != 0 ? 1 : 0,
              "pass color clear matches mask");
    e2eEqSize(backend.countSubmittedOfType(CommandType::ClearDepthBuffer),
              (mask & 2) != 0 ? 1 : 0,
              "pass depth clear matches mask");
    for (size_t i = 0; i < backend.getLastSubmittedCount(); ++i) {
      const RenderCommand& command = backend.getLastSubmitted(i);
      if (command.commandType == CommandType::ClearDepthBuffer) {
        e2eTrue(command.clearDepthValue == 0.25f,
                "pass preserves requested depth clear value");
      }
    }
  }
}

static void
testRenderSceneMeshVisualRestoresPassTarget()
{
  std::printf("\n--- e2e: MeshVisual restores active pass target after shadow "
              "pass ---\n");
  E2ENullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);

  MockBackend mock;
  mock.Initialize();

  Renderer renderer(&window, &env, &camera, &mock, false);
  renderer.ensureBuiltinStyles();

  Scene scene(&window, &camera);

  RenderPassDesc geomPass;
  geomPass.name = "WorldGeomPass";
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
  geomPass.clear.clearDepth = true;

  RenderPassDesc postPass;
  postPass.name = "MotionBlurResolve";
  postPass.type = PassType::PostProcess;
  postPass.useScreenTarget = true;
  postPass.styleHandle =
    renderer.getBuiltinStyleHandle(RenderStyleId::MotionBlur);

  PassInputTargetBinding colorBinding;
  colorBinding.targetName = "WorldColorVelocity";
  colorBinding.attachmentIndex = 0;
  colorBinding.slot = 0;
  colorBinding.samplerUniformName = "uColorTexture";
  postPass.inputTargetTextures.push_back(colorBinding);

  PassInputTargetBinding velBinding;
  velBinding.targetName = "WorldColorVelocity";
  velBinding.attachmentIndex = 1;
  velBinding.slot = 1;
  velBinding.samplerUniformName = "uVelocityTexture";
  postPass.inputTargetTextures.push_back(velBinding);

  std::vector<RenderPassDesc> worldPasses;
  worldPasses.push_back(geomPass);
  worldPasses.push_back(postPass);
  scene.SetLayerPasses(RenderLayerId::World, worldPasses);

  MeshVisual visual;
  visual.prepare(&renderer);
  visual.addSolidCube(
    glm::vec3(0.0f), glm::vec3(0.5f), ColorRgba{ 200, 200, 200, 255 });
  visual.addLine(
    glm::vec3(0.0f), glm::vec3(1.0f), ColorRgba{ 100, 100, 100, 255 });
  visual.setLightingEnabled(true);
  visual.setShadowsEnabled(true);
  visual.setMotionBlurEnabled(true);

  scene.AddDrawable(&visual, RenderLayerId::World);

  renderer.RenderScene(&scene, &camera);

  PooledRenderTarget pooledTarget =
    renderer.getRenderTarget("WorldColorVelocity");
  e2eTrue(pooledTarget.isValid(), "pooled render target is valid");

  // Verify that after shadow pass, SetFramebuffer was called with
  // pooledTarget.fboHandle, NOT 0!
  // Sequence of SetFramebuffer calls:
  // 1. Initial pass target: WorldColorVelocity FBO
  // 2. Shadow pass: shadowFboHandle
  // 3. Restored pass target: WorldColorVelocity FBO (not screen / 0!)
  // 4. Post-process pass target: screen / 0
  std::vector<FramebufferHandle> boundFbos;
  bool sawLinePrevMvp = false;
  bool sawLineMotionBlur = false;
  for (size_t i = 0; i < mock.getLastSubmittedCount(); ++i) {
    const RenderCommand& cmd = mock.getLastSubmitted(i);
    if (cmd.commandType == CommandType::SetFramebuffer) {
      boundFbos.push_back(cmd.bindFramebuffer.handle);
    }
    if (cmd.commandType == CommandType::SetUniformMat4 &&
        std::strcmp(cmd.uniformMat4.name, "uPrevMVP") == 0) {
      sawLinePrevMvp = true;
    }
    if (cmd.commandType == CommandType::SetUniformInt &&
        std::strcmp(cmd.uniformInt.name, "uMotionBlurEnabled") == 0 &&
        cmd.uniformInt.value == 1) {
      sawLineMotionBlur = true;
    }
  }

  e2eTrue(sawLinePrevMvp, "lines emit uPrevMVP uniform");
  e2eTrue(sawLineMotionBlur, "lines emit uMotionBlurEnabled uniform");

  e2eTrue(boundFbos.size() >= 5, "at least 5 framebuffer binds submitted");
  if (boundFbos.size() >= 5) {
    e2eTrue(!boundFbos[0].isValid(), "frame clear binds screen first");
    e2eTrue(boundFbos[1] == pooledTarget.fboHandle,
            "pass initially binds target FBO");
    e2eTrue(boundFbos[2].isValid() && boundFbos[2] != pooledTarget.fboHandle,
            "shadow pass binds shadow FBO");
    e2eTrue(boundFbos[3] == pooledTarget.fboHandle,
            "MeshVisual restored target FBO, not screen 0");
    e2eTrue(!boundFbos[4].isValid(), "post-process pass binds screen 0");
  }
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

static void
testRendererSkyboxVisual()
{
  std::printf("\n--- e2e: SkyboxVisual token emission ---\n");
  E2ENullRenderWindow window(1280, 720);
  MockBackend backend;
  backend.Initialize();
  Camera camera;
  camera.setPerspective(60.0f, 0.1f, 1000.0f);
  camera.setProjectionType(ProjectionType::Perspective);
  camera.lookAt(glm::vec3(0.0f, 0.0f, 5.0f),
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f));

  Renderer renderer(&window, nullptr, &camera, &backend, false);
  renderer.ensureBuiltinStyles();

  std::array<unsigned char, 4> px = { 100, 150, 200, 255 };
  std::array<const unsigned char*, 6> faces = {
    px.data(), px.data(), px.data(), px.data(), px.data(), px.data()
  };
  TextureHandle cubemap = renderer.enrollCubemap(faces, 1, 1, 4);
  e2eTrue(cubemap.isValid(), "Cubemap texture handle should be valid");

  SkyboxVisual skybox(cubemap);
  skybox.prepare(&renderer);

  Scene scene(&window, &camera);
  scene.AddDrawable(&skybox, RenderLayerId::World);

  renderer.RenderScene(&scene, &camera);

  bool foundTextureBind = false;
  bool foundUniformViewProj = false;
  bool foundDraw = false;

  for (size_t i = 0; i < backend.getLastSubmittedCount(); ++i) {
    const RenderCommand& cmd = backend.getLastSubmitted(i);
    if (cmd.commandType == CommandType::SetTexture &&
        cmd.bindTexture.handle == cubemap) {
      foundTextureBind = true;
    }
    if (cmd.commandType == CommandType::SetUniformMat4 &&
        std::strcmp(cmd.uniformMat4.name, "uViewProjection") == 0) {
      foundUniformViewProj = true;
    }
    if (cmd.commandType == CommandType::DrawIndexed &&
        cmd.drawIndexed.elementCount == 36) {
      foundDraw = true;
    }
  }

  e2eTrue(foundTextureBind, "Skybox should bind cubemap texture");
  e2eTrue(foundUniformViewProj, "Skybox should push uViewProjection matrix");
  e2eTrue(foundDraw, "Skybox should issue DrawIndexed with 36 indices");
}

static void
testAssetManagerCubemapFromCross()
{
  std::printf("\n--- e2e: AssetManager cubemap from cross ---\n");
  E2ENullRenderWindow window(1280, 720);
  MockBackend backend;
  backend.Initialize();
  Camera camera;
  Renderer renderer(&window, nullptr, &camera, &backend, false);
  AssetManager assets(&renderer, false);

  std::string skyboxPath = "Assets/Skybox/skybox-daylight.png";
  if (!std::filesystem::exists(skyboxPath)) {
    skyboxPath = (std::filesystem::path(__FILE__).parent_path().parent_path() /
                  "Assets" / "Skybox" / "skybox-daylight.png")
                   .string();
  }

  TextureHandle cubemap =
    assets.acquireCubemapFromCross(skyboxPath, AssetLoadMode::Synchronous);
  e2eTrue(cubemap.isValid(),
          "Should load cubemap cross from skybox-daylight.png");

  TextureInfo info = assets.getTextureInfo(cubemap);
  e2eEqInt(info.width, 512, "Face width should be 512");
  e2eEqInt(info.height, 512, "Face height should be 512");

  TextureHandle cached =
    assets.acquireCubemapFromCross(skyboxPath, AssetLoadMode::Synchronous);
  e2eTrue(cached == cubemap, "Repeated acquire should return cached handle");
}

static int
writeCubemapChannelFixture(const std::string& path, int channels, bool cross)
{
  // Uncompressed TGA supports native gray, gray-alpha, RGB, and RGBA.
  const int width = cross ? 4 : 1;
  const int height = cross ? 3 : 1;
  unsigned char header[18]{};
  header[2] = channels <= 2 ? 3 : 2;
  header[12] = static_cast<unsigned char>(width);
  header[14] = static_cast<unsigned char>(height);
  header[16] = static_cast<unsigned char>(channels * 8);
  header[17] = static_cast<unsigned char>(
    0x20 | ((channels == 2 || channels == 4) ? 8 : 0));
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(header), sizeof(header));
  for (int pixel = 0; pixel < width * height; ++pixel) {
    const unsigned char gray[] = { 40, 90 };
    const unsigned char color[] = { 60, 50, 40, 90 };
    file.write(reinterpret_cast<const char*>(channels <= 2 ? gray : color),
               channels);
  }
  file.close();
  return file ? 1 : 0;
}

class CubemapInspectBackend : public MockBackend
{
public:
  bool ReplaceCubemap(TextureHandle handle,
                      const std::array<const unsigned char*, 6>& faces,
                      int width,
                      int height,
                      int channels) override
  {
    if (!MockBackend::ReplaceCubemap(handle, faces, width, height, channels)) {
      return false;
    }
    receivedChannels = channels;
    for (size_t i = 0; i < faces.size(); ++i) {
      pixels[i].assign(faces[i],
                       faces[i] + static_cast<size_t>(width) *
                                    static_cast<size_t>(height) *
                                    static_cast<size_t>(channels));
    }
    return true;
  }
  std::array<std::vector<unsigned char>, 6> pixels;
  int receivedChannels = 0;
  TextureHandle CreateCubemap(const std::array<const unsigned char*, 6>& faces,
                              int width,
                              int height,
                              int channels) override
  {
    receivedChannels = channels;
    for (std::size_t face = 0; face < faces.size(); ++face) {
      pixels[face].assign(faces[face],
                          faces[face] + static_cast<std::size_t>(width) *
                                          static_cast<std::size_t>(height) *
                                          static_cast<std::size_t>(channels));
    }
    return MockBackend::CreateCubemap(faces, width, height, channels);
  }
};

static void
testCubemapDecodedChannels()
{
  E2ENullRenderWindow window(128, 128);
  CubemapInspectBackend backend;
  backend.Initialize();
  Camera camera;
  Renderer renderer(&window, nullptr, &camera, &backend, false);
  AssetManager assets(&renderer, false);
  const int channels[] = { 4, 3, 2, 1, 3, 4 };
  std::array<std::string, 6> paths;
  for (std::size_t face = 0; face < paths.size(); ++face) {
    paths[face] = "cube-channel-" + std::to_string(face) + ".tga";
    e2eTrue(writeCubemapChannelFixture(paths[face], channels[face], false) != 0,
            "write native-channel face fixture");
  }
  const TextureHandle cube = assets.acquireCubemap(paths);
  e2eTrue(cube.isValid() && backend.receivedChannels == 4,
          "mixed native channels enroll as declared RGBA");
  for (std::size_t face = 0; face < paths.size(); ++face) {
    const std::vector<unsigned char> expected = {
      40,
      static_cast<unsigned char>(channels[face] <= 2 ? 40 : 50),
      static_cast<unsigned char>(channels[face] <= 2 ? 40 : 60),
      static_cast<unsigned char>(
        channels[face] == 2 || channels[face] == 4 ? 90 : 255)
    };
    e2eTrue(backend.pixels[face] == expected,
            "face bytes match RGBA conversion");
    std::filesystem::remove(paths[face]);
  }
  for (int channelCount = 1; channelCount <= 4; ++channelCount) {
    const std::string path =
      "cube-cross-" + std::to_string(channelCount) + ".tga";
    e2eTrue(writeCubemapChannelFixture(path, channelCount, true) != 0,
            "write cross fixture");
    const TextureHandle cross = assets.acquireCubemapFromCross(path);
    e2eTrue(cross.isValid() && assets.getTextureInfo(cross).channels == 4,
            "cross reports actual RGBA output channels");
    const std::vector<unsigned char> expected = {
      40,
      static_cast<unsigned char>(channelCount <= 2 ? 40 : 50),
      static_cast<unsigned char>(channelCount <= 2 ? 40 : 60),
      static_cast<unsigned char>(channelCount == 2 || channelCount == 4 ? 90
                                                                        : 255)
    };
    for (const std::vector<unsigned char>& face : backend.pixels) {
      e2eTrue(face == expected, "cross extraction uses decoded RGBA stride");
    }
    std::filesystem::remove(path);
  }
}

static int
runRendererE2ECase(void (*testFunction)())
{
  g_e2e_failures = 0;
  testFunction();
  return g_e2e_failures;
}

static void
testCubemapReload()
{
  E2ENullRenderWindow window(128, 128);
  CubemapInspectBackend backend;
  backend.Initialize();
  Camera camera;
  Renderer renderer(&window, nullptr, &camera, &backend, false);
  AssetManager assets(&renderer, false);
  assets.setHotReloadEnabled(false);
  std::array<std::string, 6> paths;
  for (size_t i = 0; i < paths.size(); ++i) {
    paths[i] = "cube-reload-" + std::to_string(i) + ".tga";
    e2eTrue(writeCubemapChannelFixture(paths[i], 4, false) != 0,
            "write reload face");
  }
  const TextureHandle cube = assets.acquireCubemap(paths);
  e2eTrue(cube.isValid() && assets.getState(cube).revision == 1,
          "initial cubemap has published revision");
  for (size_t i = 0; i < paths.size(); ++i) {
    writeCubemapChannelFixture(paths[i], 1, false);
    e2eTrue(assets.reload(paths[i]) == 1,
            "every canonical face path triggers reload");
    assets.completePendingForTests();
    e2eTrue(assets.getState(cube).state == AssetState::Ready &&
              assets.getState(cube).revision == i + 2 &&
              backend.pixels[i] ==
                std::vector<unsigned char>({ 40, 40, 40, 255 }),
            "face reload retains kind and publishes new RGBA pixels");
  }
  const uint64_t revision = assets.getState(cube).revision;
  const std::array<std::vector<unsigned char>, 6> previous = backend.pixels;
  std::filesystem::remove(paths[4]);
  e2eTrue(assets.reloadAll() == 1, "reloadAll includes cubemap");
  assets.completePendingForTests();
  e2eTrue(assets.getState(cube).state == AssetState::Ready &&
            assets.getState(cube).revision == revision &&
            !assets.getState(cube).lastError.empty() &&
            backend.pixels == previous,
          "missing face retains last valid revision and GPU data");
  writeCubemapChannelFixture(paths[4], 4, true);
  assets.reload(cube);
  assets.completePendingForTests();
  e2eTrue(assets.getState(cube).revision == revision &&
            backend.pixels == previous,
          "nonsquare face rejects complete reload transaction");
  writeCubemapChannelFixture(paths[4], 4, false);
  backend.setRejectNextTextureReplacement(true);
  assets.reload(cube);
  assets.completePendingForTests();
  e2eTrue(assets.getState(cube).revision == revision &&
            backend.pixels == previous,
          "backend rejection retains prior cubemap");
  assets.reload(cube);
  assets.completePendingForTests();
  e2eTrue(assets.getState(cube).revision == revision + 1 &&
            assets.getState(cube).lastError.empty(),
          "valid retry recovers");
  std::filesystem::last_write_time(paths[5],
                                   std::filesystem::last_write_time(paths[5]) +
                                     std::chrono::seconds(2));
  assets.setHotReloadEnabled(true);
  assets.pump();
  e2eTrue(assets.getState(cube).reloadPending,
          "timestamp polling observes the sixth face dependency");
  assets.setHotReloadEnabled(false);
  assets.completePendingForTests();
  e2eTrue(assets.getState(cube).revision == revision + 2,
          "polled reload publishes a complete cubemap");
  assets.reload(cube);
  assets.releaseTexture(cube);
  assets.completePendingForTests();
  e2eTrue(!backend.IsTextureValid(cube),
          "released queued cubemap is not resurrected");
  for (const std::string& path : paths) {
    std::filesystem::remove(path);
  }
  const std::string crossPath = "cube-reload-cross.tga";
  writeCubemapChannelFixture(crossPath, 4, true);
  const TextureHandle cross = assets.acquireCubemapFromCross(crossPath);
  e2eTrue(assets.reload(std::string{}) == 0 &&
            !assets.getState(cross).reloadPending,
          "empty reload path does not match unused source slots");
  writeCubemapChannelFixture(crossPath, 1, true);
  e2eTrue(assets.reload(crossPath) == 1, "cross path triggers cubemap reload");
  assets.completePendingForTests();
  e2eTrue(assets.getState(cross).revision == 2 &&
            backend.pixels[5] ==
              std::vector<unsigned char>({ 40, 40, 40, 255 }),
          "cross reload keeps cubemap faces and RGBA stride");
  std::filesystem::remove(crossPath);
}

static void
testCubemapReplacement()
{
  E2ENullRenderWindow window(128, 128);
  MockBackend backend;
  backend.Initialize();
  Camera camera;
  Renderer renderer(&window, nullptr, &camera, &backend, false);
  const unsigned char pixel[4] = { 1, 2, 3, 255 };
  std::array<const unsigned char*, 6> faces;
  faces.fill(pixel);
  const TextureHandle cube = renderer.enrollCubemap(faces, 1, 1, 4);
  const TextureHandle flat = renderer.enrollTexture(pixel, 1, 1);
  e2eTrue(!renderer.replaceTexture(cube, pixel, 1, 1, 4, TextureOptions{}),
          "2D replacement rejects cubemap kind");
  e2eTrue(!renderer.replaceCubemap(flat, faces, 1, 1, 4),
          "cubemap replacement rejects 2D kind");
  e2eTrue(renderer.replaceCubemap(cube, faces, 1, 1, 4),
          "cubemap replacement retains valid handle");
  faces[3] = nullptr;
  e2eTrue(!renderer.replaceCubemap(cube, faces, 1, 1, 4),
          "missing face rejects replacement");
  faces[3] = pixel;
  e2eTrue(!renderer.replaceCubemap(cube, faces, 2, 1, 4),
          "nonsquare replacement rejected");
  e2eTrue(!renderer.replaceCubemap(cube, faces, 1, 1, 2),
          "unsupported raw channels rejected");
  renderer.destroyTexture(cube);
  const TextureHandle reused = renderer.enrollTexture(pixel, 1, 1);
  e2eTrue(!renderer.replaceCubemap(cube, faces, 1, 1, 4),
          "stale generation rejected after slot reuse");
  e2eTrue(renderer.replaceTexture(reused, pixel, 1, 1, 4, TextureOptions{}),
          "slot reuse does not retain cubemap kind");
}

void
registerRendererE2ETests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Renderer.PassClearMasks",
               []() { return runRendererE2ECase(testPassClearMasks); });
  registry.add("Illumo.Renderer.OrdinaryLayerTargetRestore", []() {
    return runRendererE2ECase(testOrdinaryLayerTargetRestore);
  });
  registry.add("Illumo.AssetManager.CubemapReload",
               []() { return runRendererE2ECase(testCubemapReload); });
  registry.add("Illumo.Renderer.CubemapReplacement",
               []() { return runRendererE2ECase(testCubemapReplacement); });
  registry.add("Illumo.AssetManager.CubemapDecodedChannels",
               []() { return runRendererE2ECase(testCubemapDecodedChannels); });
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
  registry.add("Illumo.Renderer.MeshVisualRestoresPassTarget", []() {
    return runRendererE2ECase(testRenderSceneMeshVisualRestoresPassTarget);
  });
  registry.add("Illumo.Renderer.RenderTargetPoolResizing", []() {
    return runRendererE2ECase(testRenderTargetPoolResizing);
  });
  registry.add("Illumo.Renderer.SkyboxVisual",
               []() { return runRendererE2ECase(testRendererSkyboxVisual); });
  registry.add("Illumo.AssetManager.CubemapLoading", []() {
    return runRendererE2ECase(testAssetManagerCubemapFromCross);
  });
}
