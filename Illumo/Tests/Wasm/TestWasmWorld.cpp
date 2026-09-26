// Frame schema v6: host render world operations (HostRender, D-E30); v8:
// several worlds per guest, one per scene (D-R30).

#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/WasmFrameRenderer.h>
#include <array>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

static GuestFrame
worldFrame(std::vector<GuestWorldOperation> operations)
{
  GuestFrame frame;
  frame.width = 640;
  frame.height = 480;
  // An identity camera sees the [-1, 1] cube, where the test mesh sits.
  frame.hasCamera = true;
  frame.worldOperations = std::move(operations);
  return frame;
}

static std::vector<std::byte>
encode(const GuestFrame& frame)
{
  GuestWireWriter wire;
  frame.write(wire);
  return wire.take();
}

static GuestWorldOperation
materialCreate(std::uint32_t id)
{
  GuestWorldOperation operation;
  operation.op = GuestWorldOp::MaterialCreate;
  operation.id = id;
  operation.material.tint = { 0.5f, 0.6f, 0.7f, 1.0f };
  operation.material.blend = true;
  return operation;
}

static GuestWorldOperation
instanceCreate(std::uint32_t id,
               const GuestResourceId& mesh,
               std::uint32_t material)
{
  GuestWorldOperation operation;
  operation.op = GuestWorldOp::InstanceCreate;
  operation.id = id;
  operation.mesh = mesh;
  operation.indexCount = 3;
  operation.materialId = material;
  operation.transform[12] = 0.25f;
  operation.tint = { 1.0f, 0.5f, 0.25f, 1.0f };
  return operation;
}

static GuestWorldOperation
simple(GuestWorldOp op, std::uint32_t id)
{
  GuestWorldOperation operation;
  operation.op = op;
  operation.id = id;
  return operation;
}

