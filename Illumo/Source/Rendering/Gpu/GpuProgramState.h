#pragma once

#include "Rendering/Gpu/GlslToSpirv.h"
#include <Illumo/Rendering/IMesh.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// OpenGL program and vertex-array semantics the explicit-API backends (Vulkan,
// Direct3D 12) reproduce on the CPU. No device types; tested headlessly.

struct TransparentStringHash
{
  using is_transparent = void;
  size_t operator()(std::string_view value) const noexcept
  {
    return std::hash<std::string_view>{}(value);
  }
};

struct GpuUniformSlot
{
  GlslValueType type = GlslValueType::Other;
  unsigned offset = 0;
  unsigned arraySize = 1;
  unsigned arrayStride = 0;
  // Sampler uniforms: index into the sampler units instead of block storage.
  int sampler = -1;
};

// The uniform state an OpenGL program object keeps: the default block's
// values and each sampler's texture unit persist across draws and frames and
// start at zero, and glUniform* calls of the wrong type or out of range are
// ignored.
class GpuProgramUniforms
{
public:
  void configure(const GlslProgram& reflection);

  // glUniform*: `bytes` of `value` for a uniform named like OpenGL names it
  // ("name" or "name[3]"). Int values set sampler units.
  void set(const char* name,
           GlslValueType given,
           const void* value,
           size_t bytes);

  const std::vector<unsigned char>& block() const { return m_block; }
  // The unit sampler `sampler` (GlslBinding::sampler) reads.
  int samplerUnit(unsigned sampler) const
  {
    return sampler < m_samplerUnits.size() ? m_samplerUnits[sampler] : 0;
  }
  // The block changed since the backend last uploaded it.
  bool dirty() const { return m_dirty; }
  void markUploaded() { m_dirty = false; }

private:
  std::unordered_map<std::string,
                     GpuUniformSlot,
                     TransparentStringHash,
                     std::equal_to<>>
    m_slots;
  std::vector<unsigned char> m_block;
  std::vector<int> m_samplerUnits;
  bool m_dirty = true;
};

enum class GpuAttributeFormat : unsigned char
{
  Float2,
  Float3,
  Unorm8x4,
};

struct GpuMeshAttribute
{
  uint32_t location = 0;
  GpuAttributeFormat format = GpuAttributeFormat::Float3;
  uint32_t offset = 0;
};

// The attribute layouts GLMesh::setupAttributes configures: fills
// `attributes`, sets the vertex stride and returns the attribute count.
unsigned
gpuMeshAttributes(MeshVertexLayout layout,
                  std::array<GpuMeshAttribute, 4>& attributes,
                  uint32_t* stride);
