#include "GlslToSpirv.h"

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>

// iomapper.h defines TIoMapper, which is deleted here; it expects glslang's
// public and type headers first, as glslang's own sources include them.
#include <cassert>
#include <glslang/Include/Common.h>
#include <glslang/Include/InfoSink.h>
#include <glslang/Include/Types.h>
#include <map>

#include <glslang/MachineIndependent/iomapper.h>

#include <memory>

static int s_compilerUsers = 0;

// The block glslang gathers loose uniforms into (OpenGL's default block).
static const char* const kDefaultBlockName = "IllumoDefaultUniforms";

// OpenGL type enumerants glslang reports in reflection.
static constexpr int kGlFloat = 0x1406;
static constexpr int kGlFloatVec2 = 0x8B50;
static constexpr int kGlFloatVec3 = 0x8B51;
static constexpr int kGlFloatVec4 = 0x8B52;
static constexpr int kGlInt = 0x1404;
static constexpr int kGlIntVec2 = 0x8B53;
static constexpr int kGlIntVec3 = 0x8B54;
static constexpr int kGlIntVec4 = 0x8B55;
static constexpr int kGlUnsignedInt = 0x1405;
static constexpr int kGlUnsignedIntVec2 = 0x8DC6;
static constexpr int kGlUnsignedIntVec3 = 0x8DC7;
static constexpr int kGlUnsignedIntVec4 = 0x8DC8;
static constexpr int kGlBool = 0x8B56;
static constexpr int kGlBoolVec2 = 0x8B57;
static constexpr int kGlBoolVec3 = 0x8B58;
static constexpr int kGlBoolVec4 = 0x8B59;
static constexpr int kGlFloatMat2 = 0x8B5A;
static constexpr int kGlFloatMat3 = 0x8B5B;
static constexpr int kGlFloatMat4 = 0x8B5C;
static constexpr int kGlSampler2D = 0x8B5E;
static constexpr int kGlSamplerCube = 0x8B60;

static GlslValueType
valueTypeOf(int glType)
{
  switch (glType) {
    case kGlFloat:
      return GlslValueType::Float;
    case kGlFloatVec2:
      return GlslValueType::Vec2;
    case kGlFloatVec3:
      return GlslValueType::Vec3;
    case kGlFloatVec4:
      return GlslValueType::Vec4;
    case kGlInt:
      return GlslValueType::Int;
    case kGlIntVec2:
      return GlslValueType::IVec2;
    case kGlIntVec3:
      return GlslValueType::IVec3;
    case kGlIntVec4:
      return GlslValueType::IVec4;
    case kGlBool:
      return GlslValueType::Bool;
    case kGlBoolVec2:
      return GlslValueType::BVec2;
    case kGlBoolVec3:
      return GlslValueType::BVec3;
    case kGlBoolVec4:
      return GlslValueType::BVec4;
    case kGlFloatMat4:
      return GlslValueType::Mat4;
    case kGlSampler2D:
      return GlslValueType::Sampler2D;
    case kGlSamplerCube:
      return GlslValueType::SamplerCube;
    default:
      return GlslValueType::Other;
  }
}

static bool
isSamplerGlType(int glType)
{
  // GL_SAMPLER_1D through GL_UNSIGNED_INT_SAMPLER_BUFFER, and the multisample
  // and 2D rectangle ranges.
  return (glType >= 0x8B5D && glType <= 0x8B64) ||
         (glType >= 0x8DC0 && glType <= 0x8DD8) ||
         (glType >= 0x9108 && glType <= 0x910D);
}

static unsigned
inputLocationCount(int glType)
{
  switch (glType) {
    case kGlFloatMat2:
      return 2;
    case kGlFloatMat3:
      return 3;
    case kGlFloatMat4:
      return 4;
    default:
      return 1;
  }
}

static bool
isIntegerGlType(int glType)
{
  return glType == kGlInt || glType == kGlIntVec2 || glType == kGlIntVec3 ||
         glType == kGlIntVec4 || glType == kGlUnsignedInt ||
         glType == kGlUnsignedIntVec2 || glType == kGlUnsignedIntVec3 ||
         glType == kGlUnsignedIntVec4;
}

static size_t
countNewlines(const std::string& text, size_t end)
{
  size_t count = 0;
  for (size_t index = 0; index < end && index < text.size(); ++index) {
    count += text[index] == '\n' ? 1u : 0u;
  }
  return count;
}

