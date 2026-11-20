// Real-GPU parity of the OpenGL backend with Vulkan: every scene renders
// through each in one process and each image is compared with OpenGL's.
// Scenes cover the
// token contract end to end: lit, shadowed and instanced 3D with a sky, 2D
// shapes, sprites and text through the offscreen panel path, raw tokens
// (culling, points, lines, wireframe, blending, depth, scissored clears,
// instancing, uniform arrays, texture units, missing attributes), texture
// formats with in-frame updates, render-target readback conversions and
// multiple-target clears, and the default framebuffer with the motion-blur
// passes.

#include "GpuShared.h"
#include "Rendering/OpenGL/CreateOpenGLBackend.h"
#include "Rendering/Vulkan/CreateVulkanBackend.h"
#include <GLFW/glfw3.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/RenderPass.h>
#include <Illumo/Rendering/RenderWorld.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/TestHarness.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <string>
#include <thread>
#include <vector>

static constexpr int kSize = 256;

class ParityWindow final : public NullRenderWindow
{
public:
  explicit ParityWindow(GLFWwindow* value,
                        int width = kSize,
                        int height = kSize)
    : NullRenderWindow(width, height)
    , window(value)
  {
  }
  GLFWwindow* getWindowInstance() override { return window; }
  void swapBuffers() override
  {
    if (window != nullptr && getWindowDimensions()[0] == kSize) {
      glfwSwapBuffers(window);
    }
  }

private:
  GLFWwindow* window;
};

enum class ParityApi
{
  OpenGl,
  Vulkan,
};

struct ParityRig
{
  const char* name = "";
  // The suffix of the images saveComparison writes.
  const char* tag = "";
  std::unique_ptr<ParityWindow> window;
  std::unique_ptr<EnvVars> env;
  std::unique_ptr<Camera> camera;
  std::unique_ptr<Renderer> renderer;
};

struct ParityScene
{
  const char* name;
  std::function<bool(Renderer&, FrameReadback&)> render;
  // Share of pixels allowed to differ by more than two levels.
  double tolerance;
};

static bool
makeRig(ParityRig& rig,
        ParityApi api,
        GLFWwindow* context,
        int width = kSize,
        int height = kSize)
{
  rig.window = std::make_unique<ParityWindow>(
    api == ParityApi::OpenGl ? context : nullptr, width, height);
  rig.env = std::make_unique<EnvVars>();
  rig.env->setVar("WinX", width);
  rig.env->setVar("WinY", height);
  rig.camera =
    std::make_unique<Camera>(glm::vec2(0.0f, 0.0f), 1.0f, rig.env.get());
  std::unique_ptr<IBackend> backend;
  if (api == ParityApi::OpenGl) {
    rig.name = "OpenGL";
    rig.tag = "gl";
    backend = CreateOpenGLBackend(rig.window.get());
  } else {
    rig.name = "Vulkan";
    rig.tag = "vk";
    backend = CreateVulkanBackend(rig.window.get(), false);
  }
  const char* name = rig.name;
  if (!backend || !backend->Initialize()) {
    std::printf("SKIPPED: the %s backend did not initialize\n", name);
    return false;
  }
  rig.renderer = std::make_unique<Renderer>(
    rig.window.get(), rig.env.get(), rig.camera.get(), std::move(backend));
  rig.renderer->ensureBuiltinStyles();
  return true;
}

static std::array<float, 16>
toArray(const glm::mat4& matrix)
{
  std::array<float, 16> values{};
  std::memcpy(values.data(), glm::value_ptr(matrix), sizeof(values));
  return values;
}

static FramebufferHandle
createTarget(Renderer& renderer, bool depth)
{
  FramebufferDesc desc;
  desc.width = kSize;
  desc.height = kSize;
  desc.colorAttachments.push_back(FramebufferAttachmentDesc{});
  desc.depthStencilFormat =
    depth ? TextureFormat::Depth24 : TextureFormat::None;
  return renderer.getBackend()->CreateFramebuffer(desc);
}

static bool
readTarget(Renderer& renderer,
           FramebufferHandle target,
           int width,
           int height,
           FrameReadback& image)
{
  IBackend* backend = renderer.getBackend();
  if (!backend->requestFramebufferReadback(9, target, width, height) ||
      !backend->takeFramebufferReadback(9, true, image)) {
    std::printf("  readback failed: %s\n", image.error.c_str());
    backend->releaseReadbackStream(9);
    return false;
  }
  backend->releaseReadbackStream(9);
  return true;
}

// Draws its drawables into a target and forwards the shared shadow pass.
class ParityTargetScope final : public DrawableBase
{
public:
  FramebufferHandle target{};
  std::vector<DrawableBase*> inner;
  std::array<float, 4> clear{ 0.1f, 0.2f, 0.3f, 1.0f };

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
    renderer->pushClearScreen(clear[0], clear[1], clear[2], clear[3]);
    bool complete = true;
    for (DrawableBase* drawable : inner) {
      complete = drawable->AppendCommands(renderer) && complete;
    }
    renderer->pushFramebuffer(FramebufferHandle{});
    return complete;
  }
};

static bool
renderFrame(Renderer& renderer,
            DrawList& scene,
            const std::array<float, 16>& viewProjection)
{
  renderer.setNextWorldViewProjection(viewProjection);
  renderer.BeginFrame();
  renderer.RenderScene(&scene, renderer.getCamera());
  const bool clean = renderer.frameError().empty();
  if (!clean) {
    std::printf("  frame error: %s\n", renderer.frameError().c_str());
  }
  return clean;
}

static TextureHandle
makeCubemap(Renderer& renderer)
{
  std::array<std::vector<unsigned char>, 6> faces;
  std::array<const unsigned char*, 6> pointers{};
  for (size_t face = 0; face < 6; ++face) {
    faces[face].resize(16u * 16u * 4u);
    for (size_t y = 0; y < 16; ++y) {
      for (size_t x = 0; x < 16; ++x) {
        unsigned char* texel = faces[face].data() + (y * 16u + x) * 4u;
        texel[0] = static_cast<unsigned char>(40 + face * 35);
        texel[1] = static_cast<unsigned char>(x * 15);
        texel[2] = static_cast<unsigned char>(y * 15);
        texel[3] = 255;
      }
    }
    pointers[face] = faces[face].data();
  }
  return renderer.enrollCubemap(pointers, 16, 16, 4);
}

// Lit meshes with shadows, one RenderWorld of instanced cubes and a sky.
static bool
sceneWorld(Renderer& renderer, FrameReadback& image)
{
  const FramebufferHandle target = createTarget(renderer, true);
  const TextureHandle sky = makeCubemap(renderer);
  // Lit MeshVisual primitives: a floor, a cube and a pyramid, plus lines.
  MeshVisual floor;
  MeshVisual shapes;
  const glm::vec3 light(0.8f, 0.7f, 0.4f);
  for (MeshVisual* visual : { &floor, &shapes }) {
    visual->setLightingEnabled(true);
    visual->setLightDirection(light);
    visual->setShadowsEnabled(true);
    visual->setShadowPcfEnabled(true);
    visual->setMotionBlurEnabled(false);
  }
  floor.addSolidCube(glm::vec3(0.0f, -1.0f, 0.0f),
                     glm::vec3(4.0f, 0.05f, 4.0f),
                     ColorRgba{ 180, 180, 180, 255 });
  shapes.addSolidCube(glm::vec3(-1.5f, -0.4f, 0.0f),
                      glm::vec3(0.6f),
                      ColorRgba{ 210, 110, 65, 255 });
  shapes.addSolidPyramid(glm::vec3(-0.2f, -0.5f, 1.5f),
                         glm::vec3(0.5f),
                         ColorRgba{ 120, 200, 90, 255 });
  shapes.addWireCube(glm::vec3(0.0f, 0.8f, 0.0f), glm::vec3(0.4f));
  floor.prepare(&renderer);
  shapes.prepare(&renderer);

  // A RenderWorld of instanced cubes, shadowed, in front of a sky.
  const MeshAssetInfo cube = enrollCube(renderer);
  RenderWorld world;
  RenderEnvironment environment;
  environment.lightDirection = { light.x, light.y, light.z };
  environment.shadowsEnabled = true;
  environment.shadowPcf = true;
  world.setEnvironment(environment);
  bool created =
    world.setSkybox(RenderSkyboxDesc{ sky, { 1.0f, 0.9f, 0.8f, 1.0f } }) &&
    world.createMaterial(1, RenderMaterialDesc{});
  for (int index = 0; index < 6; ++index) {
    RenderInstanceDesc desc;
    desc.mesh = cube.handle;
    desc.indexCount = cube.indexCount;
    desc.material = 1;
    desc.hasBounds = true;
    desc.localBounds = { cube.minBounds, cube.maxBounds };
    const glm::vec3 position(1.0f + static_cast<float>(index % 3) * 1.2f,
                             -0.4f,
                             -1.5f + static_cast<float>(index / 3) * 2.0f);
    desc.world = toArray(glm::rotate(glm::translate(glm::mat4(1.0f), position),
                                     0.3f * static_cast<float>(index),
                                     glm::vec3(0.0f, 1.0f, 0.0f)));
    created =
      world.createInstance(static_cast<RenderInstanceId>(index + 1), desc) &&
      created;
  }
  ParityTargetScope scope;
  scope.target = target;
  scope.inner = { &world, &floor, &shapes };
  DrawList scene(renderer.getWindow(), renderer.getCamera());
  scene.AddDrawable(&scope, RenderLayerId::World);
  const std::array<float, 16> viewProjection =
    toArray(glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 50.0f) *
            glm::lookAt(glm::vec3(3.5f, 3.0f, 6.0f),
                        glm::vec3(0.0f, -0.5f, 0.0f),
                        glm::vec3(0, 1, 0)));
  const bool rendered = created && renderFrame(renderer, scene, viewProjection);
  renderer.EndFrame();
  const bool read =
    rendered && readTarget(renderer, target, kSize, kSize, image);
  world.releaseResources();
  renderer.destroyMesh(cube.handle);
  renderer.destroyTexture(sky);
  renderer.getBackend()->DestroyFramebuffer(target);
  return read;
}

struct PbrParityVertex
{
  float position[3];
  float normal[3];
  std::uint8_t color[4];
  float uv[2];
};

