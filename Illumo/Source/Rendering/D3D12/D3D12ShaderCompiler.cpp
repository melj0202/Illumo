#include "D3D12ShaderCompiler.h"
#include "D3D12Common.h"

#include <Illumo/Foundation/Fatal.h>
#include <Illumo/Foundation/Profile.h>
#include <d3d12shader.h>
#include <d3dcompiler.h>
#include <spirv_hlsl.hpp>

#include <algorithm>
#include <cstdlib>
#include <string>

// SPIRV-Cross reports its errors here instead of throwing (D-F3; the vendored
// copy's one patch). Only first-party engine shaders reach the translator,
// so an error is a programming error.
extern "C" [[noreturn]] void
IllumoSpirvCrossFatal(const char* message)
{
  const std::string text =
    std::string("SPIR-V to HLSL translation failed: ") + message;
  illumoFatal(text.c_str());
}

static void
configureCompiler(spirv_cross::CompilerHLSL& compiler,
                  bool vertexStage,
                  const std::vector<GlslBinding>& bindings)
{
  spirv_cross::CompilerGLSL::Options common = compiler.get_common_options();
  // The shared SPIR-V already has Direct3D's [0, w] clip depth. Negating y
  // stores images bottom row first, as OpenGL and the Vulkan backend do.
  common.vertex.flip_vert_y = vertexStage;
  compiler.set_common_options(common);
  spirv_cross::CompilerHLSL::Options options = compiler.get_hlsl_options();
  options.shader_model = 51;
  options.point_size_compat = true;
  // A matrix input reads TEXCOORD<location> per column, so input layouts
  // name every attribute by its location.
  options.flatten_matrix_vertex_input_semantics = true;
  compiler.set_hlsl_options(options);
  for (const GlslBinding& binding : bindings) {
    spirv_cross::HLSLResourceBinding resource;
    resource.stage =
      vertexStage ? spv::ExecutionModelVertex : spv::ExecutionModelFragment;
    resource.desc_set = 0;
    resource.binding = binding.binding;
    resource.cbv.register_binding = binding.binding;
    resource.srv.register_binding = binding.binding;
    resource.sampler.register_binding = binding.binding;
    compiler.add_hlsl_resource_binding(resource);
  }
}

// One member of a SPIRV-Cross stage struct: "    <type> <name> : <SEMANTIC>;".
struct HlslStageMember
{
  std::string line;
  std::string semantic;
  // TEXCOORD<n>; -1 for system values.
  int location = -1;
};

// The members of `struct <name> { ... };` and where its body starts and
// ends in `hlsl`; false when the struct is absent.
static bool
stageStruct(const std::string& hlsl,
            const char* name,
            size_t* bodyBegin,
            size_t* bodyEnd,
            std::vector<HlslStageMember>* members)
{
  const std::string header = std::string("struct ") + name + "\n{\n";
  const size_t found = hlsl.find(header);
  if (found == std::string::npos) {
    return false;
  }
  *bodyBegin = found + header.size();
  *bodyEnd = hlsl.find("};", *bodyBegin);
  if (*bodyEnd == std::string::npos) {
    return false;
  }
  members->clear();
  size_t lineBegin = *bodyBegin;
  while (lineBegin < *bodyEnd) {
    size_t lineEnd = hlsl.find('\n', lineBegin);
    if (lineEnd == std::string::npos || lineEnd > *bodyEnd) {
      lineEnd = *bodyEnd;
    }
    const std::string line = hlsl.substr(lineBegin, lineEnd - lineBegin);
    const size_t colon = line.rfind(" : ");
    const size_t semicolon = line.rfind(';');
    if (colon != std::string::npos && semicolon != std::string::npos &&
        semicolon > colon) {
      HlslStageMember member;
      member.line = line;
      member.semantic = line.substr(colon + 3, semicolon - colon - 3);
      if (member.semantic.compare(0, 8, "TEXCOORD") == 0) {
        member.location = std::atoi(member.semantic.c_str() + 8);
      }
      members->push_back(member);
    }
    lineBegin = lineEnd + 1;
  }
  return true;
}

