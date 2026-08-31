#pragma once

#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <memory>
#include <string>

class Font;

// Screen/world text run rasterized via Font atlas into tinted quads on the
// sprite mesh (absolute positions). Not a Scene Drawable by itself.
struct TextPrimitive
{
  std::string content;
  float x = 0.0f;
  float y = 0.0f;
  float sizePt = 12.0f; // 12 pt -> scale 1.0 (matches historical GLString)
  ColorRgba color;
  RenderStyleHandle styleHandle{};
  std::shared_ptr<Font> font = nullptr;
  int drawOrder = 0;
  bool visible = true;
};
