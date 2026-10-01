#pragma once

#include <cstdint>
#include <string>
#include <vector>

// GLSL as the renderer writes it for OpenGL 3.3, compiled for Vulkan with
// OpenGL's program semantics (docs/vulkan-backend-plan.md, section 5.2).
// Nothing here needs a Vulkan device, so it is exercised headlessly.

enum class GlslValueType : unsigned char
{
  Float,
  Vec2,
  Vec3,
  Vec4,
  Int,
  IVec2,
  IVec3,
  IVec4,
  Bool,
  BVec2,
  BVec3,
  BVec4,
  Mat4,
  Sampler2D,
  SamplerCube,
  Other,
};

// A member of the default uniform block (the program's loose uniforms).
struct GlslUniform
{
  std::string name;
  GlslValueType type = GlslValueType::Other;
  unsigned offset = 0;
  unsigned arraySize = 1;
  unsigned arrayStride = 0;
};

enum class GlslBindingKind : unsigned char
{
  DefaultBlock,
  NamedBlock,
  Sampler2D,
  SamplerCube,
};

// One descriptor of set 0. Samplers index GlslProgram::samplerNames.
struct GlslBinding
{
  GlslBindingKind kind = GlslBindingKind::DefaultBlock;
  unsigned binding = 0;
  unsigned size = 0;
  unsigned sampler = 0;
  std::string name;
};

// A vertex input: the locations it occupies and whether it reads integers.
struct GlslVertexInput
{
  unsigned location = 0;
  unsigned locationCount = 1;
  bool integer = false;
};

struct GlslProgram
{
  std::vector<uint32_t> vertexSpirv;
  std::vector<uint32_t> fragmentSpirv;
  std::vector<GlslBinding> bindings;
  std::vector<GlslUniform> uniforms;
  std::vector<std::string> samplerNames;
  std::vector<GlslVertexInput> inputs;
  unsigned defaultBlockSize = 0;
  // Bit n: the fragment shader writes colour output location n.
  uint32_t fragmentOutputs = 0;
};

// Rewrites preprocessed OpenGL GLSL for the Vulkan compiler: the version
// becomes 450, and a vertex shader's main is wrapped so its clip depth maps
// from OpenGL's [-w, w] to Vulkan's [0, w] and its point size is OpenGL's
// default of one. Line numbers of the original source are kept.
std::string
prepareGlslForVulkan(const std::string& source, bool vertexStage);

// Starts and ends the compiler's process-wide state; reference counted.
bool
initializeGlslCompiler();
void
finalizeGlslCompiler();

// Compiles and links one program. False with a message in *error.
bool
compileGlslProgram(const std::string& vertexSource,
                   const std::string& fragmentSource,
                   GlslProgram* program,
                   std::string* error);
