#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/Font.h>
#include <algorithm>
#include <cmath>

float
GuiKit::estimateTextWidth(const std::string& text, float sizePt)
{
  return static_cast<float>(text.size()) * sizePt * 0.6f;
}

float
GuiKit::defaultLineHeight(float sizePt)
{
  return sizePt * 1.35f;
}

float
GuiKit::caretOriginAfterText(const std::string& text, float textX, float sizePt)
{
  std::shared_ptr<Font> font = Font::getDefaultFont();
  if (font == nullptr) {
    return textX + estimateTextWidth(text, sizePt);
  }
  const FontMetrics& metrics = font->getMetrics();
  const float scale =
    metrics.pixelSize > 0.0f ? sizePt / metrics.pixelSize : 1.0f;
  float inkRight = textX;
  if (!text.empty()) {
    // Measured advance excludes the final glyph's right side bearing.
    const TextBounds bounds = font->measureText(text, sizePt);
    const GlyphInfo* lastGlyph =
      font->getGlyph(static_cast<unsigned char>(text.back()));
    inkRight += bounds.width;
    if (lastGlyph != nullptr) {
      inkRight +=
        (lastGlyph->bearingX + lastGlyph->width - lastGlyph->advanceX) * scale;
    }
  }
  const GlyphInfo* caretGlyph = font->getGlyph('|');
  const float caretBearing =
    caretGlyph == nullptr ? 0.0f : caretGlyph->bearingX * scale;
  return inkRight + 1.0f - caretBearing;
}

void
GuiKit::drawTextCentered(GameVisual& visual,
                         const std::string& text,
                         float centerX,
                         float centerY,
                         float sizePt,
                         ColorRgba color)
{
  std::shared_ptr<Font> font = Font::getDefaultFont();
  const float textW = font ? font->measureText(text, sizePt).width
                           : estimateTextWidth(text, sizePt);
  const float textX = centerX - textW * 0.5f;
  const float textY = centerY - sizePt * 0.5f;
  visual.addText(text, textX, textY, sizePt, color);
}

void
GuiKit::drawTextAligned(GameVisual& visual,
                        const std::string& text,
                        float x,
                        float y,
                        float width,
                        float sizePt,
                        ColorRgba color,
                        GuiAlignment alignment)
{
  std::shared_ptr<Font> font = Font::getDefaultFont();
  const float textW = font ? font->measureText(text, sizePt).width
                           : estimateTextWidth(text, sizePt);
  float drawX = x;
  if (alignment == GuiAlignment::Center) {
    drawX = x + std::max(0.0f, (width - textW) * 0.5f);
  } else if (alignment == GuiAlignment::Right) {
    drawX = x + std::max(0.0f, width - textW);
  }
  visual.addText(text, drawX, y, sizePt, color);
}

void
GuiKit::drawLabelValue(GameVisual& visual,
                       const std::string& label,
                       const std::string& value,
                       float x,
                       float y,
                       float width,
                       float sizePt,
                       ColorRgba labelColor,
                       ColorRgba valueColor)
{
  visual.addText(label, x, y, sizePt, labelColor);
  std::shared_ptr<Font> font = Font::getDefaultFont();
  const float valW = font ? font->measureText(value, sizePt).width
                          : estimateTextWidth(value, sizePt);
  const float valX = x + std::max(0.0f, width - valW);
  visual.addText(value, valX, y, sizePt, valueColor);
}

// Quarter-circle unit directions at 15-degree steps (0..90 degrees).
static const float kQuarterCos[7] = { 1.0f,        0.96592583f, 0.86602540f,
                                      0.70710678f, 0.5f,        0.25881905f,
                                      0.0f };
static const float kQuarterSin[7] = { 0.0f,        0.25881905f, 0.5f,
                                      0.70710678f, 0.86602540f, 0.96592583f,
                                      1.0f };
static const int kCornerPoints = 7;
static const int kPerimeterPoints = 4 * kCornerPoints;

struct GuiPoint
{
  float x = 0.0f;
  float y = 0.0f;
};

static bool
finiteRect(float x, float y, float width, float height)
{
  return std::isfinite(x) && std::isfinite(y) && std::isfinite(width) &&
         std::isfinite(height) && width > 0.0f && height > 0.0f;
}

// Unit direction for step `step` of corner `corner` (0 top-left, 1 top-right,
// 2 bottom-right, 3 bottom-left), walking the outline clockwise on screen.
static GuiPoint
cornerDirection(int corner, int step)
{
  const float c = kQuarterCos[step];
  const float s = kQuarterSin[step];
  if (corner == 0) {
    return GuiPoint{ -c, -s };
  }
  if (corner == 1) {
    return GuiPoint{ s, -c };
  }
  if (corner == 2) {
    return GuiPoint{ c, s };
  }
  return GuiPoint{ -s, c };
}

// Clockwise outline of the rounded rect grown by `offset` (negative insets).
// The offset curve keeps the corner centers, so bands between two offsets are
// exactly parallel; insets deeper than the radius collapse to sharp corners.
static void
roundedPerimeter(float x,
                 float y,
                 float width,
                 float height,
                 float radius,
                 float offset,
                 GuiPoint* points)
{
  const float grownRadius = std::max(0.0f, radius + offset);
  const float left = x - offset + grownRadius;
  const float right = x + width + offset - grownRadius;
  const float top = y - offset + grownRadius;
  const float bottom = y + height + offset - grownRadius;
  const float centersX[4] = { left, right, right, left };
  const float centersY[4] = { top, top, bottom, bottom };
  for (int corner = 0; corner < 4; ++corner) {
    for (int step = 0; step < kCornerPoints; ++step) {
      const GuiPoint direction = cornerDirection(corner, step);
      points[corner * kCornerPoints + step] =
        GuiPoint{ centersX[corner] + direction.x * grownRadius,
                  centersY[corner] + direction.y * grownRadius };
    }
  }
}

static float
clampedRadius(float radius, float width, float height)
{
  if (!std::isfinite(radius)) {
    return 0.0f;
  }
  return std::clamp(radius, 0.0f, std::min(width, height) * 0.5f);
}

