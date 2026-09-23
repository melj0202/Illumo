#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/ShapePrimitive.h>
#include <Illumo/Rendering/Primitives/SoftwareCanvas.h>
#include <Illumo/Rendering/Primitives/TextPrimitive.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>

void
SoftwareCanvas::resize(int width, int height)
{
  m_width = std::max(0, width);
  m_height = std::max(0, height);
  m_pixels.assign(static_cast<std::size_t>(m_width) *
                    static_cast<std::size_t>(m_height) * 4u,
                  0);
}

void
SoftwareCanvas::clear(ColorRgba color)
{
  for (std::size_t i = 0; i + 3 < m_pixels.size(); i += 4) {
    m_pixels[i + 0] = color.r;
    m_pixels[i + 1] = color.g;
    m_pixels[i + 2] = color.b;
    m_pixels[i + 3] = color.a;
  }
}

ColorRgba
SoftwareCanvas::pixel(int x, int y) const
{
  if (x < 0 || y < 0 || x >= m_width || y >= m_height) {
    return ColorRgba{ 0, 0, 0, 0 };
  }
  const std::size_t offset =
    (static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) +
     static_cast<std::size_t>(x)) *
    4u;
  return ColorRgba{ m_pixels[offset + 0],
                    m_pixels[offset + 1],
                    m_pixels[offset + 2],
                    m_pixels[offset + 3] };
}

void
SoftwareCanvas::blendPixel(int x, int y, ColorRgba color, float coverage)
{
  if (x < 0 || y < 0 || x >= m_width || y >= m_height) {
    return;
  }
  const float alpha =
    std::clamp(coverage, 0.0f, 1.0f) * (static_cast<float>(color.a) / 255.0f);
  if (alpha <= 0.0f) {
    return;
  }
  const std::size_t offset =
    (static_cast<std::size_t>(y) * static_cast<std::size_t>(m_width) +
     static_cast<std::size_t>(x)) *
    4u;
  const float keep = 1.0f - alpha;
  const unsigned char source[3] = { color.r, color.g, color.b };
  for (int channel = 0; channel < 3; ++channel) {
    const float blended = static_cast<float>(source[channel]) * alpha +
                          static_cast<float>(m_pixels[offset + channel]) * keep;
    m_pixels[offset + channel] =
      static_cast<std::uint8_t>(std::lround(std::clamp(blended, 0.0f, 255.0f)));
  }
  const float destinationAlpha =
    alpha * 255.0f + static_cast<float>(m_pixels[offset + 3]) * keep;
  m_pixels[offset + 3] = static_cast<std::uint8_t>(
    std::lround(std::clamp(destinationAlpha, 0.0f, 255.0f)));
}

void
SoftwareCanvas::fillRect(float x, float y, float w, float h, ColorRgba color)
{
  if (w <= 0.0f || h <= 0.0f) {
    return;
  }
  const float x1 = x + w;
  const float y1 = y + h;
  // Fractional edges get partial coverage, so rectangles on whole pixels stay
  // crisp and sub-pixel ones stay proportionate.
  const int startX = std::max(0, static_cast<int>(std::floor(x)));
  const int endX = std::min(m_width, static_cast<int>(std::ceil(x1)));
  const int startY = std::max(0, static_cast<int>(std::floor(y)));
  const int endY = std::min(m_height, static_cast<int>(std::ceil(y1)));
  for (int py = startY; py < endY; ++py) {
    const float coverY = std::min(static_cast<float>(py + 1), y1) -
                         std::max(static_cast<float>(py), y);
    for (int px = startX; px < endX; ++px) {
      const float coverX = std::min(static_cast<float>(px + 1), x1) -
                           std::max(static_cast<float>(px), x);
      blendPixel(px, py, color, coverX * coverY);
    }
  }
}

