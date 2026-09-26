#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/RecordedCommandList.h>
#include <Illumo/Rendering/RenderWorld.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestRegistry.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <memory>
#include <string>
#include <vector>

static bool
check(bool condition, const char* message)
{
  if (!condition) {
    std::printf("FAILED: %s\n", message);
  }
  return condition;
}

static const std::array<float, 16> kIdentity{ 1, 0, 0, 0, 0, 1, 0, 0,
                                              0, 0, 1, 0, 0, 0, 0, 1 };

static std::array<float, 16>
translated(float x)
{
  std::array<float, 16> matrix = kIdentity;
  matrix[12] = x;
  return matrix;
}

struct WorldFixture
{
  HeadlessRenderFixture render{ 640, 480 };
  RenderWorld world;
  Scene scene{ &render.window, &render.camera };
  MeshHandle mesh{};

  WorldFixture()
  {
    const std::array<unsigned char, 36 * 3> vertices{};
    const std::array<unsigned int, 3> indices{ 0, 1, 2 };
    mesh = render.renderer.enrollMesh(vertices.data(),
                                      vertices.size(),
                                      indices.data(),
                                      sizeof(indices),
                                      MeshVertexLayout::Pos3Norm3Color4U8Uv2,
                                      false);
    scene.AddDrawable(&world, RenderLayerId::World);
  }
  ~WorldFixture() { world.releaseResources(); }

  RenderInstanceDesc instance(RenderMaterialId material, float x) const
  {
    RenderInstanceDesc desc;
    desc.mesh = mesh;
    desc.indexCount = 3;
    desc.material = material;
    desc.world = translated(x);
    desc.hasBounds = true;
    desc.localBounds = { Vector3(-0.25f), Vector3(0.25f) };
    return desc;
  }

  // An identity camera sees the [-1, 1] cube.
  void frame()
  {
    render.renderer.setNextWorldViewProjection(kIdentity);
    render.renderer.BeginFrame();
    render.renderer.RenderScene(&scene, &render.camera);
    render.renderer.EndFrame();
  }

  // Instance counts of the instanced draws the last frame executed.
  std::vector<unsigned int> drawCounts() const
  {
    std::vector<unsigned int> counts;
    for (size_t index = 0; index < render.mock.getLastNonEmptySubmittedCount();
         ++index) {
      const RenderCommand& command =
        render.mock.getLastNonEmptySubmitted(index);
      if (command.commandType == CommandType::DrawIndexedInstanced) {
        counts.push_back(command.drawIndexedInstanced.instanceCount);
      }
    }
    return counts;
  }
};

static int
bucketsRecordOnce()
{
  WorldFixture fixture;
  RenderWorld& world = fixture.world;
  bool ok = check(world.createMaterial(1, RenderMaterialDesc{}) &&
                    world.createInstance(10, fixture.instance(1, 0.0f)) &&
                    world.createInstance(11, fixture.instance(1, 0.5f)) &&
                    world.createInstance(12, fixture.instance(1, -0.5f)),
                  "A material and three instances are created");
  fixture.frame();
  ok = check(fixture.drawCounts() == std::vector<unsigned int>{ 3 } &&
               fixture.render.mock.countNonEmptyOfType(
                 CommandType::ExecuteList) == 1 &&
               world.stats().buckets == 1 && world.stats().recordings == 1,
             "Three instances share one bucket drawn by one recorded list") &&
       ok;

  fixture.frame();
  ok = check(world.stats().recordings == 1 &&
               fixture.drawCounts() == std::vector<unsigned int>{ 3 },
             "A later frame reuses the recording") &&
       ok;

  ok = check(world.setInstanceTransform(11, translated(100.0f)),
             "An instance moves") &&
       ok;
  fixture.frame();
  ok =
    check(fixture.drawCounts() == std::vector<unsigned int>{ 2 } &&
            world.stats().recordings == 1 && world.stats().drawnInstances == 2,
          "Culling changes the patched count, not the recording") &&
    ok;

  ok = check(world.createMaterial(2, RenderMaterialDesc{}) &&
               world.createInstance(20, fixture.instance(2, 0.0f)),
             "A second material's instance is created") &&
       ok;
  fixture.frame();
  ok = check(world.stats().buckets == 2 && world.stats().recordings == 2 &&
               fixture.render.mock.countNonEmptyOfType(
                 CommandType::ExecuteList) == 2,
             "A new material makes and records a second bucket") &&
       ok;

  RenderMaterialDesc red;
  red.tint = { 1.0f, 0.0f, 0.0f, 1.0f };
  ok = check(world.updateMaterial(1, red), "A material updates") && ok;
  fixture.frame();
  ok = check(world.stats().recordings == 3,
             "Updating a material re-records only its bucket") &&
       ok;

  ok = check(!world.destroyMaterial(2), "A material in use is kept") && ok;
  ok = check(world.destroyInstance(20) && world.destroyMaterial(2) &&
               !world.destroyInstance(20),
             "Instances and then their material are destroyed once") &&
       ok;
  fixture.frame();
  ok =
    check(world.stats().buckets == 1 && fixture.render.mock.countNonEmptyOfType(
                                          CommandType::ExecuteList) == 1,
          "An emptied bucket stops drawing") &&
    ok;

  size_t frameBlocks = 0;
  for (size_t index = 0;
       index < fixture.render.mock.getLastNonEmptySubmittedCount();
       ++index) {
    const RenderCommand& command =
      fixture.render.mock.getLastNonEmptySubmitted(index);
    frameBlocks +=
      command.commandType == CommandType::WriteBuffer &&
          command.writeBuffer.sizeBytes == sizeof(Renderer::FrameUniforms)
        ? 1u
        : 0u;
  }
  ok = check(frameBlocks == 1, "The frame block is written once") && ok;
  return ok ? 0 : 1;
}