static MeshAssetInfo
enrollPbrSphereAndDegenerateQuad(Renderer& renderer)
{
  std::vector<PbrParityVertex> vertices;
  std::vector<unsigned int> indices;
  constexpr int kStacks = 16;
  constexpr int kSlices = 24;
  constexpr float kPi = 3.14159265359f;
  for (int stack = 0; stack <= kStacks; ++stack) {
    const float v = static_cast<float>(stack) / static_cast<float>(kStacks);
    const float phi = v * kPi;
    const float sinPhi = std::sin(phi);
    const float cosPhi = std::cos(phi);
    for (int slice = 0; slice <= kSlices; ++slice) {
      const float u = static_cast<float>(slice) / static_cast<float>(kSlices);
      const float theta = u * 2.0f * kPi;
      const glm::vec3 n(
        sinPhi * std::cos(theta), cosPhi, sinPhi * std::sin(theta));
      PbrParityVertex vert{};
      vert.position[0] = n.x * 0.75f;
      vert.position[1] = n.y * 0.75f;
      vert.position[2] = n.z * 0.75f;
      vert.normal[0] = n.x;
      vert.normal[1] = n.y;
      vert.normal[2] = n.z;
      vert.color[0] = 255;
      vert.color[1] = 255;
      vert.color[2] = 255;
      vert.color[3] = 255;
      vert.uv[0] = u * 2.0f;
      vert.uv[1] = v * 2.0f;
      vertices.push_back(vert);
    }
  }
  const int ring = kSlices + 1;
  for (int stack = 0; stack < kStacks; ++stack) {
    for (int slice = 0; slice < kSlices; ++slice) {
      const unsigned int a = static_cast<unsigned int>(stack * ring + slice);
      const unsigned int b = a + static_cast<unsigned int>(ring);
      indices.push_back(a);
      indices.push_back(b);
      indices.push_back(a + 1u);
      indices.push_back(a + 1u);
      indices.push_back(b);
      indices.push_back(b + 1u);
    }
  }
  // Constant-UV quad exercising the NaN-safe cotangent normal fallback.
  const unsigned int base = static_cast<unsigned int>(vertices.size());
  const float quadCorners[4][2] = {
    { -0.4f, -0.4f }, { 0.4f, -0.4f }, { 0.4f, 0.4f }, { -0.4f, 0.4f }
  };
  for (int c = 0; c < 4; ++c) {
    PbrParityVertex vert{};
    vert.position[0] = quadCorners[c][0];
    vert.position[1] = quadCorners[c][1];
    vert.position[2] = 0.85f;
    vert.normal[0] = 0.0f;
    vert.normal[1] = 0.0f;
    vert.normal[2] = 1.0f;
    vert.color[0] = 230;
    vert.color[1] = 220;
    vert.color[2] = 210;
    vert.color[3] = 255;
    vert.uv[0] = 0.5f;
    vert.uv[1] = 0.5f;
    vertices.push_back(vert);
  }
  indices.push_back(base + 0u);
  indices.push_back(base + 1u);
  indices.push_back(base + 2u);
  indices.push_back(base + 0u);
  indices.push_back(base + 2u);
  indices.push_back(base + 3u);

  MeshAssetInfo info;
  info.handle = renderer.enrollMesh(vertices.data(),
                                    vertices.size() * sizeof(PbrParityVertex),
                                    indices.data(),
                                    indices.size() * sizeof(unsigned int),
                                    MeshVertexLayout::Pos3Norm3Color4U8Uv2,
                                    false);
  info.vertexCount = static_cast<unsigned int>(vertices.size());
  info.indexCount = static_cast<unsigned int>(indices.size());
  info.minBounds = glm::vec3(-0.9f);
  info.maxBounds = glm::vec3(0.9f);
  return info;
}

