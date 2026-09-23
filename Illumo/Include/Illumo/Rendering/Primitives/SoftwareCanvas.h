#pragma once

#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>

#include <cstdint>
#include <vector>

class GameVisual;
struct ShapePrimitive;
struct TextPrimitive;

// CPU rasterizer for pixel-space GameVisual primitives, for surfaces that have
// no GPU context (the detached developer console). Draws in the visual's
// painter order into a straight-alpha RGBA8 image with source-over blending.
// Supports filled/outline rectangles, lines, and font-atlas text using the
// same glyph metrics as GameVisual; other shape kinds, sprites, and shape
// transforms are ignored. Main-thread value type; no renderer involvement.
class SoftwareCanvas
{
public:
  SoftwareCanvas() = default;

  void resize(int width, int height);
  void clear(ColorRgba color);
  void draw(const GameVisual& visual);

  void fillRect(float x, float y, float w, float h, ColorRgba color);
  void drawLine(float x0,
                float y0,
                float x1,
                float y1,
                float lineWidth,
                ColorRgba color);
  void drawText(const TextPrimitive& text);

  int width() const { return m_width; }
  int height() const { return m_height; }
  // Row-major, top-down, 4 bytes per pixel (R, G, B, A).
  const std::vector<std::uint8_t>& pixels() const { return m_pixels; }
  ColorRgba pixel(int x, int y) const;

private:
  void blendPixel(int x, int y, ColorRgba color, float coverage);
  void drawShape(const ShapePrimitive& shape);

  int m_width = 0;
  int m_height = 0;
  std::vector<std::uint8_t> m_pixels;
};