static int
rejectsInvalidCalls()
{
  WorldFixture fixture;
  RenderWorld& world = fixture.world;
  const float nan = std::numeric_limits<float>::quiet_NaN();
  bool ok = check(world.createMaterial(1, RenderMaterialDesc{}) &&
                    !world.createMaterial(1, RenderMaterialDesc{}) &&
                    !world.createMaterial(0, RenderMaterialDesc{}),
                  "Material ids are nonzero and unique");
  RenderInstanceDesc desc = fixture.instance(1, 0.0f);
  ok = check(world.createInstance(5, desc) && !world.createInstance(5, desc) &&
               !world.createInstance(0, desc),
             "Instance ids are nonzero and unique") &&
       ok;
  RenderInstanceDesc unknownMaterial = desc;
  unknownMaterial.material = 9;
  RenderInstanceDesc noMesh = desc;
  noMesh.mesh = MeshHandle{};
  RenderInstanceDesc noIndices = desc;
  noIndices.indexCount = 0;
  RenderInstanceDesc badWorld = desc;
  badWorld.world[5] = nan;
  ok = check(!world.createInstance(6, unknownMaterial) &&
               !world.createInstance(7, noMesh) &&
               !world.createInstance(8, noIndices) &&
               !world.createInstance(9, badWorld),
             "Invalid instance descriptions are refused") &&
       ok;
  std::array<float, 16> badTransform = kIdentity;
  badTransform[0] = nan;
  ok = check(!world.setInstanceTransform(5, badTransform) &&
               !world.setInstanceTransform(99, kIdentity) &&
               !world.setInstanceTint(99, { 1, 1, 1, 1 }) &&
               !world.setInstanceVisible(99, false),
             "Bad values and unknown ids change nothing") &&
       ok;
  ok = check(world.setInstanceVisible(5, false), "An instance hides") && ok;
  fixture.frame();
  ok = check(fixture.drawCounts().empty() && world.stats().instances == 1,
             "A hidden instance is not drawn") &&
       ok;
  return ok ? 0 : 1;
}

static int
shadowsAndBlending()
{
  WorldFixture fixture;
  RenderWorld& world = fixture.world;
  RenderEnvironment environment;
  environment.shadowsEnabled = true;
  environment.shadowMapSize = 256;
  world.setEnvironment(environment);
  RenderMaterialDesc glass;
  glass.blend = true;
  bool ok = check(world.createMaterial(1, RenderMaterialDesc{}) &&
                    world.createMaterial(2, glass) &&
                    world.createInstance(1, fixture.instance(1, 0.0f)) &&
                    world.createInstance(2, fixture.instance(1, 0.5f)) &&
                    world.createInstance(3, fixture.instance(2, -0.5f)) &&
                    world.createInstance(4, fixture.instance(2, 0.25f)),
                  "Opaque and blended instances are created");
  fixture.frame();
  ok = check(fixture.render.renderer.getShadowCasters().size() == 4,
             "Every bounded caster takes part in shadow fitting") &&
       ok;
  ok = check(fixture.render.renderer.getShadowFrameContext().active ||
               world.stats().shadowInstances > 0,
             "The shared shadow pass ran") &&
       ok;
  ok = check(world.stats().buckets == 3 && world.stats().drawnInstances == 4,
             "Blended instances each get a bucket") &&
       ok;
  const std::vector<unsigned int> counts = fixture.drawCounts();
  // Shadow draws come first, then opaque color, then blended in order.
  ok = check(counts.size() >= 3 && counts[counts.size() - 3] == 2 &&
               counts[counts.size() - 2] == 1 && counts.back() == 1,
             "Opaque buckets draw before blended ones") &&
       ok;
  ok = check(world.stats().shadowInstances == 4,
             "All four instances draw into the shadow map") &&
       ok;
  return ok ? 0 : 1;
}

