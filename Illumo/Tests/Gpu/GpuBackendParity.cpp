// Real-GPU parity of the OpenGL and Vulkan backends: every scene renders
// through both in one process and the images are compared. Scenes cover the
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
  explicit ParityWindow(GLFWwindow* value)
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

struct ParityRig
{
  const char* name = "";
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
makeRig(ParityRig& rig, const char* name, GLFWwindow* context)
{
  rig.name = name;
  rig.window = std::make_unique<ParityWindow>(context);
  rig.env = std::make_unique<EnvVars>();
  rig.env->setVar("WinX", kSize);
  rig.env->setVar("WinY", kSize);
  rig.camera =
    std::make_unique<Camera>(glm::vec2(0.0f, 0.0f), 1.0f, rig.env.get());
  std::unique_ptr<IBackend> backend =
    context != nullptr ? CreateOpenGLBackend(rig.window.get())
                       : CreateVulkanBackend(rig.window.get(), false);
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

// Raw tokens exercising the pipeline state the backends must agree on.
class ParityTokenScene final : public DrawableBase
{
public:
  FramebufferHandle target{};
  ShaderHandle shader{};
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
  if (!tokens.shader.isValid() || !tokens.mesh.isValid() ||
      !tokens.positionsOnly.isValid() || !tokens.texture.isValid()) {
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

static bool
compareImages(const char* label,
              const FrameReadback& reference,
              const FrameReadback& candidate,
              double tolerance)
{
  if (reference.pixels.size() != candidate.pixels.size() ||
      reference.pixels.empty()) {
    std::printf("FAILED: %s images have different sizes\n", label);
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
              label,
              differing,
              total,
              largest);
  if (share > tolerance) {
    std::printf("FAILED: %s differs between OpenGL and Vulkan\n", label);
    return false;
  }
  return true;
}

// Writes both images as PPM files into `directory` for inspection.
static void
saveComparison(const std::string& directory,
               const char* label,
               const FrameReadback& openGl,
               const FrameReadback& vulkan)
{
  for (int which = 0; which < 2; ++which) {
    const FrameReadback& image = which == 0 ? openGl : vulkan;
    const std::string path =
      directory + "/" + label + (which == 0 ? "-gl.ppm" : "-vk.ppm");
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << image.width << " " << image.height << "\n255\n";
    for (size_t index = 0; index + 3 < image.pixels.size(); index += 4) {
      file.write(reinterpret_cast<const char*>(image.pixels.data() + index), 3);
    }
  }
}

int
runBackendParity(GLFWwindow* context, const std::string& dumpDirectory)
{
  ParityRig openGl;
  ParityRig vulkan;
  if (!makeRig(openGl, "OpenGL", context) ||
      !makeRig(vulkan, "Vulkan", nullptr)) {
    return 77;
  }
  const std::vector<ParityScene> scenes = {
    { "world", sceneWorld, 0.002 },
    { "2d", scene2D, 0.002 },
    { "tokens", sceneTokens, 0.002 },
    { "textures", sceneTextures, 0.0 },
    { "formats", sceneFormats, 0.0 },
    { "panels", scenePanels, 0.002 },
    { "backbuffer", sceneBackbuffer, 0.002 },
  };
  int failures = 0;
  for (const ParityScene& scene : scenes) {
    FrameReadback reference;
    FrameReadback candidate;
    const bool openGlRendered = scene.render(*openGl.renderer, reference);
    const bool vulkanRendered = scene.render(*vulkan.renderer, candidate);
    if (!openGlRendered || !vulkanRendered) {
      std::printf("FAILED: %s did not render (OpenGL %s, Vulkan %s)\n",
                  scene.name,
                  openGlRendered ? "ok" : "failed",
                  vulkanRendered ? "ok" : "failed");
      failures += 1;
      continue;
    }
    if (!dumpDirectory.empty()) {
      saveComparison(dumpDirectory, scene.name, reference, candidate);
    }
    if (!compareImages(scene.name, reference, candidate, scene.tolerance)) {
      failures += 1;
    }
  }
  std::printf(
    "Backend parity: %d of %zu scenes differ\n", failures, scenes.size());
  vulkan.renderer.reset();
  openGl.renderer.reset();
  return failures == 0 ? 0 : 1;
}