// Non-overlapping quarter fans keep translucent rounded surfaces evenly tinted.
// Each corner packs two 15-degree wedges per quad: (center, r0, r1) and
// (r1, r2, center) share the fan-ordered quad (center, r0, r1, r2).
void
GuiKit::drawRoundedRect(GameVisual& visual,
                        float x,
                        float y,
                        float width,
                        float height,
                        float radius,
                        ColorRgba color)
{
  if (!finiteRect(x, y, width, height) || !std::isfinite(radius) ||
      color.a == 0) {
    return;
  }
  radius = clampedRadius(radius, width, height);
  if (radius == 0.0f) {
    visual.addFilledRect(x, y, width, height, color);
    return;
  }
  visual.addFilledRect(x + radius, y, width - 2.0f * radius, height, color);
  visual.addFilledRect(x, y + radius, radius, height - 2.0f * radius, color);
  visual.addFilledRect(
    x + width - radius, y + radius, radius, height - 2.0f * radius, color);
  GuiPoint rim[kPerimeterPoints];
  roundedPerimeter(x, y, width, height, radius, 0.0f, rim);
  const float centersX[4] = {
    x + radius, x + width - radius, x + width - radius, x + radius
  };
  const float centersY[4] = {
    y + radius, y + radius, y + height - radius, y + height - radius
  };
  for (int corner = 0; corner < 4; ++corner) {
    const int base = corner * kCornerPoints;
    for (int step = 0; step + 2 < kCornerPoints; step += 2) {
      const GuiPoint& r0 = rim[base + step];
      const GuiPoint& r1 = rim[base + step + 1];
      const GuiPoint& r2 = rim[base + step + 2];
      visual.addGradientQuad(centersX[corner],
                             centersY[corner],
                             r0.x,
                             r0.y,
                             r1.x,
                             r1.y,
                             r2.x,
                             r2.y,
                             color,
                             color,
                             color,
                             color);
    }
  }
}

void
GuiKit::drawRoundedPanel(GameVisual& visual,
                         float x,
                         float y,
                         float width,
                         float height,
                         unsigned char opacity)
{
  GuiGlassStyle style;
  style.opacity = opacity;
  drawGlassPanel(visual, x, y, width, height, style);
}

// Horizontal slices pair the right outline with its mirror on the left, so
// every quad is a convex trapezoid and colors interpolate only along y.
void
GuiKit::drawRoundedGradientRect(GameVisual& visual,
                                float x,
                                float y,
                                float width,
                                float height,
                                float radius,
                                ColorRgba top,
                                ColorRgba bottom)
{
  if (!finiteRect(x, y, width, height) || (top.a == 0 && bottom.a == 0)) {
    return;
  }
  radius = clampedRadius(radius, width, height);
  GuiPoint rim[kPerimeterPoints];
  roundedPerimeter(x, y, width, height, radius, 0.0f, rim);
  // Right side top to bottom: top-right corner then bottom-right corner.
  GuiPoint rightSide[2 * kCornerPoints];
  GuiPoint leftSide[2 * kCornerPoints];
  for (int step = 0; step < kCornerPoints; ++step) {
    rightSide[step] = rim[kCornerPoints + step];
    rightSide[kCornerPoints + step] = rim[2 * kCornerPoints + step];
    // Left mirrors: top-left walks up, bottom-left walks down, so reverse.
    leftSide[step] = rim[kCornerPoints - 1 - step];
    leftSide[kCornerPoints + step] = rim[4 * kCornerPoints - 1 - step];
  }
  for (int level = 0; level + 1 < 2 * kCornerPoints; ++level) {
    const GuiPoint& l0 = leftSide[level];
    const GuiPoint& r0 = rightSide[level];
    const GuiPoint& r1 = rightSide[level + 1];
    const GuiPoint& l1 = leftSide[level + 1];
    if (r1.y - r0.y <= 0.0001f) {
      continue;
    }
    const ColorRgba upper = UiTheme::mix(top, bottom, (r0.y - y) / height);
    const ColorRgba lower = UiTheme::mix(top, bottom, (r1.y - y) / height);
    visual.addGradientQuad(l0.x,
                           l0.y,
                           r0.x,
                           r0.y,
                           r1.x,
                           r1.y,
                           l1.x,
                           l1.y,
                           upper,
                           upper,
                           lower,
                           lower);
  }
}

void
GuiKit::drawRoundedBand(GameVisual& visual,
                        float x,
                        float y,
                        float width,
                        float height,
                        float radius,
                        float innerOffset,
                        float outerOffset,
                        ColorRgba innerColor,
                        ColorRgba outerColor)
{
  if (!finiteRect(x, y, width, height) || !std::isfinite(innerOffset) ||
      !std::isfinite(outerOffset) || outerOffset <= innerOffset ||
      (innerColor.a == 0 && outerColor.a == 0)) {
    return;
  }
  radius = clampedRadius(radius, width, height);
  GuiPoint inner[kPerimeterPoints];
  GuiPoint outer[kPerimeterPoints];
  roundedPerimeter(x, y, width, height, radius, innerOffset, inner);
  roundedPerimeter(x, y, width, height, radius, outerOffset, outer);
  for (int index = 0; index < kPerimeterPoints; ++index) {
    const int next = (index + 1) % kPerimeterPoints;
    visual.addGradientQuad(inner[index].x,
                           inner[index].y,
                           outer[index].x,
                           outer[index].y,
                           outer[next].x,
                           outer[next].y,
                           inner[next].x,
                           inner[next].y,
                           innerColor,
                           outerColor,
                           outerColor,
                           innerColor);
  }
}

void
GuiKit::drawRoundedOutline(GameVisual& visual,
                           float x,
                           float y,
                           float width,
                           float height,
                           float radius,
                           float thickness,
                           ColorRgba color)
{
  drawRoundedBand(
    visual, x, y, width, height, radius, -thickness, 0.0f, color, color);
}