std::string
prepareGlslForSpirv(const std::string& source, bool vertexStage)
{
  std::string body = source;
  size_t firstBodyLine = 1;
  const size_t start = source.find_first_not_of(" \t\r\n");
  if (start != std::string::npos && source.compare(start, 8, "#version") == 0) {
    const size_t versionEnd = source.find('\n', start);
    if (versionEnd == std::string::npos) {
      body.clear();
      firstBodyLine = countNewlines(source, source.size()) + 2;
    } else {
      body = source.substr(versionEnd + 1);
      firstBodyLine = countNewlines(source, versionEnd + 1) + 1;
    }
  }
  std::string prepared = "#version 450\n";
  if (vertexStage) {
    prepared += "#define main illumo_user_main\n";
  }
  prepared += "#line " + std::to_string(firstBodyLine) + "\n";
  prepared += body;
  if (vertexStage) {
    prepared += "\n#undef main\n"
                "void main()\n"
                "{\n"
                "  illumo_user_main();\n"
                "  gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\n"
                "  gl_PointSize = 1.0;\n"
                "}\n";
  }
  return prepared;
}

bool
initializeGlslCompiler()
{
  if (s_compilerUsers == 0 && !glslang::InitializeProcess()) {
    return false;
  }
  ++s_compilerUsers;
  return true;
}

void
finalizeGlslCompiler()
{
  if (s_compilerUsers > 0) {
    --s_compilerUsers;
    if (s_compilerUsers == 0) {
      glslang::FinalizeProcess();
    }
  }
}

static const EShMessages kMessages =
  static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);

// TShader keeps the string and length array pointers until parse(), so both
// must outlive that call.
static void
configureShader(glslang::TShader& shader,
                EShLanguage stage,
                const char* const* text,
                const int* length)
{
  shader.setStringsWithLengths(text, length, 1);
  shader.setEnvInput(
    glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);
  shader.setEnvInputVulkanRulesRelaxed();
  shader.setGlobalUniformBlockName(kDefaultBlockName);
  shader.setGlobalUniformSet(0);
  shader.setGlobalUniformBinding(0);
  shader.setAutoMapBindings(true);
  shader.setAutoMapLocations(true);
}

static bool
parseStage(glslang::TShader& shader, const char* label, std::string* error)
{
  if (shader.parse(
        GetDefaultResources(), 450, ECoreProfile, false, false, kMessages)) {
    return true;
  }
  *error =
    std::string(label) + " shader failed to compile: " + shader.getInfoLog();
  return false;
}