static bool
worldFrameValidation()
{
  TestCounters counters;
  const GuestResourceId mesh{ 700, GuestResourceKind::Mesh, 3, 1 };
  GuestWorldOperation update = simple(GuestWorldOp::InstanceUpdate, 10);
  update.visible = false;
  update.tint = { 0.1f, 0.2f, 0.3f, 0.4f };
  GuestWorldOperation moved = simple(GuestWorldOp::InstanceTransform, 10);
  moved.transform[13] = 2.0f;
  GuestWorldOperation environment = simple(GuestWorldOp::Environment, 0);
  environment.environment.shadowsEnabled = true;
  environment.environment.shadowPcf = true;
  environment.environment.shadowMapSize = 2048;
  environment.environment.lightColor = { 0.9f, 0.8f, 0.7f };
  const GuestFrame frame =
    worldFrame({ materialCreate(1),
                 simple(GuestWorldOp::MaterialUpdate, 1),
                 instanceCreate(10, mesh, 1),
                 update,
                 moved,
                 environment,
                 simple(GuestWorldOp::InstanceDestroy, 10),
                 simple(GuestWorldOp::MaterialDestroy, 1) });
  const std::vector<std::byte> bytes = encode(frame);
  GuestFrame decoded;
  const bool read = GuestFrame::read(bytes, decoded);
  const std::vector<GuestWorldOperation>& ops = decoded.worldOperations;
  testTrue(counters,
           read && ops.size() == 8 &&
             ops[0].material.tint == frame.worldOperations[0].material.tint &&
             ops[0].material.blend && ops[0].material.castsShadow &&
             ops[2].mesh.slot == 3 && ops[2].materialId == 1 &&
             ops[2].indexCount == 3 && ops[2].transform[12] == 0.25f &&
             ops[2].tint[1] == 0.5f && ops[3].tint[3] == 0.4f &&
             !ops[3].visible && ops[4].transform[13] == 2.0f &&
             ops[5].environment.shadowsEnabled &&
             ops[5].environment.shadowPcf &&
             ops[5].environment.shadowMapSize == 2048 &&
             ops[5].environment.lightColor[2] == 0.7f &&
             ops[7].op == GuestWorldOp::MaterialDestroy,
           "Every world operation round-trips");

  bool truncations = true;
  for (std::size_t size = 0; size < bytes.size(); ++size) {
    GuestFrame ignored;
    truncations =
      truncations && !GuestFrame::read(std::span(bytes).first(size), ignored);
  }
  testTrue(counters, truncations, "Every truncated v6 frame is rejected");

  const float nan = std::numeric_limits<float>::quiet_NaN();
  GuestWorldOperation unknown = simple(static_cast<GuestWorldOp>(9), 4);
  GuestWorldOperation zeroId = materialCreate(0);
  GuestWorldOperation environmentWithId = environment;
  environmentWithId.id = 5;
  GuestWorldOperation tinyShadowMap = environment;
  tinyShadowMap.environment.shadowMapSize = 32;
  GuestWorldOperation nanEnvironment = environment;
  nanEnvironment.environment.shadowBias = nan;
  GuestWorldOperation nanTransform = moved;
  nanTransform.transform[0] = nan;
  GuestWorldOperation ragged = instanceCreate(11, mesh, 1);
  ragged.indexCount = 4;
  GuestWorldOperation textureMesh = instanceCreate(11, mesh, 1);
  textureMesh.mesh.kind = GuestResourceKind::Texture;
  GuestWorldOperation noMaterial = instanceCreate(11, mesh, 0);
  bool denied = true;
  for (const GuestWorldOperation& invalid : { unknown,
                                              zeroId,
                                              environmentWithId,
                                              tinyShadowMap,
                                              nanEnvironment,
                                              nanTransform,
                                              ragged,
                                              textureMesh,
                                              noMaterial }) {
    GuestFrame ignored;
    denied =
      denied && !GuestFrame::read(encode(worldFrame({ invalid })), ignored);
  }
  testTrue(counters,
           denied,
           "Unknown operations, bad ids, non-finite values, ragged index "
           "counts and non-mesh resources are denied");

  GuestFrameLimits oneOperation;
  oneOperation.worldOperations = 1;
  GuestFrame ignored;
  const GuestFrame two = worldFrame({ materialCreate(1), materialCreate(2) });
  testTrue(counters,
           !GuestFrame::read(encode(two), ignored, oneOperation) &&
             two.exceededLimit(oneOperation) != nullptr &&
             two.exceededLimit() == nullptr,
           "The world operation quota is enforced and reported to guests");

  // Version 7: the world's sky.
  GuestWorldOperation sky = simple(GuestWorldOp::Skybox, 0);
  sky.texture = { 700, GuestResourceKind::Texture, 9, 2 };
  sky.tint = { 0.5f, 0.25f, 1.0f, 1.0f };
  GuestFrame skyFrame;
  const bool skyRead = GuestFrame::read(
    encode(worldFrame({ sky, simple(GuestWorldOp::Skybox, 0) })), skyFrame);
  GuestWorldOperation skyWithId = sky;
  skyWithId.id = 3;
  GuestWorldOperation meshSky = sky;
  meshSky.texture.kind = GuestResourceKind::Mesh;
  std::vector<std::byte> skyVersion6 = encode(worldFrame({ sky }));
  skyVersion6[4] = std::byte{ 6 };
  skyVersion6.resize(skyVersion6.size() - 8);
  testTrue(counters,
           skyRead && skyFrame.worldOperations.size() == 2 &&
             skyFrame.worldOperations[0].texture.slot == 9 &&
             skyFrame.worldOperations[0].tint[1] == 0.25f &&
             skyFrame.worldOperations[1].texture.owner == 0 &&
             !GuestFrame::read(encode(worldFrame({ skyWithId })), ignored) &&
             !GuestFrame::read(encode(worldFrame({ meshSky })), ignored) &&
             !GuestFrame::read(skyVersion6, ignored),
           "Skybox operations round-trip in v7 only, with id zero and a "
           "texture or none");

  // Version 5: no world section (nor the v7 sections), nothing to apply.
  std::vector<std::byte> version5 = encode(worldFrame({}));
  version5[4] = std::byte{ 5 };
  version5.resize(version5.size() - 12);
  testTrue(counters,
           GuestFrame::read(version5, ignored) &&
             ignored.worldOperations.empty(),
           "Version 5 frames remain accepted without world operations");
  return counters.failures == 0;
}