void
GuiKit::drawSoftShadow(GameVisual& visual,
                       float x,
                       float y,
                       float width,
                       float height,
                       float radius,
                       float spread,
                       float offsetY,
                       ColorRgba color)
{
  if (!std::isfinite(offsetY) || !finiteRect(x, y, width, height)) {
    return;
  }
  // Start slightly inside the edge so the falloff has no hard seam at the
  // rim, and fill the core so an offset shadow leaves no gap under its caster.
  const float inset = std::min({ radius, 4.0f, width * 0.5f, height * 0.5f });
  drawRoundedRect(visual,
                  x + inset,
                  y + offsetY + inset,
                  width - 2.0f * inset,
                  height - 2.0f * inset,
                  std::max(0.0f, radius - inset),
                  color);
  drawRoundedBand(visual,
                  x,
                  y + offsetY,
                  width,
                  height,
                  radius,
                  -inset,
                  spread,
                  color,
                  UiTheme::transparentOf(color));
}

void
GuiKit::drawSoftGlow(GameVisual& visual,
                     float centerX,
                     float centerY,
                     float radiusX,
                     float radiusY,
                     ColorRgba color,
                     int segments)
{
  if (!std::isfinite(centerX) || !std::isfinite(centerY) ||
      !std::isfinite(radiusX) || !std::isfinite(radiusY) || radiusX <= 0.0f ||
      radiusY <= 0.0f || color.a == 0) {
    return;
  }
  const int clamped = std::clamp(segments, 8, 48);
  const int count = clamped - clamped % 2;
  // Three rings approximate a smooth gaussian-like falloff.
  const float ringRadius[3] = { 0.32f, 0.66f, 1.0f };
  const float ringAlpha[3] = { 0.62f, 0.2f, 0.0f };
  const float step = 6.28318531f / static_cast<float>(count);
  const ColorRgba ringColors[3] = { UiTheme::fade(color, ringAlpha[0]),
                                    UiTheme::fade(color, ringAlpha[1]),
                                    UiTheme::transparentOf(color) };
  for (int segment = 0; segment < count; segment += 2) {
    const float a0 = step * static_cast<float>(segment);
    const float a1 = a0 + step;
    const float a2 = a1 + step;
    const float r = ringRadius[0];
    visual.addGradientQuad(centerX,
                           centerY,
                           centerX + std::cos(a0) * radiusX * r,
                           centerY + std::sin(a0) * radiusY * r,
                           centerX + std::cos(a1) * radiusX * r,
                           centerY + std::sin(a1) * radiusY * r,
                           centerX + std::cos(a2) * radiusX * r,
                           centerY + std::sin(a2) * radiusY * r,
                           color,
                           ringColors[0],
                           ringColors[0],
                           ringColors[0]);
  }
  for (int ring = 0; ring < 2; ++ring) {
    const float inner = ringRadius[ring];
    const float outer = ringRadius[ring + 1];
    for (int segment = 0; segment < count; ++segment) {
      const float a0 = step * static_cast<float>(segment);
      const float a1 = a0 + step;
      const float c0 = std::cos(a0);
      const float s0 = std::sin(a0);
      const float c1 = std::cos(a1);
      const float s1 = std::sin(a1);
      visual.addGradientQuad(centerX + c0 * radiusX * inner,
                             centerY + s0 * radiusY * inner,
                             centerX + c0 * radiusX * outer,
                             centerY + s0 * radiusY * outer,
                             centerX + c1 * radiusX * outer,
                             centerY + s1 * radiusY * outer,
                             centerX + c1 * radiusX * inner,
                             centerY + s1 * radiusY * inner,
                             ringColors[ring],
                             ringColors[ring + 1],
                             ringColors[ring + 1],
                             ringColors[ring]);
    }
  }
}

void
GuiKit::drawVignette(GameVisual& visual,
                     float width,
                     float height,
                     ColorRgba center,
                     ColorRgba edge,
                     float innerFraction)
{
  if (!finiteRect(0.0f, 0.0f, width, height)) {
    return;
  }
  const int kSegments = 32;
  const float cx = width * 0.5f;
  const float cy = height * 0.5f;
  // The outer ellipse passes through the screen corners.
  const float rx = cx * 1.4142136f;
  const float ry = cy * 1.4142136f;
  const float inner = std::clamp(innerFraction, 0.05f, 0.95f);
  const float step = 6.28318531f / static_cast<float>(kSegments);
  if (center.a != 0) {
    // The center fan shares the ring's vertices so no sliver stays uncovered.
    for (int segment = 0; segment < kSegments; segment += 2) {
      const float a0 = step * static_cast<float>(segment);
      const float a1 = a0 + step;
      const float a2 = a1 + step;
      visual.addGradientQuad(cx,
                             cy,
                             cx + std::cos(a0) * rx * inner,
                             cy + std::sin(a0) * ry * inner,
                             cx + std::cos(a1) * rx * inner,
                             cy + std::sin(a1) * ry * inner,
                             cx + std::cos(a2) * rx * inner,
                             cy + std::sin(a2) * ry * inner,
                             center,
                             center,
                             center,
                             center);
    }
  }
  for (int segment = 0; segment < kSegments; ++segment) {
    const float a0 = step * static_cast<float>(segment);
    const float a1 = a0 + step;
    const float c0 = std::cos(a0);
    const float s0 = std::sin(a0);
    const float c1 = std::cos(a1);
    const float s1 = std::sin(a1);
    visual.addGradientQuad(cx + c0 * rx * inner,
                           cy + s0 * ry * inner,
                           cx + c0 * rx,
                           cy + s0 * ry,
                           cx + c1 * rx,
                           cy + s1 * ry,
                           cx + c1 * rx * inner,
                           cy + s1 * ry * inner,
                           center,
                           edge,
                           edge,
                           center);
  }
}