// Textured GGX PBR-lite materials, cotangent normal map + degenerate-UV guard,
// emissive map, and 16 colored point/spot lights across RenderWorld +
// MeshVisual.
static bool
scenePbrPunctualLights(Renderer& renderer, FrameReadback& image)
{
  const FramebufferHandle target = createTarget(renderer, true);
  const MeshAssetInfo sphere = enrollPbrSphereAndDegenerateQuad(renderer);
  constexpr int kTexDim = 16;
  std::vector<unsigned char> albedoBytes(kTexDim * kTexDim * 4u);
  std::vector<unsigned char> normalBytes(kTexDim * kTexDim * 4u);
  std::vector<unsigned char> ormBytes(kTexDim * kTexDim * 4u);
  std::vector<unsigned char> emissiveBytes(kTexDim * kTexDim * 4u);
  for (int y = 0; y < kTexDim; ++y) {
    for (int x = 0; x < kTexDim; ++x) {
      const size_t idx = static_cast<size_t>(y * kTexDim + x) * 4u;
      const bool tile = (((x / 4) + (y / 4)) % 2) == 0;
      albedoBytes[idx + 0] = tile ? 235 : 140;
      albedoBytes[idx + 1] = tile ? 195 : 165;
      albedoBytes[idx + 2] = tile ? 120 : 220;
      albedoBytes[idx + 3] = 255;

      const float nx =
        ((x % 4) == 0) ? -0.45f : (((x % 4) == 3) ? 0.45f : 0.0f);
      const float ny =
        ((y % 4) == 0) ? -0.45f : (((y % 4) == 3) ? 0.45f : 0.0f);
      const glm::vec3 n = glm::normalize(glm::vec3(nx, ny, 1.0f));
      normalBytes[idx + 0] = static_cast<unsigned char>(
        std::clamp((n.x * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
      normalBytes[idx + 1] = static_cast<unsigned char>(
        std::clamp((n.y * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
      normalBytes[idx + 2] = static_cast<unsigned char>(
        std::clamp((n.z * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f));
      normalBytes[idx + 3] = 255;

      ormBytes[idx + 0] = tile ? 255 : 190;
      ormBytes[idx + 1] = tile ? 70 : 190;
      ormBytes[idx + 2] = tile ? 255 : 40;
      ormBytes[idx + 3] = 255;

      const bool band = (y == 7 || y == 8);
      emissiveBytes[idx + 0] = band ? 255 : 0;
      emissiveBytes[idx + 1] = band ? 140 : 0;
      emissiveBytes[idx + 2] = band ? 40 : 0;
      emissiveBytes[idx + 3] = 255;
    }
  }
  TextureOptions repeatLinear;
  repeatLinear.filter = TextureFilter::Linear;
  repeatLinear.wrapX = TextureWrap::Repeat;
  repeatLinear.wrapY = TextureWrap::Repeat;
  const TextureHandle albedoTex = renderer.enrollTexture(
    albedoBytes.data(), kTexDim, kTexDim, 4, repeatLinear);
  const TextureHandle normalTex = renderer.enrollTexture(
    normalBytes.data(), kTexDim, kTexDim, 4, repeatLinear);
  const TextureHandle ormTex =
    renderer.enrollTexture(ormBytes.data(), kTexDim, kTexDim, 4, repeatLinear);
  const TextureHandle emissiveTex = renderer.enrollTexture(
    emissiveBytes.data(), kTexDim, kTexDim, 4, repeatLinear);

  RenderWorld world;
  RenderEnvironment env;
  env.lightDirection = { 0.3f, 1.0f, 0.4f };
  env.lightColor = { 0.15f, 0.15f, 0.18f };
  env.ambientColor = { 0.04f, 0.04f, 0.05f };
  env.shadowsEnabled = false;
  world.setEnvironment(env);

  RenderMaterialDesc metallicMat;
  metallicMat.albedoTexture = albedoTex;
  metallicMat.normalTexture = normalTex;
  metallicMat.metallicRoughnessTexture = ormTex;
  metallicMat.emissiveTexture = emissiveTex;
  metallicMat.metallic = 1.0f;
  metallicMat.roughness = 0.35f;
  metallicMat.emissiveFactor = { 0.35f, 0.25f, 0.1f };

  RenderMaterialDesc dielectricMat = metallicMat;
  dielectricMat.metallic = 0.0f;
  dielectricMat.roughness = 0.55f;
  dielectricMat.emissiveFactor = { 0.0f, 0.0f, 0.0f };

  bool ok = world.createMaterial(1, metallicMat) &&
            world.createMaterial(2, dielectricMat);
  const glm::vec3 positions[3] = {
    glm::vec3(-1.55f, 0.6f, 0.0f),
    glm::vec3(0.0f, 0.6f, 0.0f),
    glm::vec3(1.55f, -0.7f, 0.0f),
  };
  for (size_t i = 0; i < 3; ++i) {
    RenderInstanceDesc inst;
    inst.mesh = sphere.handle;
    inst.indexCount = sphere.indexCount;
    inst.material = (i % 2 == 0) ? 1u : 2u;
    inst.hasBounds = true;
    inst.localBounds = { sphere.minBounds, sphere.maxBounds };
    inst.world = toArray(glm::translate(glm::mat4(1.0f), positions[i]));
    ok =
      world.createInstance(static_cast<RenderInstanceId>(i + 1u), inst) && ok;
  }

  // MeshVisual companion exercising non-instanced PBR + cotangent normals.
  MeshVisual visual;
  visual.setMeshAsset(sphere, ColorRgba{ 255, 255, 255, 255 });
  visual.setModelMatrix(
    glm::translate(glm::mat4(1.0f), glm::vec3(-0.75f, -0.75f, 0.2f)));
  visual.setLightingEnabled(true);
  visual.setLightDirection(glm::vec3(0.3f, 1.0f, 0.4f));
  visual.setLightColor(glm::vec3(0.15f, 0.15f, 0.18f));
  visual.setAmbientColor(glm::vec3(0.04f, 0.04f, 0.05f));
  visual.setShadowsEnabled(false);
  visual.setMotionBlurEnabled(false);
  visual.setAlbedoTexture(albedoTex);
  visual.setNormalTexture(normalTex);
  visual.setMetallicRoughnessTexture(ormTex);
  visual.setEmissiveTexture(emissiveTex);
  visual.setMetallic(0.85f);
  visual.setRoughness(0.3f);
  visual.setEmissiveFactor(glm::vec3(0.2f, 0.35f, 0.45f));
  visual.prepare(&renderer);

  ParityTargetScope scope;
  scope.target = target;
  scope.clear = { 0.02f, 0.02f, 0.03f, 1.0f };
  scope.inner = { &world, &visual };
  DrawList scene(renderer.getWindow(), renderer.getCamera());
  scene.AddDrawable(&scope, RenderLayerId::World);

  const std::array<float, 16> viewProjection =
    toArray(glm::perspective(glm::radians(50.0f), 1.0f, 0.1f, 50.0f) *
            glm::lookAt(glm::vec3(0.0f, 0.2f, 4.8f),
                        glm::vec3(0.0f, -0.05f, 0.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)));

  // Render dark baseline before punctual lights are added.
  FrameReadback unlitPunctual;
  ok = ok && renderFrame(renderer, scene, viewProjection);
  renderer.EndFrame();
  ok = ok && readTarget(renderer, target, kSize, kSize, unlitPunctual);

  // Add 16 colored point and spot lights around the spheres.
  constexpr float kTwoPi = 6.28318530718f;
  for (uint32_t i = 0; i < 16u; ++i) {
    const float angle = (static_cast<float>(i) / 16.0f) * kTwoPi;
    const glm::vec3 pos(std::cos(angle) * 2.4f,
                        ((i % 2u) == 0u) ? 1.3f : -1.3f,
                        1.6f + std::sin(angle) * 0.8f);
    const glm::vec3 toOrigin = glm::normalize(-pos);
    RenderLightDesc light;
    light.kind =
      ((i % 3u) == 0u) ? RenderLightKind::Spot : RenderLightKind::Point;
    light.position = { pos.x, pos.y, pos.z };
    light.direction = { toOrigin.x, toOrigin.y, toOrigin.z };
    light.color = { 0.3f + 0.7f * (0.5f + 0.5f * std::cos(angle)),
                    0.3f + 0.7f * (0.5f + 0.5f * std::sin(angle)),
                    0.3f + 0.7f * (0.5f - 0.5f * std::cos(angle)) };
    light.intensity = ((i % 3u) == 0u) ? 4.5f : 2.2f;
    light.range = 6.5f;
    light.innerConeCos = std::cos(glm::radians(18.0f));
    light.outerConeCos = std::cos(glm::radians(32.0f));
    ok = world.createLight(i + 1u, light) && ok;
  }

  ok = ok && renderFrame(renderer, scene, viewProjection);
  renderer.EndFrame();
  ok = ok && readTarget(renderer, target, kSize, kSize, image);

  size_t litGainPixels = 0;
  for (size_t px = 0; ok && px + 3 < image.pixels.size(); px += 4) {
    const int delta =
      std::max({ static_cast<int>(image.pixels[px + 0]) -
                   static_cast<int>(unlitPunctual.pixels[px + 0]),
                 static_cast<int>(image.pixels[px + 1]) -
                   static_cast<int>(unlitPunctual.pixels[px + 1]),
                 static_cast<int>(image.pixels[px + 2]) -
                   static_cast<int>(unlitPunctual.pixels[px + 2]) });
    if (delta > 16) {
      litGainPixels += 1;
    }
  }
  if (litGainPixels < 1500) {
    std::printf("  pbr punctual lights brightened only %zu pixels\n",
                litGainPixels);
    ok = false;
  }

  world.releaseResources();
  renderer.destroyMesh(sphere.handle);
  renderer.destroyTexture(albedoTex);
  renderer.destroyTexture(normalTex);
  renderer.destroyTexture(ormTex);
  renderer.destroyTexture(emissiveTex);
  renderer.getBackend()->DestroyFramebuffer(target);
  return ok;
}

// 4-quadrant shadow atlas: 3 directional CSM cascades + 1 hero spot shadow.
static bool
sceneShadowAtlas(Renderer& renderer, FrameReadback& image)
{
  const FramebufferHandle target = createTarget(renderer, true);
  const MeshAssetInfo cube = enrollCube(renderer);
  RenderWorld world;

  RenderEnvironment env;
  env.lightDirection = { 0.55f, 1.0f, 0.35f };
  env.lightColor = { 0.85f, 0.82f, 0.78f };
  env.ambientColor = { 0.05f, 0.05f, 0.06f };
  env.shadowsEnabled = true;
  env.shadowMapSize = 512;
  env.shadowPcf = true;
  world.setEnvironment(env);

  RenderMaterialDesc floorMat;
  floorMat.tint = { 0.85f, 0.85f, 0.88f, 1.0f };
  floorMat.roughness = 0.85f;
  floorMat.castsShadow = false;
  floorMat.receivesShadow = true;

  RenderMaterialDesc casterMat;
  casterMat.tint = { 0.75f, 0.35f, 0.25f, 1.0f };
  casterMat.roughness = 0.5f;
  casterMat.castsShadow = false;
  casterMat.receivesShadow = true;

  bool ok =
    world.createMaterial(1, floorMat) && world.createMaterial(2, casterMat);

  RenderInstanceDesc floorInst;
  floorInst.mesh = cube.handle;
  floorInst.indexCount = cube.indexCount;
  floorInst.material = 1;
  floorInst.hasBounds = true;
  floorInst.localBounds = { cube.minBounds, cube.maxBounds };
  floorInst.world =
    toArray(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.2f, -12.0f)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(24.0f, 0.4f, 36.0f)));
  ok = world.createInstance(1, floorInst) && ok;

  const glm::vec3 pillars[4] = {
    glm::vec3(-1.0f, 0.2f, -3.5f),
    glm::vec3(1.2f, 0.4f, -9.5f),
    glm::vec3(-0.8f, 0.8f, -20.0f),
    glm::vec3(1.6f, 0.2f, -4.2f),
  };
  for (size_t i = 0; i < 4; ++i) {
    RenderInstanceDesc pillar;
    pillar.mesh = cube.handle;
    pillar.indexCount = cube.indexCount;
    pillar.material = 2;
    pillar.hasBounds = true;
    pillar.localBounds = { cube.minBounds, cube.maxBounds };
    pillar.world =
      toArray(glm::translate(glm::mat4(1.0f), pillars[i]) *
              glm::scale(glm::mat4(1.0f), glm::vec3(0.9f, 2.6f, 0.9f)));
    ok =
      world.createInstance(static_cast<RenderInstanceId>(i + 2u), pillar) && ok;
  }

  RenderLightDesc spot;
  spot.kind = RenderLightKind::Spot;
  spot.position = { 3.8f, 2.8f, -4.2f };
  spot.direction = { -1.0f, -0.85f, 0.0f };
  spot.color = { 0.3f, 0.7f, 1.0f };
  spot.intensity = 28.0f;
  spot.range = 14.0f;
  spot.innerConeCos = std::cos(glm::radians(18.0f));
  spot.outerConeCos = std::cos(glm::radians(34.0f));
  spot.castsShadow = false;
  ok = world.createLight(1, spot) && ok;

  ParityTargetScope scope;
  scope.target = target;
  scope.clear = { 0.04f, 0.05f, 0.07f, 1.0f };
  scope.inner = { &world };
  DrawList scene(renderer.getWindow(), renderer.getCamera());
  scene.AddDrawable(&scope, RenderLayerId::World);

  const std::array<float, 16> viewProjection =
    toArray(glm::perspective(glm::radians(55.0f), 1.0f, 0.2f, 45.0f) *
            glm::lookAt(glm::vec3(0.0f, 3.6f, 2.8f),
                        glm::vec3(0.0f, -0.6f, -10.0f),
                        glm::vec3(0.0f, 1.0f, 0.0f)));

  // Baseline without shadows cast by pillars or spot light.
  FrameReadback unshadowed;
  ok = ok && renderFrame(renderer, scene, viewProjection);
  renderer.EndFrame();
  ok = ok && readTarget(renderer, target, kSize, kSize, unshadowed);

  // Enable pillar shadow casting (directional CSM quadrants 0..2).
  casterMat.castsShadow = true;
  ok = world.updateMaterial(2, casterMat) && ok;
  FrameReadback sunShadowed;
  ok = ok && renderFrame(renderer, scene, viewProjection);
  renderer.EndFrame();
  ok = ok && readTarget(renderer, target, kSize, kSize, sunShadowed);

  // Enable hero spot shadow casting (quadrant 3).
  spot.castsShadow = true;
  ok = world.updateLight(1, spot) && ok;
  ok = ok && renderFrame(renderer, scene, viewProjection);
  renderer.EndFrame();
  ok = ok && readTarget(renderer, target, kSize, kSize, image);

  size_t sunDarkened = 0;
  size_t spotDarkened = 0;
  for (size_t px = 0; ok && px + 3 < image.pixels.size(); px += 4) {
    const int sunDrop = static_cast<int>(unshadowed.pixels[px + 0]) -
                        static_cast<int>(sunShadowed.pixels[px + 0]);
    const int spotDrop = static_cast<int>(sunShadowed.pixels[px + 2]) -
                         static_cast<int>(image.pixels[px + 2]);
    if (sunDrop > 15) {
      sunDarkened += 1;
    }
    if (spotDrop > 12) {
      spotDarkened += 1;
    }
  }
  if (sunDarkened < 250 || spotDarkened < 80) {
    std::printf("  shadow atlas darkened too few pixels (sun=%zu spot=%zu)\n",
                sunDarkened,
                spotDarkened);
    ok = false;
  }

  world.releaseResources();
  renderer.destroyMesh(cube.handle);
  renderer.getBackend()->DestroyFramebuffer(target);
  return ok;
}

// 2D primitives through renderOffscreen, the detached panel path.
static bool
scene2D(Renderer& renderer, FrameReadback& image)
{
  const FramebufferHandle target = createTarget(renderer, false);
  std::vector<unsigned char> checker(8u * 8u * 4u);
  for (size_t y = 0; y < 8; ++y) {
    for (size_t x = 0; x < 8; ++x) {
      unsigned char* texel = checker.data() + (y * 8u + x) * 4u;
      const bool light = ((x + y) % 2) == 0;
      texel[0] = light ? 240 : 30;
      texel[1] = static_cast<unsigned char>(x * 30);
      texel[2] = static_cast<unsigned char>(y * 30);
      texel[3] = static_cast<unsigned char>(128 + x * 16);
    }
  }
  TextureOptions nearest;
  TextureOptions linear;
  linear.filter = TextureFilter::Linear;
  linear.wrapX = TextureWrap::Repeat;
  const TextureHandle sharp =
    renderer.enrollTexture(checker.data(), 8, 8, 4, nearest);
  const TextureHandle smooth =
    renderer.enrollTexture(checker.data(), 8, 8, 4, linear);
  GameVisual visual;
  visual.setWindow(renderer.getWindow());
  visual.setSpace(PrimitiveSpace::Pixels);
  visual.prepare(&renderer);
  visual.addFilledRect(10, 10, 100, 60, ColorRgba{ 200, 40, 40, 200 });
  visual.addOutlineRect(30, 30, 120, 80, ColorRgba{ 40, 220, 90, 255 }, 3.0f);
  visual.addLine(5, 250, 250, 120, ColorRgba{ 250, 250, 60, 255 }, 1.0f);
  visual.addLine(5, 200, 200, 190, ColorRgba{ 90, 90, 250, 160 }, 2.5f);
  visual.addFilledEllipse(150, 20, 90, 70, ColorRgba{ 60, 200, 220, 180 });
  visual.addFilledTriangle(
    20, 240, 90, 150, 120, 230, ColorRgba{ 255, 128, 0, 255 });
  visual.addGradientRect(140,
                         100,
                         100,
                         60,
                         ColorRgba{ 255, 0, 0, 255 },
                         ColorRgba{ 0, 255, 0, 255 },
                         ColorRgba{ 0, 0, 255, 255 },
                         ColorRgba{ 255, 255, 255, 64 });
  visual.addSprite(sharp, 130, 170, 48, 48);
  visual.addSprite(
    smooth, 185, 170, 64, 48, ColorRgba{}, 0.0f, 0.0f, 2.0f, 1.0f);
  visual.addText("Illumo Vk 123", 12, 110, 18, ColorRgba{ 255, 255, 255, 255 });
  GameVisual clipped;
  clipped.setWindow(renderer.getWindow());
  clipped.setSpace(PrimitiveSpace::Pixels);
  clipped.prepare(&renderer);
  clipped.setPixelClipRect(Rect2{ 60, 60, 70, 50 });
  clipped.addFilledEllipse(40, 40, 120, 100, ColorRgba{ 255, 255, 255, 220 });
  const bool rendered = renderer.renderOffscreen(target,
                                                 kSize,
                                                 kSize,
                                                 { &visual, &clipped },
                                                 { 0.05f, 0.05f, 0.08f, 1.0f },
                                                 1.0f);
  const bool read =
    rendered && readTarget(renderer, target, kSize, kSize, image);
  renderer.destroyTexture(sharp);
  renderer.destroyTexture(smooth);
  renderer.getBackend()->DestroyFramebuffer(target);
  return read;
}

struct ParityColoredVertex
{
  float position[3];
  std::uint8_t color[4];
};

static const char* kTokenVertex = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
uniform mat4 uMVP;
uniform vec2 uOffset;
uniform float uScale;
uniform int uUseInstance;
out vec4 vColor;
void main() {
  vec2 shift = uUseInstance != 0 ? vec2(float(gl_InstanceID) * 0.2, 0.0) : vec2(0.0);
  gl_Position = uMVP * vec4(aPos.xy * uScale + uOffset + shift, aPos.z, 1.0);
  vColor = aColor;
}
)";

static const char* kTokenFragment = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;
uniform vec4 uTint;
uniform bool uUseTexture;
uniform sampler2D uTexture;
uniform vec3 uWeights[3];
void main() {
  vec4 color = vColor * uTint;
  if (uUseTexture) {
    color *= texture(uTexture, gl_FragCoord.xy / 64.0);
  }
  color.rgb *= (uWeights[0] + uWeights[1] + uWeights[2]);
  FragColor = color;
}
)";

static const char* kMultiUboVertex = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (std140) uniform FrameUniforms {
  vec4 uFrameOffsetScale;
};
void main() {
  vec2 pos = aPos.xy * uFrameOffsetScale.z + uFrameOffsetScale.xy;
  gl_Position = vec4(pos, 0.0, 1.0);
}
)";

static const char* kMultiUboFragment = R"(
#version 330 core
layout (std140) uniform FrameUniforms {
  vec4 uFrameOffsetScale;
};
layout (std140) uniform LightUniforms {
  vec4 uLightColorA;
  vec4 uLightColorB;
};
out vec4 FragColor;
void main() {
  vec3 rgb = uLightColorA.rgb * uFrameOffsetScale.w + uLightColorB.rgb;
  FragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)";

// Raw tokens exercising the pipeline state the backends must agree on.
class ParityTokenScene final : public DrawableBase
{
public:
  FramebufferHandle target{};
  ShaderHandle shader{};
  ShaderHandle multiUboShader{};
  BufferHandle frameUbo{};
  BufferHandle lightUbo{};
  std::array<float, 4> frameUboData{ 0.45f, -0.85f, 0.14f, 0.5f };
  std::array<float, 8> lightUboData{ 0.8f,  0.2f,  0.1f,  1.0f,
                                     0.15f, 0.55f, 0.75f, 0.0f };
  MeshHandle mesh{};
  MeshHandle positionsOnly{};
  TextureHandle texture{};
  unsigned int indexCount = 0;

  void Draw() override {}
  void state(Renderer* r,
             bool depth,
             bool blend,
             BlendFactor source,
             BlendFactor destination,
             bool cull,
             CullMode face,
             WindingOrder front,
             bool wireframe,
             Primitives primitives)
  {
    PipelineState ps;
    ps.depthTestEnabled = depth;
    ps.blendEnabled = blend;
    ps.blendSrc = source;
    ps.blendDst = destination;
    ps.faceCullingEnabled = cull;
    ps.cullFace = face;
    ps.frontFace = front;
    ps.wireframe = wireframe;
    ps.primitives = primitives;
    r->pushPipelineState(ps);
  }
  void place(Renderer* r, float x, float y, float scale)
  {
    r->pushUniformVec2("uOffset", x, y);
    r->pushUniformFloat("uScale", scale);
  }
  bool AppendCommands(Renderer* r) override
  {
    const glm::mat4 identity(1.0f);
    r->pushFramebuffer(target);
    r->pushViewport(0, 0, kSize, kSize);
    r->pushClearScreen(0.2f, 0.2f, 0.25f, 1.0f);
    r->pushSetShader(shader);
    r->pushUniformMat4("uMVP", glm::value_ptr(identity));
    r->pushUniformVec4("uTint", 1.0f, 1.0f, 1.0f, 1.0f);
    r->pushUniformInt("uUseTexture", 0);
    r->pushUniformInt("uUseInstance", 0);
    r->pushUniformVec3("uWeights[0]", 0.5f, 0.5f, 0.5f);
    r->pushUniformVec3("uWeights[1]", 0.25f, 0.5f, 0.25f);
    r->pushUniformVec3("uWeights[2]", 0.25f, 0.0f, 0.25f);
    r->pushSetMesh(mesh);
    // Back-face culling keeps only the counter-clockwise triangle.
    state(r,
          false,
          false,
          BlendFactor::One,
          BlendFactor::Zero,
          true,
          CullMode::Back,
          WindingOrder::CounterClockwise,
          false,
          Primitives::Triangles);
    place(r, -0.75f, 0.6f, 0.25f);
    r->pushDrawIndexed(6, 0);
    // Front-face culling with clockwise fronts keeps the same one.
    state(r,
          false,
          false,
          BlendFactor::One,
          BlendFactor::Zero,
          true,
          CullMode::Front,
          WindingOrder::Clockwise,
          false,
          Primitives::Triangles);
    place(r, -0.25f, 0.6f, 0.25f);
    r->pushDrawIndexed(6, 0);
    // Wireframe, points and lines.
    state(r,
          false,
          false,
          BlendFactor::One,
          BlendFactor::Zero,
          false,
          CullMode::Back,
          WindingOrder::CounterClockwise,
          true,
          Primitives::Triangles);
    place(r, 0.25f, 0.6f, 0.25f);
    r->pushDrawIndexed(6, 0);
    state(r,
          false,
          false,
          BlendFactor::One,
          BlendFactor::Zero,
          false,
          CullMode::Back,
          WindingOrder::CounterClockwise,
          false,
          Primitives::Points);
    place(r, 0.75f, 0.6f, 0.25f);
    r->pushDrawIndexed(6, 0);
    state(r,
          false,
          false,
          BlendFactor::One,
          BlendFactor::Zero,
          false,
          CullMode::Back,
          WindingOrder::CounterClockwise,
          false,
          Primitives::Lines);
    place(r, -0.75f, 0.1f, 0.3f);
    r->pushDrawIndexed(6, 0);
    // Additive and colour-keyed blending over overlapping quads.
    state(r,
          false,
          true,
          BlendFactor::One,
          BlendFactor::One,
          false,
          CullMode::Back,
          WindingOrder::CounterClockwise,
          false,
          Primitives::Triangles);
    place(r, -0.2f, 0.1f, 0.3f);
    r->pushDrawIndexed(6, 6);
    place(r, -0.1f, 0.05f, 0.3f);
    r->pushDrawIndexed(6, 6);
    state(r,
          false,
          true,
          BlendFactor::SrcColor,
          BlendFactor::OneMinusSrcColor,
          false,
          CullMode::Back,
          WindingOrder::CounterClockwise,
          false,
          Primitives::Triangles);
    place(r, 0.4f, 0.1f, 0.3f);
    r->pushDrawIndexed(6, 6);
    // Depth: a scissored clear to 0.5, then quads in front and behind.
    r->pushScissor(true, 16, 16, 96, 64);
    r->pushClearColor(0.8f, 0.1f, 0.1f, 1.0f);
    r->pushClearDepth(0.5f);
    state(r,
          true,
          false,
          BlendFactor::One,
          BlendFactor::Zero,
          false,
          CullMode::Back,
          WindingOrder::CounterClockwise,
          false,
          Primitives::Triangles);
    place(r, -0.6f, -0.6f, 0.35f);
    r->pushUniformVec4("uTint", 0.2f, 1.0f, 0.2f, 1.0f);
    r->pushDrawIndexed(6, 12);
    r->pushScissor(false, 0, 0, 0, 0);
    // Instanced copies.
    state(r,
          false,
          false,
          BlendFactor::One,
          BlendFactor::Zero,
          false,
          CullMode::Back,
          WindingOrder::CounterClockwise,
          false,
          Primitives::Triangles);
    r->pushUniformVec4("uTint", 1.0f, 1.0f, 1.0f, 1.0f);
    r->pushUniformInt("uUseInstance", 1);
    place(r, 0.0f, -0.4f, 0.15f);
    RenderCommand instanced;
    instanced.commandType = CommandType::DrawInstanced;
    instanced.drawInstanced.elementCount = 3;
    instanced.drawInstanced.instanceCount = 4;
    r->getBackend()->PushToCommandQueue(instanced);
    r->pushUniformInt("uUseInstance", 0);
    // A non-indexed draw of the second triangle's vertices.
    place(r, -0.75f, -0.3f, 0.2f);
    RenderCommand arrays;
    arrays.commandType = CommandType::Draw;
    arrays.draw.elementCount = 3;
    arrays.draw.first = 1;
    r->getBackend()->PushToCommandQueue(arrays);
    // A texture on unit 3, then a unit with nothing bound (black).
    r->pushSetTexture(texture, 3);
    r->pushUniformInt("uTexture", 3);
    r->pushUniformInt("uUseTexture", 1);
    place(r, 0.5f, -0.6f, 0.35f);
    r->pushDrawIndexed(6, 6);
    r->pushUniformInt("uTexture", 7);
    place(r, 0.8f, -0.1f, 0.15f);
    r->pushDrawIndexed(6, 6);
    r->pushUniformInt("uUseTexture", 0);
    // A mesh without colours reads (0, 0, 0, 1), as OpenGL's disabled
    // attribute does.
    r->pushSetMesh(positionsOnly);
    r->pushUniformVec4("uTint", 1.0f, 1.0f, 1.0f, 1.0f);
    place(r, -0.2f, -0.85f, 0.2f);
    r->pushDrawIndexed(6, 0);
    // Simultaneous FrameUniforms (slot 0) + LightUniforms (slot 1) UBO reads.
    r->pushWriteBuffer(frameUbo,
                       0,
                       static_cast<unsigned int>(sizeof(frameUboData)),
                       frameUboData.data());
    r->pushWriteBuffer(lightUbo,
                       0,
                       static_cast<unsigned int>(sizeof(lightUboData)),
                       lightUboData.data());
    r->pushBindUniformBuffer(frameUbo, FrameUniformsBindingPoint);
    r->pushBindUniformBuffer(lightUbo, LightUniformsBindingPoint);
    r->pushSetShader(multiUboShader);
    r->pushDrawIndexed(6, 0);
    r->pushFramebuffer(FramebufferHandle{});
    return true;
  }
};

static bool
sceneTokens(Renderer& renderer, FrameReadback& image)
{
  ParityTokenScene tokens;
  tokens.target = createTarget(renderer, true);
  ShaderSources sources;
  sources.vertexSource = kTokenVertex;
  sources.fragmentSource = kTokenFragment;
  tokens.shader = renderer.enrollShader(sources);
  ShaderSources multiUboSources;
  multiUboSources.vertexSource = kMultiUboVertex;
  multiUboSources.fragmentSource = kMultiUboFragment;
  tokens.multiUboShader = renderer.enrollShader(multiUboSources);
  tokens.frameUbo =
    renderer.getBackend()->CreateBuffer(BufferUsage::Uniform, 64);
  tokens.lightUbo =
    renderer.getBackend()->CreateBuffer(BufferUsage::Uniform, 64);
  // Two triangles of opposite winding, then a unit quad, then a quad at
  // half depth.
  const ParityColoredVertex vertices[] = {
    { { -1.0f, -1.0f, 0.0f }, { 255, 60, 60, 255 } },
    { { 1.0f, -1.0f, 0.0f }, { 60, 255, 60, 255 } },
    { { -1.0f, 1.0f, 0.0f }, { 60, 60, 255, 255 } },
    { { 1.0f, 1.0f, 0.0f }, { 255, 255, 60, 255 } },
    { { -1.0f, -1.0f, -0.2f }, { 90, 200, 255, 160 } },
    { { 1.0f, -1.0f, -0.2f }, { 90, 200, 255, 160 } },
    { { 1.0f, 1.0f, -0.2f }, { 255, 120, 60, 160 } },
    { { -1.0f, 1.0f, -0.2f }, { 255, 120, 60, 160 } },
  };
  const unsigned int indices[] = { 0, 1, 2, 1, 2, 3, 4, 5, 6,
                                   4, 6, 7, 4, 5, 6, 4, 6, 7 };
  tokens.mesh = renderer.enrollMesh(vertices,
                                    sizeof(vertices),
                                    indices,
                                    sizeof(indices),
                                    MeshVertexLayout::Pos3Color4U8,
                                    false);
  const float positions[] = { -1, -1, 0, 1, -1, 0, -1, 1, 0, 1, 1, 0 };
  tokens.positionsOnly = renderer.enrollMesh(positions,
                                             sizeof(positions),
                                             indices,
                                             6 * sizeof(unsigned int),
                                             MeshVertexLayout::Pos3,
                                             false);
  std::vector<unsigned char> stripes(4u * 4u * 3u);
  for (size_t index = 0; index < 16; ++index) {
    stripes[index * 3u] = static_cast<unsigned char>(index * 16);
    stripes[index * 3u + 1] = 200;
    stripes[index * 3u + 2] = static_cast<unsigned char>(255 - index * 16);
  }
  TextureOptions repeat;
  repeat.wrapX = TextureWrap::Repeat;
  repeat.wrapY = TextureWrap::Repeat;
  tokens.texture = renderer.enrollTexture(stripes.data(), 4, 4, 3, repeat);
  if (!tokens.shader.isValid() || !tokens.multiUboShader.isValid() ||
      !tokens.frameUbo.isValid() || !tokens.lightUbo.isValid() ||
      !tokens.mesh.isValid() || !tokens.positionsOnly.isValid() ||
      !tokens.texture.isValid()) {
    std::printf("  token scene resources failed\n");
    return false;
  }
  DrawList scene(renderer.getWindow(), renderer.getCamera());
  scene.AddDrawable(&tokens, RenderLayerId::World);
  const bool rendered = renderFrame(renderer, scene, toArray(glm::mat4(1.0f)));
  renderer.EndFrame();
  const bool read =
    rendered && readTarget(renderer, tokens.target, kSize, kSize, image);
  renderer.destroyShader(tokens.shader);
  renderer.destroyShader(tokens.multiUboShader);
  renderer.getBackend()->DestroyBuffer(tokens.frameUbo);
  renderer.getBackend()->DestroyBuffer(tokens.lightUbo);
  renderer.destroyMesh(tokens.mesh);
  renderer.destroyMesh(tokens.positionsOnly);
  renderer.destroyTexture(tokens.texture);
  renderer.getBackend()->DestroyFramebuffer(tokens.target);
  return read;
}

struct ParitySpriteVertex
{
  float position[3];
  std::uint8_t color[4];
  float uv[2];
};

// Draws a texture as a sprite, updates it, and draws it again in the same
// submission: the second copy must show the update, the first must not.
class ParityUpdateScene final : public DrawableBase
{
public:
  FramebufferHandle target{};
  MeshHandle quad{};
  std::array<TextureHandle, 4> textures{};
  std::vector<unsigned char> patch;
  std::vector<unsigned char> red;
  TextureHandle mrtExtra{};

  void Draw() override {}
  void sprite(Renderer* r, TextureHandle texture, float x, float y, float size)
  {
    const glm::mat4 mvp =
      glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f)) *
      glm::scale(glm::mat4(1.0f), glm::vec3(size, size, 1.0f));
    r->pushUniformMat4("uMVP", glm::value_ptr(mvp));
    r->pushSetTexture(texture, 0);
    r->pushUniformInt("uTexture", 0);
    r->pushSetMesh(quad);
    r->pushDrawIndexed(6, 0);
  }
  bool AppendCommands(Renderer* r) override
  {
    r->pushFramebuffer(target);
    r->pushViewport(0, 0, kSize, kSize);
    r->pushClearScreen(0.0f, 0.0f, 0.0f, 1.0f);
    r->bindStyle(RenderStyleId::Sprite);
    for (size_t index = 0; index < textures.size(); ++index) {
      sprite(r, textures[index], -0.75f + 0.5f * index, 0.55f, 0.22f);
    }
    // RGBA source into each texture, with a row stride, and single-channel
    // data into the RGBA one.
    r->pushUpdateTexture(textures[0], 1, 2, 4, 3, 4, patch.data(), 6);
    r->pushUpdateTexture(textures[1], 2, 1, 4, 3, 4, patch.data(), 6);
    r->pushUpdateTexture(textures[2], 0, 0, 4, 3, 4, patch.data(), 6);
    r->pushUpdateTexture(textures[0], 5, 5, 2, 2, 1, red.data(), 0);
    for (size_t index = 0; index < textures.size(); ++index) {
      sprite(r, textures[index], -0.75f + 0.5f * index, 0.0f, 0.22f);
    }
    // The second attachment of a cleared two-target framebuffer is zero.
    sprite(r, mrtExtra, -0.5f, -0.6f, 0.3f);
    r->pushFramebuffer(FramebufferHandle{});
    return true;
  }
};