bool
compileGlslProgram(const std::string& vertexSource,
                   const std::string& fragmentSource,
                   GlslProgram* program,
                   std::string* error)
{
  *program = GlslProgram{};
  const std::string vertexText = prepareGlslForSpirv(vertexSource, true);
  const std::string fragmentText = prepareGlslForSpirv(fragmentSource, false);
  const char* const vertexString = vertexText.c_str();
  const char* const fragmentString = fragmentText.c_str();
  const int vertexLength = static_cast<int>(vertexText.size());
  const int fragmentLength = static_cast<int>(fragmentText.size());
  glslang::TShader vertex(EShLangVertex);
  glslang::TShader fragment(EShLangFragment);
  configureShader(vertex, EShLangVertex, &vertexString, &vertexLength);
  configureShader(fragment, EShLangFragment, &fragmentString, &fragmentLength);
  if (!parseStage(vertex, "Vertex", error) ||
      !parseStage(fragment, "Fragment", error)) {
    return false;
  }

  // Declared after the shaders so it is destroyed before them.
  glslang::TProgram linked;
  linked.addShader(&vertex);
  linked.addShader(&fragment);
  if (!linked.link(kMessages)) {
    *error =
      std::string("Shader program failed to link: ") + linked.getInfoLog();
    return false;
  }
  // One resolver across both stages matches unlocated varyings by name, as
  // an OpenGL link does.
  std::unique_ptr<glslang::TIoMapResolver> resolver(
    linked.getGlslIoResolver(EShLangVertex));
  std::unique_ptr<glslang::TIoMapper> mapper(glslang::GetGlslIoMapper());
  if (!resolver || !mapper || !linked.mapIO(resolver.get(), mapper.get())) {
    *error = std::string("Shader program interfaces could not be mapped: ") +
             linked.getInfoLog();
    return false;
  }
  if (!linked.buildReflection(EShReflectionDefault)) {
    *error = "Shader program reflection failed";
    return false;
  }

  for (int index = 0; index < linked.getNumUniformBlocks(); ++index) {
    const glslang::TObjectReflection& block = linked.getUniformBlock(index);
    GlslBinding binding;
    binding.binding = static_cast<unsigned>(block.getBinding());
    binding.size = block.size > 0 ? static_cast<unsigned>(block.size) : 0u;
    binding.name = block.name;
    if (block.name == kDefaultBlockName) {
      binding.kind = GlslBindingKind::DefaultBlock;
      program->defaultBlockSize = binding.size;
    } else {
      binding.kind = GlslBindingKind::NamedBlock;
    }
    if (block.getBinding() < 0) {
      *error = "Uniform block " + block.name + " has no binding";
      return false;
    }
    program->bindings.push_back(binding);
  }

  for (int index = 0; index < linked.getNumUniformVariables(); ++index) {
    const glslang::TObjectReflection& uniform = linked.getUniform(index);
    if (isSamplerGlType(uniform.glDefineType)) {
      const GlslValueType type = valueTypeOf(uniform.glDefineType);
      if (type != GlslValueType::Sampler2D &&
          type != GlslValueType::SamplerCube) {
        *error = "Sampler " + uniform.name +
                 " has a type the Vulkan backend does not support";
        return false;
      }
      if (uniform.getBinding() < 0 || uniform.size > 1) {
        *error = "Sampler " + uniform.name + " has no single binding";
        return false;
      }
      GlslBinding binding;
      binding.kind = type == GlslValueType::SamplerCube
                       ? GlslBindingKind::SamplerCube
                       : GlslBindingKind::Sampler2D;
      binding.binding = static_cast<unsigned>(uniform.getBinding());
      binding.sampler = static_cast<unsigned>(program->samplerNames.size());
      binding.name = uniform.name;
      program->samplerNames.push_back(uniform.name);
      program->bindings.push_back(binding);
      continue;
    }
    if (uniform.index < 0 || uniform.index >= linked.getNumUniformBlocks() ||
        linked.getUniformBlock(uniform.index).name != kDefaultBlockName ||
        uniform.offset < 0) {
      continue;
    }
    GlslUniform member;
    member.name = uniform.name;
    member.type = valueTypeOf(uniform.glDefineType);
    member.offset = static_cast<unsigned>(uniform.offset);
    member.arraySize =
      uniform.size > 1 ? static_cast<unsigned>(uniform.size) : 1u;
    member.arrayStride =
      uniform.arrayStride > 0 ? static_cast<unsigned>(uniform.arrayStride) : 0u;
    // "name[0]" also answers to "name", as OpenGL resolves it.
    const size_t bracket = member.name.find('[');
    if (bracket != std::string::npos) {
      member.name = member.name.substr(0, bracket);
    }
    program->uniforms.push_back(member);
  }

  for (int index = 0; index < linked.getNumPipeInputs(); ++index) {
    const glslang::TObjectReflection& input = linked.getPipeInput(index);
    if (input.name.compare(0, 3, "gl_") == 0) {
      continue;
    }
    const unsigned location = input.layoutLocation();
    if (location >= 64u) {
      continue;
    }
    GlslVertexInput vertexInput;
    vertexInput.location = location;
    vertexInput.locationCount =
      inputLocationCount(input.glDefineType) *
      (input.size > 1 ? static_cast<unsigned>(input.size) : 1u);
    vertexInput.integer = isIntegerGlType(input.glDefineType);
    program->inputs.push_back(vertexInput);
  }
  for (int index = 0; index < linked.getNumPipeOutputs(); ++index) {
    const glslang::TObjectReflection& output = linked.getPipeOutput(index);
    const unsigned location = output.layoutLocation();
    if (output.name.compare(0, 3, "gl_") != 0 && location < 32u) {
      program->fragmentOutputs |= 1u << location;
    }
  }

  glslang::SpvOptions options;
  options.generateDebugInfo = false;
  options.disableOptimizer = true;
  options.validate = false;
  glslang::GlslangToSpv(
    *linked.getIntermediate(EShLangVertex), program->vertexSpirv, &options);
  glslang::GlslangToSpv(
    *linked.getIntermediate(EShLangFragment), program->fragmentSpirv, &options);
  if (program->vertexSpirv.empty() || program->fragmentSpirv.empty()) {
    *error = "SPIR-V generation failed";
    return false;
  }
  return true;
}
