#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/FrameCapture.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestAccess.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/WasmFileServices.h>
#include <Illumo/Wasm/WasmFrameRenderer.h>
#include <Illumo/Wasm/WasmGameModule.h>
#include <Illumo/Wasm/WasmGameServices.h>
#include <IllumoGuest/Clipboard.h>
#include <IllumoGuest/Console.h>
#include <IllumoGuest/Dialog.h>
#include <IllumoGuest/Display.h>
#include <IllumoGuest/Input.h>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>

// TestWasmAudio.cpp: the Audio capability cases.
bool
runWasmAudioTest(const std::string& name);

class MemoryClipboard final : public WasmHostClipboard
{
public:
  std::string text;
  std::string getText() override { return text; }
  bool setText(const std::string& value) override
  {
    text = value;
    return true;
  }
};

class MemoryDialogs final : public WasmHostDialogs
{
public:
  std::string path;
  unsigned int calls = 0;
  std::string pick(bool,
                   const std::string&,
                   const std::string&,
                   const std::string&) override
  {
    ++calls;
    return path;
  }
};

class ThrowingBackend : public MockBackend
{
public:
  int replacementsBeforeThrow = -1;
  bool throwTexture = false;
  bool sawModColor = false;
  std::size_t observedDraws = 0;
  std::size_t uploadedBufferBytes = 0;
  std::vector<std::size_t> writesAfterDraws;
  void PushToCommandQueue(RenderCommand command) override
  {
    if (command.commandType == CommandType::DrawIndexed) {
      ++observedDraws;
    } else if (command.commandType == CommandType::UpdateTexture) {
      writesAfterDraws.push_back(observedDraws);
    } else if (command.commandType == CommandType::UpdateBuffer) {
      uploadedBufferBytes += command.updateBuffer.sizeBytes;
    } else if (command.commandType == CommandType::UpdateIndexBuffer) {
      uploadedBufferBytes += command.updateIndexBuffer.sizeBytes;
    }
    if (command.commandType == CommandType::UpdateBuffer &&
        command.updateBuffer.data != nullptr) {
      const std::byte* bytes =
        static_cast<const std::byte*>(command.updateBuffer.data);
      for (std::size_t offset = 12;
           offset + 4 <= command.updateBuffer.sizeBytes;
           offset += 16) {
        std::uint32_t color = 0;
        std::memcpy(&color, bytes + offset, sizeof(color));
        sawModColor = sawModColor || color == 0xffff77bb;
      }
    }
    MockBackend::PushToCommandQueue(command);
  }
  bool ReplaceMesh(MeshHandle handle,
                   const void* vertices,
                   std::size_t vertexBytes,
                   const void* indices,
                   std::size_t indexBytes,
                   MeshVertexLayout layout,
                   bool dynamic) override
  {
    if (replacementsBeforeThrow == 0) {
      throw std::runtime_error("Injected mesh failure");
    }
    if (replacementsBeforeThrow > 0) {
      --replacementsBeforeThrow;
    }
    return MockBackend::ReplaceMesh(
      handle, vertices, vertexBytes, indices, indexBytes, layout, dynamic);
  }
  TextureHandle CreateTexture(const unsigned char* data,
                              int width,
                              int height,
                              int channels,
                              const TextureOptions& options) override
  {
    if (throwTexture) {
      throw std::runtime_error("Injected texture failure");
    }
    return MockBackend::CreateTexture(data, width, height, channels, options);
  }
};

static GuestFrame
makeFrame()
{
  GuestFrame frame;
  frame.width = 640;
  frame.height = 480;
  GuestBatch batch;
  batch.vertices = { { { 0, 0, 0 }, 0xff0000ff, { 0, 0 } },
                     { { 50, 0, 0 }, 0xff00ff00, { 1, 0 } },
                     { { 0, 50, 0 }, 0xffff0000, { 0, 1 } } };
  batch.indices = { 0, 1, 2 };
  batch.clipped = true;
  batch.clip = { 5, 10, 100, 200 };
  frame.batches.push_back(std::move(batch));
  return frame;
}

static void
rejectsFrame(TestCounters& counters,
             const GuestFrame& candidate,
             const char* label)
{
  GuestWireWriter bytes;
  candidate.write(bytes);
  GuestFrame ignored;
  testTrue(counters, !GuestFrame::read(bytes.data(), ignored), label);
}