struct LitVertex
{
  float position[3];
  float normal[3];
  std::uint32_t rgba;
  float uv[2];
};

static bool
worldOperations()
{
  TestCounters counters;
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0, 0), 1, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  renderer.ensureBuiltinStyles();
  WasmFrameRenderer bridge(renderer, 700);

  // A retained lit triangle inside the unit cube.
  const std::array<LitVertex, 3> vertices{
    { { { 0.0f, 0.0f, 0.0f }, { 0, 0, 1 }, 0xffffffffu, { 0, 0 } },
      { { 0.5f, 0.0f, 0.0f }, { 0, 0, 1 }, 0xffffffffu, { 1, 0 } },
      { { 0.0f, 0.5f, 0.0f }, { 0, 0, 1 }, 0xffffffffu, { 0, 1 } } }
  };
  const std::array<std::uint32_t, 3> indices{ 0, 1, 2 };
  GuestMeshRequest request;
  request.style = static_cast<std::uint32_t>(GuestBatchStyle::LitMesh);
  request.vertexBytes = sizeof(vertices);
  request.indexBytes = sizeof(indices);
  const GuestResourceId pending = bridge.createMesh(request);
  const GuestResourceId mesh = bridge.createMesh(request);
  GuestMeshWrite vertexWrite;
  vertexWrite.mesh = mesh;
  vertexWrite.bytes.assign(reinterpret_cast<const std::byte*>(vertices.data()),
                           reinterpret_cast<const std::byte*>(vertices.data()) +
                             sizeof(vertices));
  GuestMeshWrite indexWrite;
  indexWrite.mesh = mesh;
  indexWrite.indices = true;
  indexWrite.bytes.assign(reinterpret_cast<const std::byte*>(indices.data()),
                          reinterpret_cast<const std::byte*>(indices.data()) +
                            sizeof(indices));
  testTrue(counters,
           bridge.writeMesh(vertexWrite) && bridge.writeMesh(indexWrite),
           "A retained lit mesh completes");

  const std::function<std::vector<unsigned int>()> render = [&]() {
    DrawList scene(&window, &camera);
    bridge.dispatch(scene);
    renderer.BeginFrame();
    renderer.RenderScene(&scene, &camera);
    renderer.EndFrame();
    std::vector<unsigned int> counts;
    for (std::size_t index = 0; index < mock.getLastNonEmptySubmittedCount();
         ++index) {
      const RenderCommand& command = mock.getLastNonEmptySubmitted(index);
      if (command.commandType == CommandType::DrawIndexedInstanced) {
        counts.push_back(command.drawIndexedInstanced.instanceCount);
      }
    }
    return counts;
  };

  GuestWorldOperation opaque = materialCreate(1);
  opaque.material.blend = false;
  testTrue(
    counters,
    bridge.accept(encode(worldFrame(
      { opaque, instanceCreate(10, mesh, 1), instanceCreate(11, mesh, 1) }))) &&
      bridge.counters().worldInstances == 2 &&
      bridge.counters().worldOperations == 3,
    "Materials and instances are created on the host");
  testTrue(counters,
           render() == std::vector<unsigned int>{ 2 } &&
             renderer.frameError().empty(),
           "Both instances draw in one instanced call");

  // Rejections leave the world exactly as it was.
  const GuestFrame partial =
    worldFrame({ materialCreate(2), instanceCreate(12, mesh, 99) });
  testTrue(counters,
           !bridge.accept(encode(partial)) &&
             bridge.accept(encode(worldFrame({ materialCreate(2) }))),
           "A rejected frame applies none of its operations");
  GuestWorldOperation shapeInstance = instanceCreate(13, mesh, 1);
  const GuestResourceId shape = bridge.createMesh([] {
    GuestMeshRequest shapeRequest;
    shapeRequest.style = static_cast<std::uint32_t>(GuestBatchStyle::Shape);
    shapeRequest.vertexBytes = 48;
    shapeRequest.indexBytes = 12;
    return shapeRequest;
  }());
  shapeInstance.mesh = shape;
  GuestWorldOperation incomplete = instanceCreate(13, pending, 1);
  GuestWorldOperation outside = instanceCreate(13, mesh, 1);
  outside.firstIndex = 3;
  bool denied = true;
  for (const GuestFrame& invalid :
       { worldFrame({ simple(GuestWorldOp::MaterialDestroy, 1) }),
         worldFrame({ instanceCreate(10, mesh, 1) }),
         worldFrame({ simple(GuestWorldOp::InstanceTransform, 99) }),
         worldFrame({ simple(GuestWorldOp::InstanceUpdate, 99) }),
         worldFrame({ materialCreate(1) }),
         worldFrame({ shapeInstance }),
         worldFrame({ incomplete }),
         worldFrame({ outside }),
         worldFrame({ simple(GuestWorldOp::InstanceDestroy, 11),
                      simple(GuestWorldOp::MaterialDestroy, 1) }) }) {
    denied = denied && !bridge.accept(encode(invalid));
  }
  testTrue(counters,
           denied && bridge.counters().worldInstances == 2,
           "Busy materials, duplicate or unknown ids, and unusable meshes "
           "are denied");

  testTrue(counters,
           bridge.accept(encode(
             worldFrame({ simple(GuestWorldOp::InstanceDestroy, 10),
                          simple(GuestWorldOp::InstanceDestroy, 11),
                          simple(GuestWorldOp::MaterialDestroy, 1) }))) &&
             bridge.counters().worldInstances == 0 && render().empty(),
           "Instances and then their material are destroyed in one frame");

  // Instances keep their mesh after the guest releases it.
  testTrue(counters,
           bridge.accept(encode(worldFrame({ instanceCreate(20, mesh, 2) }))) &&
             bridge.releaseMesh(mesh) &&
             render() == std::vector<unsigned int>{ 1 } &&
             renderer.frameError().empty(),
           "A released mesh keeps drawing for its instances");

  // The sky: a cubemap only, drawn with the Skybox style's 36 indices.
  const std::vector<std::byte> faces(GuestCubemapRequest::bytesFor(1));
  const GuestResourceId cubemap = bridge.createCubemap(faces, 1);
  const std::array<std::byte, 4> pixel{};
  const GuestResourceId flat = bridge.createTexture(pixel, 1, 1, 4, false);
  const auto skyOp = [](const GuestResourceId& texture) {
    GuestWorldOperation operation = simple(GuestWorldOp::Skybox, 0);
    operation.texture = texture;
    return operation;
  };
  const auto skyDrawn = [&]() {
    render();
    for (std::size_t index = 0; index < mock.getLastNonEmptySubmittedCount();
         ++index) {
      const RenderCommand& command = mock.getLastNonEmptySubmitted(index);
      if (command.commandType == CommandType::DrawIndexed &&
          command.drawIndexed.elementCount == 36) {
        return true;
      }
    }
    return false;
  };
  testTrue(counters,
           cubemap.owner != 0 &&
             !bridge.accept(encode(worldFrame({ skyOp(flat) }))) &&
             bridge.accept(encode(worldFrame({ skyOp(cubemap) }))) &&
             bridge.releaseTexture(cubemap) && skyDrawn() &&
             renderer.frameError().empty(),
           "A cubemap sky is drawn, and kept alive after the guest releases "
           "it; a flat texture is refused");
  testTrue(counters,
           bridge.accept(encode(worldFrame({ skyOp(GuestResourceId{}) }))) &&
             !skyDrawn(),
           "An empty Skybox operation removes the sky");

  bridge.retire();
  testTrue(counters,
           render().empty() && bridge.counters().worldInstances == 1,
           "Retirement stops the world at once");
  return counters.failures == 0;
}

