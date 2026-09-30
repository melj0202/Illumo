// Real-GPU checks, one per command-line case:
// - instancing: a grid of lit cubes drawn one call per cube through LitMesh
//   and in one instanced call through LitMeshInstanced give the same image.
// - renderworld: a shadowed scene drawn by one MeshVisual per object and by
//   one RenderWorld give the same image, shadows included.
// - backendparity: OpenGL and Vulkan draw the same scenes to the same pixels
//   (GpuBackendParity.cpp).
// Options: `--api vulkan` runs instancing and renderworld on Vulkan instead;
// `--dump <dir>` saves the backendparity images. Returns 77 (skipped) when the
// chosen API has no device or context.

#include "GpuShared.h"
#include "Rendering/OpenGL/CreateOpenGLBackend.h"
#include "Rendering/Vulkan/CreateVulkanBackend.h"
#include <GLFW/glfw3.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/RenderWorld.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/TestHarness.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <string>
#include <vector>

static constexpr int kSize = 256;
static constexpr int kSkipped = 77;
static constexpr float kClear[3] = { 0.1f, 0.2f, 0.3f };

class HiddenWindow final : public NullRenderWindow
{
public:
  explicit HiddenWindow(GLFWwindow* value)
    : NullRenderWindow(kSize, kSize)
    , window(value)
  {
  }
  GLFWwindow* getWindowInstance() override { return window; }
  void swapBuffers() override
  {
    if (window != nullptr) {
      glfwSwapBuffers(window);
    }
  }

private:
  GLFWwindow* window;
};

struct LitVertex
{
  float position[3];
  float normal[3];
  std::uint8_t color[4];
  float uv[2];
};
static_assert(sizeof(LitVertex) == 36, "Pos3Norm3Color4U8Uv2 stride");

struct InstanceRecord
{
  float model[16];
  float previousModel[16];
  float tint[4];
};
static_assert(sizeof(InstanceRecord) == 144, "LitModelTint stride");

static void
addFace(std::vector<LitVertex>& vertices,
        std::vector<unsigned int>& indices,
        const glm::vec3& normal,
        const glm::vec3& up,
        const std::array<std::uint8_t, 4>& color)
{
  const glm::vec3 right = glm::cross(up, normal);
  const unsigned int base = static_cast<unsigned int>(vertices.size());
  const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
  for (const float* corner : corners) {
    const glm::vec3 p = 0.5f * (normal + corner[0] * right + corner[1] * up);
    LitVertex vertex{};
    vertex.position[0] = p.x;
    vertex.position[1] = p.y;
    vertex.position[2] = p.z;
    vertex.normal[0] = normal.x;
    vertex.normal[1] = normal.y;
    vertex.normal[2] = normal.z;
    std::memcpy(vertex.color, color.data(), 4);
    vertices.push_back(vertex);
  }
  for (unsigned int offset : { 0u, 1u, 2u, 0u, 2u, 3u }) {
    indices.push_back(base + offset);
  }
}

MeshAssetInfo
enrollCube(Renderer& renderer)
{
  std::vector<LitVertex> vertices;
  std::vector<unsigned int> indices;
  addFace(vertices, indices, { 0, 0, 1 }, { 0, 1, 0 }, { 230, 90, 70, 255 });
  addFace(vertices, indices, { 0, 0, -1 }, { 0, 1, 0 }, { 70, 200, 90, 255 });
  addFace(vertices, indices, { 1, 0, 0 }, { 0, 1, 0 }, { 80, 120, 230, 255 });
  addFace(vertices, indices, { -1, 0, 0 }, { 0, 1, 0 }, { 220, 210, 80, 255 });
  addFace(vertices, indices, { 0, 1, 0 }, { 0, 0, -1 }, { 200, 90, 210, 255 });
  addFace(vertices, indices, { 0, -1, 0 }, { 0, 0, 1 }, { 90, 210, 210, 255 });
  MeshAssetInfo info;
  info.handle = renderer.enrollMesh(vertices.data(),
                                    vertices.size() * sizeof(LitVertex),
                                    indices.data(),
                                    indices.size() * sizeof(unsigned int),
                                    MeshVertexLayout::Pos3Norm3Color4U8Uv2,
                                    false);
  info.vertexCount = static_cast<unsigned int>(vertices.size());
  info.indexCount = static_cast<unsigned int>(indices.size());
  info.minBounds = glm::vec3(-0.5f);
  info.maxBounds = glm::vec3(0.5f);
  return info;
}

