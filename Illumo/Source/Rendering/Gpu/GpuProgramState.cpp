#include "GpuProgramState.h"

#include <cstring>

void
GpuProgramUniforms::configure(const GlslProgram& reflection)
{
  m_slots.clear();
  m_slotIndices.clear();
  m_names.clear();
  m_block.assign(reflection.defaultBlockSize, 0);
  m_samplerUnits.assign(reflection.samplerNames.size(), 0);
  m_dirty = true;
  const auto add = [this](const std::string& name, const GpuUniformSlot& slot) {
    const std::unordered_map<std::string,
                             std::int32_t,
                             TransparentStringHash,
                             std::equal_to<>>::const_iterator found =
      m_slotIndices.find(name);
    if (found != m_slotIndices.end()) {
      m_slots[static_cast<size_t>(found->second)] = slot;
      return;
    }
    m_slotIndices.emplace(name, static_cast<std::int32_t>(m_slots.size()));
    m_slots.push_back(slot);
  };
  for (const GlslUniform& uniform : reflection.uniforms) {
    GpuUniformSlot slot;
    slot.type = uniform.type;
    slot.offset = uniform.offset;
    slot.arraySize = uniform.arraySize;
    slot.arrayStride = uniform.arrayStride;
    add(uniform.name, slot);
  }
  for (size_t index = 0; index < reflection.samplerNames.size(); ++index) {
    GpuUniformSlot slot;
    slot.type = GlslValueType::Int;
    slot.sampler = static_cast<int>(index);
    add(reflection.samplerNames[index], slot);
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
                        const UniformKey& key,
                        GlslValueType given,
                        const void* value,
                        size_t bytes)
{
  if (name == nullptr) {
    return;
  }
  const std::int32_t entry =
    lookupUniform(m_names, key, name, [this](const char* uniform) {
      // First use of this name: its base name's reflected slot and its
      // element, packed as kElementShift; -1 for a malformed suffix, an
      // element no block could hold or a uniform the program lacks.
      std::string_view base(uniform);
      uint32_t element = 0u;
      const size_t open = base.find('[');
      if (open != std::string_view::npos) {
        size_t index = open + 1u;
        size_t digits = 0u;
        for (; index < base.size() && base[index] >= '0' && base[index] <= '9';
             ++index, ++digits) {
          element = element * 10u + static_cast<uint32_t>(base[index] - '0');
          if (element > kMaximumElement) {
            return static_cast<std::int32_t>(-1);
          }
        }
        if (digits == 0u || index + 1u != base.size() || base[index] != ']') {
          return static_cast<std::int32_t>(-1);
        }
        base = base.substr(0, open);
      }
      const std::unordered_map<std::string,
                               std::int32_t,
                               TransparentStringHash,
                               std::equal_to<>>::const_iterator found =
        m_slotIndices.find(base);
      if (found == m_slotIndices.end() || found->second > kMaximumSlot) {
        return static_cast<std::int32_t>(-1);
      }
      return static_cast<std::int32_t>(static_cast<uint32_t>(found->second) |
                                       (element << kElementShift));
    });
  if (entry < 0) {
    return;
  }
  const size_t index = static_cast<size_t>(entry) & kMaximumSlot;
  const unsigned element = static_cast<unsigned>(entry) >> kElementShift;
  const GpuUniformSlot& slot = m_slots[index];
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
