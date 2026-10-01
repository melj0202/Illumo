// Headless checks of the Vulkan backend's device-free pieces: GLSL
// preparation and reflection (GlslToSpirv) and texel conversion
// (GpuTexels). The GPU paths are covered by IllumoGpuTests.

#include "Rendering/Gpu/GlslToSpirv.h"
#include "Rendering/Gpu/GpuTexels.h"
#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <array>
#include <string>
#include <vector>

#ifndef ILLUMO_ENGINE_SHADERS
#define ILLUMO_ENGINE_SHADERS "Shader"
#endif

static const GlslUniform*
findUniform(const GlslProgram& program, const std::string& name)
{
  for (const GlslUniform& uniform : program.uniforms) {
    if (uniform.name == name) {
      return &uniform;
    }
  }
  return nullptr;
}

static const GlslBinding*
findBinding(const GlslProgram& program, GlslBindingKind kind)
{
  for (const GlslBinding& binding : program.bindings) {
    if (binding.kind == kind) {
      return &binding;
    }
  }
  return nullptr;
}

static bool
compileFiles(const std::string& vertex,
             const std::string& fragment,
             GlslProgram* program,
             std::string* error)
{
  const std::string directory = ILLUMO_ENGINE_SHADERS;
  const PreprocessResult vertexSource =
    ShaderPreprocessor::ProcessFile(directory + "/" + vertex);
  const PreprocessResult fragmentSource =
    ShaderPreprocessor::ProcessFile(directory + "/" + fragment);
  if (!vertexSource.success || !fragmentSource.success) {
    *error = vertexSource.errorMessage + fragmentSource.errorMessage;
    return false;
  }
  return compileGlslProgram(
    vertexSource.source, fragmentSource.source, program, error);
}

static int
testPrepareGlsl()
{
  TestCounters counters;
  testSection("Vulkan GLSL: version and clip-depth wrapper");
  const std::string vertex = "#version 330 core\n"
                             "layout (location = 0) in vec3 aPos;\n"
                             "void main() { gl_Position = vec4(aPos, 1.0); }\n";
  const std::string prepared = prepareGlslForSpirv(vertex, true);
  testTrue(counters,
           prepared.compare(0, 13, "#version 450\n") == 0,
           "the version becomes 450");
  testTrue(counters,
           prepared.find("#version 330") == std::string::npos,
           "the OpenGL version line is gone");
  testTrue(counters,
           prepared.find("#line 2\n") != std::string::npos,
           "the body keeps its original line numbers");
  testTrue(counters,
           prepared.find("(gl_Position.z + gl_Position.w) * 0.5") !=
             std::string::npos,
           "vertex shaders remap OpenGL clip depth to Vulkan's");
  testTrue(counters,
           prepared.find("gl_PointSize = 1.0") != std::string::npos,
           "vertex shaders write OpenGL's default point size");
  const std::string fragment = prepareGlslForSpirv(
    "#version 330 core\nout vec4 c;\nvoid main() { c = vec4(1.0); }\n", false);
  testTrue(counters,
           fragment.find("illumo_user_main") == std::string::npos,
           "fragment shaders are not wrapped");
  const std::string unversioned =
    prepareGlslForSpirv("void main() {}\n", false);
  testTrue(counters,
           unversioned.compare(0, 13, "#version 450\n") == 0 &&
             unversioned.find("#line 1\n") != std::string::npos,
           "a source without a version gets one and keeps line 1");
  return counters.failures;
}