static int
recordingAndNesting()
{
  HeadlessRenderFixture fixture;
  Renderer& renderer = fixture.renderer;
  RecordedCommandList list;
  std::array<float, 16> matrix = translated(3.0f);
  renderer.beginRecording(&list);
  renderer.pushUniformMat4("uModel", matrix.data());
  renderer.pushDrawIndexed(3, 0);
  renderer.endRecording();
  const float* recorded = list.at(0).uniformMat4.value;
  matrix[12] = 7.0f;
  renderer.BeginFrame();
  renderer.EndFrame();
  bool ok = check(list.size() == 2 && recorded != matrix.data() &&
                    recorded[12] == 3.0f && !renderer.isRecording(),
                  "Recorded matrices are owned by the list");

  renderer.BeginFrame();
  ok = check(renderer.pushExecuteList(&list), "A list queues") && ok;
  renderer.EndFrame();
  ok =
    check(fixture.mock.getExecutedListCount() == 1 &&
            fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) == 1 &&
            renderer.getRecordedListStats().lists == 1 &&
            renderer.getRecordedListStats().tokens == 2,
          "A queued list runs in place and is counted") &&
    ok;

  RenderCommand nested;
  nested.commandType = CommandType::ExecuteList;
  nested.executeList.list = &list;
  RecordedCommandList outer;
  ok = check(!outer.append(nested) && outer.failed(),
             "A list cannot execute another list") &&
       ok;
  renderer.BeginFrame();
  ok =
    check(!renderer.pushExecuteList(&outer) && !renderer.frameError().empty(),
          "A failed list is refused with a frame error") &&
    ok;
  renderer.EndFrame();

  RecordedCommandList small(2);
  RenderCommand draw;
  draw.commandType = CommandType::DrawIndexed;
  ok = check(small.append(draw) && small.append(draw) && !small.append(draw) &&
               small.failed(),
             "A list stops at its ceiling") &&
       ok;
  small.clear();
  ok =
    check(!small.failed() && small.size() == 0, "Clearing a list resets it") &&
    ok;
  return ok ? 0 : 1;
}

// Median CPU time of a whole RenderScene (emission plus MockBackend
// submission) and the tokens it queued.
static void
timeFrames(WorldFixture& fixture, double* microseconds, size_t* tokens)
{
  std::vector<double> samples;
  for (int frame = 0; frame < 13; ++frame) {
    const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
    fixture.frame();
    const double elapsed = std::chrono::duration<double, std::micro>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    *tokens = fixture.render.mock.getLastNonEmptySubmittedCount();
    // The mock keeps every submitted frame; drop them between samples.
    fixture.render.mock.resetCounters();
    if (frame >= 3) {
      samples.push_back(elapsed);
    }
  }
  std::sort(samples.begin(), samples.end());
  *microseconds = samples[samples.size() / 2];
}

static int
renderWorldBench()
{
  bool ok = true;
  for (size_t count : { 2000u, 50000u }) {
    const int columns = 250;
    std::vector<std::array<float, 16>> worlds;
    for (size_t index = 0; index < count; ++index) {
      worlds.push_back(
        translated(-0.9f + 1.8f * static_cast<float>(index % columns) /
                             static_cast<float>(columns)));
    }
    double visualMicros = 0.0;
    size_t visualTokens = 0;
    // One MeshVisual per object overflows the 65,536-token queue past about
    // 2,600 objects, so it is measured only at the smaller size.
    if (count <= 2000) {
      WorldFixture fixture;
      MeshAssetInfo asset;
      asset.handle = fixture.mesh;
      asset.indexCount = 3;
      asset.minBounds = Vector3(-0.25f);
      asset.maxBounds = Vector3(0.25f);
      // The fixture's own world stays empty here.
      std::vector<std::unique_ptr<MeshVisual>> visuals;
      for (const std::array<float, 16>& world : worlds) {
        std::unique_ptr<MeshVisual> visual = std::make_unique<MeshVisual>();
        visual->setMeshAsset(asset, ColorRgba{ 255, 255, 255, 255 });
        visual->setModelMatrix(glm::make_mat4(world.data()));
        visual->prepare(&fixture.render.renderer);
        fixture.scene.AddDrawable(visual.get(), RenderLayerId::World);
        visuals.push_back(std::move(visual));
      }
      timeFrames(fixture, &visualMicros, &visualTokens);
    }

    WorldFixture fixture;
    ok = fixture.world.createMaterial(1, RenderMaterialDesc{}) && ok;
    for (size_t index = 0; index < count; ++index) {
      RenderInstanceDesc desc = fixture.instance(1, 0.0f);
      desc.world = worlds[index];
      ok = fixture.world.createInstance(
             static_cast<RenderInstanceId>(index + 1), desc) &&
           ok;
    }
    double worldMicros = 0.0;
    size_t worldTokens = 0;
    timeFrames(fixture, &worldMicros, &worldTokens);
    ok = check(fixture.world.stats().drawnInstances == count,
               "The bench draws every instance") &&
         ok;
    std::printf("RenderWorldBench instances=%zu meshvisual_us=%.1f "
                "meshvisual_tokens=%zu renderworld_us=%.1f "
                "renderworld_tokens=%zu recordings=%zu\n",
                count,
                visualMicros,
                visualTokens,
                worldMicros,
                worldTokens,
                fixture.world.stats().recordings);
  }
  return ok ? 0 : 1;
}

