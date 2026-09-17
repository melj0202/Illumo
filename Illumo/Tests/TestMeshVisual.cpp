#include <Illumo/Rendering/AssetManager.h>
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

static MeshData
makeSharedQuadMesh(bool alternateDiagonal)
{
  MeshData mesh;
  MeshVertex vertex;
  vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
  vertex.position = glm::vec3(-1.0f, -1.0f, 0.0f);
  mesh.vertices.push_back(vertex);
  vertex.position = glm::vec3(1.0f, -1.0f, 0.0f);
  mesh.vertices.push_back(vertex);
  vertex.position = glm::vec3(1.0f, 1.0f, 0.0f);
  mesh.vertices.push_back(vertex);
  vertex.position = glm::vec3(-1.0f, 1.0f, 0.0f);
  mesh.vertices.push_back(vertex);
  if (alternateDiagonal) {
    mesh.indices = { 0, 1, 3, 1, 2, 3 };
  } else {
    mesh.indices = { 0, 1, 2, 0, 2, 3 };
  }
  return mesh;
}

static void
testMeshVisualIndexedTriangles()
{
  testSection("MeshVisual: triangles preserve shared indexed geometry");
  NullRenderWindow window(640, 480);
  EnvVars env;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  MeshVisual visual;
  visual.prepare(&renderer);
  visual.addMesh(makeSharedQuadMesh(false));

  renderer.BeginFrame();
  testTrue(g, visual.AppendCommands(&renderer), "indexed quad emits tokens");
  renderer.EndFrame();

  const RenderCommand* vertexUpdate =
    findSubmittedCommand(mock, CommandType::UpdateBuffer, 0);
  const RenderCommand* indexUpdate =
    findSubmittedCommand(mock, CommandType::UpdateIndexBuffer, 0);
  testTrue(g, vertexUpdate != nullptr, "indexed quad uploads vertices");
  testTrue(g, indexUpdate != nullptr, "indexed quad uploads indices");
  if (vertexUpdate != nullptr) {
    testEqSize(g,
               vertexUpdate->updateBuffer.sizeBytes,
               4u * 36u,
               "four unique LitVertex values are uploaded");
  }
  if (indexUpdate != nullptr) {
    testEqSize(g,
               indexUpdate->updateIndexBuffer.sizeBytes,
               6u * sizeof(unsigned int),
               "six source indices are uploaded");
    const unsigned int expected[6] = { 0, 1, 2, 0, 2, 3 };
    const unsigned int* uploaded =
      static_cast<const unsigned int*>(indexUpdate->updateIndexBuffer.data);
    testTrue(g,
             uploaded != nullptr &&
               std::memcmp(uploaded, expected, sizeof(expected)) == 0,
             "source index topology is preserved byte-for-byte");
  }

  MeshHandle retainedHandle{};
  size_t indexedDrawCount = 0;
  bool allDrawCountsMatch = true;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
    if (command.commandType == CommandType::SetMesh &&
        !retainedHandle.isValid()) {
      retainedHandle = command.bindMesh.handle;
    }
    if (command.commandType == CommandType::DrawIndexed) {
      indexedDrawCount += 1;
      allDrawCountsMatch =
        allDrawCountsMatch && command.drawIndexed.elementCount == 6u;
    }
  }
  testTrue(g, retainedHandle.isValid(), "indexed triangle mesh is retained");
  testTrue(g,
           indexedDrawCount >= 1u && allDrawCountsMatch,
           "every triangle pass draws the six source indices");

  mock.resetCounters();
  renderer.BeginFrame();
  testTrue(g, visual.AppendCommands(&renderer), "unchanged quad still draws");
  renderer.EndFrame();
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             0u,
             "unchanged triangle vertices skip upload");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateIndexBuffer),
             0u,
             "unchanged triangle indices skip upload");

  visual.clearPrimitives();
  visual.addMesh(makeSharedQuadMesh(true));
  mock.resetCounters();
  renderer.BeginFrame();
  testTrue(g,
           visual.AppendCommands(&renderer),
           "changed topology inside capacity emits tokens");
  renderer.EndFrame();
  const RenderCommand* changedMesh =
    findSubmittedCommand(mock, CommandType::SetMesh, 0);
  testTrue(g,
           changedMesh != nullptr &&
             changedMesh->bindMesh.handle == retainedHandle,
           "changed topology inside capacity reuses the mesh handle");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             1u,
             "changed topology refreshes unique vertices once");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateIndexBuffer),
             1u,
             "changed topology refreshes indices once");

  visual.clearPrimitives();
  for (size_t i = 0; i < 17u; ++i) {
    visual.addMesh(makeSharedQuadMesh(false));
  }
  mock.resetCounters();
  renderer.BeginFrame();
  testTrue(g, visual.AppendCommands(&renderer), "capacity growth emits tokens");
  renderer.EndFrame();
  const RenderCommand* grownMesh =
    findSubmittedCommand(mock, CommandType::SetMesh, 0);
  testTrue(g,
           grownMesh != nullptr && grownMesh->bindMesh.handle == retainedHandle,
           "capacity growth replaces storage without changing the handle");

  MeshData invalid = makeSharedQuadMesh(false);
  invalid.indices[5] = 4;
  MeshVisual invalidVisual;
  invalidVisual.prepare(&renderer);
  invalidVisual.addMesh(invalid);
  mock.resetCounters();
  renderer.BeginFrame();
  testTrue(g,
           invalidVisual.AppendCommands(&renderer),
           "invalid mesh is rejected without failing extraction");
  renderer.EndFrame();
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             0u,
             "invalid mesh uploads no vertices");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateIndexBuffer),
             0u,
             "invalid mesh uploads no indices");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::DrawIndexed),
             0u,
             "invalid mesh emits no draw");
}

