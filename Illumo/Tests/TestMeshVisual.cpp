#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Rendering/WorldLook.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

static TestCounters g;

static bool
matricesNear(const float* left, const glm::mat4& right)
{
  if (left == nullptr) {
    return false;
  }
  const float* rightPtr = glm::value_ptr(right);
  for (int i = 0; i < 16; ++i) {
    if (std::abs(left[i] - rightPtr[i]) > 0.0001f) {
      return false;
    }
  }
  return true;
}

static const RenderCommand*
findSubmittedCommand(const MockBackend& mock, CommandType type, size_t ordinal)
{
  size_t found = 0;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
    if (command.commandType == type) {
      if (found == ordinal) {
        return &command;
      }
      found += 1;
    }
  }
  return nullptr;
}

static const RenderCommand*
findSubmittedUniformMat4(const MockBackend& mock,
                         const char* name,
                         size_t ordinal)
{
  size_t found = 0;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
    if (command.commandType != CommandType::SetUniformMat4) {
      continue;
    }
    if (std::strcmp(command.uniformMat4.name, name) != 0) {
      continue;
    }
    if (found == ordinal) {
      return &command;
    }
    found += 1;
  }
  return nullptr;
}

static bool
submittedUsePixelsOne(const MockBackend& mock)
{
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
    if (command.commandType == CommandType::SetUniformInt &&
        std::strcmp(command.uniformInt.name, "uUsePixels") == 0 &&
        command.uniformInt.value == 1) {
      return true;
    }
  }
  return false;
}

static int
runMeshVisualCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

static void
testMeshVisualDynamicMeshReuse()
{
  testSection("MeshVisual: dirty geometry reuses dynamic mesh handles");
  NullRenderWindow window(640, 480);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  MeshVisual visual;
  visual.prepare(&renderer);
  visual.addAxes();

  renderer.BeginFrame();
  testTrue(
    g, visual.AppendCommands(&renderer), "initial geometry emits tokens");
  renderer.EndFrame();
  const RenderCommand* firstMesh =
    findSubmittedCommand(mock, CommandType::SetMesh, 0);
  testTrue(g, firstMesh != nullptr, "initial draw binds a mesh");
  MeshHandle initialMeshHandle{};
  if (firstMesh != nullptr) {
    initialMeshHandle = firstMesh->bindMesh.handle;
  }
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             1u,
             "initial geometry uploads one dynamic buffer");

  mock.resetCounters();
  renderer.BeginFrame();
  testTrue(
    g, visual.AppendCommands(&renderer), "unchanged geometry emits tokens");
  renderer.EndFrame();
  const RenderCommand* unchangedMesh =
    findSubmittedCommand(mock, CommandType::SetMesh, 0);
  testTrue(g,
           initialMeshHandle.isValid() && unchangedMesh != nullptr &&
             initialMeshHandle == unchangedMesh->bindMesh.handle,
           "unchanged geometry keeps its mesh handle");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             0u,
             "unchanged geometry skips buffer uploads");

  visual.addWireCube(
    glm::vec3(0.0f), glm::vec3(1.0f), ColorRgba{ 255, 255, 255, 255 });
  mock.resetCounters();
  renderer.BeginFrame();
  testTrue(
    g, visual.AppendCommands(&renderer), "expanded geometry emits tokens");
  renderer.EndFrame();
  const RenderCommand* expandedMesh =
    findSubmittedCommand(mock, CommandType::SetMesh, 0);
  testTrue(g,
           initialMeshHandle.isValid() && expandedMesh != nullptr &&
             initialMeshHandle == expandedMesh->bindMesh.handle,
           "growth inside retained capacity keeps the mesh handle");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             1u,
             "expanded geometry updates the retained buffer");
}

