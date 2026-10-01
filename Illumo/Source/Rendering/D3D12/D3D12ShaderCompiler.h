#pragma once

#include "Rendering/Gpu/GlslToSpirv.h"
#include <cstdint>
#include <string>
#include <vector>

// The shared SPIR-V (GlslToSpirv) translated to HLSL shader model 5.1 by
// SPIRV-Cross and compiled to DXBC by the system's FXC
// (docs/d3d12-backend-plan.md, section 5.2). Every binding keeps its number
// as its register: blocks bN, textures tN and their samplers sN. Nothing here
// needs a device, so it is exercised headlessly.

// HLSL for both stages of a program. The vertex stage negates clip y, so
// images are stored bottom row first as OpenGL stores them; point size is
// always one. Direct3D links stages by signature register, not by location,
// so the pixel stage also declares the vertex outputs it never reads and the
// two signatures line up.
void
translateProgramToHlsl(const GlslProgram& program,
                       std::string* vertexHlsl,
                       std::string* pixelHlsl);

// FXC: DXBC for one stage of `hlsl`. False with the compiler's message.
bool
compileHlsl(const std::string& hlsl,
            bool vertexStage,
            std::vector<unsigned char>* bytecode,
            std::string* error);

// The attributes compiled vertex code reads, from its DXBC input signature
// (TEXCOORD<location>, one location each; matrices are split per column).
// Inputs OpenGL reflection calls inactive can still be declared there, so
// input layouts follow this list. False when the code cannot be reflected.
bool
reflectVertexInputs(const std::vector<unsigned char>& vertexCode,
                    std::vector<GlslVertexInput>* inputs,
                    std::string* error);

// Where a draw puts each binding (GlslProgram::bindings, same order): a
// block's root CBV parameter, or a sampler's SRV table and sampler table.
struct D3D12RootBinding
{
  unsigned parameter = 0;
  unsigned samplerParameter = 0;
};

// A serialized root signature for the program: one root CBV per uniform
// block, a one-descriptor table per texture and per sampler, input
// assembler allowed. False when the bindings do not fit 64 root DWORDs.
bool
serializeRootSignature(const GlslProgram& program,
                       std::vector<unsigned char>* blob,
                       std::vector<D3D12RootBinding>* layout,
                       std::string* error);