static void
testMeshVisualSharedMeshAsset()
{
  testSection("MeshVisual: instances share one managed mesh upload");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  AssetManager assets(&renderer, false);

  MeshData mesh;
  MeshVertex a;
  a.position = glm::vec3(0.0f, 0.0f, 0.0f);
  MeshVertex b;
  b.position = glm::vec3(1.0f, 0.0f, 0.0f);
  MeshVertex c;
  c.position = glm::vec3(0.0f, 1.0f, 0.0f);
  mesh.vertices = { a, b, c };
  mesh.indices = { 0, 1, 2 };
  const MeshHandle handle = assets.acquireMesh(mesh);
  const MeshAssetInfo info = assets.getMeshInfo(handle);
  testTrue(g, info.isValid(), "mesh asset enrollment succeeds");

  {
    MeshVisual first;
    first.prepare(&renderer);
    first.setShadowsEnabled(false);
    first.setLightingEnabled(false);
    first.setMeshAsset(info, ColorRgba{ 255, 128, 64, 255 });

    MeshVisual second;
    second.prepare(&renderer);
    second.setShadowsEnabled(false);
    second.setLightingEnabled(false);
    second.setModelMatrix(
      glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)));
    second.setMeshAsset(info, ColorRgba{ 64, 128, 255, 255 });

    mock.resetCounters();
    renderer.BeginFrame();
    testTrue(g, first.AppendCommands(&renderer), "first instance emits tokens");
    testTrue(
      g, second.AppendCommands(&renderer), "second instance emits tokens");
    renderer.EndFrame();

    const RenderCommand* firstMesh =
      findSubmittedCommand(mock, CommandType::SetMesh, 0);
    const RenderCommand* secondMesh =
      findSubmittedCommand(mock, CommandType::SetMesh, 1);
    testTrue(g,
             firstMesh != nullptr && secondMesh != nullptr &&
               firstMesh->bindMesh.handle == handle &&
               secondMesh->bindMesh.handle == handle,
             "both instances bind the same managed mesh handle");
    testEqSize(g,
               mock.getCreateCount(),
               0u,
               "drawing instances performs no additional mesh enrollment");
    testEqSize(g,
               mock.countNonEmptyOfType(CommandType::UpdateBuffer),
               0u,
               "immutable managed mesh skips per-instance buffer uploads");

    const RenderCommand* firstMvp =
      findSubmittedUniformMat4(mock, WorldLook::kMvpUniform, 0);
    const RenderCommand* secondMvp =
      findSubmittedUniformMat4(mock, WorldLook::kMvpUniform, 1);
    testTrue(g,
             firstMvp != nullptr && secondMvp != nullptr,
             "both instances emit their own transform");
    if (firstMvp != nullptr && secondMvp != nullptr) {
      glm::mat4 firstMatrix(1.0f);
      std::memcpy(glm::value_ptr(firstMatrix),
                  firstMvp->uniformMat4.value,
                  16 * sizeof(float));
      testTrue(g,
               !matricesNear(secondMvp->uniformMat4.value, firstMatrix),
               "shared geometry keeps per-instance transforms distinct");
    }
  }

  testTrue(g,
           mock.IsMeshValid(handle),
           "destroying visuals does not destroy manager-owned mesh");
  testTrue(g, assets.releaseMesh(handle), "asset owner releases shared mesh");
  testTrue(
    g, !mock.IsMeshValid(handle), "final asset release destroys shared mesh");
}