static bool
sceneTextures(Renderer& renderer, FrameReadback& image)
{
  ParityUpdateScene updates;
  updates.target = createTarget(renderer, false);
  const ParitySpriteVertex vertices[] = {
    { { -1, -1, 0 }, { 255, 255, 255, 255 }, { 0, 0 } },
    { { 1, -1, 0 }, { 255, 255, 255, 255 }, { 1, 0 } },
    { { 1, 1, 0 }, { 255, 255, 255, 255 }, { 1, 1 } },
    { { -1, 1, 0 }, { 255, 255, 255, 255 }, { 0, 1 } },
  };
  const unsigned int indices[] = { 0, 1, 2, 0, 2, 3 };
  updates.quad = renderer.enrollMesh(vertices,
                                     sizeof(vertices),
                                     indices,
                                     sizeof(indices),
                                     MeshVertexLayout::Pos3Color4U8Uv2,
                                     false);
  std::vector<unsigned char> base(8u * 8u * 4u);
  for (size_t index = 0; index < base.size(); ++index) {
    base[index] = static_cast<unsigned char>((index * 37u) % 251u);
  }
  TextureOptions nearest;
  updates.textures[0] = renderer.enrollTexture(base.data(), 8, 8, 4, nearest);
  updates.textures[1] = renderer.enrollTexture(base.data(), 8, 8, 3, nearest);
  updates.textures[2] = renderer.enrollTexture(base.data(), 8, 8, 1, nearest);
  std::vector<unsigned char> large(64u * 64u * 4u);
  for (size_t y = 0; y < 64; ++y) {
    for (size_t x = 0; x < 64; ++x) {
      const unsigned char value = ((x / 4 + y / 4) % 2) == 0 ? 255 : 0;
      unsigned char* texel = large.data() + (y * 64u + x) * 4u;
      texel[0] = value;
      texel[1] = static_cast<unsigned char>(x * 4);
      texel[2] = value;
      texel[3] = 255;
    }
  }
  TextureOptions mipmapped;
  mipmapped.filter = TextureFilter::Linear;
  mipmapped.generateMipmaps = true;
  updates.textures[3] =
    renderer.enrollTexture(large.data(), 64, 64, 4, mipmapped);
  updates.patch.resize(6u * 3u * 4u);
  for (size_t index = 0; index < updates.patch.size(); ++index) {
    updates.patch[index] = static_cast<unsigned char>(200 - index * 3);
  }
  updates.red = { 255, 128, 64, 32 };

  FramebufferDesc mrt;
  mrt.width = 16;
  mrt.height = 16;
  mrt.colorAttachments.push_back(FramebufferAttachmentDesc{});
  mrt.colorAttachments.push_back(FramebufferAttachmentDesc{});
  FramebufferAttachments attachments;
  const FramebufferHandle twoTargets =
    renderer.getBackend()->CreateFramebuffer(mrt, &attachments);
  if (!twoTargets.isValid() || attachments.colorTextures.size() != 2) {
    std::printf("  two-target framebuffer failed\n");
    return false;
  }
  updates.mrtExtra = attachments.colorTextures[1];
  // Clearing a two-target framebuffer writes the colour to the first target
  // only; the second stays zero.
  {
    renderer.BeginFrame();
    renderer.pushFramebuffer(twoTargets);
    renderer.pushViewport(0, 0, 16, 16);
    RenderCommand clearAll;
    clearAll.commandType = CommandType::ClearAll;
    clearAll.clear = { 0.9f, 0.4f, 0.1f, 1.0f };
    renderer.getBackend()->PushToCommandQueue(clearAll);
    renderer.pushFramebuffer(FramebufferHandle{});
    renderer.EndFrame();
  }
  DrawList scene(renderer.getWindow(), renderer.getCamera());
  scene.AddDrawable(&updates, RenderLayerId::World);
  const bool rendered = renderFrame(renderer, scene, toArray(glm::mat4(1.0f)));
  renderer.EndFrame();
  const bool read =
    rendered && readTarget(renderer, updates.target, kSize, kSize, image);
  for (TextureHandle texture : updates.textures) {
    renderer.destroyTexture(texture);
  }
  renderer.destroyMesh(updates.quad);
  renderer.getBackend()->DestroyFramebuffer(twoTargets);
  renderer.getBackend()->DestroyFramebuffer(updates.target);
  return read;
}

