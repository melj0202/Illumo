#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/RecordedCommandList.h>
#include <Illumo/Rendering/RenderWorld.h>
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
  DrawList scene{ &render.window, &render.camera };
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

// Instance buffers keep their contents, so an unchanged bucket skips its
// records and upload; a move rewrites the bucket in its frame (previous
// transform) and in the next (previous transform dropped), then stops.
static int
unchangedBucketsSkipUploads()
{
  WorldFixture fixture;
  RenderWorld& world = fixture.world;
  bool ok = check(world.createMaterial(1, RenderMaterialDesc{}) &&
                    world.createInstance(10, fixture.instance(1, 0.0f)) &&
                    world.createInstance(11, fixture.instance(1, 0.5f)) &&
                    world.createMaterial(2, RenderMaterialDesc{}) &&
                    world.createInstance(20, fixture.instance(2, -0.5f)),
                  "Two buckets of instances are created");
  // The instance-buffer writes the last frame submitted (the frame block
  // excluded), with the first record's previous-model translation.
  const auto instanceWrites = [&fixture](float* previousX) {
    size_t writes = 0;
    for (size_t index = 0;
         index < fixture.render.mock.getLastNonEmptySubmittedCount();
         ++index) {
      const RenderCommand& command =
        fixture.render.mock.getLastNonEmptySubmitted(index);
      if (command.commandType == CommandType::WriteBuffer &&
          command.writeBuffer.sizeBytes != sizeof(Renderer::FrameUniforms) &&
          command.writeBuffer.sizeBytes != sizeof(Renderer::LightUniforms)) {
        writes += 1;
        if (previousX != nullptr && command.writeBuffer.data != nullptr) {
          const float* record =
            static_cast<const float*>(command.writeBuffer.data);
          *previousX = record[16 + 12];
        }
      }
    }
    return writes;
  };
  fixture.frame();
  ok = check(instanceWrites(nullptr) == 2 &&
               world.stats().uploadedInstances == 3 &&
               world.stats().drawnInstances == 3,
             "The first frame uploads every bucket") &&
       ok;
  fixture.frame();
  ok = check(instanceWrites(nullptr) == 0 &&
               world.stats().uploadedInstances == 0 &&
               fixture.drawCounts() == std::vector<unsigned int>{ 2, 1 },
             "An unchanged frame draws from the held buffers") &&
       ok;

  ok = check(world.setInstanceTransform(20, translated(-0.25f)),
             "An instance moves") &&
       ok;
  float previousX = 0.0f;
  fixture.frame();
  ok = check(instanceWrites(&previousX) == 1 &&
               world.stats().uploadedInstances == 1 && previousX == -0.5f,
             "Only the moved bucket uploads, with the previous transform") &&
       ok;
  fixture.frame();
  ok =
    check(instanceWrites(&previousX) == 1 && previousX == -0.25f,
          "The next frame uploads it again without the previous transform") &&
    ok;
  fixture.frame();
  ok = check(instanceWrites(nullptr) == 0, "Then it holds again") && ok;

  ok = check(world.setInstanceTransform(11, translated(100.0f)),
             "An instance leaves the view") &&
       ok;
  fixture.frame();
  fixture.frame();
  fixture.frame();
  ok = check(instanceWrites(nullptr) == 0 &&
               fixture.drawCounts() == std::vector<unsigned int>{ 1, 1 },
             "A smaller visible set uploads once and then holds") &&
       ok;
  ok = check(world.setInstanceTint(10, { 0.5f, 0.5f, 0.5f, 1.0f }) &&
               world.setInstanceVisible(20, false),
             "A tint and a visibility change") &&
       ok;
  fixture.frame();
  ok = check(instanceWrites(nullptr) == 1 &&
               fixture.drawCounts() == std::vector<unsigned int>{ 1 },
             "A tint re-uploads its bucket; a hidden bucket stops drawing") &&
       ok;

  // A camera change culls again: what is visible follows the camera.
  const auto cameraFrame = [&fixture](float x) {
    fixture.render.renderer.setNextWorldViewProjection(translated(x));
    fixture.render.renderer.BeginFrame();
    fixture.render.renderer.RenderScene(&fixture.scene, &fixture.render.camera);
    fixture.render.renderer.EndFrame();
  };
  cameraFrame(1.4f);
  ok = check(fixture.drawCounts().empty(),
             "A camera that sees nothing draws nothing") &&
       ok;
  cameraFrame(-100.0f);
  ok = check(fixture.drawCounts() == std::vector<unsigned int>{ 1 } &&
               instanceWrites(nullptr) == 1,
             "Another camera draws the other instance, uploaded once") &&
       ok;
  cameraFrame(-100.0f);
  ok = check(fixture.drawCounts() == std::vector<unsigned int>{ 1 } &&
               instanceWrites(nullptr) == 0,
             "A held camera draws from the held buffer") &&
       ok;
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

  // HDR PBR + 32 punctual lights + 4-quadrant shadow atlas (3 CSM + 1 spot)
  // CPU world emission benchmark (2,000 textured instances across 4 buckets).
  {
    WorldFixture hdrFixture;
    Renderer& renderer = hdrFixture.render.renderer;
    const std::array<unsigned char, 4> texel{ 200, 180, 160, 255 };
    const TextureHandle albedo = renderer.enrollTexture(texel.data(), 1, 1, 4);
    const TextureHandle normal = renderer.enrollTexture(texel.data(), 1, 1, 4);
    const TextureHandle orm = renderer.enrollTexture(texel.data(), 1, 1, 4);
    const TextureHandle emissive =
      renderer.enrollTexture(texel.data(), 1, 1, 4);

    RenderEnvironment env;
    env.lightDirection = { 0.45f, 1.0f, 0.35f };
    env.lightColor = { 0.9f, 0.88f, 0.82f };
    env.ambientColor = { 0.04f, 0.04f, 0.05f };
    env.shadowsEnabled = true;
    env.shadowMapSize = 1024;
    env.shadowPcf = true;
    hdrFixture.world.setEnvironment(env);

    for (uint32_t m = 1; m <= 4; ++m) {
      RenderMaterialDesc mat;
      mat.albedoTexture = albedo;
      mat.normalTexture = normal;
      mat.metallicRoughnessTexture = orm;
      mat.emissiveTexture = emissive;
      mat.metallic = 0.25f * static_cast<float>(m - 1u);
      mat.roughness = 0.2f + 0.2f * static_cast<float>(m - 1u);
      mat.castsShadow = true;
      mat.receivesShadow = true;
      ok = hdrFixture.world.createMaterial(m, mat) && ok;
    }

    for (size_t i = 0; i < 2000u; ++i) {
      const size_t row = i / 50u;
      const float gx = -18.0f + 36.0f * static_cast<float>(i % 50u) / 50.0f;
      const float gz = -4.0f - 40.0f * static_cast<float>(row) / 40.0f;
      RenderInstanceDesc inst =
        hdrFixture.instance(static_cast<RenderMaterialId>((i % 4u) + 1u), 0.0f);
      const glm::mat4 model =
        glm::translate(glm::mat4(1.0f), glm::vec3(gx, 0.0f, gz));
      std::copy_n(glm::value_ptr(model), 16, inst.world.begin());
      ok = hdrFixture.world.createInstance(
             static_cast<RenderInstanceId>(i + 1u), inst) &&
           ok;
    }

    for (uint32_t l = 1; l <= 32u; ++l) {
      const uint32_t lightRow = l / 8u;
      RenderLightDesc light;
      light.kind = (l == 1u || (l % 4u) == 0u) ? RenderLightKind::Spot
                                               : RenderLightKind::Point;
      light.position = { -15.0f + static_cast<float>(l % 8u) * 4.2f,
                         3.0f,
                         -6.0f - static_cast<float>(lightRow) * 8.0f };
      light.direction = { 0.0f, -1.0f, -0.2f };
      light.color = { 0.8f, 0.7f, 0.6f };
      light.intensity = 12.0f;
      light.range = 14.0f;
      light.innerConeCos = std::cos(glm::radians(18.0f));
      light.outerConeCos = std::cos(glm::radians(32.0f));
      light.castsShadow = (l == 1u);
      ok = hdrFixture.world.createLight(l, light) && ok;
    }

    const glm::mat4 vp =
      glm::perspective(glm::radians(60.0f), 2560.0f / 1440.0f, 0.2f, 90.0f) *
      glm::lookAt(glm::vec3(0.0f, 8.0f, 8.0f),
                  glm::vec3(0.0f, 0.0f, -22.0f),
                  glm::vec3(0.0f, 1.0f, 0.0f));
    std::array<float, 16> vpArray{};
    std::copy_n(glm::value_ptr(vp), 16, vpArray.begin());

    // Warm up 5 frames so RecordedCommandLists and static shadow caches settle.
    for (int w = 0; w < 5; ++w) {
      hdrFixture.render.mock.resetCounters();
      renderer.setNextWorldViewProjection(vpArray);
      renderer.BeginFrame();
      renderer.RenderScene(&hdrFixture.scene, &hdrFixture.render.camera);
      renderer.EndFrame();
    }

    std::vector<double> staticSamples;
    std::vector<double> dynamicSamples;
    size_t hdrTokens = 0;
    for (int iter = 0; iter < 45; ++iter) {
      hdrFixture.render.mock.resetCounters();
      const std::chrono::steady_clock::time_point t0 =
        std::chrono::steady_clock::now();
      renderer.setNextWorldViewProjection(vpArray);
      renderer.BeginFrame();
      renderer.RenderScene(&hdrFixture.scene, &hdrFixture.render.camera);
      renderer.EndFrame();
      const std::chrono::steady_clock::time_point t1 =
        std::chrono::steady_clock::now();
      staticSamples.push_back(
        std::chrono::duration<double, std::micro>(t1 - t0).count());
      hdrTokens = hdrFixture.render.mock.getLastNonEmptySubmittedCount();
    }

    for (int iter = 0; iter < 45; ++iter) {
      RenderLightDesc movingSpot;
      movingSpot.kind = RenderLightKind::Spot;
      movingSpot.position = { std::sin(static_cast<float>(iter) * 0.1f) * 4.0f,
                              3.5f,
                              -8.0f };
      movingSpot.direction = { 0.0f, -1.0f, -0.15f };
      movingSpot.color = { 0.9f, 0.8f, 0.7f };
      movingSpot.intensity = 18.0f;
      movingSpot.range = 16.0f;
      movingSpot.innerConeCos = std::cos(glm::radians(18.0f));
      movingSpot.outerConeCos = std::cos(glm::radians(32.0f));
      movingSpot.castsShadow = false;
      hdrFixture.world.updateLight(2u, movingSpot);

      hdrFixture.render.mock.resetCounters();
      const std::chrono::steady_clock::time_point t0 =
        std::chrono::steady_clock::now();
      renderer.setNextWorldViewProjection(vpArray);
      renderer.BeginFrame();
      renderer.RenderScene(&hdrFixture.scene, &hdrFixture.render.camera);
      renderer.EndFrame();
      const std::chrono::steady_clock::time_point t1 =
        std::chrono::steady_clock::now();
      dynamicSamples.push_back(
        std::chrono::duration<double, std::micro>(t1 - t0).count());
    }

    std::sort(staticSamples.begin(), staticSamples.end());
    std::sort(dynamicSamples.begin(), dynamicSamples.end());
    std::printf("RenderWorldHdrBench instances=2000 lights=32 "
                "hdr_static_us=%.1f hdr_dynamic_us=%.1f hdr_tokens=%zu\n",
                staticSamples[staticSamples.size() / 2],
                dynamicSamples[dynamicSamples.size() / 2],
                hdrTokens);
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

static int
punctualLightsAndMaterials()
{
  WorldFixture fixture;
  RenderWorld& world = fixture.world;
  Renderer& renderer = fixture.render.renderer;
  const float nan = std::numeric_limits<float>::quiet_NaN();

  // Material PBR validation and texture binding.
  const std::array<unsigned char, 4> texel{ 200, 150, 100, 255 };
  const TextureHandle albedo = renderer.enrollTexture(texel.data(), 1, 1, 4);
  const TextureHandle normal = renderer.enrollTexture(texel.data(), 1, 1, 4);
  const TextureHandle orm = renderer.enrollTexture(texel.data(), 1, 1, 4);
  const TextureHandle emissive = renderer.enrollTexture(texel.data(), 1, 1, 4);

  RenderMaterialDesc badMat;
  badMat.metallic = nan;
  RenderMaterialDesc badRough;
  badRough.roughness = nan;
  RenderMaterialDesc badEmissive;
  badEmissive.emissiveFactor = { 0.0f, nan, 0.0f };
  bool ok = check(!world.createMaterial(1, badMat) &&
                    !world.createMaterial(1, badRough) &&
                    !world.createMaterial(1, badEmissive),
                  "materials reject non-finite PBR factors");

  RenderMaterialDesc pbrMat;
  pbrMat.albedoTexture = albedo;
  pbrMat.normalTexture = normal;
  pbrMat.metallicRoughnessTexture = orm;
  pbrMat.emissiveTexture = emissive;
  pbrMat.metallic = 0.85f;
  pbrMat.roughness = 0.25f;
  pbrMat.emissiveFactor = { 1.5f, 0.5f, 0.25f };
  ok = check(world.createMaterial(1, pbrMat) &&
               world.createInstance(1, fixture.instance(1, 0.0f)),
             "textured PBR material and instance are created") &&
       ok;

  // Light CRUD and validation.
  RenderLightDesc badRange;
  badRange.range = 0.0f;
  RenderLightDesc badNan;
  badNan.position = { nan, 0.0f, 0.0f };
  RenderLightDesc badCone;
  badCone.kind = RenderLightKind::Spot;
  badCone.innerConeCos = 0.5f;
  badCone.outerConeCos = 0.8f;
  RenderLightDesc badSpotDir;
  badSpotDir.kind = RenderLightKind::Spot;
  badSpotDir.direction = { 0.0f, 0.0f, 0.0f };
  ok =
    check(
      !world.createLight(0, RenderLightDesc{}) &&
        !world.createLight(1, badRange) && !world.createLight(1, badNan) &&
        !world.createLight(1, badCone) && !world.createLight(1, badSpotDir) &&
        !world.updateLight(99, RenderLightDesc{}) && !world.destroyLight(99),
      "invalid light ids and parameters are rejected") &&
    ok;

  // Add 80 visible lights + 3 culled/hidden lights; tie-break id 12 vs id 50.
  for (uint32_t i = 1; i <= 80; ++i) {
    RenderLightDesc light;
    light.kind = (i % 4 == 0) ? RenderLightKind::Spot : RenderLightKind::Point;
    light.position = { 0.0f, 0.0f, 0.0f };
    light.direction = { 0.0f, -1.0f, 0.0f };
    light.color = { static_cast<float>(i) / 100.0f, 0.5f, 0.25f };
    light.intensity = (i == 12 || i == 50) ? 100.0f : static_cast<float>(i);
    light.range = 2.0f;
    light.castsShadow = (i == 16);
    ok = world.createLight(i, light) && ok;
  }
  RenderLightDesc duplicate;
  ok = check(!world.createLight(12, duplicate), "duplicate light id refused") &&
       ok;

  RenderLightDesc hiddenLight;
  hiddenLight.visible = false;
  hiddenLight.intensity = 500.0f;
  RenderLightDesc farLight;
  farLight.position = { 50.0f, 0.0f, 0.0f };
  farLight.range = 1.0f;
  farLight.intensity = 500.0f;
  ok =
    check(world.createLight(90, hiddenLight) && world.createLight(91, farLight),
          "hidden and out-of-frustum lights are created") &&
    ok;

  fixture.frame();
  const Renderer::LightUniforms& uploaded = renderer.getLightUniforms();
  ok = check(world.stats().lights == 82 && world.stats().activeLights == 64 &&
               uploaded.countsAndFlags[0] == 64,
             "frustum-visible lights cap at 64 active lights") &&
       ok;
  // Equal top score (intensity=100, range=2, dist=0): id 12 must precede id 50.
  const float firstR = uploaded.punctualLights[0].radianceAndOuterCos[0];
  const float secondR = uploaded.punctualLights[1].radianceAndOuterCos[0];
  ok =
    check(std::abs(firstR - 12.0f) < 1e-3f && std::abs(secondR - 50.0f) < 1e-3f,
          "equal-priority lights tie-break deterministically by id") &&
    ok;

  // Verify material textures bound on units 2..5 via recorded command list.
  bool sawAlbedo = false;
  bool sawNormal = false;
  bool sawOrm = false;
  bool sawEmissive = false;
  for (size_t index = 0;
       index < fixture.render.mock.getLastNonEmptySubmittedCount();
       ++index) {
    const RenderCommand& cmd =
      fixture.render.mock.getLastNonEmptySubmitted(index);
    if (cmd.commandType == CommandType::ExecuteList &&
        cmd.executeList.list != nullptr) {
      const RecordedCommandList& list = *cmd.executeList.list;
      for (size_t sub = 0; sub < list.size(); ++sub) {
        const RenderCommand& item = list.at(sub);
        if (item.commandType == CommandType::SetTexture) {
          sawAlbedo = sawAlbedo || (item.bindTexture.slot == 2u &&
                                    item.bindTexture.handle == albedo);
          sawNormal = sawNormal || (item.bindTexture.slot == 3u &&
                                    item.bindTexture.handle == normal);
          sawOrm = sawOrm || (item.bindTexture.slot == 4u &&
                              item.bindTexture.handle == orm);
          sawEmissive = sawEmissive || (item.bindTexture.slot == 5u &&
                                        item.bindTexture.handle == emissive);
        }
      }
    }
  }
  ok = check(sawAlbedo && sawNormal && sawOrm && sawEmissive,
             "recorded bucket binds albedo, normal, ORM and emissive maps") &&
       ok;

  // Update and destroy lights.
  hiddenLight.visible = true;
  ok = check(world.updateLight(90, hiddenLight) && world.destroyLight(12) &&
               !world.destroyLight(12),
             "lights update and destroy once") &&
       ok;
  fixture.frame();
  ok =
    check(
      world.stats().lights == 81 &&
        std::abs(
          renderer.getLightUniforms().punctualLights[0].radianceAndOuterCos[0] -
          500.0f) < 1.0f,
      "updated light enters top priority slot") &&
    ok;

  renderer.destroyTexture(albedo);
  renderer.destroyTexture(normal);
  renderer.destroyTexture(orm);
  renderer.destroyTexture(emissive);
  return ok ? 0 : 1;
}

static RenderInstanceDesc
makeInstanceAtXz(const WorldFixture& fixture,
                 RenderMaterialId mat,
                 float x,
                 float z)
{
  RenderInstanceDesc desc = fixture.instance(mat, 0.0f);
  std::array<float, 16> transform = kIdentity;
  transform[12] = x;
  transform[14] = z;
  desc.world = transform;
  return desc;
}

static size_t
countSubmittedInstanceWrites(const MockBackend& mock)
{
  size_t writes = 0;
  for (size_t index = 0; index < mock.getLastNonEmptySubmittedCount();
       ++index) {
    const RenderCommand& cmd = mock.getLastNonEmptySubmitted(index);
    if (cmd.commandType == CommandType::WriteBuffer &&
        cmd.writeBuffer.sizeBytes != sizeof(Renderer::FrameUniforms) &&
        cmd.writeBuffer.sizeBytes != sizeof(Renderer::LightUniforms)) {
      writes += 1;
    }
  }
  return writes;
}

static void
runWorldVpFrame(WorldFixture& fixture, const std::array<float, 16>& vpArray)
{
  fixture.render.mock.resetCounters();
  fixture.render.renderer.setNextWorldViewProjection(vpArray);
  fixture.render.renderer.BeginFrame();
  fixture.render.renderer.RenderScene(&fixture.scene, &fixture.render.camera);
  fixture.render.renderer.EndFrame();
}

static int
shadowAtlasQuadrantsAndZeroUpload()
{
  WorldFixture fixture;
  RenderWorld& world = fixture.world;
  Renderer& renderer = fixture.render.renderer;
  RenderEnvironment environment;
  environment.shadowsEnabled = true;
  environment.shadowMapSize = 1024;
  world.setEnvironment(environment);

  bool ok = check(world.createMaterial(1, RenderMaterialDesc{}) &&
                    world.createMaterial(2, RenderMaterialDesc{}),
                  "atlas materials created");

  ok =
    check(
      world.createInstance(1, makeInstanceAtXz(fixture, 1, -0.5f, -3.0f)) &&
        world.createInstance(2, makeInstanceAtXz(fixture, 1, 0.5f, -14.0f)) &&
        world.createInstance(3, makeInstanceAtXz(fixture, 2, 0.0f, -45.0f)),
      "near, mid and far casters created") &&
    ok;

  RenderLightDesc spot;
  spot.kind = RenderLightKind::Spot;
  spot.position = { 0.0f, 3.0f, -3.0f };
  spot.direction = { -0.15f, -1.0f, 0.0f };
  spot.color = { 1.0f, 0.9f, 0.7f };
  spot.intensity = 25.0f;
  spot.range = 15.0f;
  spot.innerConeCos = std::cos(glm::radians(15.0f));
  spot.outerConeCos = std::cos(glm::radians(30.0f));
  spot.castsShadow = true;
  ok =
    check(world.createLight(1, spot), "hero spot shadow light created") && ok;

  const glm::mat4 vp =
    glm::perspective(glm::radians(60.0f), 640.0f / 480.0f, 0.2f, 90.0f) *
    glm::lookAt(glm::vec3(0.0f, 3.0f, 4.0f),
                glm::vec3(0.0f, 0.0f, -20.0f),
                glm::vec3(0, 1, 0));
  std::array<float, 16> vpArray{};
  std::copy_n(glm::value_ptr(vp), 16, vpArray.begin());

  runWorldVpFrame(fixture, vpArray);
  const Renderer::LightUniforms& lightBlock = renderer.getLightUniforms();
  ok = check(renderer.isShadowAtlasActive() &&
               renderer.getShadowCascadeCount() == 3 &&
               lightBlock.countsAndFlags[1] == 3 &&
               lightBlock.countsAndFlags[2] == 0 &&
               (lightBlock.countsAndFlags[3] & 4) != 0 &&
               std::abs(lightBlock.cascadeSplitsAndTexel[3] -
                        (1.0f / 2048.0f)) < 1e-6f,
             "perspective scene with hero spot activates 2048x2048 4-quadrant "
             "atlas") &&
       ok;

  bool sawQuad0 = false;
  bool sawQuad3 = false;
  for (size_t index = 0;
       index < fixture.render.mock.getLastNonEmptySubmittedCount();
       ++index) {
    const RenderCommand& cmd =
      fixture.render.mock.getLastNonEmptySubmitted(index);
    if (cmd.commandType == CommandType::SetUniformInt &&
        std::string(cmd.uniformInt.name) == "uShadowQuadrant") {
      sawQuad0 = sawQuad0 || cmd.uniformInt.value == 0;
      sawQuad3 = sawQuad3 || cmd.uniformInt.value == 3;
    }
  }
  ok = check(sawQuad0 && sawQuad3,
             "shadow pass emits draws into cascade 0 and spot quadrant 3") &&
       ok;
  ok = check(countSubmittedInstanceWrites(fixture.render.mock) > 0 &&
               world.stats().uploadedInstances > 0,
             "initial atlas frame uploads visible caster streams") &&
       ok;

  // Static second frame must skip instance uploads across all 4 quadrants.
  runWorldVpFrame(fixture, vpArray);
  ok = check(countSubmittedInstanceWrites(fixture.render.mock) == 0 &&
               world.stats().uploadedInstances == 0 &&
               world.stats().shadowInstances > 0,
             "unchanged atlas frame emits zero instance-buffer uploads") &&
       ok;

  // Live Renderer::ShadowDebugOverrides disables shadows immediately and
  // restores them when deactivated.
  const size_t recordingsBefore = world.stats().recordings;
  Renderer::ShadowDebugOverrides overrides{};
  overrides.active = true;
  overrides.shadowsEnabled = false;
  renderer.setShadowDebugOverrides(overrides);
  runWorldVpFrame(fixture, vpArray);
  ok = check(world.stats().shadowInstances == 0 &&
               world.stats().recordings > recordingsBefore,
             "active ShadowDebugOverrides suppresses shadows and re-records") &&
       ok;

  overrides.active = false;
  renderer.setShadowDebugOverrides(overrides);
  runWorldVpFrame(fixture, vpArray);
  ok = check(world.stats().shadowInstances > 0,
             "deactivating ShadowDebugOverrides restores scene shadows") &&
       ok;

  return ok ? 0 : 1;
}

void
registerRenderWorldTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.RenderWorld.SkyboxFromFrameCamera",
               skyboxFromFrameCamera);
  registry.add("Illumo.RenderWorld.Bench", renderWorldBench, 120);
  registry.add("Illumo.RenderWorld.BucketsRecordOnce", bucketsRecordOnce);
  registry.add("Illumo.RenderWorld.UnchangedBucketsSkipUploads",
               unchangedBucketsSkipUploads);
  registry.add("Illumo.RenderWorld.RejectsInvalidCalls", rejectsInvalidCalls);
  registry.add("Illumo.RenderWorld.ShadowsAndBlending", shadowsAndBlending);
  registry.add("Illumo.RenderWorld.PunctualLightsAndMaterials",
               punctualLightsAndMaterials);
  registry.add("Illumo.RenderWorld.ShadowAtlasQuadrantsAndZeroUpload",
               shadowAtlasQuadrantsAndZeroUpload);
  registry.add("Illumo.RecordedList.RecordingAndNesting", recordingAndNesting);
}