void
GuiKit::drawSheen(GameVisual& visual,
                  float x,
                  float y,
                  float width,
                  float height,
                  float inset,
                  float progress,
                  ColorRgba color)
{
  if (!finiteRect(x, y, width, height) || !std::isfinite(progress) ||
      progress <= 0.0f || progress >= 1.0f || color.a == 0) {
    return;
  }
  const float minX = x + std::max(0.0f, inset);
  const float maxX = x + width - std::max(0.0f, inset);
  if (maxX <= minX) {
    return;
  }
  const float band = std::max(12.0f, height * 1.1f);
  const float skew = height * 0.45f;
  const float travel = (maxX - minX) + band * 2.0f + skew;
  const float centerBottom = minX - band - skew + travel * progress;
  const float centerTop = centerBottom + skew;
  const float top = y + 1.0f;
  const float bottom = y + height - 1.0f;
  const ColorRgba clear = UiTheme::transparentOf(color);
  // Left half fades in, right half fades out; clamping x keeps both convex.
  const float leftTop0 = std::clamp(centerTop - band * 0.5f, minX, maxX);
  const float leftTop1 = std::clamp(centerTop, minX, maxX);
  const float leftBottom0 = std::clamp(centerBottom - band * 0.5f, minX, maxX);
  const float leftBottom1 = std::clamp(centerBottom, minX, maxX);
  if (leftTop1 > leftTop0 || leftBottom1 > leftBottom0) {
    visual.addGradientQuad(leftTop0,
                           top,
                           leftTop1,
                           top,
                           leftBottom1,
                           bottom,
                           leftBottom0,
                           bottom,
                           clear,
                           color,
                           color,
                           clear);
  }
  const float rightTop1 = std::clamp(centerTop + band * 0.5f, minX, maxX);
  const float rightBottom1 = std::clamp(centerBottom + band * 0.5f, minX, maxX);
  if (rightTop1 > leftTop1 || rightBottom1 > leftBottom1) {
    visual.addGradientQuad(leftTop1,
                           top,
                           rightTop1,
                           top,
                           rightBottom1,
                           bottom,
                           leftBottom1,
                           bottom,
                           color,
                           clear,
                           clear,
                           color);
  }
}

void
GuiKit::drawGlassPanel(GameVisual& visual,
                       float x,
                       float y,
                       float width,
                       float height,
                       const GuiGlassStyle& style)
{
  if (!finiteRect(x, y, width, height) || style.opacity == 0) {
    return;
  }
  const unsigned char opacity = style.opacity;
  const float radius = clampedRadius(style.radius, width, height);
  if (style.shadow) {
    drawSoftShadow(visual,
                   x,
                   y,
                   width,
                   height,
                   radius,
                   28.0f,
                   12.0f,
                   UiTheme::applyOpacity(UiTheme::glowShadow(), opacity));
  }
  const float glow = std::clamp(style.glow, 0.0f, 1.0f);
  if (glow > 0.0f) {
    drawRoundedBand(
      visual,
      x,
      y,
      width,
      height,
      radius,
      0.0f,
      22.0f,
      UiTheme::applyOpacity(
        UiTheme::fade(UiTheme::accentCool(), 0.05f + 0.13f * glow), opacity),
      UiTheme::transparentOf(UiTheme::accentCool()));
  }
  drawRoundedRect(visual,
                  x,
                  y,
                  width,
                  height,
                  radius,
                  UiTheme::applyOpacity(UiTheme::glassRim(), opacity));
  drawRoundedGradientRect(
    visual,
    x + 1.0f,
    y + 1.0f,
    width - 2.0f,
    height - 2.0f,
    std::max(0.0f, radius - 1.0f),
    UiTheme::applyOpacity(UiTheme::glassTop(), opacity),
    UiTheme::applyOpacity(UiTheme::glassBottom(), opacity));
  // A lit inner rim on the upper edge reads as light catching the glass.
  const float litWidth = std::max(0.0f, width - 2.0f * radius);
  if (litWidth > 0.0f) {
    const ColorRgba lit = UiTheme::applyOpacity(
      UiTheme::fade(UiTheme::glassRimLit(), 0.55f), opacity);
    const ColorRgba clear = UiTheme::transparentOf(lit);
    visual.addGradientRect(
      x + radius, y + 1.0f, litWidth * 0.5f, 1.0f, clear, lit, lit, clear);
    visual.addGradientRect(x + radius + litWidth * 0.5f,
                           y + 1.0f,
                           litWidth * 0.5f,
                           1.0f,
                           lit,
                           clear,
                           clear,
                           lit);
  }
  if (!style.accentLine) {
    return;
  }
  const float reveal = std::clamp(style.accentReveal, 0.0f, 1.0f);
  const float lineWidth = std::max(0.0f, width - 48.0f) * reveal;
  if (lineWidth <= 0.0f) {
    return;
  }
  const float lineX =
    x + 24.0f + (std::max(0.0f, width - 48.0f) - lineWidth) * 0.5f;
  const ColorRgba cyan = UiTheme::applyOpacity(UiTheme::accentCool(), opacity);
  const ColorRgba violet =
    UiTheme::applyOpacity(UiTheme::accentViolet(), opacity);
  visual.addGradientRect(lineX, y, lineWidth, 2.0f, cyan, violet, violet, cyan);
  // A glint travels the hairline twice per ambient cycle.
  const float phase =
    std::isfinite(style.ambientPhase)
      ? style.ambientPhase / 6.0f - std::floor(style.ambientPhase / 6.0f)
      : 0.0f;
  const float glintWidth = std::min(90.0f, lineWidth * 0.35f);
  const float glintX = lineX - glintWidth + (lineWidth + glintWidth) * phase;
  const float glintLeft = std::clamp(glintX, lineX, lineX + lineWidth);
  const float glintMid =
    std::clamp(glintX + glintWidth * 0.5f, lineX, lineX + lineWidth);
  const float glintRight =
    std::clamp(glintX + glintWidth, lineX, lineX + lineWidth);
  const ColorRgba white =
    UiTheme::applyOpacity(ColorRgba{ 236, 252, 255, 230 }, opacity);
  const ColorRgba clearWhite = UiTheme::transparentOf(white);
  if (glintMid > glintLeft) {
    visual.addGradientRect(glintLeft,
                           y - 0.5f,
                           glintMid - glintLeft,
                           3.0f,
                           clearWhite,
                           white,
                           white,
                           clearWhite);
  }
  if (glintRight > glintMid) {
    visual.addGradientRect(glintMid,
                           y - 0.5f,
                           glintRight - glintMid,
                           3.0f,
                           white,
                           clearWhite,
                           clearWhite,
                           white);
  }
}