// The world's sky draws first with SkyboxVisual's rotation-only view
// projection, rebuilt from the frame's full view projection alone.
static int
skyboxFromFrameCamera()
{
  WorldFixture fixture;
  Renderer& renderer = fixture.render.renderer;
  std::array<unsigned char, 4> pixel{ 255, 255, 255, 255 };
  std::array<const unsigned char*, 6> data{};
  data.fill(pixel.data());
  const TextureHandle cubemap = renderer.enrollCubemap(data, 1, 1, 4);
  const glm::mat4 projection =
    glm::perspective(glm::radians(60.0f), 640.0f / 480.0f, 0.1f, 500.0f);
  const glm::mat4 view = glm::lookAt(
    glm::vec3(12.0f, -3.0f, 40.0f), glm::vec3(0.0f), glm::vec3(0, 1, 0));
  const glm::mat4 expected = projection * glm::mat4(glm::mat3(view));
  std::array<float, 16> viewProjection{};
  const glm::mat4 full = projection * view;
  std::copy_n(glm::value_ptr(full), 16, viewProjection.begin());

  const auto frame = [&]() {
    fixture.render.mock.resetCounters();
    renderer.setNextWorldViewProjection(viewProjection);
    renderer.BeginFrame();
    renderer.RenderScene(&fixture.scene, &fixture.render.camera);
    renderer.EndFrame();
  };
  const auto skyMatrix = [&]() -> const float* {
    for (size_t index = 0;
         index < fixture.render.mock.getLastNonEmptySubmittedCount();
         ++index) {
      const RenderCommand& command =
        fixture.render.mock.getLastNonEmptySubmitted(index);
      if (command.commandType == CommandType::SetUniformMat4 &&
          std::string(command.uniformMat4.name) == "uViewProjection") {
        return command.uniformMat4.value;
      }
    }
    return nullptr;
  };

  bool ok = check(
    !fixture.world.setSkybox(
      { cubemap, { std::numeric_limits<float>::quiet_NaN(), 1, 1, 1 } }) &&
      !fixture.world.hasSkybox(),
    "a non-finite tint is refused");
  ok = check(fixture.world.setSkybox({ cubemap, { 1.0f, 0.5f, 0.5f, 1.0f } }),
             "the sky is set") &&
       ok;
  frame();
  const float* drawn = skyMatrix();
  float largest = drawn == nullptr ? 1.0f : 0.0f;
  for (int index = 0; drawn != nullptr && index < 16; ++index) {
    largest = std::max(
      largest, std::abs(drawn[index] - glm::value_ptr(expected)[index]));
  }
  ok = check(drawn != nullptr && largest < 1e-4f,
             "the sky's matrix is projection times the rotation-only view") &&
       ok;
  fixture.world.setSkybox({});
  frame();
  ok =
    check(skyMatrix() == nullptr, "an invalid cubemap removes the sky") && ok;
  renderer.destroyTexture(cubemap);
  return ok ? 0 : 1;
}

void
registerRenderWorldTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.RenderWorld.SkyboxFromFrameCamera",
               skyboxFromFrameCamera);
  registry.add("Illumo.RenderWorld.Bench", renderWorldBench, 120);
  registry.add("Illumo.RenderWorld.BucketsRecordOnce", bucketsRecordOnce);
  registry.add("Illumo.RenderWorld.RejectsInvalidCalls", rejectsInvalidCalls);
  registry.add("Illumo.RenderWorld.ShadowsAndBlending", shadowsAndBlending);
  registry.add("Illumo.RecordedList.RecordingAndNesting", recordingAndNesting);
}
