#include "GpuProgramState.h"

#include <cstring>

void
GpuProgramUniforms::configure(const GlslProgram& reflection)
{
  m_slots.clear();
  m_block.assign(reflection.defaultBlockSize, 0);
  m_samplerUnits.assign(reflection.samplerNames.size(), 0);
  m_dirty = true;
  for (const GlslUniform& uniform : reflection.uniforms) {
    GpuUniformSlot slot;
    slot.type = uniform.type;
    slot.offset = uniform.offset;
    slot.arraySize = uniform.arraySize;
    slot.arrayStride = uniform.arrayStride;
    m_slots[uniform.name] = slot;
  }
  for (size_t index = 0; index < reflection.samplerNames.size(); ++index) {
    GpuUniformSlot slot;
    slot.type = GlslValueType::Int;
    slot.sampler = static_cast<int>(index);
    m_slots[reflection.samplerNames[index]] = slot;
  }
}

static bool
acceptsValue(GlslValueType slot, GlslValueType given)
{
  switch (given) {
    case GlslValueType::Float:
      return slot == GlslValueType::Float || slot == GlslValueType::Bool;
    case GlslValueType::Vec2:
      return slot == GlslValueType::Vec2 || slot == GlslValueType::BVec2;
    case GlslValueType::Vec3:
      return slot == GlslValueType::Vec3 || slot == GlslValueType::BVec3;
    case GlslValueType::Vec4:
      return slot == GlslValueType::Vec4 || slot == GlslValueType::BVec4;
    case GlslValueType::Int:
      return slot == GlslValueType::Int || slot == GlslValueType::Bool;
    case GlslValueType::Mat4:
      return slot == GlslValueType::Mat4;
    default:
      return false;
  }
}

static bool
isBoolType(GlslValueType type)
{
  return type == GlslValueType::Bool || type == GlslValueType::BVec2 ||
         type == GlslValueType::BVec3 || type == GlslValueType::BVec4;
}

void
GpuProgramUniforms::set(const char* name,
                        GlslValueType given,
                        const void* value,
                        size_t bytes)
{
  if (name == nullptr) {
    return;
  }
  std::string_view base(name);
  unsigned element = 0;
  const size_t bracket = base.find('[');
  if (bracket != std::string_view::npos) {
    const size_t close = base.find(']', bracket);
    if (close == std::string_view::npos || close != base.size() - 1) {
      return;
    }
    element = 0;
    for (size_t index = bracket + 1; index < close; ++index) {
      const char digit = base[index];
      if (digit < '0' || digit > '9') {
        return;
      }
      element = element * 10u + static_cast<unsigned>(digit - '0');
    }
    base = base.substr(0, bracket);
  }
  const std::unordered_map<std::string,
                           GpuUniformSlot,
                           TransparentStringHash,
                           std::equal_to<>>::const_iterator found =
    m_slots.find(base);
  if (found == m_slots.end()) {
    return;
  }
  const GpuUniformSlot& slot = found->second;
  if (slot.sampler >= 0) {
    if (given == GlslValueType::Int && element == 0) {
      int unit = 0;
      std::memcpy(&unit, value, sizeof(unit));
      m_samplerUnits[static_cast<size_t>(slot.sampler)] = unit;
    }
    return;
  }
  if (!acceptsValue(slot.type, given) || element >= slot.arraySize) {
    return;
  }
  const size_t offset = static_cast<size_t>(slot.offset) +
                        element * static_cast<size_t>(slot.arrayStride);
  if (offset + bytes > m_block.size()) {
    return;
  }
  unsigned char* destination = m_block.data() + offset;
  if (isBoolType(slot.type)) {
    // Booleans are 32-bit words that read true when nonzero.
    const size_t components = bytes / 4u;
    for (size_t component = 0; component < components; ++component) {
      uint32_t word = 0;
      if (given == GlslValueType::Int) {
        int source = 0;
        std::memcpy(&source,
                    static_cast<const unsigned char*>(value) + component * 4u,
                    4);
        word = source != 0 ? 1u : 0u;
      } else {
        float source = 0.0f;
        std::memcpy(&source,
                    static_cast<const unsigned char*>(value) + component * 4u,
                    4);
        word = source != 0.0f ? 1u : 0u;
      }
      if (std::memcmp(destination + component * 4u, &word, 4) != 0) {
        std::memcpy(destination + component * 4u, &word, 4);
        m_dirty = true;
      }
    }
  } else if (std::memcmp(destination, value, bytes) != 0) {
    // An unchanged value (the same screen matrix on every 2D draw) keeps the
    // uploaded block.
    std::memcpy(destination, value, bytes);
    m_dirty = true;
  }
}

unsigned
gpuMeshAttributes(MeshVertexLayout layout,
                  std::array<GpuMeshAttribute, 4>& attributes,
                  uint32_t* stride)
{
  const GpuAttributeFormat vec3 = GpuAttributeFormat::Float3;
  const GpuAttributeFormat vec2 = GpuAttributeFormat::Float2;
  const GpuAttributeFormat color8 = GpuAttributeFormat::Unorm8x4;
  switch (layout) {
    case MeshVertexLayout::Pos3Color4U8:
      *stride = 16;
      attributes[0] = { 0, vec3, 0 };
      attributes[1] = { 1, color8, 12 };
      return 2;
    case MeshVertexLayout::Pos3Color4U8Uv2:
      *stride = 24;
      attributes[0] = { 0, vec3, 0 };
      attributes[1] = { 1, color8, 12 };
      attributes[2] = { 2, vec2, 16 };
      return 3;
    case MeshVertexLayout::Pos3Norm3Color4U8Uv2:
      *stride = 36;
      attributes[0] = { 0, vec3, 0 };
      attributes[1] = { 3, vec3, 12 };
      attributes[2] = { 1, color8, 24 };
      attributes[3] = { 2, vec2, 28 };
      return 4;
    case MeshVertexLayout::Pos3Norm3Uv2:
      *stride = 32;
      attributes[0] = { 0, vec3, 0 };
      attributes[1] = { 3, vec3, 12 };
      attributes[2] = { 2, vec2, 24 };
      return 3;
    case MeshVertexLayout::Pos3:
      *stride = 12;
      attributes[0] = { 0, vec3, 0 };
      return 1;
    default:
      *stride = 32;
      attributes[0] = { 0, vec3, 0 };
      attributes[1] = { 1, vec3, 12 };
      attributes[2] = { 2, vec2, 24 };
      return 3;
  }
}