static FramebufferHandle
createTarget(Renderer& renderer)
{
  FramebufferDesc desc;
  desc.width = kSize;
  desc.height = kSize;
  desc.colorAttachments.push_back(FramebufferAttachmentDesc{});
  desc.depthStencilFormat = TextureFormat::Depth24;
  return renderer.getBackend()->CreateFramebuffer(desc);
}

// Renders `scene` with `viewProjection` and reads `target` back.
static bool
renderToImage(Renderer& renderer,
              DrawList& scene,
              const std::array<float, 16>& viewProjection,
              FramebufferHandle target,
              FrameReadback& image)
{
  renderer.setNextWorldViewProjection(viewProjection);
  renderer.BeginFrame();
  renderer.RenderScene(&scene, renderer.getCamera());
  IBackend* backend = renderer.getBackend();
  const bool requested =
    backend->requestFramebufferReadback(1, target, kSize, kSize);
  renderer.EndFrame();
  if (!requested || !backend->takeFramebufferReadback(1, true, image)) {
    std::printf("Readback failed: %s\n", image.error.c_str());
    return false;
  }
  if (!renderer.frameError().empty() || !backend->submissionError().empty()) {
    std::printf("Frame failed: %s %s\n",
                renderer.frameError().c_str(),
                backend->submissionError().c_str());
    return false;
  }
  return true;
}

// Interiors must match exactly; edge pixels may flip because one path
// multiplies model matrices on the GPU and the other on the CPU.
static bool
imagesMatch(const char* label,
            const FrameReadback& reference,
            const FrameReadback& candidate)
{
  if (reference.pixels.size() != candidate.pixels.size() ||
      reference.pixels.empty()) {
    std::printf("FAILED: %s images have different sizes\n", label);
    return false;
  }
  size_t covered = 0;
  size_t differing = 0;
  int largest = 0;
  for (size_t index = 0; index < reference.pixels.size(); index += 4) {
    bool isClear = true;
    int difference = 0;
    for (size_t channel = 0; channel < 3; ++channel) {
      const int a = reference.pixels[index + channel];
      const int b = candidate.pixels[index + channel];
      const int clear = static_cast<int>(kClear[channel] * 255.0f + 0.5f);
      isClear = isClear && std::abs(a - clear) <= 1;
      difference = std::max(difference, std::abs(a - b));
    }
    covered += isClear ? 0u : 1u;
    differing += difference > 2 ? 1u : 0u;
    largest = std::max(largest, difference);
  }
  const size_t total = reference.pixels.size() / 4;
  std::printf("%s: %zu of %zu pixels covered, %zu differ by more than 2 "
              "(largest %d)\n",
              label,
              covered,
              total,
              differing,
              largest);
  if (covered < total / 10 || differing * 200 > covered) {
    std::printf("FAILED: %s images differ\n", label);
    return false;
  }
  return true;
}

static std::array<float, 16>
toArray(const glm::mat4& matrix)
{
  std::array<float, 16> values{};
  std::memcpy(values.data(), glm::value_ptr(matrix), sizeof(values));
  return values;
}

class CubeGrid final : public DrawableBase
{
public:
  bool instanced = false;
  bool failed = false;
  FramebufferHandle target{};
  MeshHandle mesh{};
  unsigned int indexCount = 0;
  BufferHandle instances{};
  std::vector<InstanceRecord> records;
  std::array<float, 16> viewProjection{};

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override
  {
    renderer->pushFramebuffer(target);
    renderer->pushViewport(0, 0, kSize, kSize);
    renderer->pushClearScreen(kClear[0], kClear[1], kClear[2], 1.0f);
    if (instanced) {
      failed = !renderer->bindStyle(RenderStyleId::LitMeshInstanced) ||
               !renderer->useFrameUniforms() || failed;
    } else {
      failed = !renderer->bindStyle(RenderStyleId::LitMesh) || failed;
    }
    renderer->pushUniformVec3("uLightDir", 0.4f, 1.0f, 0.3f);
    renderer->pushUniformVec3("uLightColor", 1.0f, 0.95f, 0.9f);
    renderer->pushUniformVec3("uAmbientColor", 0.2f, 0.22f, 0.25f);
    renderer->pushUniformInt("uShadowsEnabled", 0);
    renderer->pushUniformInt("uShadowPcf", 0);
    renderer->pushUniformInt("uMotionBlurEnabled", 0);
    renderer->pushUniformVec4("uTint", 0.9f, 0.8f, 1.0f, 1.0f);
    renderer->pushSetMesh(mesh);
    if (instanced) {
      const unsigned int bytes =
        static_cast<unsigned int>(records.size() * sizeof(InstanceRecord));
      failed =
        !renderer->pushWriteBuffer(instances, 0, bytes, records.data()) ||
        failed;
      renderer->pushInstanceStream(instances, 0, InstanceLayout::LitModelTint);
      renderer->pushDrawIndexedInstanced(
        indexCount, 0, static_cast<unsigned int>(records.size()));
    } else {
      const glm::mat4 vp = glm::make_mat4(viewProjection.data());
      for (const InstanceRecord& record : records) {
        const glm::mat4 model = glm::make_mat4(record.model);
        const glm::mat4 mvp = vp * model;
        renderer->pushUniformMat4("uMVP", glm::value_ptr(mvp));
        renderer->pushUniformMat4("uPrevMVP", glm::value_ptr(mvp));
        renderer->pushUniformMat4("uModel", record.model);
        renderer->pushDrawIndexed(indexCount, 0);
      }
    }
    renderer->pushFramebuffer(FramebufferHandle{});
    return true;
  }
};