// Frame schema v8: SelectWorld, ShowWorld and DestroyWorld.
static bool
worldAddressingValidation()
{
  TestCounters counters;
  GuestFrame decoded;
  const GuestFrame frame =
    worldFrame({ simple(GuestWorldOp::SelectWorld, 4),
                 materialCreate(1),
                 simple(GuestWorldOp::ShowWorld, 4),
                 simple(GuestWorldOp::ShowWorld, 0),
                 simple(GuestWorldOp::DestroyWorld, 4) });
  const std::vector<std::byte> bytes = encode(frame);
  testTrue(counters,
           GuestFrame::read(bytes, decoded) &&
             decoded.worldOperations.size() == 5 &&
             decoded.worldOperations[0].op == GuestWorldOp::SelectWorld &&
             decoded.worldOperations[0].id == 4 &&
             decoded.worldOperations[3].op == GuestWorldOp::ShowWorld &&
             decoded.worldOperations[3].id == 0 &&
             decoded.worldOperations[4].op == GuestWorldOp::DestroyWorld,
           "World addressing round-trips; ShowWorld may name no world");
  bool denied = true;
  for (const GuestWorldOperation& invalid :
       { simple(GuestWorldOp::SelectWorld, 0),
         simple(GuestWorldOp::DestroyWorld, 0),
         simple(static_cast<GuestWorldOp>(13), 1) }) {
    GuestFrame ignored;
    denied =
      denied && !GuestFrame::read(encode(worldFrame({ invalid })), ignored);
  }
  std::vector<std::byte> version7 = bytes;
  version7[4] = std::byte{ 7 };
  GuestFrame ignored;
  testTrue(counters,
           denied && !GuestFrame::read(version7, ignored),
           "Selecting or destroying world zero, unknown operations and "
           "world addressing in a version 7 frame are denied");
  return counters.failures == 0;
}