static void
testMeshVisualSpriteAndCube()
{
  testSection("MeshVisual: sprite and cube emit the canonical look");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  unsigned char px[4] = { 255, 255, 255, 255 };
  TextureHandle texture = renderer.enrollTexture(px, 1, 1, 4);
  MeshVisual visual;
  visual.prepare(&renderer);
  visual.addSolidCube(
    glm::vec3(0.0f), glm::vec3(0.5f), ColorRgba{ 200, 200, 200, 255 });
  visual.addSprite(texture,
                   glm::vec3(0.0f, 0.0f, 0.0f),
                   glm::vec2(2.0f, 2.0f),
                   ColorRgba{ 255, 255, 255, 255 },
                   MeshFacing::World);

  Scene scene(&window, &camera);
  scene.AddDrawable(&visual, RenderLayerId::World);
  mock.resetCounters();
  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();

  testTrue(g,
           mock.countNonEmptyOfType(CommandType::SetShader) >= 2u,
           "cube and sprite bind styles");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::SetTexture) >= 1u,
           "sprite binds a texture");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 2u,
           "cube and sprite draw");
  testTrue(g,
           findSubmittedUniformMat4(mock, WorldLook::kMvpUniform, 0) != nullptr,
           "draws push uMVP");
  testTrue(
    g, !submittedUsePixelsOne(mock), "world draws do not set uUsePixels=1");
}

static void
testMeshVisualBillboard()
{
  testSection("MeshVisual: billboard faces the camera");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  camera.lookAt(glm::vec3(10.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f));
  camera.setPerspective(55.0f, 0.1f, 100.0f);
  camera.setProjectionType(ProjectionType::Perspective);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  unsigned char px[4] = { 255, 255, 255, 255 };
  TextureHandle texture = renderer.enrollTexture(px, 1, 1, 4);

  MeshVisual worldAligned;
  worldAligned.prepare(&renderer);
  worldAligned.addSprite(texture,
                         glm::vec3(0.0f, 0.0f, 0.0f),
                         glm::vec2(1.0f, 1.0f),
                         ColorRgba{},
                         MeshFacing::World);

  MeshVisual billboard;
  billboard.prepare(&renderer);
  billboard.addSprite(texture,
                      glm::vec3(0.0f, 0.0f, 0.0f),
                      glm::vec2(1.0f, 1.0f),
                      ColorRgba{},
                      MeshFacing::Billboard);

  Scene scene(&window, &camera);
  scene.AddDrawable(&worldAligned, RenderLayerId::World);
  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();
  const RenderCommand* worldMvpCmd =
    findSubmittedUniformMat4(mock, WorldLook::kMvpUniform, 0);
  testTrue(g, worldMvpCmd != nullptr, "world-aligned sprite submits uMVP");
  float worldMvp[16] = {};
  if (worldMvpCmd != nullptr) {
    std::memcpy(worldMvp, worldMvpCmd->uniformMat4.m, sizeof(worldMvp));
  }

  mock.resetCounters();
  Scene billboardScene(&window, &camera);
  billboardScene.AddDrawable(&billboard, RenderLayerId::World);
  renderer.BeginFrame();
  renderer.RenderScene(&billboardScene, &camera);
  renderer.EndFrame();
  const RenderCommand* billboardMvpCmd =
    findSubmittedUniformMat4(mock, WorldLook::kMvpUniform, 0);
  testTrue(g, billboardMvpCmd != nullptr, "billboard sprite submits uMVP");
  float billboardMvp[16] = {};
  if (billboardMvpCmd != nullptr) {
    std::memcpy(
      billboardMvp, billboardMvpCmd->uniformMat4.m, sizeof(billboardMvp));
  }

  const float aspect = 640.0f / 480.0f;
  const glm::mat4 viewProjection = camera.GetMVPMatrix(aspect);
  const glm::mat4 view = camera.GetViewMatrix();
  Transform3D spriteLocal;
  spriteLocal.scale = Vector3(1.0f, 1.0f, 1.0f);
  const glm::mat4 expectedWorld = viewProjection * spriteLocal.toMatrix();
  const glm::mat4 expectedBillboard =
    viewProjection * WorldLook::billboardWorld(spriteLocal.toMatrix(), view);
  testTrue(g,
           worldMvpCmd != nullptr && matricesNear(worldMvp, expectedWorld),
           "world-aligned uMVP is camera VP times local");
  testTrue(g,
           billboardMvpCmd != nullptr &&
             matricesNear(billboardMvp, expectedBillboard),
           "billboard uMVP uses WorldLook::billboardWorld");
  testTrue(g,
           worldMvpCmd != nullptr && billboardMvpCmd != nullptr &&
             !matricesNear(worldMvp, expectedBillboard),
           "side-on camera makes billboard differ from world-aligned");
}

