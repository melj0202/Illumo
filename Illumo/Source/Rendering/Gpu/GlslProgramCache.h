#pragma once

#include "Rendering/Gpu/GlslToSpirv.h"
#include "Rendering/Gpu/GpuShaderCache.h"

#include <string>
#include <vector>

// A compiled program (SPIR-V plus reflection) as bytes, and back; false for
// truncated or malformed input.
void
serializeGlslProgram(const GlslProgram& program,
                     std::vector<unsigned char>* bytes);
bool
deserializeGlslProgram(const std::vector<unsigned char>& bytes,
                       GlslProgram* program);

// compileGlslProgram through the cache (D-R38): a program an earlier run
// compiled from the same sources with the same build is loaded, otherwise
// compiled and stored. The key holds the build version, so a new build
// recompiles; bump kGlslProgramCacheFormat when GlslToSpirv changes its
// output without a new build version.
bool
compileGlslProgramCached(GpuShaderCache& cache,
                         const std::string& vertexSource,
                         const std::string& fragmentSource,
                         GlslProgram* program,
                         std::string* error);

inline constexpr char kGlslProgramCacheFormat[] = "glsl-program-1";