// Clears render targets of every colour format and reads them back, so the
// readback conversions agree (RGB8 alpha, R8 and float expansion).
static bool
sceneFormats(Renderer& renderer, FrameReadback& image)
{
  const TextureFormat formats[] = {
    TextureFormat::RGBA8,   TextureFormat::RGB8,  TextureFormat::R8,
    TextureFormat::RGBA16F, TextureFormat::RG16F, TextureFormat::R16F
  };
  image = FrameReadback{};
  image.width = 8;
  image.height = 8 * 6;
  for (TextureFormat format : formats) {
    FramebufferDesc desc;
    desc.width = 8;
    desc.height = 8;
    FramebufferAttachmentDesc attachment;
    attachment.format = format;
    desc.colorAttachments.push_back(attachment);
    const FramebufferHandle target =
      renderer.getBackend()->CreateFramebuffer(desc);
    if (!target.isValid()) {
      std::printf("  format target failed\n");
      return false;
    }
    renderer.BeginFrame();
    renderer.pushFramebuffer(target);
    renderer.pushViewport(0, 0, 8, 8);
    renderer.pushClearColor(0.25f, 0.5f, 0.75f, 0.6f);
    renderer.pushScissor(true, 2, 2, 4, 4);
    renderer.pushClearColor(1.5f, -0.5f, 0.125f, 0.3f);
    renderer.pushScissor(false, 0, 0, 0, 0);
    renderer.pushFramebuffer(FramebufferHandle{});
    renderer.EndFrame();
    FrameReadback part;
    if (!readTarget(renderer, target, 8, 8, part)) {
      return false;
    }
    image.pixels.insert(
      image.pixels.end(), part.pixels.begin(), part.pixels.end());
    renderer.getBackend()->DestroyFramebuffer(target);
  }
  return true;
}

// The detached panel loop: between frames, a changing 2D surface renders
// offscreen and queues an asynchronous readback (at most two in flight);
// completed copies are taken without waiting, one or more frames late. The
// newest copy after draining is compared.
static bool
scenePanels(Renderer& renderer, FrameReadback& image)
{
  constexpr std::uint32_t kStream = 12;
  constexpr int kWidth = 200;
  constexpr int kHeight = 120;
  FramebufferDesc desc;
  desc.width = kWidth;
  desc.height = kHeight;
  desc.colorAttachments.push_back(FramebufferAttachmentDesc{});
  IBackend* backend = renderer.getBackend();
  const FramebufferHandle target = backend->CreateFramebuffer(desc);
  GameVisual visual;
  visual.setWindow(renderer.getWindow());
  visual.setSpace(PrimitiveSpace::Pixels);
  visual.prepare(&renderer);
  int pending = 0;
  int earlyTakes = 0;
  bool rendered = target.isValid();
  // The last pass drains the stream first, so both backends end on the same
  // content whatever their completion timing.
  constexpr int kFrames = 9;
  for (int frame = 0; rendered && frame < kFrames; ++frame) {
    // Frames are paced as a running app's are, so the GPU finishes a copy
    // between them.
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    renderer.BeginFrame();
    renderer.EndFrame();
    while (frame == kFrames - 1 && rendered && pending > 0) {
      FrameReadback copy;
      rendered = backend->takeFramebufferReadback(kStream, true, copy);
      --pending;
    }
    visual.clearPrimitives();
    const float shift = static_cast<float>(frame) * 9.0f;
    visual.addFilledRect(8 + shift, 10, 60, 40, ColorRgba{ 220, 80, 40, 255 });
    visual.addFilledEllipse(
      100, 20 + shift * 0.5f, 70, 50, ColorRgba{ 60, 180, 240, 200 });
    visual.addText("Panel " + std::to_string(frame),
                   10,
                   70,
                   16,
                   ColorRgba{ 240, 240, 240, 255 });
    if (pending < 2) {
      if (!renderer.renderOffscreen(target,
                                    kWidth,
                                    kHeight,
                                    { &visual },
                                    { 0.12f, 0.12f, 0.14f, 1.0f },
                                    1.0f)) {
        std::printf("  frame %d: offscreen render failed: %s\n",
                    frame,
                    renderer.frameError().c_str());
        rendered = false;
      } else if (!backend->requestFramebufferReadback(
                   kStream, target, kWidth, kHeight)) {
        std::printf("  frame %d: readback request refused\n", frame);
        rendered = false;
      } else {
        ++pending;
      }
    }
    FrameReadback copy;
    while (frame < kFrames - 1 && pending > 0 &&
           backend->takeFramebufferReadback(kStream, false, copy)) {
      --pending;
      ++earlyTakes;
    }
  }
  while (rendered && pending > 0) {
    FrameReadback copy;
    rendered = backend->takeFramebufferReadback(kStream, true, copy);
    --pending;
    image = std::move(copy);
  }
  backend->releaseReadbackStream(kStream);
  backend->DestroyFramebuffer(target);
  std::printf("  panels: %d copies taken without waiting\n", earlyTakes);
  if (earlyTakes == 0) {
    std::printf("  no copy completed without a blocking wait\n");
    return false;
  }
  return rendered && image.success();
}