// One arrow glyph centered at (cx, cy); dx/dy is the pointing direction.
static void
drawArrowGlyph(GameVisual& visual,
               float cx,
               float cy,
               float half,
               float dx,
               float dy,
               ColorRgba color)
{
  // Base across the perpendicular, tip along the direction.
  const float px = -dy;
  const float py = dx;
  visual.addFilledTriangle(cx - dx * half * 0.6f + px * half,
                           cy - dy * half * 0.6f + py * half,
                           cx - dx * half * 0.6f - px * half,
                           cy - dy * half * 0.6f - py * half,
                           cx + dx * half * 0.7f,
                           cy + dy * half * 0.7f,
                           color);
}

static bool
drawArrowLabel(GameVisual& visual,
               const std::string& label,
               float x,
               float y,
               float size,
               ColorRgba color)
{
  // Arrow glyphs are not in the ASCII atlas; draw them as small triangles.
  const float half = size * 0.36f;
  const float cx = x + half;
  const float cy = y + size * 0.5f;
  if (label == "UP") {
    drawArrowGlyph(visual, cx, cy, half, 0.0f, -1.0f, color);
  } else if (label == "DOWN") {
    drawArrowGlyph(visual, cx, cy, half, 0.0f, 1.0f, color);
  } else if (label == "LEFT") {
    drawArrowGlyph(visual, cx, cy, half, -1.0f, 0.0f, color);
  } else if (label == "RIGHT") {
    drawArrowGlyph(visual, cx, cy, half, 1.0f, 0.0f, color);
  } else if (label == "UPDOWN") {
    drawArrowGlyph(
      visual, cx, cy - size * 0.24f, half * 0.8f, 0.0f, -1.0f, color);
    drawArrowGlyph(
      visual, cx, cy + size * 0.24f, half * 0.8f, 0.0f, 1.0f, color);
  } else if (label == "LEFTRIGHT") {
    drawArrowGlyph(visual, cx, cy, half * 0.8f, -1.0f, 0.0f, color);
    drawArrowGlyph(
      visual, cx + half * 1.7f, cy, half * 0.8f, 1.0f, 0.0f, color);
  } else {
    return false;
  }
  return true;
}

static float
keycapLabelWidth(const std::string& label, float sizePt)
{
  const float arrow = sizePt * 0.72f;
  if (label == "UP" || label == "DOWN" || label == "LEFT" || label == "RIGHT" ||
      label == "UPDOWN") {
    return arrow;
  }
  if (label == "LEFTRIGHT") {
    return arrow + sizePt * 0.36f * 1.2f;
  }
  std::shared_ptr<Font> font = Font::getDefaultFont();
  return font ? font->measureText(label, sizePt).width
              : GuiKit::estimateTextWidth(label, sizePt);
}

float
GuiKit::drawKeycap(GameVisual& visual,
                   float x,
                   float y,
                   const std::string& label,
                   float sizePt,
                   unsigned char opacity)
{
  const float padding = sizePt * 0.55f;
  const float labelWidth = keycapLabelWidth(label, sizePt);
  const float width = std::max(sizePt * 1.7f, labelWidth + padding * 2.0f);
  const float height = sizePt * 1.75f;
  const float radius = sizePt * 0.42f;
  // Darker lower edge first, then the face, so the key reads as raised.
  drawRoundedRect(visual,
                  x,
                  y + 1.5f,
                  width,
                  height,
                  radius,
                  UiTheme::applyOpacity(UiTheme::keycapEdge(), opacity));
  drawRoundedGradientRect(
    visual,
    x,
    y,
    width,
    height,
    radius,
    UiTheme::applyOpacity(UiTheme::keycapFace(), opacity),
    UiTheme::applyOpacity(
      UiTheme::mix(UiTheme::keycapFace(), UiTheme::keycapEdge(), 0.35f),
      opacity));
  const ColorRgba ink =
    UiTheme::applyOpacity(UiTheme::textSecondary(), opacity);
  const float labelX = x + (width - labelWidth) * 0.5f;
  const float labelY = y + (height - sizePt) * 0.5f;
  if (!drawArrowLabel(visual, label, labelX, labelY, sizePt, ink)) {
    visual.addText(label, labelX, labelY, sizePt, ink);
  }
  return width;
}

float
GuiKit::measureKeyHint(const std::string& key,
                       const std::string& action,
                       float sizePt)
{
  const float padding = sizePt * 0.55f;
  const float keyWidth =
    std::max(sizePt * 1.7f, keycapLabelWidth(key, sizePt) + padding * 2.0f);
  std::shared_ptr<Font> font = Font::getDefaultFont();
  const float actionWidth = font ? font->measureText(action, sizePt).width
                                 : estimateTextWidth(action, sizePt);
  return keyWidth + sizePt * 0.6f + actionWidth + sizePt * 1.6f;
}

float
GuiKit::drawKeyHint(GameVisual& visual,
                    float x,
                    float y,
                    const std::string& key,
                    const std::string& action,
                    float sizePt,
                    unsigned char opacity)
{
  const float keyWidth = drawKeycap(visual, x, y, key, sizePt, opacity);
  const float height = sizePt * 1.75f;
  visual.addText(action,
                 x + keyWidth + sizePt * 0.6f,
                 y + (height - sizePt) * 0.5f,
                 sizePt,
                 UiTheme::applyOpacity(UiTheme::textMuted(), opacity));
  return measureKeyHint(key, action, sizePt);
}

bool
GuiKit::isPointInRect(float px,
                      float py,
                      float rx,
                      float ry,
                      float rw,
                      float rh)
{
  return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
}

void
GuiKit::drawBackdrop(GameVisual& visual,
                     float screenWidth,
                     float screenHeight,
                     unsigned char opacity)
{
  visual.addFilledRect(
    0.0f, 0.0f, screenWidth, screenHeight, ColorRgba{ 0, 0, 0, opacity });
}

void
GuiKit::drawShadow(GameVisual& visual,
                   float x,
                   float y,
                   float w,
                   float h,
                   float offset,
                   ColorRgba color)
{
  visual.addFilledRect(x + offset, y + offset, w, h, color);
}