static void
testMeshVisualSceneAttachment()
{
  testSection("MeshVisual: scene node world transform composes into uMVP");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  MeshVisual visual;
  visual.prepare(&renderer);
  visual.addSolidCube(
    glm::vec3(0.0f), glm::vec3(0.25f), ColorRgba{ 255, 255, 255, 255 });

  SceneGraph graph;
  const SceneNodeHandle node = graph.createNode();
  Matrix4 translation =
    glm::translate(Matrix4(1.0f), Vector3(2.0f, 0.0f, 0.0f));
  testTrue(g,
           graph.setLocalTransform(node, translation),
           "node translation is stored");
  testTrue(g,
           graph.setRenderAttachment(node, &visual),
           "MeshVisual attaches to the node");

  Scene scene(&window, &camera);
  scene.AddDrawable(&graph, RenderLayerId::World);
  mock.resetCounters();
  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();

  size_t uMvpCount = 0;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
    if (command.commandType == CommandType::SetUniformMat4 &&
        std::strcmp(command.uniformMat4.name, WorldLook::kMvpUniform) == 0) {
      ++uMvpCount;
    }
  }
  const RenderCommand* mvp =
    uMvpCount > 0
      ? findSubmittedUniformMat4(mock, WorldLook::kMvpUniform, uMvpCount - 1)
      : nullptr;
  const float aspect = 640.0f / 480.0f;
  const glm::mat4 expected = camera.GetMVPMatrix(aspect) * translation;
  testTrue(g, mvp != nullptr, "attachment submits uMVP");
  testTrue(g,
           mvp != nullptr && matricesNear(mvp->uniformMat4.m, expected),
           "uMVP equals camera MVP times node world");
  testTrue(
    g, !submittedUsePixelsOne(mock), "attachment does not use pixel mode");
}

static void
testMeshVisualNewPrimitivesEmitTokens()
{
  testSection("MeshVisual: pyramid and sphere emit 3D tokens");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  MeshVisual pyramid;
  pyramid.prepare(&renderer);
  pyramid.addSolidPyramid(
    glm::vec3(0.0f), glm::vec3(0.5f), ColorRgba{ 200, 180, 80, 255 });
  renderer.BeginFrame();
  testTrue(g, pyramid.AppendCommands(&renderer), "pyramid appends tokens");
  renderer.EndFrame();
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "pyramid emits an indexed draw");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::UpdateBuffer) >= 1u,
           "pyramid uploads triangle vertices");
  const RenderCommand* pyramidDraw =
    findSubmittedCommand(mock, CommandType::DrawIndexed, 0);
  testTrue(g,
           pyramidDraw != nullptr &&
             pyramidDraw->drawIndexed.elementCount >= 3u,
           "pyramid draw has triangle indices");

  mock.resetCounters();
  MeshVisual sphere;
  sphere.prepare(&renderer);
  sphere.addWireSphere(glm::vec3(0.0f), 1.0f, ColorRgba{ 80, 180, 255, 255 });
  renderer.BeginFrame();
  testTrue(g, sphere.AppendCommands(&renderer), "sphere appends tokens");
  renderer.EndFrame();
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "sphere emits an indexed line draw");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::UpdateBuffer) >= 1u,
           "sphere uploads line vertices");
  const RenderCommand* sphereDraw =
    findSubmittedCommand(mock, CommandType::DrawIndexed, 0);
  testTrue(g,
           sphereDraw != nullptr && sphereDraw->drawIndexed.elementCount >= 6u,
           "sphere draw has line indices");

  mock.resetCounters();
  MeshVisual ellipse;
  ellipse.prepare(&renderer);
  ellipse.addSolidEllipse(glm::vec3(0.0f),
                          glm::vec2(1.0f, 0.5f),
                          ColorRgba{ 180, 220, 255, 255 },
                          16);
  renderer.BeginFrame();
  testTrue(g, ellipse.AppendCommands(&renderer), "ellipse appends tokens");
  renderer.EndFrame();
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "ellipse emits an indexed triangle draw");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::UpdateBuffer) >= 1u,
           "ellipse uploads triangle vertices");
  const RenderCommand* ellipseDraw =
    findSubmittedCommand(mock, CommandType::DrawIndexed, 0);
  testTrue(g,
           ellipseDraw != nullptr &&
             ellipseDraw->drawIndexed.elementCount == 16u * 3u,
           "ellipse draw has 16 triangle fan indices");
}

