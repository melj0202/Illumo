#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestRegistry.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

// The std140 block in the instanced shaders is three mat4 and one vec4.
static_assert(sizeof(Renderer::FrameUniforms) == 208,
              "FrameUniforms must match the std140 shader block");

static bool
check(bool condition, const char* message)
{
  if (!condition) {
    std::printf("FAILED: %s\n", message);
  }
  return condition;
}

class FrameUniformUser : public DrawableBase
{
public:
  int calls = 0;
  bool accepted = true;
  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override
  {
    for (int call = 0; call < calls; ++call) {
      accepted = renderer->useFrameUniforms() && accepted;
    }
    return true;
  }
};

static std::array<float, 16>
translation(float x)
{
  std::array<float, 16> matrix{
    1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1
  };
  matrix[12] = x;
  return matrix;
}

// The frame block written by the last RenderScene, and how many times it was
// written and bound.
static bool
submittedFrameUniforms(const MockBackend& mock,
                       Renderer::FrameUniforms* uniforms,
                       size_t* writes,
                       size_t* binds)
{
  *writes = 0;
  *binds = 0;
  bool bindingsMatch = true;
  for (size_t index = 0; index < mock.getLastNonEmptySubmittedCount();
       ++index) {
    const RenderCommand& command = mock.getLastNonEmptySubmitted(index);
    if (command.commandType == CommandType::WriteBuffer &&
        command.writeBuffer.sizeBytes == sizeof(Renderer::FrameUniforms)) {
      ++*writes;
      std::memcpy(uniforms, command.writeBuffer.data, sizeof(*uniforms));
    } else if (command.commandType == CommandType::BindUniformBuffer) {
      ++*binds;
      bindingsMatch = bindingsMatch && command.bindUniformBuffer.binding ==
                                         FrameUniformsBindingPoint;
    }
  }
  return bindingsMatch;
}

static int
frameUniformsOncePerScene()
{
  HeadlessRenderFixture fixture(640, 480);
  FrameUniformUser user;
  user.calls = 2;
  DrawList scene(&fixture.window, &fixture.camera);
  scene.AddDrawable(&user, RenderLayerId::World);
  bool ok = true;

  const std::array<float, 16> first = translation(1.0f);
  fixture.renderer.setNextWorldViewProjection(first);
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();
  Renderer::FrameUniforms uniforms;
  size_t writes = 0;
  size_t binds = 0;
  ok = check(submittedFrameUniforms(fixture.mock, &uniforms, &writes, &binds),
             "The block binds at the shared binding point") &&
       ok;
  ok = check(user.accepted && writes == 1 && binds == 2,
             "Two uses in one scene write once and bind each time") &&
       ok;
  ok = check(uniforms.viewProjection == first &&
               uniforms.previousViewProjection == first &&
               uniforms.shadowState[0] == 0.0f,
             "The first frame carries its camera and no shadow map") &&
       ok;

  const std::array<float, 16> second = translation(2.0f);
  fixture.renderer.setNextWorldViewProjection(second);
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();
  ok = check(submittedFrameUniforms(fixture.mock, &uniforms, &writes, &binds),
             "The second frame binds at the shared binding point") &&
       ok;
  ok = check(writes == 1 && uniforms.viewProjection == second &&
               uniforms.previousViewProjection == first,
             "Each scene rewrites the block with the previous camera") &&
       ok;

  FrameUniformUser idle;
  DrawList idleScene(&fixture.window, &fixture.camera);
  idleScene.AddDrawable(&idle, RenderLayerId::World);
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&idleScene, &fixture.camera);
  fixture.renderer.EndFrame();
  ok = check(fixture.mock.countNonEmptyOfType(CommandType::WriteBuffer) == 0 &&
               fixture.mock.countNonEmptyOfType(
                 CommandType::BindUniformBuffer) == 0,
             "A scene that never uses the block emits nothing for it") &&
       ok;
  return ok ? 0 : 1;
}