// The default framebuffer: a World layer drawn through the motion-blur
// passes (two colour targets, then a post-process pass), read back with
// readBackbuffer after two frames of camera motion.
static bool
sceneBackbuffer(Renderer& renderer, FrameReadback& image)
{
  MeshVisual cube;
  cube.setLightingEnabled(true);
  cube.setMotionBlurEnabled(true);
  cube.addSolidCube(Vector3(0), Vector3(1.4f), ColorRgba{ 230, 200, 60, 255 });
  cube.prepare(&renderer);
  DrawList scene(renderer.getWindow(), renderer.getCamera());
  scene.AddDrawable(&cube, RenderLayerId::World);

  RenderPassDesc geometry;
  geometry.name = "ParityGeometry";
  geometry.type = PassType::Draw;
  geometry.useScreenTarget = false;
  geometry.pooledTargetName = "ParityColorVelocity";
  geometry.targetDesc.name = "ParityColorVelocity";
  geometry.targetDesc.windowRelative = true;
  FramebufferAttachmentDesc color;
  color.format = TextureFormat::RGBA8;
  geometry.targetDesc.colorAttachments.push_back(color);
  FramebufferAttachmentDesc velocity;
  velocity.format = TextureFormat::RG16F;
  geometry.targetDesc.colorAttachments.push_back(velocity);
  geometry.targetDesc.depthStencilFormat = TextureFormat::Depth24;
  geometry.clear.clearColor = true;
  geometry.clear.clearColorValue = { 0.1f, 0.1f, 0.1f, 1.0f };
  geometry.clear.clearDepth = true;
  RenderPassDesc post;
  post.name = "ParityBlur";
  post.type = PassType::PostProcess;
  post.useScreenTarget = true;
  post.styleHandle = renderer.getBuiltinStyleHandle(RenderStyleId::MotionBlur);
  PassInputTargetBinding colorInput;
  colorInput.targetName = "ParityColorVelocity";
  colorInput.attachmentIndex = 0;
  colorInput.slot = 0;
  colorInput.samplerUniformName = "uColorTexture";
  post.inputTargetTextures.push_back(colorInput);
  PassInputTargetBinding velocityInput;
  velocityInput.targetName = "ParityColorVelocity";
  velocityInput.attachmentIndex = 1;
  velocityInput.slot = 1;
  velocityInput.samplerUniformName = "uVelocityTexture";
  post.inputTargetTextures.push_back(velocityInput);
  post.uniformFloats.push_back(PassUniformFloat{ "uMotionBlurAmount", 1.0f });
  post.uniformFloats.push_back(PassUniformFloat{ "uMotionBlurMax", 0.2f });
  post.uniformInts.push_back(PassUniformInt{ "uMotionBlurSamples", 8 });
  std::vector<RenderPassDesc> passes;
  passes.push_back(geometry);
  passes.push_back(post);
  scene.SetDefaultLayerPasses(RenderLayerId::World, std::move(passes));

  const glm::mat4 projection =
    glm::perspective(glm::radians(55.0f), 1.0f, 0.1f, 50.0f);
  bool rendered = true;
  for (int frame = 0; frame < 2; ++frame) {
    const glm::mat4 view = glm::lookAt(
      glm::vec3(2.5f + static_cast<float>(frame) * 0.6f, 2.0f, 3.5f),
      glm::vec3(0.0f),
      glm::vec3(0, 1, 0));
    if (frame == 1) {
      renderer.setBeforePresent([&image](Renderer& current) {
        image = current.getBackend()->readBackbuffer(kSize, kSize);
      });
    }
    rendered =
      renderFrame(renderer, scene, toArray(projection * view)) && rendered;
    renderer.EndFrame();
  }
  renderer.setBeforePresent({});
  if (!image.success()) {
    std::printf("  backbuffer readback failed: %s\n", image.error.c_str());
    return false;
  }
  return rendered;
}

static PooledRenderTargetDesc
makeParityBloomMipDesc(int mipIndex)
{
  static constexpr const char* kNames[5] = { "ParityBloomMip0",
                                             "ParityBloomMip1",
                                             "ParityBloomMip2",
                                             "ParityBloomMip3",
                                             "ParityBloomMip4" };
  PooledRenderTargetDesc desc;
  desc.name = kNames[std::clamp(mipIndex, 0, 4)];
  desc.windowRelative = true;
  desc.mipLevel = std::clamp(mipIndex, 0, 4) + 1;
  FramebufferAttachmentDesc color;
  color.format = TextureFormat::RGBA16F;
  color.filter = TextureFilter::Linear;
  color.wrap = TextureWrap::ClampToEdge;
  desc.colorAttachments.push_back(color);
  return desc;
}

static std::vector<RenderPassDesc>
buildParityHdrPasses(Renderer& renderer, bool enableBloom)
{
  std::vector<RenderPassDesc> passes;

  PooledRenderTargetDesc hdrDesc;
  hdrDesc.name = "ParityWorldHdrVelocity";
  hdrDesc.windowRelative = true;
  FramebufferAttachmentDesc hdrColor;
  hdrColor.format = TextureFormat::RGBA16F;
  hdrColor.filter = TextureFilter::Linear;
  hdrColor.wrap = TextureWrap::ClampToEdge;
  hdrDesc.colorAttachments.push_back(hdrColor);
  FramebufferAttachmentDesc hdrVel;
  hdrVel.format = TextureFormat::RG16F;
  hdrVel.filter = TextureFilter::Nearest;
  hdrVel.wrap = TextureWrap::ClampToEdge;
  hdrDesc.colorAttachments.push_back(hdrVel);
  hdrDesc.depthStencilFormat = TextureFormat::Depth24;

  RenderPassDesc geom;
  geom.name = "ParityHdrGeom";
  geom.type = PassType::Draw;
  geom.useScreenTarget = false;
  geom.pooledTargetName = hdrDesc.name;
  geom.targetDesc = hdrDesc;
  geom.clear.clearColor = true;
  geom.clear.clearColorValue = { 0.0f, 0.0f, 0.0f, 1.0f };
  geom.clear.clearDepth = true;
  passes.push_back(geom);

  const RenderStyleHandle downStyle =
    renderer.getBuiltinStyleHandle(RenderStyleId::BloomDownsample);
  const RenderStyleHandle upStyle =
    renderer.getBuiltinStyleHandle(RenderStyleId::BloomUpsample);
  const RenderStyleHandle compStyle =
    renderer.getBuiltinStyleHandle(RenderStyleId::HdrComposite);
  const RenderStyleHandle fxaaStyle =
    renderer.getBuiltinStyleHandle(RenderStyleId::FxaaResolve);

  if (enableBloom) {
    for (int m = 0; m < 5; ++m) {
      const PooledRenderTargetDesc mipDesc = makeParityBloomMipDesc(m);
      RenderPassDesc down;
      down.name = std::string("ParityBloomDown") + std::to_string(m);
      down.type = PassType::PostProcess;
      down.useScreenTarget = false;
      down.pooledTargetName = mipDesc.name;
      down.targetDesc = mipDesc;
      down.styleHandle = downStyle;
      PassInputTargetBinding src;
      src.targetName =
        (m == 0) ? hdrDesc.name : makeParityBloomMipDesc(m - 1).name;
      src.attachmentIndex = 0;
      src.slot = 0;
      src.samplerUniformName = "uSourceTexture";
      down.inputTargetTextures.push_back(src);
      down.uniformInts.push_back(
        PassUniformInt{ "uKarisAverage", (m == 0) ? 1 : 0 });
      passes.push_back(down);
    }
    for (int m = 3; m >= 0; --m) {
      const PooledRenderTargetDesc dstDesc = makeParityBloomMipDesc(m);
      RenderPassDesc up;
      up.name = std::string("ParityBloomUp") + std::to_string(m);
      up.type = PassType::PostProcess;
      up.useScreenTarget = false;
      up.pooledTargetName = dstDesc.name;
      up.targetDesc = dstDesc;
      up.styleHandle = upStyle;
      PassInputTargetBinding src;
      src.targetName = makeParityBloomMipDesc(m + 1).name;
      src.attachmentIndex = 0;
      src.slot = 0;
      src.samplerUniformName = "uSourceTexture";
      up.inputTargetTextures.push_back(src);
      up.uniformFloats.push_back(PassUniformFloat{ "uFilterRadius", 1.0f });
      passes.push_back(up);
    }
  }

  RenderPassDesc comp;
  comp.name = "ParityHdrComposite";
  comp.type = PassType::PostProcess;
  comp.useScreenTarget = false;
  comp.pooledTargetName = "ParityWorldLdrPreFxaa";
  comp.targetDesc.name = "ParityWorldLdrPreFxaa";
  comp.targetDesc.windowRelative = true;
  FramebufferAttachmentDesc ldrColor;
  ldrColor.format = TextureFormat::RGBA8;
  ldrColor.filter = TextureFilter::Linear;
  ldrColor.wrap = TextureWrap::ClampToEdge;
  comp.targetDesc.colorAttachments.push_back(ldrColor);
  comp.styleHandle = compStyle;

  PassInputTargetBinding colorIn;
  colorIn.targetName = hdrDesc.name;
  colorIn.attachmentIndex = 0;
  colorIn.slot = 0;
  colorIn.samplerUniformName = "uColorTexture";
  comp.inputTargetTextures.push_back(colorIn);

  PassInputTargetBinding velIn;
  velIn.targetName = hdrDesc.name;
  velIn.attachmentIndex = 1;
  velIn.slot = 1;
  velIn.samplerUniformName = "uVelocityTexture";
  comp.inputTargetTextures.push_back(velIn);

  if (enableBloom) {
    PassInputTargetBinding bloomIn;
    bloomIn.targetName = "ParityBloomMip0";
    bloomIn.attachmentIndex = 0;
    bloomIn.slot = 2;
    bloomIn.samplerUniformName = "uBloomTexture";
    comp.inputTargetTextures.push_back(bloomIn);
  }

  comp.uniformInts.push_back(PassUniformInt{ "uMotionBlurEnabled", 1 });
  comp.uniformFloats.push_back(PassUniformFloat{ "uMotionBlurAmount", 0.35f });
  comp.uniformFloats.push_back(PassUniformFloat{ "uMotionBlurMax", 0.1f });
  comp.uniformInts.push_back(PassUniformInt{ "uMotionBlurSamples", 6 });
  comp.uniformInts.push_back(
    PassUniformInt{ "uBloomEnabled", enableBloom ? 1 : 0 });
  comp.uniformFloats.push_back(PassUniformFloat{ "uBloomIntensity", 0.08f });
  comp.uniformFloats.push_back(PassUniformFloat{ "uExposure", 0.25f });
  comp.uniformInts.push_back(PassUniformInt{ "uTonemapper", 2 });
  comp.uniformInts.push_back(PassUniformInt{ "uStoreLumaInAlpha", 1 });
  passes.push_back(comp);

  RenderPassDesc fxaa;
  fxaa.name = "ParityFxaaResolve";
  fxaa.type = PassType::PostProcess;
  fxaa.useScreenTarget = true;
  fxaa.styleHandle = fxaaStyle;
  PassInputTargetBinding fxaaIn;
  fxaaIn.targetName = "ParityWorldLdrPreFxaa";
  fxaaIn.attachmentIndex = 0;
  fxaaIn.slot = 0;
  fxaaIn.samplerUniformName = "uColorTexture";
  fxaa.inputTargetTextures.push_back(fxaaIn);
  passes.push_back(fxaa);

  return passes;
}