static void
testMeshVisualLitShadowPassClearsDepth()
{
  testSection("MeshVisual: lit shadow pass clears depth before drawing");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  MeshVisual visual;
  visual.prepare(&renderer);
  visual.addSolidCube(
    glm::vec3(0.0f), glm::vec3(0.5f), ColorRgba{ 200, 200, 200, 255 });

  mock.resetCounters();
  renderer.BeginFrame();
  testTrue(g, visual.AppendCommands(&renderer), "cube appends lit tokens");
  renderer.EndFrame();

  bool sawShadowFramebuffer = false;
  bool clearedDepthBeforeShadowDraw = false;
  bool sawColorClearOnShadowTarget = false;
  bool shadowDrawIssued = false;
  bool lightingUniformsPresent = false;

  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
    if (command.commandType == CommandType::SetFramebuffer &&
        command.bindFramebuffer.handle.isValid()) {
      sawShadowFramebuffer = true;
      clearedDepthBeforeShadowDraw = false;
      shadowDrawIssued = false;
      continue;
    }
    if (!sawShadowFramebuffer || shadowDrawIssued) {
      if (command.commandType == CommandType::SetUniformVec3 &&
          std::strcmp(command.uniformVec3.name, WorldLook::kLightDirUniform) ==
            0) {
        lightingUniformsPresent = true;
      }
      continue;
    }
    if (command.commandType == CommandType::ClearColorBuffer) {
      sawColorClearOnShadowTarget = true;
    }
    if (command.commandType == CommandType::ClearDepthBuffer) {
      clearedDepthBeforeShadowDraw = true;
    }
    if (command.commandType == CommandType::DrawIndexed) {
      shadowDrawIssued = true;
    }
  }

  testTrue(g, sawShadowFramebuffer, "lit cube binds a shadow framebuffer");
  testTrue(g, shadowDrawIssued, "shadow pass issues an indexed depth draw");
  testTrue(g,
           clearedDepthBeforeShadowDraw,
           "shadow pass clears depth before drawing into the depth FBO");
  testTrue(g,
           !sawColorClearOnShadowTarget,
           "shadow pass does not color-clear a depth-only target");
  testTrue(g,
           lightingUniformsPresent,
           "main pass still pushes directional lighting uniforms");
}

static bool
uniformVec3Near(const RenderCommand& command,
                const char* name,
                float x,
                float y,
                float z)
{
  return command.commandType == CommandType::SetUniformVec3 &&
         std::strcmp(command.uniformVec3.name, name) == 0 &&
         std::abs(command.uniformVec3.x - x) < 0.0001f &&
         std::abs(command.uniformVec3.y - y) < 0.0001f &&
         std::abs(command.uniformVec3.z - z) < 0.0001f;
}