void
GuiKit::drawPanel(GameVisual& visual,
                  float x,
                  float y,
                  float w,
                  float h,
                  const GuiPanelChrome& chrome)
{
  if (chrome.drawShadow && chrome.shadowOffset > 0.0f && chrome.shadow.a > 0) {
    drawShadow(visual, x, y, w, h, chrome.shadowOffset, chrome.shadow);
  }
  visual.addFilledRect(x, y, w, h, chrome.background);
  if (chrome.borderWidth > 0.0f && chrome.border.a > 0) {
    visual.addOutlineRect(x, y, w, h, chrome.border, chrome.borderWidth);
  }
  if (chrome.drawAccent && chrome.accentWidth > 0.0f && chrome.accent.a > 0) {
    visual.addFilledRect(x, y, chrome.accentWidth, h, chrome.accent);
  }
}

void
GuiKit::drawCard(GameVisual& visual,
                 float x,
                 float y,
                 float w,
                 float h,
                 ColorRgba surfaceColor,
                 ColorRgba borderColor,
                 float borderWidth)
{
  GuiPanelChrome chrome;
  chrome.background = surfaceColor;
  chrome.border = borderColor;
  chrome.borderWidth = borderWidth;
  chrome.shadow = UiTheme::panelShadow();
  chrome.shadowOffset = 3.0f;
  chrome.drawShadow = true;
  chrome.drawAccent = false;
  drawPanel(visual, x, y, w, h, chrome);
}

void
GuiKit::drawHeaderBar(GameVisual& visual,
                      float x,
                      float y,
                      float w,
                      float h,
                      const std::string& title,
                      float fontSize,
                      ColorRgba surfaceColor,
                      ColorRgba textColor,
                      ColorRgba accentColor)
{
  visual.addFilledRect(x, y, w, h, surfaceColor);
  visual.addOutlineRect(x, y, w, h, UiTheme::panelBorder(), 1.0f);
  if (accentColor.a > 0) {
    visual.addFilledRect(x, y, 3.0f, h, accentColor);
  }
  const float textY = y + (h - fontSize) * 0.5f;
  visual.addText(title, x + 10.0f, textY, fontSize, textColor);
}

void
GuiKit::drawDivider(GameVisual& visual,
                    float x,
                    float y,
                    float length,
                    bool vertical,
                    ColorRgba color)
{
  if (vertical) {
    visual.addLine(x, y, x, y + length, color, 1.0f);
  } else {
    visual.addLine(x, y, x + length, y, color, 1.0f);
  }
}

void
GuiKit::drawButton(GameVisual& visual,
                   float x,
                   float y,
                   float w,
                   float h,
                   const std::string& label,
                   float fontSize,
                   GuiButtonState state,
                   ColorRgba customAccent)
{
  ColorRgba bg = UiTheme::panelSurface();
  ColorRgba border = UiTheme::panelBorder();
  ColorRgba text = UiTheme::textPrimary();
  const ColorRgba activeAccent =
    (customAccent.a > 0) ? customAccent : UiTheme::accent();

  switch (state) {
    case GuiButtonState::Normal:
      bg = UiTheme::panelSurface();
      border = UiTheme::panelBorder();
      text = UiTheme::textPrimary();
      break;
    case GuiButtonState::Hover:
      bg = UiTheme::panelRaised();
      border = activeAccent;
      text = UiTheme::textPrimary();
      break;
    case GuiButtonState::Pressed:
      bg = UiTheme::panelInset();
      border = activeAccent;
      text = activeAccent;
      break;
    case GuiButtonState::Disabled:
      bg = UiTheme::applyOpacity(UiTheme::panelInset(), 140);
      border = UiTheme::applyOpacity(UiTheme::divider(), 120);
      text = UiTheme::textMuted();
      break;
  }

  // Drop shadow for normal / hover states
  if (state != GuiButtonState::Disabled && state != GuiButtonState::Pressed) {
    visual.addFilledRect(x + 2.0f, y + 2.0f, w, h, ColorRgba{ 0, 0, 0, 100 });
  }

  visual.addFilledRect(x, y, w, h, bg);
  visual.addOutlineRect(x, y, w, h, border, 1.0f);

  if (state == GuiButtonState::Hover) {
    visual.addFilledRect(x, y, 3.0f, h, activeAccent);
  }

  drawTextCentered(visual, label, x + w * 0.5f, y + h * 0.5f, fontSize, text);
}

void
GuiKit::drawIconButton(GameVisual& visual,
                       float x,
                       float y,
                       float w,
                       float h,
                       TextureHandle atlas,
                       const TextureRegion& iconRegion,
                       const std::string& label,
                       float fontSize,
                       GuiButtonState state,
                       ColorRgba customAccent)
{
  drawButton(visual, x, y, w, h, "", fontSize, state, customAccent);

  const float iconSize = std::min(w, h) * 0.65f;
  if (label.empty()) {
    const float iconX = x + (w - iconSize) * 0.5f;
    const float iconY = y + (h - iconSize) * 0.5f;
    visual.addSprite(atlas,
                     Rect2{ iconX, iconY, iconSize, iconSize },
                     iconRegion,
                     ColorRgba{ 255, 255, 255, 255 });
  } else {
    const float iconX = x + 6.0f;
    const float iconY = y + (h - iconSize) * 0.5f;
    visual.addSprite(atlas,
                     Rect2{ iconX, iconY, iconSize, iconSize },
                     iconRegion,
                     ColorRgba{ 255, 255, 255, 255 });
    const float textX = iconX + iconSize + 6.0f;
    const float textY = y + (h - fontSize) * 0.5f;
    visual.addText(label, textX, textY, fontSize, UiTheme::textPrimary());
  }
}

void
GuiKit::drawSelectionHighlight(GameVisual& visual,
                               float x,
                               float y,
                               float w,
                               float h,
                               ColorRgba color,
                               float outlineWidth)
{
  visual.addFilledRect(x, y, w, h, color);
  if (outlineWidth > 0.0f) {
    visual.addOutlineRect(x, y, w, h, UiTheme::accent(), outlineWidth);
  }
}

