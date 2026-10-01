// Headless checks of the Direct3D 12 backend's device-free pieces: the shared
// SPIR-V translated to HLSL (SPIRV-Cross), compiled to DXBC (FXC) and given a
// root signature. The GPU paths are covered by IllumoGpuTests.

#include <Illumo/Testing/TestRegistry.h>

#ifdef _WIN32

#include "Rendering/D3D12/D3D12ShaderCompiler.h"
#include "Rendering/Gpu/GlslToSpirv.h"
#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Testing/TestHelpers.h>
#include <array>
#include <string>
#include <vector>

#ifndef ILLUMO_ENGINE_SHADERS
#define ILLUMO_ENGINE_SHADERS "Shader"
#endif

// GLSL to DXBC for both stages plus the root signature; false with the
// failing step in *error.
static bool
buildForD3D12(const std::string& vertex,
              const std::string& fragment,
              std::string* vertexHlsl,
              std::string* error)
{
  GlslProgram program;
  if (!compileGlslProgram(vertex, fragment, &program, error)) {
    return false;
  }
  std::string pixelHlsl;
  translateProgramToHlsl(program, vertexHlsl, &pixelHlsl);
  std::vector<unsigned char> vertexCode;
  std::vector<unsigned char> pixelCode;
  if (!compileHlsl(*vertexHlsl, true, &vertexCode, error) ||
      !compileHlsl(pixelHlsl, false, &pixelCode, error)) {
    return false;
  }
  std::vector<unsigned char> root;
  std::vector<D3D12RootBinding> layout;
  return serializeRootSignature(program, &root, &layout, error) &&
         !vertexCode.empty() && !pixelCode.empty() && !root.empty() &&
         layout.size() == program.bindings.size();
}

static int
testProgramTranslates()
{
  TestCounters counters;
  testSection("D3D12 shaders: OpenGL program to HLSL and DXBC");
  if (!initializeGlslCompiler()) {
    testTrue(counters, false, "the compiler starts");
    return counters.failures;
  }
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
  std::string hlsl;
  std::string error;
  const bool built = buildForD3D12(vertex, fragment, &hlsl, &error);
  testTrue(counters, built, ("the program builds: " + error).c_str());
  testTrue(counters,
           hlsl.find("gl_Position.y = -gl_Position.y") != std::string::npos,
           "the vertex stage negates clip y");
  testTrue(counters,
           hlsl.find("TEXCOORD2") != std::string::npos,
           "vertex inputs are named by location");
  finalizeGlslCompiler();
  return counters.failures;
}

static bool
buildFiles(const char* vertex, const char* fragment, std::string* error)
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
  std::string hlsl;
  return buildForD3D12(
    vertexSource.source, fragmentSource.source, &hlsl, error);
}

static int
testEngineShadersBuild()
{
  TestCounters counters;
  testSection("D3D12 shaders: every engine shader file builds");
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
    std::string error;
    const bool built = buildFiles(pair[0], pair[1], &error);
    testTrue(counters,
             built,
             (std::string(pair[0]) + " + " + pair[1] + ": " + error).c_str());
  }
  finalizeGlslCompiler();
  return counters.failures;
}

void
registerD3D12ShaderTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.D3D12.ProgramTranslates",
               []() { return testProgramTranslates(); });
  registry.add("Illumo.D3D12.EngineShadersBuild",
               []() { return testEngineShadersBuild(); });
}

#else

void
registerD3D12ShaderTests(IllumoTestRegistry& registry)
{
  (void)registry;
}

#endif