static void
testMeshVisualLightingUniformsFromSetters()
{
  testSection("MeshVisual: lighting setters control uniform tokens");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  MeshVisual visual;
  visual.prepare(&renderer);
  visual.addSolidCube(
    glm::vec3(0.0f), glm::vec3(0.5f), ColorRgba{ 200, 200, 200, 255 });
  visual.setLightDirection(glm::vec3(0.0f, 1.0f, 0.0f));
  visual.setLightColor(glm::vec3(0.25f, 0.5f, 0.75f));
  visual.setAmbientColor(glm::vec3(0.1f, 0.2f, 0.3f));

  mock.resetCounters();
  renderer.BeginFrame();
  testTrue(g, visual.AppendCommands(&renderer), "lit cube appends tokens");
  renderer.EndFrame();

  bool sawLightDir = false;
  bool sawLightColor = false;
  bool sawAmbient = false;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
    if (uniformVec3Near(
          command, WorldLook::kLightDirUniform, 0.0f, 1.0f, 0.0f)) {
      sawLightDir = true;
    }
    if (uniformVec3Near(
          command, WorldLook::kLightColorUniform, 0.25f, 0.5f, 0.75f)) {
      sawLightColor = true;
    }
    if (uniformVec3Near(
          command, WorldLook::kAmbientColorUniform, 0.1f, 0.2f, 0.3f)) {
      sawAmbient = true;
    }
  }
  testTrue(g, sawLightDir, "light direction uniform matches setter");
  testTrue(g, sawLightColor, "light color uniform matches setter");
  testTrue(g, sawAmbient, "ambient color uniform matches setter");

  visual.setLightingEnabled(false);
  mock.resetCounters();
  renderer.BeginFrame();
  testTrue(g, visual.AppendCommands(&renderer), "unlit cube appends tokens");
  renderer.EndFrame();

  bool sawLightingUniformWhileDisabled = false;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
    if (command.commandType == CommandType::SetUniformVec3 &&
        (std::strcmp(command.uniformVec3.name, WorldLook::kLightDirUniform) ==
           0 ||
         std::strcmp(command.uniformVec3.name, WorldLook::kLightColorUniform) ==
           0 ||
         std::strcmp(command.uniformVec3.name,
                     WorldLook::kAmbientColorUniform) == 0)) {
      sawLightingUniformWhileDisabled = true;
    }
  }
  testTrue(g,
           !sawLightingUniformWhileDisabled,
           "disabled lighting does not emit light uniforms");
}

static bool
uniformFloatNear(const RenderCommand& command, const char* name, float value)
{
  return command.commandType == CommandType::SetUniformFloat &&
         std::strcmp(command.uniformFloat.name, name) == 0 &&
         std::abs(command.uniformFloat.value - value) < 0.000001f;
}

static bool
uniformIntIs(const RenderCommand& command, const char* name, int value)
{
  return command.commandType == CommandType::SetUniformInt &&
         std::strcmp(command.uniformInt.name, name) == 0 &&
         command.uniformInt.value == value;
}