static void
testMeshVisualManagedAssetParticipatesInSceneShadowPass()
{
  testSection("MeshVisual: managed asset participates in scene shadow pass");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  AssetManager assets(&renderer, false);

  MeshData mesh;
  MeshVertex a;
  a.position = glm::vec3(-1.0f, -1.0f, 0.0f);
  MeshVertex b;
  b.position = glm::vec3(1.0f, -1.0f, 0.0f);
  MeshVertex c;
  c.position = glm::vec3(0.0f, 1.0f, 0.0f);
  mesh.vertices = { a, b, c };
  mesh.indices = { 0, 1, 2 };
  const MeshHandle handle = assets.acquireMesh(mesh);
  const MeshAssetInfo info = assets.getMeshInfo(handle);
  testTrue(g, info.isValid(), "managed mesh enrollment succeeds");

  {
    MeshVisual visual;
    visual.prepare(&renderer);
    visual.setMeshAsset(info);

    Scene scene(&window, &camera);
    scene.AddDrawable(&visual, RenderLayerId::World);

    mock.resetCounters();
    renderer.BeginFrame();
    renderer.RenderScene(&scene, &camera);
    renderer.EndFrame();

    bool insideShadowTarget = false;
    MeshHandle boundMesh{};
    size_t managedShadowDraws = 0;
    size_t managedShadowTextureBinds = 0;
    for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
      const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
      if (command.commandType == CommandType::SetFramebuffer) {
        insideShadowTarget = command.bindFramebuffer.handle.isValid();
      } else if (command.commandType == CommandType::SetMesh) {
        boundMesh = command.bindMesh.handle;
      } else if (command.commandType == CommandType::DrawIndexed &&
                 insideShadowTarget && boundMesh == handle) {
        managedShadowDraws += 1;
      } else if (command.commandType == CommandType::SetTexture &&
                 !insideShadowTarget &&
                 command.bindTexture.slot == WorldLook::kShadowTextureUnit) {
        managedShadowTextureBinds += 1;
      }
    }

    testEqSize(g,
               managedShadowDraws,
               1u,
               "managed mesh contributes one draw to the shared depth pass");
    testEqSize(g,
               managedShadowTextureBinds,
               1u,
               "managed mesh samples the shared scene shadow texture");
  }

  testTrue(g, assets.releaseMesh(handle), "asset owner releases managed mesh");
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
    std::memcpy(worldMvp, worldMvpCmd->uniformMat4.value, sizeof(worldMvp));
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
      billboardMvp, billboardMvpCmd->uniformMat4.value, sizeof(billboardMvp));
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
           mvp != nullptr && matricesNear(mvp->uniformMat4.value, expected),
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
testMeshVisualSceneShadowPassCoversVisibleSet()
{
  testSection("MeshVisual: one scene shadow pass covers every visible caster");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  MeshVisual directVisual;
  directVisual.prepare(&renderer);
  directVisual.addSolidCube(
    glm::vec3(0.0f), glm::vec3(0.5f), ColorRgba{ 200, 200, 200, 255 });
  directVisual.setModelMatrix(
    glm::translate(glm::mat4(1.0f), glm::vec3(-4.0f, 0.0f, 0.0f)));

  MeshVisual attachedVisual;
  attachedVisual.prepare(&renderer);
  attachedVisual.addSolidCube(
    glm::vec3(0.0f), glm::vec3(0.5f), ColorRgba{ 160, 180, 220, 255 });

  SceneGraph graph;
  const SceneNodeHandle attachedNode = graph.createNode();
  graph.setLocalTransform(
    attachedNode, glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 0.0f, 0.0f)));
  graph.setRenderAttachment(attachedNode, &attachedVisual);

  Scene scene(&window, &camera);
  scene.AddDrawable(&directVisual, RenderLayerId::World);
  scene.AddDrawable(&graph, RenderLayerId::World);

  mock.resetCounters();
  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();

  size_t shadowFramebufferBinds = 0;
  size_t shadowDepthClears = 0;
  size_t shadowDraws = 0;
  bool clearedDepthBeforeShadowDraw = false;
  bool sawColorClearOnShadowTarget = false;
  bool lightingUniformsPresent = false;
  bool insideShadowTarget = false;
  TextureHandle sharedShadowTexture{};
  size_t shadowTextureBinds = 0;

  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(i);
    if (command.commandType == CommandType::SetFramebuffer &&
        command.bindFramebuffer.handle.isValid()) {
      shadowFramebufferBinds += 1;
      insideShadowTarget = true;
      clearedDepthBeforeShadowDraw = false;
      continue;
    }
    if (command.commandType == CommandType::SetFramebuffer &&
        !command.bindFramebuffer.handle.isValid()) {
      insideShadowTarget = false;
      continue;
    }
    if (!insideShadowTarget) {
      if (command.commandType == CommandType::SetUniformVec3 &&
          std::strcmp(command.uniformVec3.name, WorldLook::kLightDirUniform) ==
            0) {
        lightingUniformsPresent = true;
      }
      if (command.commandType == CommandType::SetTexture &&
          command.bindTexture.slot == WorldLook::kShadowTextureUnit) {
        if (!sharedShadowTexture.isValid()) {
          sharedShadowTexture = command.bindTexture.handle;
        }
        testTrue(g,
                 command.bindTexture.handle == sharedShadowTexture,
                 "every receiver binds the same scene shadow texture");
        shadowTextureBinds += 1;
      }
      continue;
    }
    if (command.commandType == CommandType::ClearColorBuffer ||
        command.commandType == CommandType::ClearScreen) {
      sawColorClearOnShadowTarget = true;
    }
    if (command.commandType == CommandType::ClearDepthBuffer) {
      shadowDepthClears += 1;
      clearedDepthBeforeShadowDraw = true;
    }
    if (command.commandType == CommandType::DrawIndexed) {
      testTrue(g,
               clearedDepthBeforeShadowDraw,
               "scene depth is cleared before every shadow draw");
      shadowDraws += 1;
    }
  }

  testEqSize(g,
             shadowFramebufferBinds,
             1u,
             "the scene binds one shared shadow framebuffer");
  testEqSize(g, shadowDepthClears, 1u, "the scene shadow map is cleared once");
  testEqSize(g,
             shadowDraws,
             2u,
             "direct and SceneGraph meshes both enter the shared depth pass");
  testTrue(g,
           !sawColorClearOnShadowTarget,
           "shadow pass does not color-clear a depth-only target");
  testTrue(g,
           lightingUniformsPresent,
           "main pass still pushes directional lighting uniforms");
  testEqSize(g,
             shadowTextureBinds,
             2u,
             "both receivers sample the shared scene shadow map");
  size_t shadowDepthTextureCreates = 0;
  for (size_t i = 0; i < mock.getCreateCount(); ++i) {
    const MockBackend::CreateRecord& create = mock.getCreate(i);
    if (create.kind == MockBackend::CreateRecord::Kind::TextureData &&
        create.width == 1024 && create.height == 1024 && create.channels == 1) {
      shadowDepthTextureCreates += 1;
    }
  }
  testEqSize(g,
             shadowDepthTextureCreates,
             1u,
             "multiple casters allocate one scene depth texture");

  const RenderCommand* firstLightMatrix =
    findSubmittedUniformMat4(mock, WorldLook::kLightSpaceMatrixUniform, 0);
  const RenderCommand* secondLightMatrix =
    findSubmittedUniformMat4(mock, WorldLook::kLightSpaceMatrixUniform, 1);
  testTrue(g,
           firstLightMatrix != nullptr && secondLightMatrix != nullptr,
           "both receivers receive a light-space matrix");
  if (firstLightMatrix != nullptr && secondLightMatrix != nullptr) {
    bool matricesMatch = true;
    for (int i = 0; i < 16; ++i) {
      matricesMatch =
        matricesMatch &&
        std::abs(firstLightMatrix->uniformMat4.value[i] -
                 secondLightMatrix->uniformMat4.value[i]) < 0.0001f;
    }
    testTrue(g, matricesMatch, "all receivers use one light-space matrix");

    glm::mat4 lightSpace(1.0f);
    std::memcpy(glm::value_ptr(lightSpace),
                firstLightMatrix->uniformMat4.value,
                16 * sizeof(float));
    const glm::vec4 leftClip = lightSpace * glm::vec4(-4.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec4 rightClip = lightSpace * glm::vec4(4.0f, 0.0f, 0.0f, 1.0f);
    const glm::vec3 leftNdc = glm::vec3(leftClip) / leftClip.w;
    const glm::vec3 rightNdc = glm::vec3(rightClip) / rightClip.w;
    testTrue(g,
             std::abs(leftNdc.x) <= 1.0f && std::abs(leftNdc.y) <= 1.0f &&
               std::abs(rightNdc.x) <= 1.0f && std::abs(rightNdc.y) <= 1.0f,
             "combined caster bounds fit inside the shadow projection");
  }
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
  Scene scene(&window, &camera);
  scene.AddDrawable(&visual, RenderLayerId::World);
  renderer.RenderScene(&scene, &camera);
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
  renderer.RenderScene(&scene, &camera);
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
    findSubmittedUniformMat4(mock, WorldLook::kMvpUniform, 0);
  const RenderCommand* firstPrev =
    findSubmittedUniformMat4(mock, WorldLook::kPrevMvpUniform, 0);
  testTrue(g, firstMvp != nullptr, "main pass emits uMVP");
  testTrue(g, firstPrev != nullptr, "main pass emits uPrevMVP");
  glm::mat4 firstMvpMatrix(1.0f);
  bool haveFirstMvp = false;
  if (firstMvp != nullptr && firstPrev != nullptr) {
    std::memcpy(glm::value_ptr(firstMvpMatrix),
                firstMvp->uniformMat4.value,
                16 * sizeof(float));
    haveFirstMvp = true;
    testTrue(g,
             matricesNear(firstPrev->uniformMat4.value, firstMvpMatrix),
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
    findSubmittedUniformMat4(mock, WorldLook::kMvpUniform, 0);
  const RenderCommand* secondPrev =
    findSubmittedUniformMat4(mock, WorldLook::kPrevMvpUniform, 0);
  testTrue(g,
           secondMvp != nullptr && secondPrev != nullptr,
           "moved frame emits current and previous MVP");
  if (haveFirstMvp && secondPrev != nullptr) {
    testTrue(g,
             matricesNear(secondPrev->uniformMat4.value, firstMvpMatrix),
             "second frame previous MVP matches the prior current MVP");
  }
  if (secondMvp != nullptr && secondPrev != nullptr) {
    glm::mat4 current(1.0f);
    std::memcpy(glm::value_ptr(current),
                secondMvp->uniformMat4.value,
                16 * sizeof(float));
    testTrue(g,
             !matricesNear(secondPrev->uniformMat4.value, current),
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
  registry.add("Illumo.MeshVisual.IndexedTriangles", []() {
    return runMeshVisualCase(testMeshVisualIndexedTriangles);
  });
  registry.add("Illumo.MeshVisual.SharedMeshAsset", []() {
    return runMeshVisualCase(testMeshVisualSharedMeshAsset);
  });
  registry.add("Illumo.MeshVisual.ManagedAssetSceneShadow", []() {
    return runMeshVisualCase(
      testMeshVisualManagedAssetParticipatesInSceneShadowPass);
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
  registry.add("Illumo.MeshVisual.SceneShadowPassCoversVisibleSet", []() {
    return runMeshVisualCase(testMeshVisualSceneShadowPassCoversVisibleSet);
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