static int
runInstancingParity(Renderer& renderer)
{
  renderer.ensureBuiltinStyles();
  const MeshAssetInfo cube = enrollCube(renderer);
  CubeGrid grid;
  grid.target = createTarget(renderer);
  grid.mesh = cube.handle;
  grid.indexCount = cube.indexCount;

  // An 8x8 grid, each cube rotated and scaled unevenly so the normal matrix
  // matters.
  for (int row = 0; row < 8; ++row) {
    for (int column = 0; column < 8; ++column) {
      glm::mat4 model =
        glm::translate(glm::mat4(1.0f),
                       glm::vec3(column * 1.5f - 5.25f, row * 1.5f - 5.25f, 0));
      model = glm::rotate(model,
                          0.3f + 0.17f * static_cast<float>(row * 8 + column),
                          glm::normalize(glm::vec3(0.3f, 1.0f, 0.5f)));
      model = glm::scale(
        model,
        glm::vec3(1.0f, 0.6f + 0.05f * static_cast<float>(column), 0.8f));
      InstanceRecord record{};
      std::memcpy(record.model, glm::value_ptr(model), sizeof(record.model));
      std::memcpy(
        record.previousModel, glm::value_ptr(model), sizeof(record.model));
      record.tint[0] = record.tint[1] = record.tint[2] = record.tint[3] = 1.0f;
      grid.records.push_back(record);
    }
  }
  grid.instances = renderer.enrollBuffer(
    BufferUsage::Instance, grid.records.size() * sizeof(InstanceRecord));
  grid.viewProjection = toArray(
    glm::perspective(glm::radians(50.0f), 1.0f, 0.1f, 100.0f) *
    glm::lookAt(glm::vec3(2, -3, 16), glm::vec3(0), glm::vec3(0, 1, 0)));
  if (!grid.target.isValid() || !grid.mesh.isValid() ||
      !grid.instances.isValid()) {
    std::printf("Resource creation failed\n");
    return 1;
  }

  DrawList scene(renderer.getWindow(), renderer.getCamera());
  scene.AddDrawable(&grid, RenderLayerId::World);
  FrameReadback reference;
  FrameReadback instanced;
  grid.instanced = false;
  if (!renderToImage(
        renderer, scene, grid.viewProjection, grid.target, reference)) {
    return 1;
  }
  grid.instanced = true;
  if (!renderToImage(
        renderer, scene, grid.viewProjection, grid.target, instanced) ||
      grid.failed || !imagesMatch("GpuInstancing", reference, instanced)) {
    return 1;
  }
  renderer.destroyBuffer(grid.instances);
  renderer.destroyMesh(grid.mesh);
  renderer.getBackend()->DestroyFramebuffer(grid.target);
  return 0;
}

// Draws its drawables into an offscreen target and forwards the shared shadow
// pass to them, so any World drawables can be compared by readback.
class TargetScope final : public DrawableBase
{
public:
  FramebufferHandle target{};
  std::vector<DrawableBase*> inner;

  void Draw() override {}
  void CollectShadowCasters(Renderer* renderer) override
  {
    for (DrawableBase* drawable : inner) {
      drawable->CollectShadowCasters(renderer);
    }
  }
  void AppendShadowCommands(Renderer* renderer) override
  {
    for (DrawableBase* drawable : inner) {
      drawable->AppendShadowCommands(renderer);
    }
  }
  bool AppendCommands(Renderer* renderer) override
  {
    renderer->pushFramebuffer(target);
    renderer->pushViewport(0, 0, kSize, kSize);
    renderer->pushClearScreen(kClear[0], kClear[1], kClear[2], 1.0f);
    bool complete = true;
    for (DrawableBase* drawable : inner) {
      complete = drawable->AppendCommands(renderer) && complete;
    }
    renderer->pushFramebuffer(FramebufferHandle{});
    return complete;
  }
};