static bool
allBuiltinStylesReady(Renderer& renderer)
{
  renderer.ensureBuiltinStyles();
  for (unsigned idx = 0; idx < renderStyleCount(); ++idx) {
    const RenderStyleId id = static_cast<RenderStyleId>(idx);
    if (renderer.getStyle(id) == nullptr) {
      std::printf("  FAILED: built-in RenderStyleId %u failed to compile\n",
                  idx);
      return false;
    }
  }
  return true;
}

static bool
sceneHdrBloomAgx(Renderer& renderer, FrameReadback& image)
{
  if (!allBuiltinStylesReady(renderer)) {
    return false;
  }
  const MeshAssetInfo sphere = enrollPbrSphereAndDegenerateQuad(renderer);
  const MeshAssetInfo cube = enrollCube(renderer);
  RenderWorld world;

  RenderEnvironment env;
  env.lightDirection = { 0.45f, 1.0f, 0.35f };
  env.lightColor = { 0.4f, 0.4f, 0.45f };
  env.ambientColor = { 0.02f, 0.02f, 0.025f };
  env.shadowsEnabled = true;
  env.shadowMapSize = 512;
  env.shadowPcf = true;
  world.setEnvironment(env);

  // Super-bright orange HDR emissive core (> 14x LDR ceiling) to verify AgX
  // highlight desaturation and Karis 5-mip bloom halo.
  RenderMaterialDesc hdrEmissiveMat;
  hdrEmissiveMat.tint = { 0.2f, 0.1f, 0.08f, 1.0f };
  hdrEmissiveMat.roughness = 0.3f;
  hdrEmissiveMat.emissiveFactor = { 16.0f, 3.2f, 0.7f };

  RenderMaterialDesc metallicMat;
  metallicMat.tint = { 0.85f, 0.88f, 0.92f, 1.0f };
  metallicMat.metallic = 0.9f;
  metallicMat.roughness = 0.25f;

  bool ok = world.createMaterial(1, hdrEmissiveMat) &&
            world.createMaterial(2, metallicMat);

  RenderInstanceDesc centerOrb;
  centerOrb.mesh = sphere.handle;
  centerOrb.indexCount = sphere.indexCount;
  centerOrb.material = 1;
  centerOrb.hasBounds = true;
  centerOrb.localBounds = { sphere.minBounds, sphere.maxBounds };
  centerOrb.world =
    toArray(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.2f, 0.0f)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(0.85f)));
  ok = world.createInstance(1, centerOrb) && ok;

  RenderInstanceDesc floorInst;
  floorInst.mesh = cube.handle;
  floorInst.indexCount = cube.indexCount;
  floorInst.material = 2;
  floorInst.hasBounds = true;
  floorInst.localBounds = { cube.minBounds, cube.maxBounds };
  floorInst.world =
    toArray(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.6f, -4.0f)) *
            glm::scale(glm::mat4(1.0f), glm::vec3(14.0f, 0.3f, 18.0f)));
  ok = world.createInstance(2, floorInst) && ok;

  RenderLightDesc heroSpot;
  heroSpot.kind = RenderLightKind::Spot;
  heroSpot.position = { 2.4f, 2.5f, 2.0f };
  heroSpot.direction = { -0.7f, -0.7f, -0.5f };
  heroSpot.color = { 0.4f, 0.7f, 1.0f };
  heroSpot.intensity = 35.0f;
  heroSpot.range = 14.0f;
  heroSpot.innerConeCos = std::cos(glm::radians(16.0f));
  heroSpot.outerConeCos = std::cos(glm::radians(32.0f));
  heroSpot.castsShadow = true;
  ok = world.createLight(1, heroSpot) && ok;

  // UI layer swatch: must stay un-tonemapped and un-blurred by HDR/AgX/FXAA.
  GameVisual uiSwatch;
  uiSwatch.setWindow(renderer.getWindow());
  uiSwatch.setSpace(PrimitiveSpace::Pixels);
  uiSwatch.prepare(&renderer);
  uiSwatch.addFilledRect(12, 12, 40, 28, ColorRgba{ 40, 220, 120, 255 });

  DrawList scene(renderer.getWindow(), renderer.getCamera());
  scene.AddDrawable(&world, RenderLayerId::World);
  scene.AddDrawable(&uiSwatch, RenderLayerId::UI);

  const glm::mat4 proj =
    glm::perspective(glm::radians(50.0f), 1.0f, 0.2f, 40.0f);
  const std::array<float, 16> vp0 =
    toArray(proj * glm::lookAt(glm::vec3(0.0f, 0.4f, 4.4f),
                               glm::vec3(0.0f, 0.0f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f)));
  const std::array<float, 16> vp1 =
    toArray(proj * glm::lookAt(glm::vec3(0.06f, 0.4f, 4.4f),
                               glm::vec3(0.0f, 0.0f, 0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f)));

  // 1. Render without bloom to capture dark halo baseline.
  FrameReadback noBloom;
  scene.SetDefaultLayerPasses(RenderLayerId::World,
                              buildParityHdrPasses(renderer, false));
  ok = ok && renderFrame(renderer, scene, vp0);
  renderer.EndFrame();
  renderer.setBeforePresent([&noBloom](Renderer& current) {
    noBloom = current.getBackend()->readBackbuffer(kSize, kSize);
  });
  ok = ok && renderFrame(renderer, scene, vp1);
  renderer.EndFrame();

  // 2. Render full HDR + 5-mip Karis Bloom + AgX + FXAA (warm 1 frame, then
  // measure frame 2 so pooled targets are steady-state).
  scene.SetDefaultLayerPasses(RenderLayerId::World,
                              buildParityHdrPasses(renderer, true));
  renderer.setBeforePresent({});
  ok = ok && renderFrame(renderer, scene, vp0);
  renderer.EndFrame();

  renderer.setBeforePresent([&image](Renderer& current) {
    image = current.getBackend()->readBackbuffer(kSize, kSize);
  });
  ok = ok && renderFrame(renderer, scene, vp1);
  renderer.EndFrame();
  renderer.setBeforePresent({});

  const size_t breaks = renderer.getBackend()->renderPassBreaks();
  if (breaks != 0) {
    std::printf("  FAILED: multi-pass HDR incurred %zu renderPassBreaks\n",
                breaks);
    ok = false;
  }
  if (!image.success() || !noBloom.success()) {
    std::printf("  HDR backbuffer readback failed\n");
    ok = false;
  }

  if (ok) {
    // Center of orb (~128, 118): AgX must desaturate {16, 3.2, 0.7} toward warm
    // white rather than clipping green/blue to low values.
    const size_t centerIdx = (118u * kSize + 128u) * 4u;
    const int cr = image.pixels[centerIdx + 0];
    const int cg = image.pixels[centerIdx + 1];
    const int cb = image.pixels[centerIdx + 2];
    if (cr < 210 || cg < 165 || cb < 120) {
      std::printf(
        "  AgX center highlight did not desaturate as expected (%d,%d,%d)\n",
        cr,
        cg,
        cb);
      ok = false;
    }

    // UI swatch center (32, 26): must stay crisp sRGB {40, 220, 120, 255}.
    const size_t uiIdx = (26u * kSize + 32u) * 4u;
    const int ur = image.pixels[uiIdx + 0];
    const int ug = image.pixels[uiIdx + 1];
    const int ub = image.pixels[uiIdx + 2];
    if (std::abs(ur - 40) > 2 || std::abs(ug - 220) > 2 ||
        std::abs(ub - 120) > 2) {
      std::printf(
        "  UI swatch was altered by HDR post-process (%d,%d,%d)\n", ur, ug, ub);
      ok = false;
    }

    // Bloom halo: dark background pixels around the orb must brighten.
    size_t bloomGlowPixels = 0;
    for (size_t px = 0; px + 3 < image.pixels.size(); px += 4) {
      const int baseR = noBloom.pixels[px + 0];
      const int bloomR = image.pixels[px + 0];
      if (baseR < 40 && bloomR - baseR >= 6) {
        bloomGlowPixels += 1;
      }
    }
    if (bloomGlowPixels < 200) {
      std::printf("  5-mip bloom brightened only %zu halo pixels\n",
                  bloomGlowPixels);
      ok = false;
    }
  }

  world.releaseResources();
  renderer.destroyMesh(sphere.handle);
  renderer.destroyMesh(cube.handle);
  return ok;
}

static bool
compareImages(const std::string& label,
              const char* candidateName,
              const FrameReadback& reference,
              const FrameReadback& candidate,
              double tolerance)
{
  if (reference.pixels.size() != candidate.pixels.size() ||
      reference.pixels.empty()) {
    std::printf("FAILED: %s images have different sizes\n", label.c_str());
    return false;
  }
  size_t differing = 0;
  int largest = 0;
  for (size_t index = 0; index < reference.pixels.size(); index += 4) {
    int difference = 0;
    for (size_t channel = 0; channel < 4; ++channel) {
      difference =
        std::max(difference,
                 std::abs(static_cast<int>(reference.pixels[index + channel]) -
                          static_cast<int>(candidate.pixels[index + channel])));
    }
    differing += difference > 2 ? 1u : 0u;
    largest = std::max(largest, difference);
  }
  const size_t total = reference.pixels.size() / 4;
  const double share =
    static_cast<double>(differing) / static_cast<double>(total);
  std::printf("%s: %zu of %zu pixels differ by more than 2 (largest %d)\n",
              label.c_str(),
              differing,
              total,
              largest);
  if (share > tolerance) {
    std::printf("FAILED: %s differs between OpenGL and %s\n",
                label.c_str(),
                candidateName);
    return false;
  }
  return true;
}

// Writes an image as `<label>-<tag>.ppm` into `directory` for inspection.
static void
saveImage(const std::string& directory,
          const char* label,
          const char* tag,
          const FrameReadback& image)
{
  const std::string path = directory + "/" + label + "-" + tag + ".ppm";
  std::ofstream file(path, std::ios::binary);
  file << "P6\n" << image.width << " " << image.height << "\n255\n";
  for (size_t index = 0; index + 3 < image.pixels.size(); index += 4) {
    file.write(reinterpret_cast<const char*>(image.pixels.data() + index), 3);
  }
}