void
SoftwareCanvas::drawLine(float x0,
                         float y0,
                         float x1,
                         float y1,
                         float lineWidth,
                         ColorRgba color)
{
  const float half = std::max(0.5f, lineWidth * 0.5f);
  // Axis-aligned rules snap to whole pixels so one-pixel lines stay crisp
  // instead of smearing across two half-covered rows.
  const float thickness = std::max(1.0f, std::round(half * 2.0f));
  if (y0 == y1) {
    fillRect(std::min(x0, x1),
             std::round(y0 - half),
             std::abs(x1 - x0),
             thickness,
             color);
    return;
  }
  if (x0 == x1) {
    fillRect(std::round(x0 - half),
             std::min(y0, y1),
             thickness,
             std::abs(y1 - y0),
             color);
    return;
  }
  // Distance-to-segment coverage for diagonal strokes (resize grips).
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float lengthSquared = dx * dx + dy * dy;
  const int startX =
    std::max(0, static_cast<int>(std::floor(std::min(x0, x1) - half - 1.0f)));
  const int endX = std::min(
    m_width, static_cast<int>(std::ceil(std::max(x0, x1) + half + 1.0f)));
  const int startY =
    std::max(0, static_cast<int>(std::floor(std::min(y0, y1) - half - 1.0f)));
  const int endY = std::min(
    m_height, static_cast<int>(std::ceil(std::max(y0, y1) + half + 1.0f)));
  for (int py = startY; py < endY; ++py) {
    for (int px = startX; px < endX; ++px) {
      const float cx = static_cast<float>(px) + 0.5f;
      const float cy = static_cast<float>(py) + 0.5f;
      const float t = std::clamp(
        ((cx - x0) * dx + (cy - y0) * dy) / lengthSquared, 0.0f, 1.0f);
      const float nearestX = x0 + t * dx;
      const float nearestY = y0 + t * dy;
      const float distance = std::sqrt((cx - nearestX) * (cx - nearestX) +
                                       (cy - nearestY) * (cy - nearestY));
      blendPixel(px, py, color, half + 0.5f - distance);
    }
  }
}