static int
testCompileLinksLikeOpenGl()
{
  TestCounters counters;
  testSection("Vulkan GLSL: OpenGL program semantics");
  if (!initializeGlslCompiler()) {
    testTrue(counters, false, "the compiler starts");
    return counters.failures;
  }
  // Loose uniforms shared by both stages, unlocated varyings matched by
  // name in a different order, a sampler and a flat varying.
  const std::string vertex = "#version 330 core\n"
                             "layout (location = 0) in vec3 aPos;\n"
                             "layout (location = 2) in vec2 aUv;\n"
                             "out vec2 uv;\n"
                             "flat out vec3 look;\n"
                             "uniform mat4 uMVP;\n"
                             "uniform int uMode;\n"
                             "void main() {\n"
                             "  gl_Position = uMVP * vec4(aPos, 1.0);\n"
                             "  uv = aUv; look = vec3(float(uMode));\n"
                             "}\n";
  const std::string fragment =
    "#version 330 core\n"
    "flat in vec3 look;\n"
    "in vec2 uv;\n"
    "layout (location = 0) out vec4 color;\n"
    "layout (location = 1) out vec2 velocity;\n"
    "uniform sampler2D uTexture;\n"
    "uniform int uMode;\n"
    "uniform vec4 uTint;\n"
    "uniform bool uFlag;\n"
    "void main() {\n"
    "  color = texture(uTexture, uv) * uTint + vec4(look, 0.0);\n"
    "  velocity = uFlag ? gl_FragCoord.xy : vec2(float(uMode));\n"
    "}\n";
  GlslProgram program;
  std::string error;
  const bool compiled = compileGlslProgram(vertex, fragment, &program, &error);
  testTrue(counters, compiled, ("the program links: " + error).c_str());
  if (!compiled) {
    finalizeGlslCompiler();
    return counters.failures;
  }
  testTrue(counters,
           !program.vertexSpirv.empty() && !program.fragmentSpirv.empty(),
           "both stages produce SPIR-V");
  const GlslBinding* block =
    findBinding(program, GlslBindingKind::DefaultBlock);
  testTrue(counters,
           block != nullptr && program.defaultBlockSize >= 64 + 4 + 16 + 4,
           "loose uniforms share one default block");
  const GlslUniform* mvp = findUniform(program, "uMVP");
  const GlslUniform* mode = findUniform(program, "uMode");
  const GlslUniform* tint = findUniform(program, "uTint");
  const GlslUniform* flag = findUniform(program, "uFlag");
  testTrue(counters,
           mvp != nullptr && mvp->type == GlslValueType::Mat4 &&
             mode != nullptr && mode->type == GlslValueType::Int &&
             tint != nullptr && tint->type == GlslValueType::Vec4 &&
             flag != nullptr && flag->type == GlslValueType::Bool,
           "each uniform is reflected once, with its type");
  if (mvp != nullptr && mode != nullptr && tint != nullptr) {
    testTrue(counters,
             mvp->offset != mode->offset && mode->offset != tint->offset &&
               tint->offset % 16 == 0 && mvp->offset % 16 == 0,
             "uniform offsets are distinct and std140-aligned");
  }
  const GlslBinding* sampler = findBinding(program, GlslBindingKind::Sampler2D);
  testTrue(counters,
           sampler != nullptr && block != nullptr &&
             sampler->binding != block->binding &&
             program.samplerNames.size() == 1 &&
             program.samplerNames[0] == "uTexture",
           "the sampler has its own binding");
  bool position = false;
  bool uv = false;
  for (const GlslVertexInput& input : program.inputs) {
    position = position || input.location == 0;
    uv = uv || input.location == 2;
  }
  testTrue(counters,
           position && uv && program.inputs.size() == 2,
           "vertex inputs keep their locations");
  testTrue(counters,
           program.fragmentOutputs == 3u,
           "fragment outputs 0 and 1 are recorded");

  GlslProgram broken;
  testTrue(
    counters,
    !compileGlslProgram(
      vertex, "#version 330 core\nvoid main() { oops; }\n", &broken, &error) &&
      error.find("Fragment") != std::string::npos,
    "a compile error names the failing stage");
  finalizeGlslCompiler();
  return counters.failures;
}

static int
testEngineShadersCompile()
{
  TestCounters counters;
  testSection("Vulkan GLSL: every engine shader file compiles");
  if (!initializeGlslCompiler()) {
    testTrue(counters, false, "the compiler starts");
    return counters.failures;
  }
  const std::array<std::array<const char*, 2>, 7> pairs = { {
    { "canvas_vertex.glsl", "canvas_frag.glsl" },
    { "capture_vertex.glsl", "capture_frag.glsl" },
    { "mesh_lit_vertex.glsl", "mesh_lit_frag.glsl" },
    { "mesh_lit_instanced_vertex.glsl", "mesh_lit_instanced_frag.glsl" },
    { "shadow_depth_vertex.glsl", "shadow_depth_frag.glsl" },
    { "shadow_depth_instanced_vertex.glsl", "shadow_depth_frag.glsl" },
    { "skybox_vertex.glsl", "skybox_frag.glsl" },
  } };
  for (const std::array<const char*, 2>& pair : pairs) {
    GlslProgram program;
    std::string error;
    const bool compiled = compileFiles(pair[0], pair[1], &program, &error);
    testTrue(counters,
             compiled,
             (std::string(pair[0]) + " + " + pair[1] + ": " + error).c_str());
  }
  GlslProgram instanced;
  std::string error;
  if (compileFiles("mesh_lit_instanced_vertex.glsl",
                   "mesh_lit_instanced_frag.glsl",
                   &instanced,
                   &error)) {
    unsigned model = 0;
    unsigned previous = 0;
    bool tint = false;
    for (const GlslVertexInput& input : instanced.inputs) {
      model = input.location == 4 ? input.locationCount : model;
      previous = input.location == 8 ? input.locationCount : previous;
      tint = tint || input.location == 12;
    }
    testTrue(counters,
             model == 4 && previous == 4 && tint,
             "instance matrices occupy four locations each");
    testTrue(counters,
             findBinding(instanced, GlslBindingKind::NamedBlock) != nullptr,
             "the FrameUniforms block is a named block");
    testTrue(counters,
             findUniform(instanced, "uViewProjection") == nullptr,
             "named block members stay out of the default block");
  }
  GlslProgram skybox;
  if (compileFiles("skybox_vertex.glsl", "skybox_frag.glsl", &skybox, &error)) {
    testTrue(counters,
             findBinding(skybox, GlslBindingKind::SamplerCube) != nullptr,
             "samplerCube is reflected as a cube binding");
  }
  finalizeGlslCompiler();
  return counters.failures;
}