static void
testMeshVisualShadowUniformsFromSetters()
{
  testSection("MeshVisual: shadow setters control pass and uniforms");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  MeshVisual visual;
  visual.prepare(&renderer);
  visual.addSolidCube(
    glm::vec3(0.0f), glm::vec3(0.5f), ColorRgba{ 200, 200, 200, 255 });
  visual.setShadowMapSize(512);
  visual.setShadowBias(0.002f);
  visual.setShadowSlopeScale(0.01f);
  visual.setShadowNormalOffset(0.02f);
  visual.setShadowPcfEnabled(false);

  mock.resetCounters();
  renderer.BeginFrame();
  testTrue(
    g, visual.AppendCommands(&renderer), "lit cube appends shadow tokens");
  renderer.EndFrame();

  bool sawShadowViewport = false;
  bool sawBias = false;
  bool sawSlope = false;
  bool sawOffset = false;
  bool sawPcfOff = false;
  bool sawShadowsOn = false;
  bool insideShadowTarget = false;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
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
    if (uniformFloatNear(command, WorldLook::kShadowBiasUniform, 0.002f)) {
      sawBias = true;
    }
    if (uniformFloatNear(command, WorldLook::kShadowSlopeScaleUniform, 0.01f)) {
      sawSlope = true;
    }
    if (uniformFloatNear(
          command, WorldLook::kShadowNormalOffsetUniform, 0.02f)) {
      sawOffset = true;
    }
    if (uniformIntIs(command, WorldLook::kShadowPcfUniform, 0)) {
      sawPcfOff = true;
    }
    if (uniformIntIs(command, WorldLook::kShadowsEnabledUniform, 1)) {
      sawShadowsOn = true;
    }
  }
  testTrue(g, visual.getShadowMapSize() == 512, "shadow map size snaps to 512");
  testTrue(g, sawShadowViewport, "shadow pass viewport matches map size");
  testTrue(g, sawBias, "shadow bias uniform matches setter");
  testTrue(g, sawSlope, "shadow slope scale uniform matches setter");
  testTrue(g, sawOffset, "shadow normal offset uniform matches setter");
  testTrue(g, sawPcfOff, "PCF disable reaches uShadowPcf");
  testTrue(g, sawShadowsOn, "shadows remain enabled");

  visual.setShadowsEnabled(false);
  mock.resetCounters();
  renderer.BeginFrame();
  testTrue(
    g, visual.AppendCommands(&renderer), "unshadowed cube appends tokens");
  renderer.EndFrame();

  bool sawShadowFramebuffer = false;
  bool sawShadowsOff = false;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
    if (command.commandType == CommandType::SetFramebuffer &&
        command.bindFramebuffer.handle.isValid()) {
      sawShadowFramebuffer = true;
    }
    if (uniformIntIs(command, WorldLook::kShadowsEnabledUniform, 0)) {
      sawShadowsOff = true;
    }
  }
  testTrue(g,
           !sawShadowFramebuffer,
           "disabled shadows skip the depth framebuffer pass");
  testTrue(g, sawShadowsOff, "uShadowsEnabled is 0 when shadows are off");
}

