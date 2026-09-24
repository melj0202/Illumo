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
  // A heavier sample of the same family, drawn over `font` at `heavyBlend`
  // (0..1) with advances interpolated between the two, so weight animates
  // between rasterized samples (FontWeightRamp). Ignored while pending.
  std::shared_ptr<Font> heavyFont = nullptr;
  float heavyBlend = 0.0f;
  // Squash and stretch: glyphs and advances scale by stretchX from the run's
  // left edge and by stretchY from its first baseline, so a stretched letter
  // keeps its feet on the line. 1 draws the run unchanged.
  float stretchX = 1.0f;
  float stretchY = 1.0f;
  int drawOrder = 0;
  bool visible = true;
};