static int
instancedTokensValidateBuffers()
{
  HeadlessRenderFixture fixture;
  Renderer& renderer = fixture.renderer;
  const unsigned int stride =
    instanceLayoutStride(InstanceLayout::LitModelTint);
  const BufferHandle instances =
    renderer.enrollBuffer(BufferUsage::Instance, stride * 4);
  const BufferHandle uniforms = renderer.enrollBuffer(BufferUsage::Uniform, 64);
  bool ok = check(stride == 144 && instances.isValid() && uniforms.isValid() &&
                    !renderer.enrollBuffer(BufferUsage::Instance, 0).isValid(),
                  "Buffers enroll with a nonzero capacity");

  std::array<unsigned char, 144 * 4> data{};
  const unsigned int size = static_cast<unsigned int>(data.size());
  renderer.BeginFrame();
  renderer.pushWriteBuffer(instances, 0, size, data.data());
  renderer.pushWriteBuffer(instances, stride, size, data.data());
  renderer.pushInstanceStream(instances, 0, InstanceLayout::LitModelTint);
  renderer.pushInstanceStream(uniforms, 0, InstanceLayout::LitModelTint);
  renderer.pushInstanceStream(instances, 0, InstanceLayout::None);
  renderer.pushBindUniformBuffer(instances, 0);
  renderer.pushBindUniformBuffer(uniforms, 0);
  renderer.pushDrawIndexedInstanced(36, 0, 4);
  renderer.EndFrame();
  const MockBackend& mock = fixture.mock;
  ok = check(mock.getRejectedStaleCommandCount() == 4,
             "Out-of-range writes and wrong-usage buffers are rejected") &&
       ok;
  ok = check(mock.countNonEmptyOfType(CommandType::WriteBuffer) == 1 &&
               mock.countNonEmptyOfType(CommandType::SetInstanceStream) == 1 &&
               mock.countNonEmptyOfType(CommandType::BindUniformBuffer) == 1 &&
               mock.countNonEmptyOfType(CommandType::DrawIndexedInstanced) == 1,
             "Valid buffer tokens reach the backend") &&
       ok;

  ok = check(renderer.destroyBuffer(instances) &&
               !renderer.destroyBuffer(instances),
             "A buffer is destroyed once") &&
       ok;
  renderer.BeginFrame();
  renderer.pushInstanceStream(instances, 0, InstanceLayout::LitModelTint);
  renderer.EndFrame();
  ok = check(mock.getRejectedStaleCommandCount() == 5,
             "A destroyed buffer's handle is stale") &&
       ok;
  return ok ? 0 : 1;
}

static std::string
readShader(const char* name)
{
  std::ifstream file(std::string(ILLUMO_ENGINE_SHADERS) + "/" + name);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

static int
instancedStylesRegistered()
{
  HeadlessRenderFixture fixture;
  fixture.renderer.ensureBuiltinStyles();
  bool litPaths = false;
  bool shadowPaths = false;
  for (size_t index = 0; index < fixture.mock.getCreateCount(); ++index) {
    const std::string& note = fixture.mock.getCreate(index).pathOrNote;
    litPaths = litPaths || note == "Shader/mesh_lit_instanced_vertex.glsl|"
                                   "Shader/mesh_lit_instanced_frag.glsl";
    shadowPaths = shadowPaths || note ==
                                   "Shader/shadow_depth_instanced_vertex.glsl|"
                                   "Shader/shadow_depth_frag.glsl";
  }
  bool ok = check(
    fixture.renderer.getStyle(RenderStyleId::LitMeshInstanced) != nullptr &&
      fixture.renderer.getStyle(RenderStyleId::ShadowDepthInstanced) !=
        nullptr &&
      litPaths && shadowPaths,
    "Instanced lit and shadow styles enroll their shader files");

  // Every instanced shader declares the block as Renderer lays it out.
  const char* block = "layout (std140) uniform FrameUniforms\n"
                      "{\n"
                      "    mat4 uViewProjection;\n"
                      "    mat4 uPreviousViewProjection;\n"
                      "    mat4 uLightSpace;\n"
                      "    vec4 uShadowState;\n"
                      "};";
  for (const char* name : { "mesh_lit_instanced_vertex.glsl",
                            "mesh_lit_instanced_frag.glsl",
                            "shadow_depth_instanced_vertex.glsl" }) {
    ok = check(readShader(name).find(block) != std::string::npos,
               "An instanced shader declares the FrameUniforms block") &&
         ok;
  }
  return ok ? 0 : 1;
}

void
registerInstancingTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Instancing.FrameUniformsOncePerScene",
               frameUniformsOncePerScene);
  registry.add("Illumo.Instancing.TokensValidateBuffers",
               instancedTokensValidateBuffers);
  registry.add("Illumo.Instancing.StylesRegistered", instancedStylesRegistered);
}