static int
runRenderWorldParity(Renderer& renderer)
{
  renderer.ensureBuiltinStyles();
  const MeshAssetInfo cube = enrollCube(renderer);
  const FramebufferHandle target = createTarget(renderer);
  if (!target.isValid() || !cube.isValid()) {
    std::printf("Resource creation failed\n");
    return 1;
  }
  // A low light casts long shadows across the floor.
  const glm::vec3 light(0.9f, 0.6f, 0.5f);
  RenderEnvironment environment;
  environment.lightDirection = { light.x, light.y, light.z };
  environment.lightColor = { 1.0f, 0.95f, 0.9f };
  environment.ambientColor = { 0.2f, 0.22f, 0.25f };
  environment.shadowsEnabled = true;
  environment.shadowPcf = true;
  environment.shadowMapSize = 1024;

  // A floor first, then a 5x5 grid of rotated cubes standing on it.
  std::vector<glm::mat4> models;
  models.push_back(
    glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.6f, 0.0f)),
               glm::vec3(14.0f, 0.2f, 14.0f)));
  for (int row = 0; row < 5; ++row) {
    for (int column = 0; column < 5; ++column) {
      glm::mat4 model = glm::translate(
        glm::mat4(1.0f),
        glm::vec3(column * 2.0f - 4.0f, 0.0f, row * 2.0f - 4.0f));
      model = glm::rotate(model,
                          0.2f * static_cast<float>(row * 5 + column),
                          glm::vec3(0.0f, 1.0f, 0.0f));
      models.push_back(glm::scale(
        model, glm::vec3(1.0f, 0.8f + 0.1f * static_cast<float>(row), 1.0f)));
    }
  }

  // Reference: one MeshVisual per object.
  std::vector<std::unique_ptr<MeshVisual>> visuals;
  TargetScope reference;
  reference.target = target;
  for (const glm::mat4& model : models) {
    std::unique_ptr<MeshVisual> visual = std::make_unique<MeshVisual>();
    visual->setMeshAsset(cube, ColorRgba{ 255, 255, 255, 255 });
    visual->setModelMatrix(model);
    visual->setLightingEnabled(true);
    visual->setLightDirection(light);
    visual->setLightColor(glm::vec3(1.0f, 0.95f, 0.9f));
    visual->setAmbientColor(glm::vec3(0.2f, 0.22f, 0.25f));
    visual->setShadowsEnabled(true);
    visual->setShadowPcfEnabled(true);
    visual->setShadowMapSize(environment.shadowMapSize);
    visual->setShadowRadius(environment.shadowMinimumRadius);
    visual->setLightDistance(environment.shadowLightDistance);
    visual->setShadowCasterDistance(environment.shadowCasterDistance);
    visual->setShadowBias(environment.shadowBias);
    visual->setShadowSlopeScale(environment.shadowSlopeScale);
    visual->setShadowNormalOffset(environment.shadowNormalOffset);
    visual->setMotionBlurEnabled(false);
    visual->prepare(&renderer);
    reference.inner.push_back(visual.get());
    visuals.push_back(std::move(visual));
  }

  // Candidate: the same objects in one RenderWorld.
  RenderWorld world;
  world.setEnvironment(environment);
  bool created = world.createMaterial(1, RenderMaterialDesc{});
  for (size_t index = 0; index < models.size(); ++index) {
    RenderInstanceDesc desc;
    desc.mesh = cube.handle;
    desc.indexCount = cube.indexCount;
    desc.material = 1;
    desc.world = toArray(models[index]);
    desc.hasBounds = true;
    desc.localBounds = { cube.minBounds, cube.maxBounds };
    created =
      world.createInstance(static_cast<RenderInstanceId>(index + 1), desc) &&
      created;
  }
  TargetScope candidate;
  candidate.target = target;
  candidate.inner.push_back(&world);
  if (!created) {
    std::printf("FAILED: RenderWorld refused the scene\n");
    return 1;
  }

  const std::array<float, 16> viewProjection =
    toArray(glm::perspective(glm::radians(55.0f), 1.0f, 0.1f, 100.0f) *
            glm::lookAt(
              glm::vec3(6.0f, 9.0f, 11.0f), glm::vec3(0), glm::vec3(0, 1, 0)));
  DrawList referenceScene(renderer.getWindow(), renderer.getCamera());
  referenceScene.AddDrawable(&reference, RenderLayerId::World);
  DrawList candidateScene(renderer.getWindow(), renderer.getCamera());
  candidateScene.AddDrawable(&candidate, RenderLayerId::World);
  FrameReadback referenceImage;
  FrameReadback candidateImage;
  const bool rendered =
    renderToImage(
      renderer, referenceScene, viewProjection, target, referenceImage) &&
    renderToImage(
      renderer, candidateScene, viewProjection, target, candidateImage);
  const RenderWorld::Stats stats = world.stats();
  std::printf("RenderWorld: %zu buckets, %zu drawn, %zu in the shadow map, %zu "
              "recordings\n",
              stats.buckets,
              stats.drawnInstances,
              stats.shadowInstances,
              stats.recordings);
  const bool matched =
    rendered &&
    imagesMatch("RenderWorldParity", referenceImage, candidateImage);

  // The match only proves shadows if they change the image: without them,
  // many floor pixels must brighten.
  environment.shadowsEnabled = false;
  world.setEnvironment(environment);
  FrameReadback unshadowed;
  size_t brightened = 0;
  const bool renderedUnshadowed =
    renderToImage(renderer, candidateScene, viewProjection, target, unshadowed);
  for (size_t index = 0; renderedUnshadowed && index < unshadowed.pixels.size();
       index += 4) {
    int difference = 0;
    for (size_t channel = 0; channel < 3; ++channel) {
      difference = std::max(difference,
                            unshadowed.pixels[index + channel] -
                              candidateImage.pixels[index + channel]);
    }
    brightened += difference > 8 ? 1u : 0u;
  }
  std::printf("RenderWorld: %zu pixels brighten without shadows\n", brightened);
  world.releaseResources();
  renderer.getBackend()->DestroyFramebuffer(target);
  if (!matched || stats.shadowInstances == 0 || brightened < 500) {
    std::printf("FAILED: RenderWorld did not match MeshVisual with shadows\n");
    return 1;
  }
  return 0;
}

