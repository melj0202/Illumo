#pragma once

#include <cstdint>

// Strong, generational backend resource handles. Slot zero is always invalid.
// The distinct types intentionally have no cross-conversion.
struct MeshHandle
{
  uint32_t slot;
  uint32_t generation;

  bool isValid() const { return slot != 0 && generation != 0; }
};

struct ShaderHandle
{
  uint32_t slot;
  uint32_t generation;

  bool isValid() const { return slot != 0 && generation != 0; }
};

struct TextureHandle
{
  uint32_t slot;
  uint32_t generation;

  bool isValid() const { return slot != 0 && generation != 0; }
};

struct RenderStyleHandle
{
  uint32_t slot;
  uint32_t generation;

  bool isValid() const { return slot != 0 && generation != 0; }
};

struct FramebufferHandle
{
  uint32_t slot;
  uint32_t generation;

  bool isValid() const { return slot != 0 && generation != 0; }
};

inline bool
operator==(MeshHandle left, MeshHandle right)
{
  return left.slot == right.slot && left.generation == right.generation;
}

inline bool
operator!=(MeshHandle left, MeshHandle right)
{
  return !(left == right);
}

inline bool
operator==(ShaderHandle left, ShaderHandle right)
{
  return left.slot == right.slot && left.generation == right.generation;
}

inline bool
operator!=(ShaderHandle left, ShaderHandle right)
{
  return !(left == right);
}

inline bool
operator==(TextureHandle left, TextureHandle right)
{
  return left.slot == right.slot && left.generation == right.generation;
}

inline bool
operator!=(TextureHandle left, TextureHandle right)
{
  return !(left == right);
}

inline bool
operator==(RenderStyleHandle left, RenderStyleHandle right)
{
  return left.slot == right.slot && left.generation == right.generation;
}

inline bool
operator!=(RenderStyleHandle left, RenderStyleHandle right)
{
  return !(left == right);
}

inline bool
operator==(FramebufferHandle left, FramebufferHandle right)
{
  return left.slot == right.slot && left.generation == right.generation;
}

inline bool
operator!=(FramebufferHandle left, FramebufferHandle right)
{
  return !(left == right);
}