int
runBackendParity(GLFWwindow* context,
                 const std::string& dumpDirectory,
                 const std::string& sceneFilter)
{
  ParityRig openGl;
  if (!makeRig(openGl, ParityApi::OpenGl, context)) {
    return 77;
  }
  // Every other backend is compared with OpenGL; one that cannot start here is
  // skipped.
  std::vector<std::unique_ptr<ParityRig>> candidates;
  const std::vector<ParityApi> apis = { ParityApi::Vulkan };
  for (ParityApi api : apis) {
    std::unique_ptr<ParityRig> rig = std::make_unique<ParityRig>();
    if (makeRig(*rig, api, context)) {
      candidates.push_back(std::move(rig));
    }
  }
  if (candidates.empty()) {
    return 77;
  }
  const std::vector<ParityScene> allScenes = {
    { "world", sceneWorld, 0.002 },
    { "pbr", scenePbrPunctualLights, 0.002 },
    { "shadowatlas", sceneShadowAtlas, 0.004 },
    { "hdrbloomagx", sceneHdrBloomAgx, 0.005 },
    { "2d", scene2D, 0.002 },
    { "tokens", sceneTokens, 0.002 },
    { "textures", sceneTextures, 0.0 },
    { "formats", sceneFormats, 0.0 },
    { "panels", scenePanels, 0.002 },
    { "backbuffer", sceneBackbuffer, 0.002 },
  };
  std::vector<ParityScene> scenes;
  for (const ParityScene& scene : allScenes) {
    if (sceneFilter.empty() || sceneFilter == scene.name) {
      scenes.push_back(scene);
    }
  }
  if (scenes.empty()) {
    std::printf("Unknown parity scene filter: %s\n", sceneFilter.c_str());
    return 2;
  }
  int failures = 0;
  for (const ParityScene& scene : scenes) {
    FrameReadback reference;
    if (!scene.render(*openGl.renderer, reference)) {
      std::printf("FAILED: %s did not render on OpenGL\n", scene.name);
      failures += 1;
      continue;
    }
    if (!dumpDirectory.empty()) {
      saveImage(dumpDirectory, scene.name, openGl.tag, reference);
    }
    for (const std::unique_ptr<ParityRig>& rig : candidates) {
      FrameReadback candidate;
      const std::string label =
        std::string(scene.name) + " (" + rig->name + ")";
      if (!scene.render(*rig->renderer, candidate)) {
        std::printf("FAILED: %s did not render\n", label.c_str());
        failures += 1;
        continue;
      }
      if (!dumpDirectory.empty()) {
        saveImage(dumpDirectory, scene.name, rig->tag, candidate);
      }
      if (!compareImages(
            label, rig->name, reference, candidate, scene.tolerance)) {
        failures += 1;
      }
    }
  }
  std::printf("Backend parity: %d of %zu comparisons differ\n",
              failures,
              scenes.size() * candidates.size());
  candidates.clear();
  openGl.renderer.reset();
  return failures == 0 ? 0 : 1;
}

static bool
benchHdrOnRig(ParityRig& rig)
{
  static constexpr int kBenchWidth = 2560;
  static constexpr int kBenchHeight = 1440;
  static constexpr int kWarmupFrames = 25;
  static constexpr int kTimedFrames = 80;

  Renderer& renderer = *rig.renderer;
  if (!allBuiltinStylesReady(renderer)) {
    return false;
  }
  const MeshAssetInfo cube = enrollCube(renderer);
  std::vector<unsigned char> albedoBytes(64u * 64u * 4u, 210u);
  std::vector<unsigned char> normalBytes(64u * 64u * 4u, 128u);
  std::vector<unsigned char> ormBytes(64u * 64u * 4u, 160u);
  std::vector<unsigned char> emissiveBytes(64u * 64u * 4u, 16u);
  for (size_t i = 0; i < 64u * 64u; ++i) {
    normalBytes[i * 4u + 2u] = 255u;
    normalBytes[i * 4u + 3u] = 255u;
    ormBytes[i * 4u + 0u] = 255u;
    ormBytes[i * 4u + 1u] = static_cast<unsigned char>(90u + (i % 64u));
    ormBytes[i * 4u + 2u] = static_cast<unsigned char>((i / 64u) * 3u);
    ormBytes[i * 4u + 3u] = 255u;
  }
  TextureOptions texOpts;
  texOpts.filter = TextureFilter::Linear;
  texOpts.wrapX = TextureWrap::Repeat;
  texOpts.wrapY = TextureWrap::Repeat;
  const TextureHandle albedoTex =
    renderer.enrollTexture(albedoBytes.data(), 64, 64, 4, texOpts);
  const TextureHandle normalTex =
    renderer.enrollTexture(normalBytes.data(), 64, 64, 4, texOpts);
  const TextureHandle ormTex =
    renderer.enrollTexture(ormBytes.data(), 64, 64, 4, texOpts);
  const TextureHandle emissiveTex =
    renderer.enrollTexture(emissiveBytes.data(), 64, 64, 4, texOpts);

  RenderWorld world;
  RenderEnvironment env;
  env.lightDirection = { 0.45f, 1.0f, 0.35f };
  env.lightColor = { 0.9f, 0.86f, 0.8f };
  env.ambientColor = { 0.03f, 0.035f, 0.045f };
  env.shadowsEnabled = true;
  env.shadowMapSize = 1024;
  env.shadowPcf = true;
  world.setEnvironment(env);

  bool ok = true;
  for (uint32_t m = 1; m <= 4u; ++m) {
    RenderMaterialDesc mat;
    mat.tint = { 0.85f, 0.82f, 0.78f, 1.0f };
    mat.albedoTexture = albedoTex;
    mat.normalTexture = normalTex;
    mat.metallicRoughnessTexture = ormTex;
    mat.emissiveTexture = emissiveTex;
    mat.metallic = 0.25f * static_cast<float>(m - 1u);
    mat.roughness = 0.2f + 0.18f * static_cast<float>(m - 1u);
    mat.emissiveFactor = (m == 4u) ? std::array<float, 3>{ 3.5f, 1.2f, 0.3f }
                                   : std::array<float, 3>{ 0.0f, 0.0f, 0.0f };
    mat.castsShadow = true;
    mat.receivesShadow = true;
    ok = world.createMaterial(m, mat) && ok;
  }

  for (size_t i = 0; i < 2000u; ++i) {
    const size_t row = i / 50u;
    const float gx = -22.0f + 44.0f * static_cast<float>(i % 50u) / 50.0f;
    const float gz = -4.0f - 48.0f * static_cast<float>(row) / 40.0f;
    const float gy = 0.35f * std::sin(static_cast<float>(i) * 0.37f);
    RenderInstanceDesc inst;
    inst.mesh = cube.handle;
    inst.indexCount = cube.indexCount;
    inst.material = static_cast<RenderMaterialId>((i % 4u) + 1u);
    inst.hasBounds = true;
    inst.localBounds = { cube.minBounds, cube.maxBounds };
    inst.world =
      toArray(glm::translate(glm::mat4(1.0f), glm::vec3(gx, gy, gz)) *
              glm::scale(glm::mat4(1.0f), glm::vec3(0.55f)));
    ok =
      world.createInstance(static_cast<RenderInstanceId>(i + 1u), inst) && ok;
  }

  for (uint32_t l = 1; l <= 32u; ++l) {
    const uint32_t lightRow = l / 8u;
    RenderLightDesc light;
    light.kind = (l == 1u || (l % 4u) == 0u) ? RenderLightKind::Spot
                                             : RenderLightKind::Point;
    light.position = { -18.0f + static_cast<float>(l % 8u) * 5.0f,
                       3.2f,
                       -6.0f - static_cast<float>(lightRow) * 10.0f };
    light.direction = { 0.0f, -1.0f, -0.25f };
    light.color = { 0.85f, 0.72f, 0.55f };
    light.intensity = (l == 1u) ? 28.0f : 10.0f;
    light.range = 14.0f;
    light.innerConeCos = std::cos(glm::radians(18.0f));
    light.outerConeCos = std::cos(glm::radians(32.0f));
    light.castsShadow = (l == 1u);
    ok = world.createLight(l, light) && ok;
  }

  std::vector<RenderPassDesc> passes = buildParityHdrPasses(renderer, true);
  if (!passes.empty()) {
    RenderPassDesc& last = passes.back();
    last.useScreenTarget = false;
    last.pooledTargetName = "BenchFinalOutput";
    last.targetDesc.name = "BenchFinalOutput";
    last.targetDesc.windowRelative = true;
    FramebufferAttachmentDesc outColor;
    outColor.format = TextureFormat::RGBA8;
    outColor.filter = TextureFilter::Linear;
    outColor.wrap = TextureWrap::ClampToEdge;
    last.targetDesc.colorAttachments.push_back(outColor);
  }

  DrawList scene(renderer.getWindow(), renderer.getCamera());
  scene.AddDrawable(&world, RenderLayerId::World);
  scene.SetDefaultLayerPasses(RenderLayerId::World, std::move(passes));

  const glm::mat4 proj =
    glm::perspective(glm::radians(58.0f), 2560.0f / 1440.0f, 0.25f, 85.0f);
  const std::array<float, 16> vp =
    toArray(proj * glm::lookAt(glm::vec3(0.0f, 9.5f, 9.0f),
                               glm::vec3(0.0f, -0.5f, -24.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f)));

  for (int w = 0; w < kWarmupFrames && ok; ++w) {
    ok = renderFrame(renderer, scene, vp) && ok;
    renderer.EndFrame();
  }
  const FramebufferHandle finalTarget =
    renderer.getRenderTarget("BenchFinalOutput").fboHandle;
  FrameReadback warmupSync;
  ok = ok && finalTarget.isValid() &&
       readTarget(renderer, finalTarget, kBenchWidth, kBenchHeight, warmupSync);

  std::vector<double> cpuMs;
  cpuMs.reserve(static_cast<size_t>(kTimedFrames));
  size_t totalBreaks = 0;

  const std::chrono::steady_clock::time_point batchStart =
    std::chrono::steady_clock::now();
  for (int f = 0; f < kTimedFrames && ok; ++f) {
    // Animate dynamic punctual lights each frame while static geometry holds.
    RenderLightDesc moving;
    moving.kind = RenderLightKind::Point;
    moving.position = { std::sin(static_cast<float>(f) * 0.08f) * 12.0f,
                        2.8f,
                        -12.0f +
                          std::cos(static_cast<float>(f) * 0.08f) * 6.0f };
    moving.color = { 0.4f, 0.8f, 1.0f };
    moving.intensity = 14.0f;
    moving.range = 14.0f;
    world.updateLight(2u, moving);

    const std::chrono::steady_clock::time_point c0 =
      std::chrono::steady_clock::now();
    ok = renderFrame(renderer, scene, vp) && ok;
    renderer.EndFrame();
    const std::chrono::steady_clock::time_point c1 =
      std::chrono::steady_clock::now();
    cpuMs.push_back(std::chrono::duration<double, std::milli>(c1 - c0).count());
    totalBreaks += renderer.getBackend()->renderPassBreaks();
  }
  FrameReadback finalSync;
  ok = ok &&
       readTarget(renderer, finalTarget, kBenchWidth, kBenchHeight, finalSync);
  const std::chrono::steady_clock::time_point batchEnd =
    std::chrono::steady_clock::now();

  std::sort(cpuMs.begin(), cpuMs.end());
  const double medianCpuMs = cpuMs.empty() ? 0.0 : cpuMs[cpuMs.size() / 2u];
  const double avgGpuMs =
    std::chrono::duration<double, std::milli>(batchEnd - batchStart).count() /
    static_cast<double>(kTimedFrames);

  std::printf("HdrForwardBench api=%s res=2560x1440 instances=2000 lights=32 "
              "cpu_submit_ms=%.3f gpu_frame_ms=%.3f pass_breaks=%zu\n",
              rig.name,
              medianCpuMs,
              avgGpuMs,
              totalBreaks);

  if (totalBreaks != 0) {
    std::printf("FAILED: %s incurred %zu renderPassBreaks during HDR bench\n",
                rig.name,
                totalBreaks);
    ok = false;
  }

  world.releaseResources();
  renderer.destroyTexture(albedoTex);
  renderer.destroyTexture(normalTex);
  renderer.destroyTexture(ormTex);
  renderer.destroyTexture(emissiveTex);
  renderer.destroyMesh(cube.handle);
  return ok;
}

int
runHdrForwardBench(GLFWwindow* context)
{
  ParityRig openGl;
  if (!makeRig(openGl, ParityApi::OpenGl, context, 2560, 1440)) {
    return 77;
  }
  ParityRig vulkan;
  if (!makeRig(vulkan, ParityApi::Vulkan, context, 2560, 1440)) {
    return 77;
  }
  const bool glOk = benchHdrOnRig(openGl);
  const bool vkOk = benchHdrOnRig(vulkan);
  vulkan.renderer.reset();
  openGl.renderer.reset();
  return (glOk && vkOk) ? 0 : 1;
}
