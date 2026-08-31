// Phase 6: headless token/backend tests (no OpenGL, no window).
// Exit 0 on success; non-zero on failure. Run via CTest target IllumoTests.

#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/IMesh.h>
#include <Illumo/Rendering/IShaderProgram.h>
#include <Illumo/Rendering/PipelineState.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cstdio>
#include <cstring>
#include <type_traits>

static_assert(!std::is_convertible_v<TextureHandle, ShaderHandle>);
static_assert(!std::is_convertible_v<ShaderHandle, MeshHandle>);
static_assert(!std::is_convertible_v<MeshHandle, TextureHandle>);

static int g_failures = 0;

static void
expectTrue(bool cond, const char* msg)
{
  if (!cond) {
    std::printf("FAIL: %s\n", msg);
    ++g_failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

static void
expectEqSize(size_t a, size_t b, const char* msg)
{
  if (a != b) {
    std::printf("FAIL: %s (got %zu, expected %zu)\n", msg, a, b);
    ++g_failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

static void
expectEqInt(int a, int b, const char* msg)
{
  if (a != b) {
    std::printf("FAIL: %s (got %d, expected %d)\n", msg, a, b);
    ++g_failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

static void
expectCommandType(CommandType got, CommandType expected, const char* msg)
{
  if (got != expected) {
    std::printf("FAIL: %s (command type mismatch)\n", msg);
    ++g_failures;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

// Mirrors the production proof-quad / frame-setup vocabulary without
// Renderer/GL.
static void
emitProofLikeFrame(MockBackend& backend,
                   ShaderHandle shaderHandle,
                   MeshHandle meshHandle,
                   TextureHandle textureHandle)
{
  backend.ClearCommandQueue();

  RenderCommand vp;
  vp.commandType = CommandType::SetViewport;
  vp.viewport.x = 0;
  vp.viewport.y = 0;
  vp.viewport.width = 1280;
  vp.viewport.height = 720;
  backend.PushToCommandQueue(vp);

  RenderCommand pipe;
  pipe.commandType = CommandType::SetPipelineState;
  pipe.pipelineState.depthTestEnabled = false;
  pipe.pipelineState.blendEnabled = false;
  pipe.pipelineState.primitives = Primitives::Triangles;
  backend.PushToCommandQueue(pipe);

  RenderCommand clear;
  clear.commandType = CommandType::ClearScreen;
  clear.clear.r = 0.05f;
  clear.clear.g = 0.08f;
  clear.clear.b = 0.18f;
  clear.clear.a = 1.0f;
  backend.PushToCommandQueue(clear);

  RenderCommand setShader;
  setShader.commandType = CommandType::SetShader;
  setShader.bindShader.handle = shaderHandle;
  backend.PushToCommandQueue(setShader);

  RenderCommand setMesh;
  setMesh.commandType = CommandType::SetMesh;
  setMesh.bindMesh.handle = meshHandle;
  backend.PushToCommandQueue(setMesh);

  RenderCommand setTex;
  setTex.commandType = CommandType::SetTexture;
  setTex.bindTexture.handle = textureHandle;
  setTex.bindTexture.slot = 0;
  backend.PushToCommandQueue(setTex);

  RenderCommand mat;
  mat.commandType = CommandType::SetUniformMat4;
  std::memset(mat.uniformMat4.name, 0, sizeof(mat.uniformMat4.name));
  std::memcpy(mat.uniformMat4.name, "uMVP", 4);
  // identity
  for (int i = 0; i < 16; ++i) {
    mat.uniformMat4.m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
  }
  backend.PushToCommandQueue(mat);

  RenderCommand samp;
  samp.commandType = CommandType::SetUniformInt;
  std::memset(samp.uniformInt.name, 0, sizeof(samp.uniformInt.name));
  std::memcpy(samp.uniformInt.name, "ourTexture", 10);
  samp.uniformInt.value = 0;
  backend.PushToCommandQueue(samp);

  RenderCommand draw;
  draw.commandType = CommandType::DrawIndexed;
  draw.drawIndexed.elementCount = 6;
  draw.drawIndexed.firstIndex = 0;
  backend.PushToCommandQueue(draw);

  backend.SubmitCommandQueue();
}

static void
testLifecycle()
{
  std::printf("\n--- lifecycle ---\n");
  MockBackend mock;
  expectTrue(!mock.wasInitialized(), "starts uninitialized");
  mock.Initialize();
  expectTrue(mock.wasInitialized(), "Initialize sets flag");
  mock.BeginFrame();
  mock.EndFrame();
  expectEqInt(mock.getBeginFrameCount(), 1, "BeginFrame counted");
  expectEqInt(mock.getEndFrameCount(), 1, "EndFrame counted");
  mock.Shutdown();
  expectTrue(mock.wasShutdown(), "Shutdown sets flag");
}

static void
testCreateRecords()
{
  std::printf("\n--- create / enroll records ---\n");
  MockBackend mock;
  mock.Initialize();

  float verts[8] = { 0 };
  unsigned int idx[6] = { 0, 1, 2, 0, 2, 3 };
  MeshHandle meshHandle =
    mock.CreateMesh(verts, sizeof(verts), idx, sizeof(idx));
  expectTrue(meshHandle.isValid(), "CreateMesh returns a typed handle");

  MeshHandle dynamicHandle = mock.CreateMesh(
    nullptr, 4096, idx, sizeof(idx), MeshVertexLayout::Pos3Color4U8, true);
  expectTrue(dynamicHandle.isValid(),
             "CreateMesh dynamic returns a typed handle");

  ShaderPaths paths;
  paths.vertexPath = "v.glsl";
  paths.fragmentPath = "f.glsl";
  ShaderHandle shaderHandle = mock.CreateShaderProgram(paths);
  expectTrue(shaderHandle.isValid(),
             "CreateShaderProgram returns a typed handle");

  unsigned char px[4] = { 255, 0, 255, 255 };
  TextureOptions textureOptions;
  TextureHandle textureHandle = mock.CreateTexture(px, 1, 1, 4, textureOptions);
  expectTrue(textureHandle.isValid(), "CreateTexture returns a typed handle");

  expectEqSize(mock.getCreateCount(), 4u, "four create records");
  expectTrue(mock.getCreate(1).dynamic == true, "dynamic mesh flagged");
  expectTrue(mock.getCreate(1).layout == MeshVertexLayout::Pos3Color4U8,
             "UI layout recorded");
  expectEqInt(mock.getCreate(3).channels, 4, "texture channels recorded");
}

static void
testCommandSubmit()
{
  std::printf("\n--- command queue submit snapshot ---\n");
  MockBackend mock;
  mock.Initialize();

  RenderCommand a;
  a.commandType = CommandType::ClearColorBuffer;
  a.clear.r = 0.1f;
  a.clear.g = 0.1f;
  a.clear.b = 0.1f;
  a.clear.a = 1.0f;
  mock.PushToCommandQueue(a);

  RenderCommand b;
  b.commandType = CommandType::SetViewport;
  b.viewport.width = 100;
  b.viewport.height = 50;
  mock.PushToCommandQueue(b);

  expectEqSize(mock.getPendingCommandCount(), 2u, "two pending before submit");
  mock.SubmitCommandQueue();
  expectEqInt(mock.getSubmitCount(), 1, "submit counted");
  expectEqSize(mock.getLastSubmittedCount(), 2u, "snapshot has two commands");
  expectCommandType(mock.getLastSubmittedType(0),
                    CommandType::ClearColorBuffer,
                    "first is clear");
  expectCommandType(mock.getLastSubmittedType(1),
                    CommandType::SetViewport,
                    "second is viewport");
  expectEqInt(
    mock.getLastSubmitted(1).viewport.width, 100, "viewport width preserved");
  expectEqInt(
    mock.getLastSubmitted(1).viewport.height, 50, "viewport height preserved");

  mock.ClearCommandQueue();
  expectEqSize(mock.getPendingCommandCount(), 0u, "clear empties pending");
  // Last submitted snapshot remains until next submit
  expectEqSize(
    mock.getLastSubmittedCount(), 2u, "clear does not wipe last snapshot");
}

static void
testProofLikeSequence()
{
  std::printf("\n--- proof-like token sequence ---\n");
  MockBackend mock;
  mock.Initialize();

  const MeshHandle meshHandle = mock.CreateMesh(
    nullptr, 128, nullptr, 0, MeshVertexLayout::Pos3Color3Uv2, false);
  ShaderSources src;
  src.vertexSource = "void main(){}";
  src.fragmentSource = "void main(){}";
  const ShaderHandle shaderHandle = mock.CreateShaderProgram(src);
  TextureOptions textureOptions;
  const TextureHandle textureHandle =
    mock.CreateTexture(nullptr, 2, 2, 4, textureOptions);

  mock.BeginFrame();
  emitProofLikeFrame(mock, shaderHandle, meshHandle, textureHandle);
  mock.EndFrame();

  expectEqInt(mock.getSubmitCount(), 1, "one submit for proof frame");
  expectEqSize(
    mock.getLastSubmittedCount(), 9u, "nine tokens in proof-like frame");

  const CommandType expected[] = {
    CommandType::SetViewport,    CommandType::SetPipelineState,
    CommandType::ClearScreen,    CommandType::SetShader,
    CommandType::SetMesh,        CommandType::SetTexture,
    CommandType::SetUniformMat4, CommandType::SetUniformInt,
    CommandType::DrawIndexed,
  };
  expectTrue(mock.submittedStartsWith(expected, 9),
             "proof sequence order matches");
  expectEqSize(
    mock.countSubmittedOfType(CommandType::DrawIndexed), 1u, "one DrawIndexed");
  expectEqSize(
    mock.countSubmittedOfType(CommandType::ClearScreen), 1u, "one ClearScreen");

  expectTrue(mock.getLastSubmitted(3).bindShader.handle == shaderHandle,
             "SetShader preserves its typed handle");
  expectTrue(mock.getLastSubmitted(4).bindMesh.handle == meshHandle,
             "SetMesh preserves its typed handle");
  expectTrue(mock.getLastSubmitted(5).bindTexture.handle == textureHandle,
             "SetTexture preserves its typed handle");
  expectEqInt(
    static_cast<int>(mock.getLastSubmitted(8).drawIndexed.elementCount),
    6,
    "DrawIndexed elementCount 6");
}

static void
testCanvasLikeUpdateTexture()
{
  std::printf("\n--- canvas-like UpdateTexture token ---\n");
  MockBackend mock;
  mock.Initialize();
  TextureOptions textureOptions;
  TextureHandle textureHandle =
    mock.CreateTexture(nullptr, 80, 60, 3, textureOptions);

  unsigned char fakePixels[12] = { 0 };
  RenderCommand upd{};
  upd.commandType = CommandType::UpdateTexture;
  upd.updateTexture.handle = textureHandle;
  upd.updateTexture.x = 0;
  upd.updateTexture.y = 0;
  upd.updateTexture.width = 80;
  upd.updateTexture.height = 60;
  upd.updateTexture.channels = 3;
  upd.updateTexture.srcRowStride = 0;
  upd.updateTexture.data = fakePixels;
  mock.PushToCommandQueue(upd);

  RenderCommand draw;
  draw.commandType = CommandType::DrawIndexed;
  draw.drawIndexed.elementCount = 6;
  mock.PushToCommandQueue(draw);
  mock.SubmitCommandQueue();

  expectEqSize(mock.countSubmittedOfType(CommandType::UpdateTexture),
               1u,
               "UpdateTexture recorded");
  expectTrue(mock.getLastSubmitted(0).updateTexture.handle == textureHandle,
             "UpdateTexture handle matches");
  expectEqInt(mock.getLastSubmitted(0).updateTexture.channels,
              3,
              "RGB channels on update");
  expectTrue(mock.getLastSubmitted(0).updateTexture.data == fakePixels,
             "data pointer preserved until submit");
}

static void
testCommandQueueOverflowPolicy()
{
  std::printf("\n--- command queue overflow policy ---\n");
  CommandQueue queue;
  expectEqSize(queue.GetCapacity(), 2048u, "initial reserve is 2048");

  RenderCommand cmd;
  cmd.commandType = CommandType::ClearScreen;
  cmd.clear.r = 0.0f;
  cmd.clear.g = 0.0f;
  cmd.clear.b = 0.0f;
  cmd.clear.a = 1.0f;

  for (size_t i = 0; i < 3000; ++i) {
    queue.Submit(cmd);
  }
  expectEqSize(queue.GetCommandCount(), 3000u, "queue grows beyond reserve");
  expectEqSize(queue.GetHighWaterMark(), 3000u, "growth updates high-water");
  expectEqSize(queue.GetRejectedThisFrame(), 0u, "growth does not reject");

  CommandQueue oddCeilingQueue(3000);
  for (size_t i = 0; i < 3000; ++i) {
    oddCeilingQueue.Submit(cmd);
  }
  expectEqSize(oddCeilingQueue.GetCapacity(),
               3000u,
               "allocation does not grow beyond a non-power-of-two ceiling");

  CommandQueue cappedQueue(2048);
  for (size_t i = 0; i < cappedQueue.GetSafetyCeiling(); ++i) {
    cappedQueue.Submit(cmd);
  }
  cappedQueue.Submit(cmd);
  cappedQueue.Submit(cmd);
  expectEqSize(
    cappedQueue.GetCommandCount(), 2048u, "safety ceiling bounds queue growth");
  expectEqSize(
    cappedQueue.GetRejectedThisFrame(), 2u, "two commands rejected this frame");
  expectEqSize(
    cappedQueue.GetTotalRejected(), 2u, "total rejected count accumulates");
  expectTrue(cappedQueue.HasRejectedThisFrame(), "rejection flag set");

  cappedQueue.Reset();
  expectEqSize(cappedQueue.GetCommandCount(), 0u, "reset clears pending");
  expectEqSize(
    cappedQueue.GetRejectedThisFrame(), 0u, "reset clears frame rejects");
  expectTrue(!cappedQueue.HasRejectedThisFrame(),
             "rejection flag cleared on reset");
  expectEqSize(
    cappedQueue.GetTotalRejected(), 2u, "lifetime rejected count preserved");
}

static void
testGenerationalHandles()
{
  std::printf("\n--- generational handles ---\n");
  MockBackend mock;
  mock.Initialize();
  unsigned char pixels[4] = { 255, 255, 255, 255 };
  TextureOptions options;
  TextureHandle first = mock.CreateTexture(pixels, 1, 1, 4, options);
  expectTrue(mock.IsTextureValid(first), "created texture is valid");
  expectTrue(mock.DestroyTexture(first), "destroy accepts current handle");
  expectTrue(!mock.IsTextureValid(first), "destroy invalidates generation");
  expectTrue(!mock.ReplaceTexture(first, pixels, 1, 1, 4, options),
             "replace rejects stale handle");

  TextureHandle second = mock.CreateTexture(pixels, 1, 1, 4, options);
  expectTrue(second.slot == first.slot, "released slot is reused");
  expectTrue(second.generation != first.generation,
             "reused slot receives a new generation");

  RenderCommand staleCommand{};
  staleCommand.commandType = CommandType::SetTexture;
  staleCommand.bindTexture.handle = first;
  staleCommand.bindTexture.slot = 0;
  mock.PushToCommandQueue(staleCommand);
  mock.SubmitCommandQueue();
  expectEqSize(mock.getLastSubmittedCount(),
               0u,
               "stale resource command is safely rejected");
  expectEqSize(mock.getRejectedStaleCommandCount(),
               1u,
               "stale command rejection is reported");

  MeshHandle mesh = mock.CreateMesh(nullptr, 64, nullptr, 0);
  expectTrue(
    mock.ReplaceMesh(
      mesh, nullptr, 128, nullptr, 0, MeshVertexLayout::Pos3Color4U8, true),
    "current mesh can be replaced in place");
  expectTrue(mock.DestroyMesh(mesh), "current mesh can be destroyed");
  expectTrue(!mock.DestroyMesh(mesh), "stale mesh destroy safely no-ops");

  ShaderSources sources;
  sources.vertexSource = "vertex";
  sources.fragmentSource = "fragment";
  ShaderHandle shader = mock.CreateShaderProgram(sources);
  expectTrue(mock.ReplaceShaderProgram(shader, sources),
             "current shader can be replaced in place");
  expectTrue(mock.DestroyShaderProgram(shader),
             "current shader can be destroyed");
  expectTrue(!mock.ReplaceShaderProgram(shader, sources),
             "stale shader replacement safely no-ops");
}

static void
testScissorEnableDisable()
{
  std::printf("\n--- explicit scissor state ---\n");
  MockBackend mock;
  mock.Initialize();

  RenderCommand enable{};
  enable.commandType = CommandType::SetScissorState;
  enable.scissor.enabled = true;
  enable.scissor.width = 40;
  enable.scissor.height = 20;
  mock.PushToCommandQueue(enable);

  RenderCommand disable{};
  disable.commandType = CommandType::SetScissorState;
  disable.scissor.enabled = false;
  mock.PushToCommandQueue(disable);
  mock.SubmitCommandQueue();

  expectEqSize(mock.getLastSubmittedCount(), 2u, "both scissor states submit");
  expectTrue(mock.getLastSubmitted(0).scissor.enabled,
             "first scissor token enables clipping");
  expectTrue(!mock.getLastSubmitted(1).scissor.enabled,
             "second scissor token disables clipping");
}

static void
testDepthFramebufferLifecycle()
{
  std::printf("\n--- depth framebuffer lifecycle and tokens ---\n");
  MockBackend mock;
  mock.Initialize();

  TextureHandle depthTex{};
  FramebufferHandle fbo = mock.CreateDepthFramebuffer(1024, 1024, &depthTex);
  expectTrue(fbo.isValid(), "depth framebuffer is valid");
  expectTrue(depthTex.isValid(), "depth texture is valid");
  expectTrue(mock.IsFramebufferValid(fbo), "framebuffer is live in backend");
  expectTrue(mock.IsTextureValid(depthTex), "depth texture is live in backend");

  RenderCommand bindFb{};
  bindFb.commandType = CommandType::SetFramebuffer;
  bindFb.bindFramebuffer.handle = fbo;
  mock.PushToCommandQueue(bindFb);

  RenderCommand unbindFb{};
  unbindFb.commandType = CommandType::SetFramebuffer;
  unbindFb.bindFramebuffer.handle = FramebufferHandle{};
  mock.PushToCommandQueue(unbindFb);

  mock.SubmitCommandQueue();
  expectEqSize(
    mock.getLastSubmittedCount(), 2u, "framebuffer bind/unbind submitted");
  expectTrue(mock.getLastSubmitted(0).bindFramebuffer.handle.isValid(),
             "first token binds target FBO");
  expectTrue(!mock.getLastSubmitted(1).bindFramebuffer.handle.isValid(),
             "second token unbinds to default FBO");

  expectTrue(mock.DestroyFramebuffer(fbo),
             "framebuffer destroyed successfully");
  expectTrue(!mock.IsFramebufferValid(fbo), "framebuffer is no longer valid");
}

static int
runMockBackendCase(void (*testFunction)())
{
  g_failures = 0;
  testFunction();
  return g_failures;
}

void
registerMockBackendTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.MockBackend.Lifecycle",
               []() { return runMockBackendCase(testLifecycle); });
  registry.add("Illumo.MockBackend.CreateRecords",
               []() { return runMockBackendCase(testCreateRecords); });
  registry.add("Illumo.MockBackend.CommandSubmit",
               []() { return runMockBackendCase(testCommandSubmit); });
  registry.add("Illumo.MockBackend.ProofLikeSequence",
               []() { return runMockBackendCase(testProofLikeSequence); });
  registry.add("Illumo.MockBackend.CanvasLikeUpdateTexture", []() {
    return runMockBackendCase(testCanvasLikeUpdateTexture);
  });
  registry.add("Illumo.MockBackend.CommandQueueOverflow", []() {
    return runMockBackendCase(testCommandQueueOverflowPolicy);
  });
  registry.add("Illumo.MockBackend.GenerationalHandles",
               []() { return runMockBackendCase(testGenerationalHandles); });
  registry.add("Illumo.MockBackend.ScissorEnableDisable",
               []() { return runMockBackendCase(testScissorEnableDisable); });
  registry.add("Illumo.MockBackend.DepthFramebufferLifecycle", []() {
    return runMockBackendCase(testDepthFramebufferLifecycle);
  });
}