// Direct3D links a vertex output to a pixel input by signature register, so
// the pixel input struct must hold the vertex outputs it lacks (OpenGL links
// by name and ignores them) in the same order: varyings by location, then
// system values. The added members are never read.
static void
linkPixelInputs(const std::string& vertexHlsl, std::string* pixelHlsl)
{
  size_t outputBegin = 0;
  size_t outputEnd = 0;
  std::vector<HlslStageMember> outputs;
  size_t inputBegin = 0;
  size_t inputEnd = 0;
  std::vector<HlslStageMember> inputs;
  if (!stageStruct(
        vertexHlsl, "SPIRV_Cross_Output", &outputBegin, &outputEnd, &outputs) ||
      !stageStruct(
        *pixelHlsl, "SPIRV_Cross_Input", &inputBegin, &inputEnd, &inputs)) {
    // A pixel stage with no inputs links with any vertex stage.
    return;
  }
  std::vector<HlslStageMember> varyings;
  std::vector<HlslStageMember> systemValues;
  for (const HlslStageMember& input : inputs) {
    (input.location >= 0 ? varyings : systemValues).push_back(input);
  }
  bool added = false;
  for (const HlslStageMember& output : outputs) {
    if (output.location < 0) {
      continue;
    }
    bool present = false;
    for (const HlslStageMember& varying : varyings) {
      present = present || varying.location == output.location;
    }
    if (present) {
      continue;
    }
    // The vertex declaration with its name replaced.
    const size_t colon = output.line.rfind(" : ");
    const size_t nameBegin = output.line.rfind(' ', colon - 1) + 1;
    HlslStageMember filler = output;
    filler.line = output.line.substr(0, nameBegin) + "illumo_unread_" +
                  std::to_string(output.location) + output.line.substr(colon);
    varyings.push_back(filler);
    added = true;
  }
  if (!added) {
    return;
  }
  std::sort(varyings.begin(),
            varyings.end(),
            [](const HlslStageMember& left, const HlslStageMember& right) {
              return left.location < right.location;
            });
  std::string body;
  for (const HlslStageMember& varying : varyings) {
    body += varying.line + "\n";
  }
  for (const HlslStageMember& value : systemValues) {
    body += value.line + "\n";
  }
  pixelHlsl->replace(inputBegin, inputEnd - inputBegin, body);
}

void
translateProgramToHlsl(const GlslProgram& program,
                       std::string* vertexHlsl,
                       std::string* pixelHlsl)
{
  ILLUMO_PROFILE_ZONE("D3D12.translateProgramToHlsl");
  spirv_cross::CompilerHLSL vertex(program.vertexSpirv.data(),
                                   program.vertexSpirv.size());
  configureCompiler(vertex, true, program.bindings);
  *vertexHlsl = vertex.compile();
  spirv_cross::CompilerHLSL fragment(program.fragmentSpirv.data(),
                                     program.fragmentSpirv.size());
  configureCompiler(fragment, false, program.bindings);
  *pixelHlsl = fragment.compile();
  linkPixelInputs(*vertexHlsl, pixelHlsl);
}

bool
compileHlsl(const std::string& hlsl,
            bool vertexStage,
            std::vector<unsigned char>* bytecode,
            std::string* error)
{
  ILLUMO_PROFILE_ZONE("D3D12.compileHlsl");
  D3D12Ref<ID3DBlob> code;
  D3D12Ref<ID3DBlob> messages;
  const HRESULT result = D3DCompile(hlsl.data(),
                                    hlsl.size(),
                                    vertexStage ? "vertex" : "fragment",
                                    nullptr,
                                    nullptr,
                                    "main",
                                    vertexStage ? "vs_5_1" : "ps_5_1",
                                    0,
                                    0,
                                    code.put(),
                                    messages.put());
  if (FAILED(result) || !code) {
    std::string detail;
    if (messages) {
      detail.assign(static_cast<const char*>(messages->GetBufferPointer()),
                    messages->GetBufferSize());
    }
    *error = std::string(vertexStage ? "Vertex" : "Fragment") +
             " shader failed to compile to DXBC: " + detail;
    return false;
  }
  const unsigned char* begin =
    static_cast<const unsigned char*>(code->GetBufferPointer());
  bytecode->assign(begin, begin + code->GetBufferSize());
  return true;
}