static void
testMeshVisualMotionBlurUniformsFromSetters()
{
  testSection("MeshVisual: motion blur setters control previous-MVP tokens");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  MeshVisual visual;
  visual.prepare(&renderer);
  visual.addSolidCube(
    glm::vec3(0.0f), glm::vec3(0.5f), ColorRgba{ 200, 200, 200, 255 });
  visual.setMotionBlurEnabled(true);
  visual.setMotionBlurAmount(0.75f);
  visual.setMotionBlurMax(0.15f);

  mock.resetCounters();
  renderer.BeginFrame();
  testTrue(
    g, visual.AppendCommands(&renderer), "first lit cube appends tokens");
  renderer.EndFrame();

  const RenderCommand* firstMvp =
    findSubmittedUniformMat4(mock, WorldLook::kMvpUniform, 1);
  const RenderCommand* firstPrev =
    findSubmittedUniformMat4(mock, WorldLook::kPrevMvpUniform, 0);
  testTrue(g, firstMvp != nullptr, "main pass emits uMVP");
  testTrue(g, firstPrev != nullptr, "main pass emits uPrevMVP");
  glm::mat4 firstMvpMatrix(1.0f);
  bool haveFirstMvp = false;
  if (firstMvp != nullptr && firstPrev != nullptr) {
    std::memcpy(glm::value_ptr(firstMvpMatrix),
                firstMvp->uniformMat4.m,
                16 * sizeof(float));
    haveFirstMvp = true;
    testTrue(g,
             matricesNear(firstPrev->uniformMat4.m, firstMvpMatrix),
             "first frame previous MVP matches current MVP");
  }

  bool sawAmount = false;
  bool sawMax = false;
  bool sawEnabled = false;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
    if (uniformFloatNear(command, WorldLook::kMotionBlurAmountUniform, 0.75f)) {
      sawAmount = true;
    }
    if (uniformFloatNear(command, WorldLook::kMotionBlurMaxUniform, 0.15f)) {
      sawMax = true;
    }
    if (uniformIntIs(command, WorldLook::kMotionBlurEnabledUniform, 1)) {
      sawEnabled = true;
    }
  }
  testTrue(g, sawAmount, "motion blur amount uniform matches setter");
  testTrue(g, sawMax, "motion blur max uniform matches setter");
  testTrue(g, sawEnabled, "motion blur remains enabled");

  visual.setModelMatrix(
    glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
  mock.resetCounters();
  renderer.BeginFrame();
  testTrue(
    g, visual.AppendCommands(&renderer), "moved lit cube appends tokens");
  renderer.EndFrame();

  const RenderCommand* secondMvp =
    findSubmittedUniformMat4(mock, WorldLook::kMvpUniform, 1);
  const RenderCommand* secondPrev =
    findSubmittedUniformMat4(mock, WorldLook::kPrevMvpUniform, 0);
  testTrue(g,
           secondMvp != nullptr && secondPrev != nullptr,
           "moved frame emits current and previous MVP");
  if (haveFirstMvp && secondPrev != nullptr) {
    testTrue(g,
             matricesNear(secondPrev->uniformMat4.m, firstMvpMatrix),
             "second frame previous MVP matches the prior current MVP");
  }
  if (secondMvp != nullptr && secondPrev != nullptr) {
    glm::mat4 current(1.0f);
    std::memcpy(
      glm::value_ptr(current), secondMvp->uniformMat4.m, 16 * sizeof(float));
    testTrue(g,
             !matricesNear(secondPrev->uniformMat4.m, current),
             "moved frame current MVP differs from previous MVP");
  }

  visual.setMotionBlurEnabled(false);
  mock.resetCounters();
  renderer.BeginFrame();
  testTrue(
    g, visual.AppendCommands(&renderer), "disabled motion blur appends tokens");
  renderer.EndFrame();

  bool sawDisabled = false;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
    if (uniformIntIs(command, WorldLook::kMotionBlurEnabledUniform, 0)) {
      sawDisabled = true;
    }
  }
  testTrue(g, sawDisabled, "uMotionBlurEnabled is 0 when motion blur is off");
}

void
registerMeshVisualTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.MeshVisual.DynamicMeshReuse", []() {
    return runMeshVisualCase(testMeshVisualDynamicMeshReuse);
  });
  registry.add("Illumo.MeshVisual.SpriteAndCube",
               []() { return runMeshVisualCase(testMeshVisualSpriteAndCube); });
  registry.add("Illumo.MeshVisual.Billboard",
               []() { return runMeshVisualCase(testMeshVisualBillboard); });
  registry.add("Illumo.MeshVisual.SceneAttachment", []() {
    return runMeshVisualCase(testMeshVisualSceneAttachment);
  });
  registry.add("Illumo.MeshVisual.NewPrimitives", []() {
    return runMeshVisualCase(testMeshVisualNewPrimitivesEmitTokens);
  });
  registry.add("Illumo.MeshVisual.LitShadowPassClearsDepth", []() {
    return runMeshVisualCase(testMeshVisualLitShadowPassClearsDepth);
  });
  registry.add("Illumo.MeshVisual.LightingUniformsFromSetters", []() {
    return runMeshVisualCase(testMeshVisualLightingUniformsFromSetters);
  });
  registry.add("Illumo.MeshVisual.ShadowUniformsFromSetters", []() {
    return runMeshVisualCase(testMeshVisualShadowUniformsFromSetters);
  });
  registry.add("Illumo.MeshVisual.MotionBlurUniformsFromSetters", []() {
    return runMeshVisualCase(testMeshVisualMotionBlurUniformsFromSetters);
  });
}
