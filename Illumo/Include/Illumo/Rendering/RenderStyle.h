#pragma once

#include <Illumo/Rendering/PipelineState.h>
#include <Illumo/Rendering/ResourceHandle.h>

// Registered draw styles owned by Renderer. A style pairs a shader handle with
// pipeline defaults. Canonical Shape/Sprite programs consume uMVP (see
// WorldLook.h). Overlay chrome supplies a Y-down screen ortho; world objects
// supply camera view-projection times node world. Sprites sample uTexture on
// texture unit zero. Custom 2D shaders must follow that contract.
enum class RenderStyleId : unsigned char
{
  Canvas = 0,
  UiText = 1,
  Console = 2,
  Shape = 3,       // solid/outline shapes (GameVisual primitives)
  Sprite = 4,      // textured quads (GameVisual primitives)
  LitMesh = 5,     // 3D lit meshes with Blinn-Phong, PCF shadows, motion blur
  ShadowDepth = 6, // Depth-only shadow mapping pass
  Count
};

struct RenderStyle
{
  ShaderHandle shaderHandle{};
  PipelineState pipeline;
  bool ready = false;
};

inline unsigned
renderStyleIndex(RenderStyleId id)
{
  return static_cast<unsigned>(id);
}

inline unsigned
renderStyleCount()
{
  return static_cast<unsigned>(RenderStyleId::Count);
}