bool
reflectVertexInputs(const std::vector<unsigned char>& vertexCode,
                    std::vector<GlslVertexInput>* inputs,
                    std::string* error)
{
  inputs->clear();
  D3D12Ref<ID3D12ShaderReflection> reflection;
  if (FAILED(D3DReflect(vertexCode.data(),
                        vertexCode.size(),
                        __uuidof(ID3D12ShaderReflection),
                        reflection.putVoid()))) {
    *error = "The vertex shader's input signature could not be read";
    return false;
  }
  D3D12_SHADER_DESC description{};
  reflection->GetDesc(&description);
  for (UINT index = 0; index < description.InputParameters; ++index) {
    D3D12_SIGNATURE_PARAMETER_DESC parameter{};
    reflection->GetInputParameterDesc(index, &parameter);
    // System values (SV_VertexID, SV_InstanceID) come from no buffer.
    if (parameter.SystemValueType != D3D_NAME_UNDEFINED) {
      continue;
    }
    GlslVertexInput input;
    input.location = parameter.SemanticIndex;
    input.locationCount = 1;
    input.integer = parameter.ComponentType == D3D_REGISTER_COMPONENT_SINT32 ||
                    parameter.ComponentType == D3D_REGISTER_COMPONENT_UINT32;
    inputs->push_back(input);
  }
  return true;
}

bool
serializeRootSignature(const GlslProgram& program,
                       std::vector<unsigned char>* blob,
                       std::vector<D3D12RootBinding>* layout,
                       std::string* error)
{
  // Each block is a root CBV (two DWORDs); each sampler two one-descriptor
  // tables (one DWORD each).
  if (program.bindings.size() * 2u > 64u) {
    *error = "The shader program uses more uniform blocks and samplers than "
             "a Direct3D 12 root signature holds";
    return false;
  }
  std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
  ranges.reserve(program.bindings.size() * 2u);
  std::vector<D3D12_ROOT_PARAMETER> parameters;
  layout->assign(program.bindings.size(), D3D12RootBinding{});
  for (size_t index = 0; index < program.bindings.size(); ++index) {
    const GlslBinding& binding = program.bindings[index];
    D3D12RootBinding& placed = (*layout)[index];
    if (binding.kind == GlslBindingKind::DefaultBlock ||
        binding.kind == GlslBindingKind::NamedBlock) {
      D3D12_ROOT_PARAMETER parameter{};
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
      parameter.Descriptor.ShaderRegister = binding.binding;
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      placed.parameter = static_cast<unsigned>(parameters.size());
      parameters.push_back(parameter);
      continue;
    }
    const D3D12_DESCRIPTOR_RANGE_TYPE types[2] = {
      D3D12_DESCRIPTOR_RANGE_TYPE_SRV, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
    };
    for (int part = 0; part < 2; ++part) {
      D3D12_DESCRIPTOR_RANGE range{};
      range.RangeType = types[part];
      range.NumDescriptors = 1;
      range.BaseShaderRegister = binding.binding;
      range.OffsetInDescriptorsFromTableStart = 0;
      ranges.push_back(range);
      D3D12_ROOT_PARAMETER parameter{};
      parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      parameter.DescriptorTable.NumDescriptorRanges = 1;
      parameter.DescriptorTable.pDescriptorRanges = &ranges.back();
      parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
      if (part == 0) {
        placed.parameter = static_cast<unsigned>(parameters.size());
      } else {
        placed.samplerParameter = static_cast<unsigned>(parameters.size());
      }
      parameters.push_back(parameter);
    }
  }
  D3D12_ROOT_SIGNATURE_DESC description{};
  description.NumParameters = static_cast<UINT>(parameters.size());
  description.pParameters = parameters.empty() ? nullptr : parameters.data();
  description.Flags =
    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  D3D12Ref<ID3DBlob> serialized;
  D3D12Ref<ID3DBlob> messages;
  if (FAILED(D3D12SerializeRootSignature(&description,
                                         D3D_ROOT_SIGNATURE_VERSION_1,
                                         serialized.put(),
                                         messages.put()))) {
    *error = "The shader program's root signature could not be built";
    if (messages) {
      error->append(": ");
      error->append(static_cast<const char*>(messages->GetBufferPointer()),
                    messages->GetBufferSize());
    }
    return false;
  }
  const unsigned char* begin =
    static_cast<const unsigned char*>(serialized->GetBufferPointer());
  blob->assign(begin, begin + serialized->GetBufferSize());
  return true;
}