void
GuiKit::drawPropertyRow(GameVisual& visual,
                        float x,
                        float y,
                        float w,
                        float h,
                        const std::string& label,
                        const std::string& value,
                        float fontSize,
                        bool isSelected,
                        bool isHovered)
{
  if (isSelected) {
    drawSelectionHighlight(visual, x, y, w, h, UiTheme::selection(), 1.0f);
  } else if (isHovered) {
    visual.addFilledRect(
      x, y, w, h, UiTheme::applyOpacity(UiTheme::panelRaised(), 160));
  }

  const float textY = y + (h - fontSize) * 0.5f;
  const ColorRgba labelColor =
    isSelected ? UiTheme::textPrimary() : UiTheme::textMuted();
  const ColorRgba valueColor =
    isSelected ? UiTheme::accent() : UiTheme::textPrimary();

  drawLabelValue(visual,
                 label,
                 value,
                 x + 8.0f,
                 textY,
                 w - 16.0f,
                 fontSize,
                 labelColor,
                 valueColor);

  drawDivider(
    visual, x, y + h, w, false, UiTheme::applyOpacity(UiTheme::divider(), 100));
}

void
GuiKit::drawToggle(GameVisual& visual,
                   float x,
                   float y,
                   float w,
                   float h,
                   bool enabled,
                   float fontSize,
                   bool isSelected)
{
  const float trackH = std::min(h * 0.7f, 20.0f);
  const float trackW = trackH * 2.0f;
  const float trackX = x + w - trackW - 8.0f;
  const float trackY = y + (h - trackH) * 0.5f;

  const ColorRgba trackBg = enabled ? UiTheme::accent() : UiTheme::panelInset();
  visual.addFilledRect(trackX, trackY, trackW, trackH, trackBg);
  visual.addOutlineRect(
    trackX, trackY, trackW, trackH, UiTheme::panelBorder(), 1.0f);

  const float knobSize = trackH - 4.0f;
  const float knobX =
    enabled ? (trackX + trackW - knobSize - 2.0f) : (trackX + 2.0f);
  const float knobY = trackY + 2.0f;
  visual.addFilledRect(
    knobX, knobY, knobSize, knobSize, UiTheme::textPrimary());
}

void
GuiKit::drawSlider(GameVisual& visual,
                   float x,
                   float y,
                   float w,
                   float h,
                   float fraction,
                   const std::string& valueText,
                   float fontSize,
                   bool isSelected)
{
  const float clampedFraction = std::clamp(fraction, 0.0f, 1.0f);
  const float trackW = w * 0.45f;
  const float trackH = 6.0f;
  const float trackX = x + w - trackW - 8.0f;
  const float trackY = y + (h - trackH) * 0.5f;

  // Track background
  visual.addFilledRect(trackX, trackY, trackW, trackH, UiTheme::panelInset());
  visual.addOutlineRect(
    trackX, trackY, trackW, trackH, UiTheme::panelBorder(), 1.0f);

  // Active track
  if (clampedFraction > 0.0f) {
    visual.addFilledRect(
      trackX, trackY, trackW * clampedFraction, trackH, UiTheme::accent());
  }

  // Thumb
  const float thumbW = 8.0f;
  const float thumbH = 16.0f;
  const float thumbX = trackX + trackW * clampedFraction - thumbW * 0.5f;
  const float thumbY = y + (h - thumbH) * 0.5f;
  visual.addFilledRect(thumbX, thumbY, thumbW, thumbH, UiTheme::textPrimary());

  // Value text next to track
  if (!valueText.empty()) {
    const float valW = estimateTextWidth(valueText, fontSize);
    const float valX = trackX - valW - 8.0f;
    const float valY = y + (h - fontSize) * 0.5f;
    visual.addText(valueText, valX, valY, fontSize, UiTheme::textPrimary());
  }
}

void
GuiKit::drawNumericStepper(GameVisual& visual,
                           float x,
                           float y,
                           float w,
                           float h,
                           const std::string& valueText,
                           float fontSize,
                           bool isSelected)
{
  const float boxW = 80.0f;
  const float boxH = std::min(h * 0.8f, 24.0f);
  const float boxX = x + w - boxW - 8.0f;
  const float boxY = y + (h - boxH) * 0.5f;

  visual.addFilledRect(boxX, boxY, boxW, boxH, UiTheme::panelInset());
  visual.addOutlineRect(boxX,
                        boxY,
                        boxW,
                        boxH,
                        isSelected ? UiTheme::accent() : UiTheme::panelBorder(),
                        1.0f);

  drawTextCentered(visual,
                   valueText,
                   boxX + boxW * 0.5f,
                   boxY + boxH * 0.5f,
                   fontSize,
                   UiTheme::textPrimary());
}

void
GuiKit::drawMenuBar(GameVisual& visual,
                    float x,
                    float y,
                    float w,
                    float h,
                    ColorRgba surfaceColor)
{
  visual.addFilledRect(x, y, w, h, surfaceColor);
  visual.addLine(x, y + h, x + w, y + h, UiTheme::divider(), 1.0f);
}

void
GuiKit::drawMenuItem(GameVisual& visual,
                     float x,
                     float y,
                     float w,
                     float h,
                     const std::string& label,
                     const std::string& shortcut,
                     float fontSize,
                     bool isHovered,
                     bool isSelected)
{
  if (isHovered || isSelected) {
    visual.addFilledRect(x, y, w, h, UiTheme::selection());
    visual.addFilledRect(x, y, 2.0f, h, UiTheme::accent());
  }

  const float textY = y + (h - fontSize) * 0.5f;
  visual.addText(label, x + 8.0f, textY, fontSize, UiTheme::textPrimary());

  if (!shortcut.empty()) {
    const float scW = estimateTextWidth(shortcut, fontSize * 0.9f);
    const float scX = x + w - scW - 8.0f;
    visual.addText(shortcut, scX, textY, fontSize * 0.9f, UiTheme::textMuted());
  }
}

void
GuiKit::drawStatusBar(GameVisual& visual,
                      float x,
                      float y,
                      float w,
                      float h,
                      const std::string& statusText,
                      float fontSize,
                      ColorRgba textColor)
{
  visual.addFilledRect(x, y, w, h, UiTheme::panelSurface());
  visual.addLine(x, y, x + w, y, UiTheme::divider(), 1.0f);
  const float textY = y + (h - fontSize) * 0.5f;
  visual.addText(statusText, x + 8.0f, textY, fontSize, textColor);
}

