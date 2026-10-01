#include <Illumo/Rendering/Font.h>

const GlyphInfo*
Font::getGlyph(char32_t codepoint) const
{
  std::unordered_map<char32_t, GlyphInfo>::const_iterator it =
    glyphs.find(codepoint);
  if (it != glyphs.end()) {
    return &it->second;
  }
  // Fallback to '?'
  it = glyphs.find(static_cast<char32_t>('?'));
  if (it != glyphs.end()) {
    return &it->second;
  }
  return nullptr;
}

TextBounds
Font::measureText(const std::string& text, float sizePt) const
{
  return measureTextRange(text.data(), text.size(), sizePt);
}

TextBounds
Font::measureTextRange(const char* text, size_t length, float sizePt) const
{
  TextBounds bounds;
  if (text == nullptr || length == 0) {
    return bounds;
  }

  const float scale =
    metrics.pixelSize > 0.0f ? (sizePt / metrics.pixelSize) : 1.0f;
  float currentX = 0.0f;
  float maxX = 0.0f;
  int lineCount = 1;

  for (size_t i = 0; i < length; ++i) {
    char c = text[i];
    if (c == '\n') {
      if (currentX > maxX) {
        maxX = currentX;
      }
      currentX = 0.0f;
      ++lineCount;
      continue;
    }
    const GlyphInfo* g = getGlyph(static_cast<unsigned char>(c));
    if (g != nullptr) {
      currentX += g->advanceX * scale;
    }
  }

  if (currentX > maxX) {
    maxX = currentX;
  }

  bounds.width = maxX;
  bounds.height = static_cast<float>(lineCount) * getLineHeight(sizePt);
  return bounds;
}

float
Font::getAdvance(char32_t codepoint, float sizePt) const
{
  const GlyphInfo* g = getGlyph(codepoint);
  const float scale =
    metrics.pixelSize > 0.0f ? (sizePt / metrics.pixelSize) : 1.0f;
  return g != nullptr ? (g->advanceX * scale) : 0.0f;
}

float
Font::getLineHeight(float sizePt) const
{
  const float scale =
    metrics.pixelSize > 0.0f ? (sizePt / metrics.pixelSize) : 1.0f;
  return metrics.lineHeight * scale;
}

float
Font::getAscender(float sizePt) const
{
  const float scale =
    metrics.pixelSize > 0.0f ? (sizePt / metrics.pixelSize) : 1.0f;
  return metrics.ascender * scale;
}

float
Font::getDescender(float sizePt) const
{
  const float scale =
    metrics.pixelSize > 0.0f ? (sizePt / metrics.pixelSize) : 1.0f;
  return metrics.descender * scale;
}
