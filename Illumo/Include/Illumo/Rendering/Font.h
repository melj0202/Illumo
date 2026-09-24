#pragma once

#include <Illumo/Rendering/ResourceHandle.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Renderer;

struct GlyphInfo
{
  char32_t codepoint = 0;
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 0.0f;
  float v1 = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float bearingX = 0.0f;
  float bearingY = 0.0f;
  float advanceX = 0.0f;
  bool visible = true;
};

struct FontMetrics
{
  float pixelSize = 32.0f;
  float ascender = 24.0f;
  float descender = -8.0f;
  float lineHeight = 32.0f;
  float maxAdvance = 20.0f;
};

struct TextBounds
{
  float width = 0.0f;
  float height = 0.0f;
  float minX = 0.0f;
  float minY = 0.0f;
};

// How loadFile samples a face. A variable face is instanced at `weight` on its
// 'wght' axis (clamped to the axis; 0 keeps the default instance, and static
// faces ignore it). Codepoints the face lacks are rasterized from
// `fallbackPath` when given. `glyphs` restricts the atlas to those printable
// ASCII characters plus space and '?'; empty rasterizes all of 32..126.
struct FontFaceOptions
{
  float weight = 0.0f;
  std::string fallbackPath;
  std::string glyphs;
};

// TrueType/OpenType font rasterizer and atlas holder backed by FreeType 2.
class Font : public std::enable_shared_from_this<Font>
{
public:
  static const float kDefaultPixelSize;

  Font();
  ~Font();

  Font(const Font&) = delete;
  Font& operator=(const Font&) = delete;
  Font(Font&&) noexcept = default;
  Font& operator=(Font&&) noexcept = default;

  static std::shared_ptr<Font> loadFromFile(
    const std::string& path,
    float pixelSize = kDefaultPixelSize);
  static std::shared_ptr<Font> loadFromMemory(
    const unsigned char* data,
    size_t size,
    float pixelSize = kDefaultPixelSize);

  static std::shared_ptr<Font> getDefaultFont();
  static std::shared_ptr<Font> createFallback(
    float pixelSize = kDefaultPixelSize);
  static void setDefaultFont(std::shared_ptr<Font> font);
  static void clearCache();

  bool loadFile(const std::string& path, float pixelSize = kDefaultPixelSize);
  bool loadFile(const std::string& path,
                float pixelSize,
                const FontFaceOptions& options);
  bool loadMemory(const unsigned char* data,
                  size_t size,
                  float pixelSize = kDefaultPixelSize);

  const GlyphInfo* getGlyph(char32_t codepoint) const;
  const FontMetrics& getMetrics() const { return metrics; }

  TextBounds measureText(const std::string& text, float sizePt) const;
  TextBounds measureTextRange(const char* text,
                              size_t length,
                              float sizePt) const;
  float getAdvance(char32_t codepoint, float sizePt) const;
  float getLineHeight(float sizePt) const;
  float getAscender(float sizePt) const;
  float getDescender(float sizePt) const;

  TextureHandle getTextureHandle(Renderer* renderer);
  const std::vector<unsigned char>& getAtlasPixels() const
  {
    return atlasPixels;
  }
  int getAtlasWidth() const { return atlasWidth; }
  int getAtlasHeight() const { return atlasHeight; }
  bool isValid() const { return valid; }
  const std::string& getPath() const { return sourcePath; }

private:
  friend class GuestFontProvider;
  std::string sourcePath;
  FontMetrics metrics;
  std::unordered_map<char32_t, GlyphInfo> glyphs;
  std::vector<unsigned char> atlasPixels;
  int atlasWidth = 0;
  int atlasHeight = 0;
  bool valid = false;

  std::map<std::weak_ptr<const void>,
           TextureHandle,
           std::owner_less<std::weak_ptr<const void>>>
    rendererTextures;
  std::weak_ptr<const void> lastRendererLifetime;
  TextureHandle textureHandle{};

  void buildFallbackAtlas(float pixelSize);
  bool rasterizeFace(void* ftFace,
                     void* ftFallbackFace,
                     float pixelSize,
                     const std::string& glyphSet);
};