static bool
run(const std::string& name)
{
  TestCounters counters;
  GuestFrame frame = makeFrame();
  GuestWireWriter wire;
  frame.write(wire);
  if (name == "FrameValidation") {
    GuestFrame accepted;
    testTrue(counters,
             GuestFrame::read(wire.data(), accepted),
             "Valid geometry packet");
    bool rejected = true;
    for (std::size_t size = 0; size < wire.data().size(); ++size) {
      if (GuestFrame::read(std::span(wire.data()).first(size), accepted)) {
        rejected = false;
      }
    }
    testTrue(counters,
             rejected && accepted.batches.size() == 1,
             "All truncations rejected transactionally");
    frame.batches[0].indices[2] = UINT32_MAX;
    wire.clear();
    frame.write(wire);
    testTrue(counters,
             !GuestFrame::read(wire.data(), accepted),
             "Out-of-range index rejected");
    frame.batches[0].indices[2] = 2;
    frame.batches[0].mvp[5] = std::numeric_limits<float>::quiet_NaN();
    wire.clear();
    frame.write(wire);
    testTrue(counters,
             !GuestFrame::read(wire.data(), accepted),
             "Non-finite matrix rejected");
    frame = makeFrame();
    wire.clear();
    frame.write(wire);
    GuestFrameLimits tiny;
    tiny.vertices = 2;
    testTrue(counters,
             !GuestFrame::read(wire.data(), accepted, tiny),
             "Aggregate vertex quota enforced");
    std::vector<std::byte> mutation = wire.data();
    for (std::size_t index = 16; index < 20; ++index) {
      mutation[index] = std::byte{ 255 };
    }
    testTrue(counters,
             !GuestFrame::read(mutation, accepted),
             "Forged count rejected before allocation");
    // Deterministic byte mutations exercise decoder ranges under ASan.
    for (std::size_t index = 0; index < wire.data().size(); ++index) {
      mutation = wire.data();
      mutation[index] ^= std::byte{ 255 };
      GuestFrame ignored;
      GuestFrame::read(mutation, ignored);
    }

    // Version 1 packets (2D only) remain valid input.
    GuestWireWriter legacy;
    legacy.u32(GuestFrame::Magic);
    legacy.u32(1);
    legacy.f32(640);
    legacy.f32(480);
    legacy.u32(0);
    legacy.u32(0);
    testTrue(counters,
             GuestFrame::read(legacy.data(), accepted) && !accepted.hasCamera,
             "Version 1 frames decode without version 2 records");

    // Version 2: world camera, lit mesh, depth-tested lines, shadow casters.
    GuestFrame world = makeFrame();
    world.batches[0].layer = GuestLayer::World;
    world.hasCamera = true;
    world.camera[0] = 0.5f;
    GuestBatch lit;
    lit.style = GuestBatchStyle::LitMesh;
    lit.layer = GuestLayer::World;
    lit.depthTest = true;
    lit.lighting.castsShadow = true;
    lit.lighting.receivesShadow = true;
    lit.lighting.model[12] = 2.0f;
    lit.vertices = { { { 0, 0, 0 }, 0xffffffff, {}, { 0, 1, 0 } },
                     { { 1, 0, 0 }, 0xffffffff, {}, { 0, 1, 0 } },
                     { { 0, 0, 1 }, 0xffffffff, {}, { 0, 1, 0 } } };
    lit.indices = { 0, 1, 2 };
    GuestBatch lines;
    lines.layer = GuestLayer::World;
    lines.primitive = GuestPrimitive::Lines;
    lines.depthTest = true;
    lines.vertices = { { { 0, 0, 0 } }, { { 0, 1, 0 } } };
    lines.indices = { 0, 1 };
    world.batches.push_back(lit);
    world.batches.push_back(lines);
    world.shadowCasters.push_back({ { -1, -1, -1 }, { 1, 1, 1 } });
    GuestWireWriter worldWire;
    world.write(worldWire);
    testTrue(counters,
             GuestFrame::read(worldWire.data(), accepted) &&
               accepted.hasCamera && accepted.camera[0] == 0.5f &&
               accepted.batches.size() == 3 &&
               accepted.batches[1].style == GuestBatchStyle::LitMesh &&
               accepted.batches[1].lighting.castsShadow &&
               accepted.batches[1].lighting.model[12] == 2.0f &&
               accepted.batches[1].vertices[0].normal[1] == 1.0f &&
               accepted.batches[2].primitive == GuestPrimitive::Lines &&
               accepted.shadowCasters.size() == 1,
             "Version 2 world records round-trip");
    for (std::size_t size = 0; size < worldWire.data().size(); ++size) {
      if (GuestFrame::read(std::span(worldWire.data()).first(size), accepted)) {
        rejected = false;
      }
    }
    testTrue(counters, rejected, "Version 2 truncations rejected");
    GuestFrame invalid = world;
    invalid.batches[1].layer = GuestLayer::Ui;
    rejectsFrame(counters, invalid, "Lit meshes are world-layer only");
    invalid = world;
    invalid.batches[2].style = GuestBatchStyle::Sprite;
    invalid.batches[2].texture = { 1, GuestResourceKind::Texture, 1, 1 };
    rejectsFrame(counters, invalid, "Only shape batches draw lines");
    invalid = world;
    invalid.batches[2].indices = { 0, 1, 0 };
    rejectsFrame(counters, invalid, "Line index counts must be even");
    invalid = world;
    invalid.shadowCasters[0].mapSize = 16;
    rejectsFrame(counters, invalid, "Shadow map sizes are bounded");
    invalid = world;
    invalid.shadowCasters[0].boundsMin[0] = 5;
    rejectsFrame(counters, invalid, "Inverted caster bounds rejected");
    invalid = world;
    invalid.batches[1].lighting.tint[0] =
      std::numeric_limits<float>::infinity();
    rejectsFrame(counters, invalid, "Non-finite lighting rejected");
    invalid = world;
    invalid.shadowCasters.resize(257, world.shadowCasters[0]);
    rejectsFrame(counters, invalid, "Shadow caster count is bounded");

    // Version 2 packets without version 3 fields remain valid input.
    GuestWireWriter version2;
    version2.u32(GuestFrame::Magic);
    version2.u32(2);
    version2.f32(640);
    version2.f32(480);
    version2.u32(0);
    for (unsigned int value = 0; value < 16; ++value) {
      version2.f32(0);
    }
    version2.u32(0);
    version2.u32(0);
    version2.u32(0);
    testTrue(counters,
             GuestFrame::read(version2.data(), accepted) &&
               accepted.batches.empty(),
             "Version 2 frames decode without version 3 records");

    // Version 3: blend flag, retained meshes and the cubemap skybox.
    GuestFrame retained = makeFrame();
    retained.batches[0].vertices.clear();
    retained.batches[0].indices.clear();
    retained.batches[0].mesh = { 7, GuestResourceKind::Mesh, 1, 1 };
    retained.batches[0].firstIndex = 3;
    retained.batches[0].indexCount = 6;
    retained.batches[0].blend = true;
    GuestBatch sky;
    sky.style = GuestBatchStyle::Skybox;
    sky.layer = GuestLayer::World;
    sky.texture = { 7, GuestResourceKind::Texture, 2, 1 };
    sky.lighting.tint = { 0.5f, 0.6f, 0.7f, 1.0f };
    sky.vertices = { { { -1, -1, -1 } }, { { 1, -1, -1 } }, { { 1, 1, -1 } } };
    sky.indices = { 0, 1, 2 };
    retained.batches.insert(retained.batches.begin(), sky);
    GuestWireWriter retainedWire;
    retained.write(retainedWire);
    testTrue(
      counters,
      GuestFrame::read(retainedWire.data(), accepted) &&
        accepted.batches.size() == 2 &&
        accepted.batches[0].style == GuestBatchStyle::Skybox &&
        accepted.batches[0].lighting.tint[2] == 0.7f &&
        accepted.batches[1].retained() && accepted.batches[1].firstIndex == 3 &&
        accepted.batches[1].indexCount == 6 &&
        accepted.batches[1].drawCount() == 6 && accepted.batches[1].blend &&
        accepted.batches[1].hasBlend && accepted.batches[1].vertices.empty(),
      "Version 3 retained and skybox batches round-trip");
    for (std::size_t size = 0; size < retainedWire.data().size(); ++size) {
      if (GuestFrame::read(std::span(retainedWire.data()).first(size),
                           accepted)) {
        rejected = false;
      }
    }
    testTrue(counters, rejected, "Version 3 truncations rejected");
    invalid = retained;
    invalid.batches[1].indexCount = 4;
    rejectsFrame(counters, invalid, "Retained index counts are whole");
    invalid = retained;
    invalid.batches[1].mesh.kind = GuestResourceKind::Texture;
    rejectsFrame(counters, invalid, "Retained batches name a mesh");
    invalid = retained;
    invalid.batches[0].layer = GuestLayer::Ui;
    invalid.batches[1].layer = GuestLayer::Ui;
    rejectsFrame(counters, invalid, "Skyboxes are world-layer only");
    invalid = retained;
    invalid.batches[0].mesh = { 7, GuestResourceKind::Mesh, 1, 1 };
    invalid.batches[0].vertices.clear();
    invalid.batches[0].indices.clear();
    invalid.batches[0].indexCount = 3;
    rejectsFrame(counters, invalid, "Skyboxes are never retained");
    invalid = makeFrame();
    invalid.batches[0].firstIndex = 1;
    rejectsFrame(counters, invalid, "Inline batches start at index zero");
    invalid = retained;
    invalid.batches[0].texture = {};
    rejectsFrame(counters, invalid, "Skyboxes need a cubemap texture");
    bool inlineRetainedRefused = false;
    try {
      invalid = retained;
      invalid.batches[1].vertices = makeFrame().batches[0].vertices;
      GuestWireWriter refused;
      invalid.write(refused);
    } catch (const std::length_error&) {
      inlineRetainedRefused = true;
    }
    testTrue(counters,
             inlineRetainedRefused,
             "Writers refuse retained batches with inline geometry");
    return counters.failures == 0;
  }
  if (name == "RetainedResources") {
    NullRenderWindow window(640, 480);
    EnvVars env;
    env.setVar("WinX", 640);
    env.setVar("WinY", 480);
    Camera camera(glm::vec2(0, 0), 1, &env);
    ThrowingBackend mock;
    mock.Initialize();
    Renderer renderer(&window, &env, &camera, &mock, false);
    renderer.ensureBuiltinStyles();
    WasmFrameRenderer bridge(renderer, 700);
    // A retained Shape mesh: three 16-byte vertices and one triangle.
    struct ShapeVertex
    {
      float x, y, z;
      std::uint32_t rgba;
    };
    const std::array<ShapeVertex, 3> vertices{ { { 0, 0, 0, 0xffffffffu },
                                                 { 50, 0, 0, 0xffffffffu },
                                                 { 0, 50, 0, 0xffffffffu } } };
    const std::array<std::uint32_t, 3> indices{ 0, 1, 2 };
    GuestMeshRequest request;
    request.style = static_cast<std::uint32_t>(GuestBatchStyle::Shape);
    request.vertexBytes = sizeof(vertices);
    request.indexBytes = sizeof(indices);
    const GuestResourceId mesh = bridge.createMesh(request);
    GuestMeshWrite write;
    write.mesh = mesh;
    write.bytes.assign(reinterpret_cast<const std::byte*>(vertices.data()),
                       reinterpret_cast<const std::byte*>(vertices.data()) +
                         sizeof(vertices));
    GuestMeshWrite outOfOrder = write;
    outOfOrder.offset = 16;
    GuestMeshWrite indexWrite;
    indexWrite.mesh = mesh;
    indexWrite.indices = true;
    indexWrite.bytes.assign(reinterpret_cast<const std::byte*>(indices.data()),
                            reinterpret_cast<const std::byte*>(indices.data()) +
                              sizeof(indices));
    GuestFrame drawn = makeFrame();
    drawn.batches[0].vertices.clear();
    drawn.batches[0].indices.clear();
    drawn.batches[0].mesh = mesh;
    drawn.batches[0].indexCount = 3;
    GuestWireWriter drawnWire;
    drawn.write(drawnWire);
    testTrue(counters,
             mesh.owner == 700 && mesh.kind == GuestResourceKind::Mesh &&
               !bridge.accept(drawnWire.data()),
             "An incomplete retained mesh cannot be drawn");
    testTrue(counters,
             bridge.writeMesh(write) && bridge.writeMesh(indexWrite),
             "Retained mesh bytes complete in order");
    testTrue(
      counters, bridge.accept(drawnWire.data()), "Retained draw accepted");
    mock.observedDraws = 0;
    {
      Scene scene(&window, &camera);
      bridge.dispatch(scene);
      renderer.BeginFrame();
      renderer.RenderScene(&scene, &camera);
      renderer.EndFrame();
    }
    testTrue(counters,
             renderer.frameError().empty() && mock.observedDraws == 1,
             "Retained mesh draws without inline geometry");
    drawn.batches[0].firstIndex = 3;
    drawnWire.clear();
    drawn.write(drawnWire);
    testTrue(counters,
             !bridge.accept(drawnWire.data()),
             "Retained index ranges are bounded by the mesh");
    drawn.batches[0].firstIndex = 0;
    drawn.batches[0].style = GuestBatchStyle::LitMesh;
    drawn.batches[0].layer = GuestLayer::World;
    drawnWire.clear();
    drawn.write(drawnWire);
    testTrue(counters,
             !bridge.accept(drawnWire.data()),
             "Retained meshes keep their style's layout");
    const GuestResourceId broken = bridge.createMesh(request);
    const std::array<std::uint32_t, 3> escaping{ 0, 1, 9 };
    GuestMeshWrite brokenVertices = write;
    brokenVertices.mesh = broken;
    GuestMeshWrite brokenIndices = indexWrite;
    brokenIndices.mesh = broken;
    brokenIndices.bytes.assign(
      reinterpret_cast<const std::byte*>(escaping.data()),
      reinterpret_cast<const std::byte*>(escaping.data()) + sizeof(escaping));
    testTrue(counters,
             !bridge.writeMesh(outOfOrder) &&
               bridge.writeMesh(brokenVertices) &&
               !bridge.writeMesh(brokenIndices),
             "Out-of-order bytes and escaping indices are rejected");
    testTrue(counters,
             bridge.releaseMesh(mesh) && !bridge.releaseMesh(mesh),
             "Retained meshes release once");

    // Frame schema v4: a dynamic mesh is ready (zero-filled) on creation,
    // is written in place by frame mesh writes, and uploads only the spans
    // written since the previous frame.
    GuestMeshRequest dynamicRequest = request;
    dynamicRequest.dynamic = true;
    GuestWireWriter encodedRequest;
    dynamicRequest.write(encodedRequest);
    GuestMeshRequest decodedRequest;
    GuestMeshRequest litRequest = dynamicRequest;
    litRequest.style = static_cast<std::uint32_t>(GuestBatchStyle::LitMesh);
    litRequest.vertexBytes = 36;
    GuestWireWriter encodedLit;
    litRequest.write(encodedLit);
    GuestMeshRequest decodedLit;
    testTrue(counters,
             GuestMeshRequest::read(encodedRequest.data(), decodedRequest) &&
               decodedRequest.dynamic &&
               !GuestMeshRequest::read(encodedLit.data(), decodedLit),
             "Dynamic mesh requests round-trip; lit meshes cannot be dynamic");
    const GuestResourceId dynamicMesh = bridge.createMesh(dynamicRequest);
    GuestFrame dynamicFrame = makeFrame();
    dynamicFrame.batches[0].vertices.clear();
    dynamicFrame.batches[0].indices.clear();
    dynamicFrame.batches[0].mesh = dynamicMesh;
    dynamicFrame.batches[0].indexCount = 3;
    dynamicFrame.meshWrites.push_back({ dynamicMesh, false, 0, write.bytes });
    dynamicFrame.meshWrites.push_back(
      { dynamicMesh, true, 0, indexWrite.bytes });
    GuestWireWriter dynamicWire;
    dynamicFrame.write(dynamicWire);
    const std::function<bool(std::span<const std::byte>)> renderDynamic =
      [&](std::span<const std::byte> packet) {
        if (!bridge.accept(packet)) {
          return false;
        }
        mock.observedDraws = 0;
        mock.uploadedBufferBytes = 0;
        Scene scene(&window, &camera);
        bridge.dispatch(scene);
        renderer.BeginFrame();
        renderer.RenderScene(&scene, &camera);
        renderer.EndFrame();
        return renderer.frameError().empty() && mock.observedDraws == 1;
      };
    testTrue(counters,
             dynamicMesh.owner == 700 && renderDynamic(dynamicWire.data()) &&
               mock.uploadedBufferBytes == sizeof(vertices) + sizeof(indices),
             "A dynamic mesh draws after its first frame writes");
    dynamicFrame.meshWrites.clear();
    dynamicWire.clear();
    dynamicFrame.write(dynamicWire);
    testTrue(counters,
             renderDynamic(dynamicWire.data()) && mock.uploadedBufferBytes == 0,
             "An unchanged dynamic mesh uploads nothing");
    GuestFrameMeshWrite oneVertex{ dynamicMesh,
                                   false,
                                   16,
                                   { write.bytes.begin() + 16,
                                     write.bytes.begin() + 32 } };
    dynamicFrame.meshWrites.push_back(oneVertex);
    dynamicWire.clear();
    dynamicFrame.write(dynamicWire);
    testTrue(counters,
             renderDynamic(dynamicWire.data()) &&
               mock.uploadedBufferBytes == 16,
             "Only the written span uploads");
    const std::uint32_t escapingIndex = 9;
    const float notFinite = std::numeric_limits<float>::infinity();
    GuestFrameMeshWrite badIndex{ dynamicMesh, true, 0, {} };
    badIndex.bytes.resize(4);
    std::memcpy(badIndex.bytes.data(), &escapingIndex, 4);
    GuestFrameMeshWrite badPosition = oneVertex;
    std::memcpy(badPosition.bytes.data(), &notFinite, 4);
    GuestFrameMeshWrite misaligned = oneVertex;
    misaligned.offset = 8;
    GuestFrameMeshWrite outside = oneVertex;
    outside.offset = 48;
    GuestFrameMeshWrite staticTarget = oneVertex;
    staticTarget.mesh = broken;
    GuestFrameMeshWrite stale = oneVertex;
    stale.mesh.generation += 1;
    bool denied = true;
    for (const GuestFrameMeshWrite& invalid :
         { badIndex, badPosition, misaligned, outside, staticTarget, stale }) {
      GuestFrame rejected = dynamicFrame;
      rejected.meshWrites = { invalid };
      GuestWireWriter rejectedWire;
      rejected.write(rejectedWire);
      denied = denied && !bridge.accept(rejectedWire.data());
    }
    testTrue(counters,
             denied,
             "Escaping indices, non-finite positions, misaligned, outside, "
             "static and stale mesh writes are denied");
    GuestFrame version3 = makeFrame();
    GuestWireWriter version3Wire;
    version3.write(version3Wire);
    std::vector<std::byte> version3Bytes = version3Wire.take();
    // Rewrite as version 3: drop the empty v4 write and v5 surface sections.
    version3Bytes[4] = std::byte{ 3 };
    version3Bytes.resize(version3Bytes.size() - 8);
    testTrue(counters,
             bridge.accept(version3Bytes),
             "Version 3 frames without mesh writes remain accepted");

    // A cubemap sampled by a skybox, and nothing else.
    const std::uint32_t size = 4;
    std::vector<std::byte> faces(
      static_cast<std::size_t>(GuestCubemapRequest::bytesFor(size)),
      std::byte{ 128 });
    const GuestResourceId cubemap = bridge.createCubemap(faces, size);
    GuestFrame skyFrame;
    skyFrame.width = 640;
    skyFrame.height = 480;
    GuestBatch sky;
    sky.style = GuestBatchStyle::Skybox;
    sky.layer = GuestLayer::World;
    sky.texture = cubemap;
    sky.vertices = { { { -1, -1, -1 } }, { { 1, -1, -1 } }, { { 1, 1, -1 } } };
    sky.indices = { 0, 1, 2 };
    skyFrame.batches.push_back(sky);
    GuestWireWriter skyWire;
    skyFrame.write(skyWire);
    testTrue(counters,
             cubemap.owner == 700 && bridge.accept(skyWire.data()),
             "Skybox samples a guest cubemap");
    mock.observedDraws = 0;
    {
      Scene scene(&window, &camera);
      bridge.dispatch(scene);
      renderer.BeginFrame();
      renderer.RenderScene(&scene, &camera);
      renderer.EndFrame();
    }
    testTrue(counters,
             renderer.frameError().empty() && mock.observedDraws == 1,
             "Skybox batch draws through the built-in style");
    skyFrame.batches[0].style = GuestBatchStyle::Sprite;
    skyFrame.batches[0].layer = GuestLayer::Ui;
    skyWire.clear();
    skyFrame.write(skyWire);
    testTrue(counters,
             !bridge.accept(skyWire.data()),
             "Cubemaps cannot be sampled as 2D textures");
    skyFrame.batches.clear();
    skyFrame.textureWrites.push_back(
      { cubemap, 0, 0, 1, 1, 4, std::vector<std::byte>(4), 0 });
    skyWire.clear();
    skyFrame.write(skyWire);
    testTrue(counters,
             !bridge.accept(skyWire.data()),
             "Cubemaps are never written through frames");
    testTrue(counters,
             !bridge.createCubemap(faces, size + 1).owner,
             "Cubemap byte counts must match their size");
    return counters.failures == 0;
  }
  if (name != "FrameRendering" && name != "FrameFailures" &&
      name != "GameHost" && name != "ModIsolation" &&
      name != "RenderServices" && name != "GuestPresentation" &&
      name != "GameJobs" && name != "SdkContract" && name != "GameFiles" &&
      name != "DisplayServices" && name != "ClipboardServices" &&
      name != "ConsoleServices" && name != "DialogServices") {
    return false;
  }
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0, 0), 1, &env);
  ThrowingBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  renderer.ensureBuiltinStyles();
  if (name == "DisplayServices") {
    WasmFrameRenderer bridge(renderer, 322);
    WasmGameServices denied(
      bridge, 0, ILLUMO_ENGINE_ASSETS, {}, {}, &window, &env);
    WasmGameServices permitted(
      bridge,
      static_cast<std::uint32_t>(GuestCapability::Display),
      ILLUMO_ENGINE_ASSETS,
      {},
      {},
      &window,
      &env);
    GuestServices requests;
    GuestWireWriter payload;
    GuestDisplayRequest{ true, { false, false, 120, 2 } }.write(payload);
    requests.records.push_back({ 1,
                                 GuestService::Display,
                                 GuestServiceStatus::Request,
                                 payload.take() });
    GuestWireWriter displayBatch;
    requests.write(displayBatch);
    std::vector<std::byte> completion;
    GuestServices result;
    const std::string initialFps = env.getVar("fps").value;
    testTrue(counters,
             denied.process(displayBatch.data(), completion) &&
               GuestServices::read(completion, result, false) &&
               result.records.size() == 1 &&
               result.records.front().status == GuestServiceStatus::Rejected &&
               env.getVar("fps").value == initialFps,
             "Display capability denial has no side effect");
    testTrue(counters,
             permitted.process(displayBatch.data(), completion) &&
               GuestServices::read(completion, result, false) &&
               result.records.size() == 1 &&
               result.records.front().status == GuestServiceStatus::Complete &&
               env.getVar("fps").valueAsLong == 120 &&
               !env.getVar("vsync").valueAsBool,
             "Absolute display settings reach host configuration");
    requests.records.front().request = 2;
    GuestWireWriter changed;
    GuestDisplayRequest{ true, { false, true, 60, 1 } }.write(changed);
    requests.records.front().payload = changed.take();
    requests.records.push_back(
      { 3, GuestService::Display, GuestServiceStatus::Request, {} });
    displayBatch.clear();
    requests.write(displayBatch);
    testTrue(counters,
             !permitted.process(displayBatch.data(), completion) &&
               env.getVar("fps").valueAsLong == 120,
             "Malformed display batch rejects before mutations");
    requests.records.pop_back();
    requests.records.push_back(requests.records.front());
    requests.records.back().request = 3;
    displayBatch.clear();
    requests.write(displayBatch);
    testTrue(counters,
             permitted.process(displayBatch.data(), completion) &&
               GuestServices::read(completion, result, false) &&
               result.records.size() == 1,
             "At most one display operation executes per frame");
    displayBatch.clear();
    GuestServices{}.write(displayBatch);
    testTrue(counters,
             permitted.process(displayBatch.data(), completion) &&
               GuestServices::read(completion, result, false) &&
               result.records.size() == 1 &&
               result.records.front().request == 3,
             "Queued display request completes on later frame");
    return counters.failures == 0;
  }
  if (name == "ClipboardServices") {
    MemoryClipboard clipboard;
    clipboard.text = "prior";
    WasmFrameRenderer bridge(renderer, 323);
    WasmGameServices denied(bridge,
                            0,
                            ILLUMO_ENGINE_ASSETS,
                            {},
                            {},
                            &window,
                            &env,
                            nullptr,
                            nullptr,
                            &clipboard);
    WasmGameServices permitted(
      bridge,
      static_cast<std::uint32_t>(GuestCapability::Clipboard),
      ILLUMO_ENGINE_ASSETS,
      {},
      {},
      &window,
      &env,
      nullptr,
      nullptr,
      &clipboard);
    GuestServices requests;
    GuestWireWriter payload;
    GuestClipboardRequest{ true, "copied" }.write(payload);
    requests.records.push_back({ 1,
                                 GuestService::Clipboard,
                                 GuestServiceStatus::Request,
                                 payload.take() });
    GuestWireWriter batch;
    requests.write(batch);
    std::vector<std::byte> completion;
    GuestServices result;
    testTrue(counters,
             denied.process(batch.data(), completion) &&
               GuestServices::read(completion, result, false) &&
               result.records.size() == 1 &&
               result.records.front().status == GuestServiceStatus::Rejected &&
               clipboard.text == "prior",
             "Clipboard capability denial has no side effect");
    testTrue(counters,
             permitted.process(batch.data(), completion) &&
               GuestServices::read(completion, result, false) &&
               result.records.size() == 1 &&
               result.records.front().status == GuestServiceStatus::Complete &&
               clipboard.text == "copied",
             "Clipboard set reaches the host store");
    GuestClipboardRequest actual;
    testTrue(
      counters,
      GuestClipboardRequest::read(result.records.front().payload, actual) &&
        !actual.set && actual.text == "copied",
      "Clipboard completion returns copied text");
    requests.records.front().request = 2;
    GuestWireWriter changed;
    GuestClipboardRequest{ true, "later" }.write(changed);
    requests.records.front().payload = changed.take();
    requests.records.push_back(
      { 3, GuestService::Clipboard, GuestServiceStatus::Request, {} });
    batch.clear();
    requests.write(batch);
    testTrue(counters,
             !permitted.process(batch.data(), completion) &&
               clipboard.text == "copied",
             "Malformed clipboard batch rejects before mutations");
    return counters.failures == 0;
  }
  if (name == "ConsoleServices") {
    CommandRegistry registry;
    WasmFrameRenderer bridge(renderer, 324);
    WasmGameServices denied(
      bridge, 0, ILLUMO_ENGINE_ASSETS, {}, {}, &window, &env, &registry);
    WasmGameServices permitted(
      bridge,
      static_cast<std::uint32_t>(GuestCapability::Console),
      ILLUMO_ENGINE_ASSETS,
      {},
      {},
      &window,
      &env,
      &registry);
    GuestConsoleRequest add;
    add.action = GuestConsoleAction::Register;
    add.name = "ruleset";
    add.usage = "ruleset [name]";
    add.description = "Show or change the ruleset";
    GuestServices requests;
    GuestWireWriter payload;
    add.write(payload);
    requests.records.push_back({ 1,
                                 GuestService::Console,
                                 GuestServiceStatus::Request,
                                 payload.take() });
    GuestWireWriter batch;
    requests.write(batch);
    std::vector<std::byte> completion;
    GuestServices result;
    testTrue(counters,
             denied.process(batch.data(), completion) &&
               GuestServices::read(completion, result, false) &&
               result.records.size() == 1 &&
               result.records.front().status == GuestServiceStatus::Rejected &&
               !registry.HasCommand("ruleset"),
             "Console capability denial does not register commands");
    GuestConsoleRequest listen;
    listen.action = GuestConsoleAction::Listen;
    payload.clear();
    listen.write(payload);
    requests.records.push_back({ 2,
                                 GuestService::Console,
                                 GuestServiceStatus::Request,
                                 payload.take() });
    batch.clear();
    requests.write(batch);
    testTrue(counters,
             permitted.process(batch.data(), completion) &&
               GuestServices::read(completion, result, false) &&
               result.records.size() == 1 &&
               result.records.front().status == GuestServiceStatus::Complete &&
               registry.HasCommand("ruleset"),
             "Guest command metadata is registered natively");
    testTrue(counters,
             registry.QueueCommand("ruleset", { "WIREWORLD" }) &&
               (registry.ExecuteQueue(), true),
             "Native console trampoline queues a guest invocation");
    batch.clear();
    GuestServices{}.write(batch);
    testTrue(counters,
             permitted.process(batch.data(), completion) &&
               GuestServices::read(completion, result, false) &&
               result.records.size() == 1 &&
               result.records.front().request == 2,
             "Standing listen completes with the native invocation");
    GuestConsoleRequest invocation;
    testTrue(
      counters,
      GuestConsoleRequest::read(result.records.front().payload, invocation) &&
        invocation.name == "ruleset" && invocation.arguments.size() == 1 &&
        invocation.arguments.front() == "WIREWORLD",
      "Invocation payload carries command name and arguments");
    permitted.cancel();
    testTrue(counters,
             !registry.HasCommand("ruleset"),
             "Retired guest unregisters native command trampolines");
    return counters.failures == 0;
  }
  if (name == "DialogServices") {
    const std::filesystem::path root = std::filesystem::absolute(
      "dialog-service-" +
      std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(root)) {
      return false;
    }
    std::filesystem::create_directory(root / "package");
    std::filesystem::create_directory(root / "storage");
    const std::filesystem::path selected = root / "world.csim";
    std::ofstream(selected, std::ios::binary) << "grid";
    MemoryDialogs dialogs;
    dialogs.path = selected.string();
    std::unique_ptr<WasmFileServices> selectedFiles =
      std::make_unique<WasmFileServices>(
        325,
        static_cast<std::uint32_t>(GuestCapability::Assets) |
          static_cast<std::uint32_t>(GuestCapability::Storage) |
          static_cast<std::uint32_t>(GuestCapability::SelectedFiles),
        root / "package",
        root / "storage");
    WasmFrameRenderer bridge(renderer, 325);
    WasmGameServices denied(bridge,
                            0,
                            ILLUMO_ENGINE_ASSETS,
                            {},
                            {},
                            &window,
                            &env,
                            nullptr,
                            nullptr,
                            nullptr,
                            &dialogs);
    WasmGameServices permitted(
      bridge,
      static_cast<std::uint32_t>(GuestCapability::SelectedFiles),
      ILLUMO_ENGINE_ASSETS,
      {},
      std::move(selectedFiles),
      &window,
      &env,
      nullptr,
      nullptr,
      nullptr,
      &dialogs);
    GuestDialogRequest load;
    load.description = "CSim Simulation";
    load.defaultName = "MyCanvas.illumo";
    load.pattern = "*.ILLUMO";
    GuestServices requests;
    GuestWireWriter payload;
    load.write(payload);
    requests.records.push_back(
      { 1, GuestService::Dialog, GuestServiceStatus::Request, payload.take() });
    GuestWireWriter batch;
    requests.write(batch);
    std::vector<std::byte> completion;
    GuestServices result;
    testTrue(counters,
             denied.process(batch.data(), completion) &&
               GuestServices::read(completion, result, false) &&
               result.records.front().status == GuestServiceStatus::Rejected &&
               dialogs.calls == 0,
             "Dialog capability denial does not open a picker");
    testTrue(counters,
             permitted.process(batch.data(), completion) &&
               GuestServices::read(completion, result, false) &&
               result.records.front().status == GuestServiceStatus::Complete &&
               dialogs.calls == 1,
             "Load dialog grants a selected file name");
    GuestDialogResult granted;
    testTrue(counters,
             GuestDialogResult::read(result.records.front().payload, granted) &&
               granted.outcome == GuestFileOutcome::Success &&
               !granted.writing && granted.name == "sel-1",
             "Dialog completion hides the host path");
    dialogs.path.clear();
    requests.records.front().request = 2;
    payload.clear();
    load.write(payload);
    requests.records.front().payload = payload.take();
    batch.clear();
    requests.write(batch);
    testTrue(
      counters,
      permitted.process(batch.data(), completion) &&
        GuestServices::read(completion, result, false) &&
        GuestDialogResult::read(result.records.front().payload, granted) &&
        granted.outcome == GuestFileOutcome::Cancelled,
      "Empty picker result is a cancellation");
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    return counters.failures == 0;
  }
  if (name == "RenderServices") {
    WasmFrameRenderer bridge(renderer, 321);
    WasmRenderServices services(
      bridge,
      static_cast<std::uint32_t>(GuestCapability::Render) |
        static_cast<std::uint32_t>(GuestCapability::Assets),
      ILLUMO_ENGINE_ASSETS);
    GuestServiceQueue queue;
    GuestTextureRequest texture;
    texture.width = 1;
    texture.height = 1;
    texture.channels = 4;
    texture.pixels.assign(4, std::byte{ 255 });
    GuestWireWriter payload;
    texture.write(payload);
    const std::uint64_t textureRequest =
      queue.enqueue(GuestService::CreateTexture, payload.take());
    GuestFontRequest font;
    payload.clear();
    font.write(payload);
    const std::uint64_t fontRequest =
      queue.enqueue(GuestService::LoadFont, payload.take());
    GuestWireWriter empty;
    GuestServices{}.write(empty);
    std::vector<std::byte> requests, completions;
    testTrue(counters,
             queue.exchange(empty.data(), requests) &&
               services.process(requests, completions),
             "Resource request round trip");
    testTrue(counters,
             queue.exchange(completions, requests),
             "Deferred completions delivered");
    GuestServiceRecord completed;
    testTrue(counters,
             queue.take(textureRequest, completed) &&
               completed.status == GuestServiceStatus::Complete,
             "Texture request completes");
    GuestWireReader created(completed.payload);
    const GuestResourceId id = GuestResourceId::read(created);
    testTrue(counters,
             created.finished() && id.owner == 321,
             "Texture authority scoped to caller");
    testTrue(counters,
             queue.take(fontRequest, completed) &&
               completed.status == GuestServiceStatus::Complete,
             "Engine font request completes");
    GuestFont received;
    testTrue(counters,
             GuestFont::read(completed.payload, received) &&
               received.atlas.owner == 321 && received.glyphs.size() == 95,
             "Copied immutable glyph metrics and scoped atlas");
    Font native;
    testTrue(counters,
             native.loadFile("Assets/Fonts/Space_Mono/SpaceMono-Bold.ttf", 32),
             "Reference font available");
    float advance = 0;
    for (char character : std::string("CSim palette")) {
      for (const GuestGlyph& glyph : received.glyphs) {
        if (glyph.codepoint == static_cast<unsigned char>(character)) {
          advance += glyph.values[8];
        }
      }
    }
    testTrue(counters,
             advance == native.measureText("CSim palette", 32).width,
             "Transferred metrics preserve native layout width");
    GuestServices malformed;
    payload.clear();
    texture.write(payload);
    malformed.records.push_back({ 3,
                                  GuestService::CreateTexture,
                                  GuestServiceStatus::Request,
                                  payload.take() });
    malformed.records.push_back(
      { 4, GuestService::CreateTexture, GuestServiceStatus::Request, {} });
    payload.clear();
    malformed.write(payload);
    const std::size_t creates = mock.getCreateCount();
    testTrue(counters,
             !services.process(payload.data(), completions) &&
               mock.getCreateCount() == creates,
             "Entire batch validated before mutation");
    GuestServices denied;
    payload.clear();
    GuestFontRequest{ "../../private.ttf", 32 }.write(payload);
    denied.records.push_back({ 3,
                               GuestService::LoadFont,
                               GuestServiceStatus::Request,
                               payload.take() });
    payload.clear();
    denied.write(payload);
    GuestServices deniedResult;
    testTrue(counters,
             services.process(payload.data(), completions) &&
               GuestServices::read(completions, deniedResult, false) &&
               deniedResult.records[0].status == GuestServiceStatus::Rejected,
             "Font request cannot escape the engine font catalog");
    testTrue(counters,
             !services.process(payload.data(), completions),
             "Resource request replay rejected");
    // The variable family takes a weight and an optional glyph subset.
    std::uint64_t nextRequest = 10;
    const std::function<bool(const std::string&, GuestFont*)> loadNamed =
      [&](const std::string& fontName, GuestFont* description) {
        GuestServices named;
        GuestWireWriter namedPayload;
        GuestFontRequest{ fontName, 48 }.write(namedPayload);
        named.records.push_back({ nextRequest++,
                                  GuestService::LoadFont,
                                  GuestServiceStatus::Request,
                                  namedPayload.take() });
        GuestWireWriter batchBytes;
        named.write(batchBytes);
        GuestServices namedResult;
        if (!services.process(batchBytes.data(), completions) ||
            !GuestServices::read(completions, namedResult, false) ||
            namedResult.records.size() != 1 ||
            namedResult.records[0].status != GuestServiceStatus::Complete) {
          return false;
        }
        return description == nullptr ||
               GuestFont::read(namedResult.records[0].payload, *description);
      };
    GuestFont word;
    testTrue(counters,
             loadNamed("kikuta:700:CSIM", &word) && word.glyphs.size() == 6u,
             "A weighted subset rasterizes only its glyphs, space and '?'");
    GuestFont full;
    testTrue(counters,
             loadNamed("kikuta:400", &full) && full.glyphs.size() == 95u &&
               loadNamed("kikuta", nullptr),
             "The whole family loads at a weight or its default");
    bool refused = true;
    for (const char* invalid : { "kikuta:0",
                                 "kikuta:1001",
                                 "kikuta:40a",
                                 "kikuta:",
                                 "kikuta:400:",
                                 "kikuta:400:\x01",
                                 "mono-bold:400" }) {
      refused = refused && !loadNamed(invalid, nullptr);
    }
    testTrue(counters,
             refused,
             "Weights outside 1..1000, bad subsets and weighted fixed faces "
             "are refused");
    return counters.failures == 0;
  }
  if (name == "GameHost" || name == "ModIsolation" ||
      name == "GuestPresentation" || name == "GameJobs" ||
      name == "SdkContract" || name == "GameFiles") {
    std::ifstream binary(name == "GuestPresentation" ? ILLUMO_PRESENTATION_GUEST
                         : name == "GameJobs"        ? ILLUMO_JOB_CONTROL_GUEST
                         : name == "SdkContract"     ? ILLUMO_SDK_CONTRACT_GUEST
                         : name == "GameFiles"       ? ILLUMO_FILE_CONTROL_GUEST
                                                     : ILLUMO_PADDLE_GUEST,
                         std::ios::binary);
    const std::vector<char> characters{ std::istreambuf_iterator<char>(binary),
                                        {} };
    std::vector<std::byte> module(characters.size());
    std::memcpy(module.data(), characters.data(), characters.size());
    InputManager input(nullptr);
    InputManagerTestAccess::setAction(input, KeyCode::F1, InputAction::Hold);
    testTrue(counters,
             input.frameAction(KeyCode::F1) == InputAction::Hold,
             "Snapshot observes published hold state, not raw platform Press");
    input.suppressKeyForFrame(KeyCode::F1);
    testTrue(counters,
             input.frameAction(KeyCode::F1) == InputAction::None,
             "Snapshot preserves optional-overlay keyboard suppression");
    IllumoContext context;
    context.renderer = &renderer;
    context.window = &window;
    context.inputManager = &input;
    context.envVars = &env;
    env.setVar("fullscreen", false);
    if (name == "ModIsolation") {
      for (const char* modPath :
           { ILLUMO_PALETTE_MOD, ILLUMO_FAULTY_MOD, ILLUMO_PADDLE_GUEST }) {
        std::ifstream modBinary(modPath, std::ios::binary);
        const std::vector<char> modCharacters{
          std::istreambuf_iterator<char>(modBinary), {}
        };
        std::vector<std::byte> mod(modCharacters.size());
        std::memcpy(mod.data(), modCharacters.data(), modCharacters.size());
        WasmGameModule modded(module, {}, {}, std::move(mod));
        testTrue(counters,
                 modded.Start(&context),
                 "Optional mod cannot prevent base game startup");
        modded.Update(1.0 / 60.0);
        Scene modScene(&window, &camera);
        modded.DispatchDrawables(&modScene);
        mock.sawModColor = false;
        renderer.BeginFrame();
        renderer.RenderScene(&modScene, &camera);
        renderer.EndFrame();
        const bool valid = std::string(modPath) == ILLUMO_PALETTE_MOD;
        testTrue(counters,
                 modded.hasActiveMod() == valid &&
                   modded.modError().empty() == valid,
                 "Faulted or wrong-role mod revoked independently");
        testTrue(
          counters,
          mock.sawModColor == valid,
          "Separate mod message changes guest-generated paddle geometry");
        testTrue(counters,
                 modded.error().empty() && modded.OnCloseRequested(),
                 "Base game continues after optional mod failure");
        modded.Exit();
      }
      return counters.failures == 0;
    }
    std::vector<std::byte> worker;
    if (name == "GameJobs") {
      std::ifstream workerFile(ILLUMO_JOB_WORKER_GUEST, std::ios::binary);
      const std::vector<char> content{
        std::istreambuf_iterator<char>(workerFile), {}
      };
      worker.resize(content.size());
      std::memcpy(worker.data(), content.data(), content.size());
    }
    WasmFileRoots files;
    std::filesystem::path fileTestRoot;
    if (name == "GameFiles") {
      fileTestRoot = std::filesystem::absolute(
        "../Testing/WasmFiles/game-" +
        std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
      if (!std::filesystem::create_directory(fileTestRoot)) {
        return false;
      }
      files = { fileTestRoot / "package", fileTestRoot / "storage" };
      std::filesystem::create_directory(files.package);
      std::filesystem::create_directory(files.storage);
      std::ofstream(files.package / "asset.txt", std::ios::binary) << "abc";
    }
    WasmGameModule game(
      std::move(module), {}, {}, {}, std::move(worker), files);
    testTrue(counters,
             game.Start(&context),
             "Generic host starts actual independent WASM game");
    std::printf("%s\n", game.error().c_str());
    for (int tick = 0; tick < 10; ++tick) {
      game.Update(1.0 / 60.0);
    }
    if (name == "GameJobs" || name == "GameFiles") {
      const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
      while (!game.OnCloseRequested() && game.error().empty() &&
             std::chrono::steady_clock::now() < deadline) {
        game.Update(1.0 / 60.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    Scene scene(&window, &camera);
    game.DispatchDrawables(&scene);
    renderer.BeginFrame();
    renderer.RenderScene(&scene, &camera);
    renderer.EndFrame();
    testEqSize(counters,
               mock.countNonEmptyOfType(CommandType::DrawIndexed),
               name == "GuestPresentation" ? 4 : 1,
               "Guest-owned game geometry reaches native token backend");
    testTrue(counters,
             game.error().empty() && renderer.frameError().empty() &&
               game.OnCloseRequested(),
             "Guest update/render/close lifecycle succeeds");
    game.Exit();
    if (!fileTestRoot.empty()) {
      std::printf("%s\n", game.error().c_str());
      std::error_code fileError;
      testTrue(counters,
               std::filesystem::file_size(files.storage / "roundtrip.csim",
                                          fileError) == 64u * 1024u * 9u + 7u &&
                 !fileError,
               "Actual guest streams a multi-block atomic save");
      std::filesystem::remove_all(fileTestRoot);
    }
    WasmGameModule missing({});
    testTrue(counters,
             !missing.Start(&context),
             "Missing guest fails without native fallback");
    return counters.failures == 0;
  }
  WasmFrameRenderer bridge(renderer, 123);
  const std::array<std::byte, 4> pixel{
    std::byte{ 255 }, std::byte{ 255 }, std::byte{ 255 }, std::byte{ 255 }
  };
  const GuestResourceId texture = bridge.createTexture(pixel, 1, 1, 4, false);
  testTrue(
    counters, texture.owner == 123, "Texture acquired in the guest table");
  GuestBatch sprite = frame.batches[0];
  sprite.style = GuestBatchStyle::Sprite;
  sprite.texture = texture;
  frame.batches.push_back(sprite);
  frame.batches.push_back(frame.batches[0]);
  frame.textureWrites.push_back(
    { texture,
      0,
      0,
      1,
      1,
      4,
      { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 255 } },
      2 });
  wire.clear();
  frame.write(wire);
  std::vector<std::byte> transferred = wire.take();
  testTrue(
    counters, bridge.accept(transferred), "Complete painter stream accepted");
  const std::size_t enrolled = mock.getCreateCount();
  testTrue(counters,
           bridge.accept(transferred) && mock.getCreateCount() == enrolled,
           "Dynamic meshes reused");
  GuestFrame invalidWrite = frame;
  invalidWrite.textureWrites[0].x = 1;
  GuestWireWriter invalidWire;
  invalidWrite.write(invalidWire);
  testTrue(counters,
           !bridge.accept(invalidWire.data()),
           "Texture writes must stay inside the owned resource");
  if (name == "FrameFailures") {
    // Inline slots are pooled per style, so only growth replaces a slot.
    // Every batch outgrows its slot; the second replacement throws after
    // the first one succeeded.
    for (GuestBatch& batch : frame.batches) {
      const std::vector<GuestVertex> original = batch.vertices;
      for (int copy = 0; copy < 3; ++copy) {
        batch.vertices.insert(
          batch.vertices.end(), original.begin(), original.end());
      }
    }
    wire.clear();
    frame.write(wire);
    mock.replacementsBeforeThrow = 1;
    testTrue(
      counters,
      !bridge.accept(wire.data()),
      "Allocation exception contained after a successful slot replacement");
    Scene scene(&window, &camera);
    bridge.dispatch(scene);
    renderer.BeginFrame();
    renderer.RenderScene(&scene, &camera);
    renderer.EndFrame();
    testEqSize(counters,
               mock.countNonEmptyOfType(CommandType::DrawIndexed),
               0,
               "Failed mutation cannot reuse incompatible old-frame payloads");
    mock.throwTexture = true;
    testTrue(counters,
             bridge.createTexture(pixel, 1, 1, 4, false).owner == 0 &&
               !bridge.error().empty(),
             "Texture allocation exception contained");
    return counters.failures == 0;
  }
  std::fill(transferred.begin(), transferred.end(), std::byte{ 0 });
  testTrue(counters,
           bridge.releaseTexture(texture),
           "Guest releases texture authority");
  bridge.retire();
  Scene scene(&window, &camera);
  bridge.dispatch(scene);
  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();
  testEqSize(counters,
             mock.countNonEmptyOfType(CommandType::DrawIndexed),
             3,
             "Copied frame survives source destruction and retirement");
  testEqSize(counters,
             mock.getRejectedStaleCommandCount(),
             0,
             "Accepted texture lease survives revocation");
  testEqSize(counters,
             mock.countNonEmptyOfType(CommandType::UpdateTexture),
             1,
             "Copied texture write survives source destruction and revocation");
  testTrue(
    counters,
    mock.writesAfterDraws == std::vector<std::size_t>{ 2 },
    "Texture writes retain their position between painter-ordered draws");
  testTrue(counters,
           renderer.frameError().empty(),
           "Native submission accepts validated frame");
  testTrue(counters,
           !bridge.accept(wire.data()),
           "Retired owner cannot publish another frame");
  return counters.failures == 0;
}

int
main(int argc, char** argv)
{
  if (argc == 3 && (std::string(argv[1]) == "--capture" ||
                    std::string(argv[1]) == "--capture-ui")) {
    const bool ui = std::string(argv[1]) == "--capture-ui";
    FrameCaptureOptions options;
    options.width = 960;
    options.height = 640;
    const FrameCaptureResult capture = FrameCapture::render(
      options,
      [&options, ui](Renderer& renderer, Camera& camera, std::string& error) {
        std::ifstream binary(ui ? ILLUMO_PRESENTATION_GUEST
                                : ILLUMO_PADDLE_GUEST,
                             std::ios::binary);
        const std::vector<char> contents{
          std::istreambuf_iterator<char>(binary), {}
        };
        const std::span<const std::byte> bytes =
          std::as_bytes(std::span(contents));
        WasmGuest guest;
        std::vector<std::byte> response;
        if (!guest.start(bytes,
                         GuestRole::Game,
                         static_cast<std::uint32_t>(GuestCapability::Render) |
                           static_cast<std::uint32_t>(GuestCapability::Assets),
                         {},
                         response)) {
          error = guest.error();
          return false;
        }
        GuestInput input;
        input.width = options.width;
        input.height = options.height;
        input.elapsed = 1.0 / 60.0;
        GuestWireWriter request;
        input.write(request);
        WasmFrameRenderer bridge(renderer, guest.session());
        WasmRenderServices services(
          bridge, guest.capabilities(), ILLUMO_ENGINE_ASSETS);
        GuestWireWriter empty;
        GuestServices{}.write(empty);
        std::vector<std::byte> completions = empty.take();
        for (int tick = 0; tick < 40; ++tick) {
          if (!guest.invoke(GuestCall::Services, completions, response) ||
              !services.process(response, completions)) {
            error = guest.error() + services.error();
            return false;
          }
          if (!guest.invoke(GuestCall::Update, request.data(), response)) {
            error = guest.error();
            return false;
          }
          if (!guest.invoke(GuestCall::Frame, {}, response) ||
              !bridge.accept(response)) {
            error = guest.error() + bridge.error();
            return false;
          }
        }
        if (!guest.invoke(GuestCall::Frame, {}, response)) {
          error = guest.error();
          return false;
        }
        if (!bridge.accept(response)) {
          error = bridge.error();
          return false;
        }
        guest
          .shutdown(); // Accepted CPU bytes remain valid after store teardown.
        Scene scene(renderer.getWindow(), &camera);
        bridge.dispatch(scene);
        renderer.RenderScene(&scene, &camera);
        renderer.SubmitOnly();
        error = renderer.frameError();
        return error.empty();
      });
    std::string error;
    if (!capture.success() ||
        !FrameCapture::savePng(argv[2], capture.image, &error)) {
      std::fprintf(stderr, "%s: %s\n", capture.error.c_str(), error.c_str());
      return 1;
    }
    return 0;
  }
  if (argc == 2 && std::string(argv[1]) == "--list") {
    std::puts("Illumo.Wasm.FrameValidation\nIllumo.Wasm.FrameRendering\nIllumo."
              "Wasm.FrameFailures\nIllumo.Wasm.GameHost\nIllumo.Wasm."
              "ModIsolation\nIllumo.Wasm.RenderServices\nIllumo.Wasm."
              "GuestPresentation\nIllumo.Wasm.GameJobs\nIllumo.Wasm."
              "SdkContract\nIllumo.Wasm.GameFiles\nIllumo.Wasm."
              "DisplayServices\nIllumo.Wasm.ClipboardServices\nIllumo.Wasm."
              "ConsoleServices\nIllumo.Wasm.DialogServices\nIllumo.Wasm."
              "RetainedResources\nIllumo.Wasm.AudioServiceDecoder\nIllumo."
              "Wasm.AudioServices\nIllumo.Wasm.GuestAudio");
    return 0;
  }
  if (argc != 3 || std::string(argv[1]) != "--run") {
    return 2;
  }
  const std::string name(argv[2]);
  if (!name.starts_with("Illumo.Wasm.")) {
    return 2;
  }
  if (name.find("Audio", 12) != std::string::npos) {
    return runWasmAudioTest(name.substr(12)) ? 0 : 1;
  }
  return run(name.substr(12)) ? 0 : 1;
}