// Frame schema v8: each guest scene's world lives on the host; only the
// shown one draws.
static bool
worldsPerScene()
{
  TestCounters counters;
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0, 0), 1, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  renderer.ensureBuiltinStyles();
  WasmFrameRenderer bridge(renderer, 700);

  const std::array<LitVertex, 3> vertices{
    { { { 0.0f, 0.0f, 0.0f }, { 0, 0, 1 }, 0xffffffffu, { 0, 0 } },
      { { 0.5f, 0.0f, 0.0f }, { 0, 0, 1 }, 0xffffffffu, { 1, 0 } },
      { { 0.0f, 0.5f, 0.0f }, { 0, 0, 1 }, 0xffffffffu, { 0, 1 } } }
  };
  const std::array<std::uint32_t, 3> indices{ 0, 1, 2 };
  GuestMeshRequest request;
  request.style = static_cast<std::uint32_t>(GuestBatchStyle::LitMesh);
  request.vertexBytes = sizeof(vertices);
  request.indexBytes = sizeof(indices);
  const GuestResourceId mesh = bridge.createMesh(request);
  GuestMeshWrite vertexWrite;
  vertexWrite.mesh = mesh;
  vertexWrite.bytes.assign(reinterpret_cast<const std::byte*>(vertices.data()),
                           reinterpret_cast<const std::byte*>(vertices.data()) +
                             sizeof(vertices));
  GuestMeshWrite indexWrite;
  indexWrite.mesh = mesh;
  indexWrite.indices = true;
  indexWrite.bytes.assign(reinterpret_cast<const std::byte*>(indices.data()),
                          reinterpret_cast<const std::byte*>(indices.data()) +
                            sizeof(indices));
  bridge.writeMesh(vertexWrite);
  bridge.writeMesh(indexWrite);

  // Instances drawn this frame, per instanced call.
  const std::function<std::vector<unsigned int>()> render = [&]() {
    DrawList scene(&window, &camera);
    bridge.dispatch(scene);
    renderer.BeginFrame();
    renderer.RenderScene(&scene, &camera);
    renderer.EndFrame();
    std::vector<unsigned int> counts;
    for (std::size_t index = 0; index < mock.getLastNonEmptySubmittedCount();
         ++index) {
      const RenderCommand& command = mock.getLastNonEmptySubmitted(index);
      if (command.commandType == CommandType::DrawIndexedInstanced) {
        counts.push_back(command.drawIndexedInstanced.instanceCount);
      }
    }
    return counts;
  };
  GuestWorldOperation opaque = materialCreate(1);
  opaque.material.blend = false;

  testTrue(counters,
           bridge.accept(encode(worldFrame(
             { simple(GuestWorldOp::SelectWorld, 2),
               opaque,
               instanceCreate(10, mesh, 1),
               instanceCreate(11, mesh, 1) }))) &&
             bridge.counters().worldInstances == 2 && render().empty(),
           "SelectWorld creates a world; world 1 stays shown and is empty");
  testTrue(counters,
           bridge.accept(
             encode(worldFrame({ simple(GuestWorldOp::ShowWorld, 2) }))) &&
             render() == std::vector<unsigned int>{ 2 },
           "ShowWorld draws the named world");
  testTrue(counters,
           bridge.accept(encode(worldFrame({ opaque,
                                             instanceCreate(10, mesh, 1) }))) &&
             bridge.counters().worldInstances == 3 &&
             render() == std::vector<unsigned int>{ 2 },
           "A frame without SelectWorld targets world 1, whose ids are its "
           "own, and a hidden world does not draw");
  testTrue(counters,
           bridge.accept(
             encode(worldFrame({ simple(GuestWorldOp::ShowWorld, 1) }))) &&
             render() == std::vector<unsigned int>{ 1 },
           "Showing world 1 again draws only its instance");

  bool denied = true;
  for (const GuestFrame& invalid :
       { worldFrame({ simple(GuestWorldOp::ShowWorld, 5) }),
         worldFrame({ simple(GuestWorldOp::DestroyWorld, 5) }),
         worldFrame({ simple(GuestWorldOp::SelectWorld, 3),
                      simple(GuestWorldOp::DestroyWorld, 3),
                      materialCreate(7) }) }) {
    denied = denied && !bridge.accept(encode(invalid));
  }
  std::vector<GuestWorldOperation> tooMany;
  for (std::uint32_t id = 3; id <= 9; ++id) {
    tooMany.push_back(simple(GuestWorldOp::SelectWorld, id));
  }
  std::vector<GuestWorldOperation> enough(tooMany.begin(), tooMany.end() - 1);
  testTrue(counters,
           denied && !bridge.accept(encode(worldFrame(tooMany))) &&
             bridge.accept(encode(worldFrame(enough))),
           "Unknown worlds, operations on a destroyed world and a ninth world "
           "are denied; eight worlds are accepted");

  GuestFrame destroyFrame;
  std::vector<GuestWorldOperation> destroys;
  for (std::uint32_t id = 3; id <= 8; ++id) {
    destroys.push_back(simple(GuestWorldOp::DestroyWorld, id));
  }
  destroys.push_back(simple(GuestWorldOp::DestroyWorld, 2));
  testTrue(counters,
           bridge.accept(encode(worldFrame(destroys))) &&
             bridge.counters().worldInstances == 1 &&
             render() == std::vector<unsigned int>{ 1 },
           "Destroying worlds releases their instances; the shown one draws");
  testTrue(counters,
           bridge.accept(
             encode(worldFrame({ simple(GuestWorldOp::DestroyWorld, 1) }))) &&
             bridge.counters().worldInstances == 0 && render().empty() &&
             bridge.accept(encode(worldFrame(
               { simple(GuestWorldOp::SelectWorld, 1), opaque }))) &&
             render().empty() && renderer.frameError().empty(),
           "Destroying the shown world shows none until another is shown");
  return counters.failures == 0;
}

bool
runWasmWorldTest(const std::string& name)
{
  if (name == "WorldAddressingValidation") {
    return worldAddressingValidation();
  }
  if (name == "WorldsPerScene") {
    return worldsPerScene();
  }
  if (name == "WorldFrameValidation") {
    return worldFrameValidation();
  }
  if (name == "WorldOperations") {
    return worldOperations();
  }
  return false;
}