void
SoftwareCanvas::drawText(const TextPrimitive& text)
{
  if (text.content.empty() || !text.visible) {
    return;
  }
  std::shared_ptr<Font> font = text.font ? text.font : Font::getDefaultFont();
  if (font == nullptr || font->getAtlasWidth() <= 0 ||
      font->getAtlasHeight() <= 0) {
    return;
  }
  const std::vector<unsigned char>& atlas = font->getAtlasPixels();
  const int atlasWidth = font->getAtlasWidth();
  const int atlasHeight = font->getAtlasHeight();
  // Same pen and glyph placement as GameVisual::pushTextRun.
  const float scale = font->getMetrics().pixelSize > 0.0f
                        ? (text.sizePt / font->getMetrics().pixelSize)
                        : 1.0f;
  const float lineHeight = font->getLineHeight(text.sizePt);
  const float ascender = font->getAscender(text.sizePt);
  float penX = 0.0f;
  float penY = 0.0f;
  for (std::size_t i = 0; i < text.content.size(); ++i) {
    const char ch = text.content[i];
    if (ch == '\n') {
      penX = 0.0f;
      penY += lineHeight;
      continue;
    }
    const GlyphInfo* glyph =
      font->getGlyph(static_cast<char32_t>(static_cast<unsigned char>(ch)));
    if (glyph == nullptr) {
      continue;
    }
    if (glyph->visible && glyph->width > 0.0f && glyph->height > 0.0f) {
      const float gx0 = text.x + penX + glyph->bearingX * scale;
      const float gy0 = text.y + penY + (ascender - glyph->bearingY * scale);
      const float gx1 = gx0 + glyph->width * scale;
      const float gy1 = gy0 + glyph->height * scale;
      const float texX0 = glyph->u0 * static_cast<float>(atlasWidth);
      const float texY0 = glyph->v0 * static_cast<float>(atlasHeight);
      const float texelsPerPixelX =
        (glyph->u1 - glyph->u0) * static_cast<float>(atlasWidth) / (gx1 - gx0);
      const float texelsPerPixelY =
        (glyph->v1 - glyph->v0) * static_cast<float>(atlasHeight) / (gy1 - gy0);
      const int startX = std::max(0, static_cast<int>(std::floor(gx0)));
      const int endX = std::min(m_width, static_cast<int>(std::ceil(gx1)));
      const int startY = std::max(0, static_cast<int>(std::floor(gy0)));
      const int endY = std::min(m_height, static_cast<int>(std::ceil(gy1)));
      for (int py = startY; py < endY; ++py) {
        // Box-filter the atlas over each destination pixel's footprint; the
        // atlas is rasterized larger than console text, so this minifies.
        const float sy0 =
          texY0 +
          (std::max(static_cast<float>(py), gy0) - gy0) * texelsPerPixelY;
        const float sy1 =
          texY0 +
          (std::min(static_cast<float>(py + 1), gy1) - gy0) * texelsPerPixelY;
        const float pixelCoverY = std::min(static_cast<float>(py + 1), gy1) -
                                  std::max(static_cast<float>(py), gy0);
        for (int px = startX; px < endX; ++px) {
          const float sx0 =
            texX0 +
            (std::max(static_cast<float>(px), gx0) - gx0) * texelsPerPixelX;
          const float sx1 =
            texX0 +
            (std::min(static_cast<float>(px + 1), gx1) - gx0) * texelsPerPixelX;
          const float pixelCoverX = std::min(static_cast<float>(px + 1), gx1) -
                                    std::max(static_cast<float>(px), gx0);
          float sum = 0.0f;
          float area = 0.0f;
          for (int ty = static_cast<int>(std::floor(sy0));
               ty < static_cast<int>(std::ceil(sy1));
               ++ty) {
            const float wy = std::min(static_cast<float>(ty + 1), sy1) -
                             std::max(static_cast<float>(ty), sy0);
            for (int tx = static_cast<int>(std::floor(sx0));
                 tx < static_cast<int>(std::ceil(sx1));
                 ++tx) {
              const float wx = std::min(static_cast<float>(tx + 1), sx1) -
                               std::max(static_cast<float>(tx), sx0);
              const float weight = wx * wy;
              area += weight;
              if (tx >= 0 && ty >= 0 && tx < atlasWidth && ty < atlasHeight) {
                const std::size_t texel =
                  (static_cast<std::size_t>(ty) *
                     static_cast<std::size_t>(atlasWidth) +
                   static_cast<std::size_t>(tx)) *
                    4u +
                  3u;
                sum += weight * static_cast<float>(atlas[texel]) / 255.0f;
              }
            }
          }
          if (area > 0.0f) {
            blendPixel(
              px, py, text.color, (sum / area) * pixelCoverX * pixelCoverY);
          }
        }
      }
    }
    penX += glyph->advanceX * scale;
  }
}

void
SoftwareCanvas::drawShape(const ShapePrimitive& shape)
{
  if (!shape.visible) {
    return;
  }
  const Rect2& rect = shape.rect;
  switch (shape.kind) {
    case ShapeKind::FilledRect:
      fillRect(rect.x, rect.y, rect.w, rect.h, shape.color);
      break;
    case ShapeKind::OutlineRect: {
      const float line = std::max(1.0f, shape.lineWidth);
      fillRect(rect.x, rect.y, rect.w, line, shape.color);
      fillRect(rect.x, rect.y + rect.h - line, rect.w, line, shape.color);
      fillRect(rect.x, rect.y + line, line, rect.h - 2.0f * line, shape.color);
      fillRect(rect.x + rect.w - line,
               rect.y + line,
               line,
               rect.h - 2.0f * line,
               shape.color);
      break;
    }
    case ShapeKind::Line:
      drawLine(
        shape.x0, shape.y0, shape.x1, shape.y1, shape.lineWidth, shape.color);
      break;
    default:
      break;
  }
}

void
SoftwareCanvas::draw(const GameVisual& visual)
{
  const std::vector<GameVisual::PrimitiveRef> order = visual.paintOrder();
  for (const GameVisual::PrimitiveRef& ref : order) {
    if (ref.kind == GameVisual::PrimitiveKind::Shape) {
      const ShapePrimitive* shape = visual.getShape(ref.index);
      if (shape != nullptr) {
        drawShape(*shape);
      }
    } else if (ref.kind == GameVisual::PrimitiveKind::Text) {
      const TextPrimitive* text = visual.getText(ref.index);
      if (text != nullptr) {
        drawText(*text);
      }
    }
  }
}