int
main(int argc, char** argv)
{
  std::string test = "instancing";
  std::string api = "opengl";
  std::string dumpDirectory;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--api" && index + 1 < argc) {
      api = argv[++index];
    } else if (argument == "--dump" && index + 1 < argc) {
      dumpDirectory = argv[++index];
    } else {
      test = argument;
    }
  }
  if (test != "instancing" && test != "renderworld" &&
      test != "backendparity") {
    std::printf("Unknown case %s (instancing, renderworld or backendparity)\n",
                test.c_str());
    return 2;
  }
  if (api != "opengl" && api != "vulkan") {
    std::printf("Unknown API %s (opengl or vulkan)\n", api.c_str());
    return 2;
  }
  if (glfwInit() != GLFW_TRUE) {
    std::printf("SKIPPED: GLFW could not initialize\n");
    return kSkipped;
  }
  // Vulkan renders offscreen with no window; OpenGL and the parity case need
  // a hidden 3.3 context.
  const bool vulkan = api == "vulkan" && test != "backendparity";
  GLFWwindow* context = nullptr;
  if (!vulkan) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    context =
      glfwCreateWindow(kSize, kSize, "IllumoGpuTests", nullptr, nullptr);
    if (context == nullptr) {
      std::printf("SKIPPED: no OpenGL 3.3 context\n");
      glfwTerminate();
      return kSkipped;
    }
    glfwMakeContextCurrent(context);
  }
  int result = 1;
  if (test == "backendparity") {
    result = runBackendParity(context, dumpDirectory);
  } else {
    HiddenWindow window(context);
    EnvVars env;
    env.setVar("WinX", kSize);
    env.setVar("WinY", kSize);
    Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
    std::unique_ptr<IBackend> backend = vulkan
                                          ? CreateVulkanBackend(&window, false)
                                          : CreateOpenGLBackend(&window);
    if (backend && backend->Initialize()) {
      Renderer renderer(&window, &env, &camera, std::move(backend));
      result = test == "instancing" ? runInstancingParity(renderer)
                                    : runRenderWorldParity(renderer);
    } else if (vulkan) {
      std::printf("SKIPPED: the Vulkan backend did not initialize\n");
      result = kSkipped;
    } else {
      std::printf("FAILED: the OpenGL backend did not initialize\n");
    }
  }
  if (context != nullptr) {
    glfwDestroyWindow(context);
  }
  glfwTerminate();
  return result;
}