static int
testTexelConversion()
{
  TestCounters counters;
  testSection("Vulkan texels: OpenGL pixel transfer rules");
  const unsigned char rgb[] = { 1, 2, 3, 4,  5,  6,  99, 99, 99,
                                7, 8, 9, 10, 11, 12, 99, 99, 99 };
  std::array<unsigned char, 16> rgba{};
  convertTexelsForStorage(rgb, 2, 2, 3, 3, 4, rgba.data());
  const std::array<unsigned char, 16> expected = { 1,  2,   3,  255, 4, 5,
                                                   6,  255, 7,  8,   9, 255,
                                                   10, 11,  12, 255 };
  testTrue(counters, rgba == expected, "RGB rows gain an opaque alpha");
  std::array<unsigned char, 4> red{};
  convertTexelsForStorage(rgba.data(), 2, 2, 4, 0, 1, red.data());
  testTrue(counters,
           red == std::array<unsigned char, 4>{ 1, 4, 7, 10 },
           "R8 storage keeps the first channel");
  const unsigned char one[] = { 200 };
  std::array<unsigned char, 4> widened{};
  convertTexelsForStorage(one, 1, 1, 1, 0, 4, widened.data());
  testTrue(counters,
           widened == std::array<unsigned char, 4>{ 200, 0, 0, 255 },
           "red data into RGBA storage reads (r, 0, 0, 1)");

  // Bottom row first in, top row first out.
  const unsigned char stored[] = { 10, 20, 30, 40, 50, 60, 70, 80 };
  std::array<unsigned char, 8> flipped{};
  convertReadbackTexels(stored, 4, 1, 2, TextureFormat::RGB8, flipped.data());
  testTrue(counters,
           flipped ==
             std::array<unsigned char, 8>{ 50, 60, 70, 255, 10, 20, 30, 255 },
           "readback flips rows and RGB8 reads opaque");
  const unsigned char reds[] = { 9, 7 };
  std::array<unsigned char, 8> expanded{};
  convertReadbackTexels(reds, 1, 1, 2, TextureFormat::R8, expanded.data());
  testTrue(counters,
           expanded ==
             std::array<unsigned char, 8>{ 7, 0, 0, 255, 9, 0, 0, 255 },
           "R8 readback reads (r, 0, 0, 1)");
  const uint16_t halves[] = { 0x3C00u, 0x3800u, 0xBC00u, 0x4000u };
  std::array<unsigned char, 4> converted{};
  convertReadbackTexels(reinterpret_cast<const unsigned char*>(halves),
                        8,
                        1,
                        1,
                        TextureFormat::RGBA16F,
                        converted.data());
  testTrue(counters,
           converted == std::array<unsigned char, 4>{ 255, 128, 0, 255 },
           "half floats clamp to [0, 1] like glReadPixels");
  testTrue(counters,
           halfToFloat(0x3555u) > 0.333f && halfToFloat(0x3555u) < 0.334f &&
             halfToFloat(0x0001u) > 0.0f,
           "halves decode, subnormals included");
  testTrue(counters,
           validTextureUpdate(256, 256, 7, 9, 200, 100, 4, 300) &&
             !validTextureUpdate(256, 256, 0, 1, 256, 256, 4, 256) &&
             !validTextureUpdate(256, 256, 0, 0, 2, 2, 2, 0),
           "update rectangles follow the OpenGL backend's rules");
  return counters.failures;
}

void
registerVulkanShaderTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Vulkan.PrepareGlsl", []() { return testPrepareGlsl(); });
  registry.add("Illumo.Vulkan.ProgramSemantics",
               []() { return testCompileLinksLikeOpenGl(); });
  registry.add("Illumo.Vulkan.EngineShadersCompile",
               []() { return testEngineShadersCompile(); });
  registry.add("Illumo.Vulkan.TexelConversion",
               []() { return testTexelConversion(); });
}