void
GuiKit::drawToast(GameVisual& visual,
                  float x,
                  float y,
                  float w,
                  float h,
                  const std::string& message,
                  float fontSize,
                  ColorRgba accentColor,
                  float progress)
{
  const float t = std::clamp(progress, 0.0f, 1.0f);
  const unsigned char alpha = static_cast<unsigned char>(255.0f * t);

  GuiPanelChrome chrome;
  chrome.background = UiTheme::applyOpacity(UiTheme::panelSurface(), alpha);
  chrome.border = UiTheme::applyOpacity(UiTheme::panelBorder(), alpha);
  chrome.accent = UiTheme::applyOpacity(accentColor, alpha);
  chrome.shadow = ColorRgba{ 0, 0, 0, static_cast<unsigned char>(140.0f * t) };
  chrome.shadowOffset = 3.0f;
  chrome.accentWidth = 3.0f;
  chrome.borderWidth = 1.0f;
  chrome.drawShadow = true;
  chrome.drawAccent = true;
  drawPanel(visual, x, y, w, h, chrome);

  const float textY = y + (h - fontSize) * 0.5f;
  visual.addText(message,
                 x + 12.0f,
                 textY,
                 fontSize,
                 UiTheme::applyOpacity(UiTheme::textPrimary(), alpha));
}

void
GuiKit::drawTreeRow(GameVisual& visual,
                    float x,
                    float y,
                    float w,
                    float h,
                    int depth,
                    const std::string& label,
                    bool isSelected,
                    bool isHovered,
                    bool isDropTarget,
                    TextureHandle atlas,
                    const TextureRegion& icon)
{
  if (isSelected) {
    drawSelectionHighlight(visual, x, y, w, h, UiTheme::selection(), 1.0f);
  } else if (isHovered) {
    visual.addFilledRect(
      x, y, w, h, UiTheme::applyOpacity(UiTheme::panelRaised(), 160));
  }

  if (isDropTarget) {
    visual.addOutlineRect(x, y, w, h, UiTheme::accent(), 2.0f);
  }

  const float indent = static_cast<float>(depth) * 16.0f;
  float contentX = x + 8.0f + indent;
  const float centerY = y + h * 0.5f;

  if (icon.u1 > icon.u0 || icon.v1 > icon.v0) {
    const float iconSize = 14.0f;
    const float iconY = centerY - iconSize * 0.5f;
    visual.addSprite(atlas, Rect2{ contentX, iconY, iconSize, iconSize }, icon);
    contentX += iconSize + 6.0f;
  }

  const float textY = y + (h - 13.0f) * 0.5f;
  visual.addText(label,
                 contentX,
                 textY,
                 13.0f,
                 isSelected ? UiTheme::textPrimary() : UiTheme::textMuted());
}

static float
measureText(const std::string& text, float sizePt)
{
  if (text.empty()) {
    return 0.0f;
  }
  std::shared_ptr<Font> font = Font::getDefaultFont();
  if (font == nullptr) {
    return GuiKit::estimateTextWidth(text, sizePt);
  }
  return font->measureText(text, sizePt).width;
}

void
GuiKit::drawTextField(GameVisual& visual,
                      float x,
                      float y,
                      float w,
                      float h,
                      const std::string& value,
                      float fontSize,
                      const GuiTextEdit* edit,
                      bool invalid,
                      bool hovered,
                      bool readOnly)
{
  const bool focused = edit != nullptr && edit->active();
  const ColorRgba background = readOnly  ? ColorRgba{ 14, 20, 30, 255 }
                               : focused ? ColorRgba{ 10, 16, 26, 255 }
                                         : ColorRgba{ 18, 26, 40, 255 };
  visual.addFilledRect(x, y, w, h, background);
  if (!readOnly) {
    const ColorRgba border = invalid   ? ColorRgba{ 240, 70, 80, 255 }
                             : focused ? ColorRgba{ 66, 214, 210, 255 }
                             : hovered ? ColorRgba{ 66, 120, 180, 255 }
                                       : ColorRgba{ 44, 62, 86, 255 };
    visual.addOutlineRect(x, y, w, h, border, focused ? 1.5f : 1.0f);
  }
  const float padding = 4.0f;
  const float textY = y + std::max(0.0f, std::round((h - fontSize) * 0.5f));
  const float room = std::max(1.0f, w - padding * 2.0f);
  const ColorRgba textColor =
    readOnly ? UiTheme::textMuted() : UiTheme::textPrimary();
  if (!focused) {
    std::string shown = value;
    if (measureText(shown, fontSize) > room) {
      while (!shown.empty() && measureText(shown + "...", fontSize) > room) {
        shown.pop_back();
        // Never leave half of a multi-byte sequence.
        while (!shown.empty() &&
               (static_cast<unsigned char>(shown.back()) & 0xc0u) == 0x80u) {
          shown.pop_back();
        }
        if (!shown.empty() &&
            static_cast<unsigned char>(shown.back()) >= 0xc0u) {
          shown.pop_back();
        }
      }
      shown += "...";
    }
    visual.addText(shown, x + padding, textY, fontSize, textColor);
    return;
  }
  const std::string& text = edit->text();
  // Scroll so the caret stays inside the field.
  const float caretX = measureText(text.substr(0, edit->caret()), fontSize);
  const float offset = std::max(0.0f, caretX - room + 2.0f);
  const float originX = x + padding - offset;
  if (edit->hasSelection()) {
    const float start =
      measureText(text.substr(0, edit->selectionStart()), fontSize);
    const float end =
      measureText(text.substr(0, edit->selectionEnd()), fontSize);
    const float left = std::max(x + 1.0f, originX + start);
    const float right = std::min(x + w - 1.0f, originX + end);
    if (right > left) {
      visual.addFilledRect(
        left, y + 2.0f, right - left, h - 4.0f, ColorRgba{ 40, 90, 140, 255 });
    }
  }
  visual.addText(text, originX, textY, fontSize, textColor);
  if (edit->caretVisible()) {
    const float caretScreenX = originX + caretX;
    visual.addLine(caretScreenX,
                   y + 3.0f,
                   caretScreenX,
                   y + h - 3.0f,
                   ColorRgba{ 230, 240, 250, 255 },
                   1.0f);
  }
}