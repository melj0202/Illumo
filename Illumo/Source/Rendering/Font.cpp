#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/ITexture.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/Logger.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

const float Font::kDefaultPixelSize = 32.0f;

namespace {

static FT_Library s_ftLibrary = nullptr;
static std::mutex s_ftMutex;
static std::shared_ptr<Font> s_defaultFont = nullptr;
static std::unordered_map<std::string, std::shared_ptr<Font>> s_fontCache;
static std::mutex s_cacheMutex;

FT_Library
getFreeTypeLibrary()
{
  std::lock_guard<std::mutex> lock(s_ftMutex);
  if (s_ftLibrary == nullptr) {
    FT_Error err = FT_Init_FreeType(&s_ftLibrary);
    if (err != 0) {
      Logger::LogError("FreeType initialization failed with code: " +
                       std::to_string(err));
      return nullptr;
    }
  }
  return s_ftLibrary;
}

std::string
resolveFontPath(const std::string& requestedPath)
{
  if (requestedPath.empty()) {
    return "";
  }
  namespace fs = std::filesystem;
  std::error_code ec;
  if (fs::exists(requestedPath, ec)) {
    return requestedPath;
  }

  const std::vector<std::string> prefixes = {
    "",
    "Assets/Fonts/Handjet/static/",
    "Assets/Fonts/Handjet/",
    "../",
    "../../",
    "../../../",
    "Illumo/",
    "../Illumo/",
    "../../Illumo/",
  };

  for (const std::string& prefix : prefixes) {
    std::string candidate = prefix + requestedPath;
    if (fs::exists(candidate, ec)) {
      return candidate;
    }
  }

  return requestedPath;
}

} // namespace

Font::Font() = default;

Font::~Font() = default;

std::shared_ptr<Font>
Font::loadFromFile(const std::string& path, float pixelSize)
{
  std::string resolved = resolveFontPath(path);
  std::string cacheKey = resolved + "@" + std::to_string(pixelSize);
  {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    std::unordered_map<std::string, std::shared_ptr<Font>>::const_iterator it =
      s_fontCache.find(cacheKey);
    if (it != s_fontCache.end()) {
      return it->second;
    }
  }

  std::shared_ptr<Font> font = std::make_shared<Font>();
  if (!font->loadFile(resolved, pixelSize)) {
    font->buildFallbackAtlas(pixelSize);
  }

  {
    std::lock_guard<std::mutex> lock(s_cacheMutex);
    s_fontCache[cacheKey] = font;
  }
  return font;
}

std::shared_ptr<Font>
Font::loadFromMemory(const unsigned char* data, size_t size, float pixelSize)
{
  std::shared_ptr<Font> font = std::make_shared<Font>();
  if (!font->loadMemory(data, size, pixelSize)) {
    font->buildFallbackAtlas(pixelSize);
  }
  return font;
}

std::shared_ptr<Font>
Font::getDefaultFont()
{
  std::lock_guard<std::mutex> lock(s_cacheMutex);
  if (s_defaultFont != nullptr) {
    return s_defaultFont;
  }

  const std::vector<std::string> candidatePaths = {
    "Assets/Fonts/Space_Mono/SpaceMono-Bold.ttf",
    "Assets/Fonts/Space_Mono/SpaceMono-Regular.ttf",
    "../Assets/Fonts/Space_Mono/SpaceMono-Bold.ttf",
    "../Assets/Fonts/Space_Mono/SpaceMono-Regular.ttf",
    "../../Assets/Fonts/Space_Mono/SpaceMono-Bold.ttf",
    "../../Assets/Fonts/Space_Mono/SpaceMono-Regular.ttf",
    "Illumo/Assets/Fonts/Space_Mono/SpaceMono-Bold.ttf",
    "Illumo/Assets/Fonts/Space_Mono/SpaceMono-Regular.ttf",
    "../Illumo/Assets/Fonts/Space_Mono/SpaceMono-Bold.ttf",
    "../Illumo/Assets/Fonts/Space_Mono/SpaceMono-Regular.ttf",
    "Assets/Fonts/Handjet/static/Handjet-Bold.ttf",
    "Assets/Fonts/Handjet/static/Handjet-Regular.ttf",
  };

  for (const std::string& candidate : candidatePaths) {
    std::string resolved = resolveFontPath(candidate);
    std::error_code ec;
    if (std::filesystem::exists(resolved, ec)) {
      std::shared_ptr<Font> font = std::make_shared<Font>();
      if (font->loadFile(resolved, kDefaultPixelSize)) {
        s_defaultFont = font;
        return s_defaultFont;
      }
    }
  }

  // Fallback programmatic font
  s_defaultFont = std::make_shared<Font>();
  s_defaultFont->buildFallbackAtlas(kDefaultPixelSize);
  return s_defaultFont;
}

void
Font::setDefaultFont(std::shared_ptr<Font> font)
{
  std::lock_guard<std::mutex> lock(s_cacheMutex);
  s_defaultFont = font;
}

void
Font::clearCache()
{
  std::lock_guard<std::mutex> lock(s_cacheMutex);
  s_fontCache.clear();
  s_defaultFont.reset();
}

bool
Font::loadFile(const std::string& path, float pixelSize)
{
  sourcePath = path;
  FT_Library library = getFreeTypeLibrary();
  if (library == nullptr) {
    return false;
  }

  FT_Face face = nullptr;
  FT_Error err = FT_New_Face(library, path.c_str(), 0, &face);
  if (err != 0 || face == nullptr) {
    return false;
  }

  bool ok = rasterizeFace(face, pixelSize);
  FT_Done_Face(face);
  return ok;
}

bool
Font::loadMemory(const unsigned char* data, size_t size, float pixelSize)
{
  sourcePath = "<memory>";
  if (data == nullptr || size == 0) {
    return false;
  }

  FT_Library library = getFreeTypeLibrary();
  if (library == nullptr) {
    return false;
  }

  FT_Face face = nullptr;
  FT_Error err =
    FT_New_Memory_Face(library, data, static_cast<FT_Long>(size), 0, &face);
  if (err != 0 || face == nullptr) {
    return false;
  }

  bool ok = rasterizeFace(face, pixelSize);
  FT_Done_Face(face);
  return ok;
}

bool
Font::rasterizeFace(void* ftFace, float pixelSize)
{
  if (ftFace == nullptr) {
    return false;
  }
  FT_Face face = static_cast<FT_Face>(ftFace);

  FT_Error err = FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize));
  if (err != 0) {
    return false;
  }

  metrics.pixelSize = pixelSize;
  metrics.ascender = static_cast<float>(face->size->metrics.ascender >> 6);
  metrics.descender = static_cast<float>(face->size->metrics.descender >> 6);
  metrics.lineHeight = static_cast<float>(face->size->metrics.height >> 6);
  metrics.maxAdvance = static_cast<float>(face->size->metrics.max_advance >> 6);

  if (metrics.lineHeight <= 0.0f) {
    metrics.lineHeight = (metrics.ascender - metrics.descender > 0.0f)
                           ? (metrics.ascender - metrics.descender)
                           : (pixelSize * 1.25f);
  }
  if (metrics.ascender <= 0.0f) {
    metrics.ascender = pixelSize * 0.8f;
  }

  // Pre-load all printable ASCII characters (32 to 126) + question mark
  struct RenderedGlyph
  {
    char32_t codepoint = 0;
    int width = 0;
    int height = 0;
    int bearingX = 0;
    int bearingY = 0;
    int advanceX = 0;
    std::vector<unsigned char> bitmap;
  };

  std::vector<RenderedGlyph> renderedGlyphs;
  renderedGlyphs.reserve(96);

  for (char32_t cp = 32; cp <= 126; ++cp) {
    RenderedGlyph rg;
    rg.codepoint = cp;
    FT_Error loadErr = FT_Load_Char(face, cp, FT_LOAD_RENDER);
    if (loadErr == 0 && face->glyph != nullptr) {
      FT_GlyphSlot slot = face->glyph;
      rg.width = slot->bitmap.width;
      rg.height = slot->bitmap.rows;
      rg.bearingX = slot->bitmap_left;
      rg.bearingY = slot->bitmap_top;
      rg.advanceX = slot->advance.x >> 6;
      if (rg.width > 0 && rg.height > 0) {
        rg.bitmap.resize(static_cast<size_t>(rg.width * rg.height));
        for (int row = 0; row < rg.height; ++row) {
          std::memcpy(rg.bitmap.data() + row * rg.width,
                      slot->bitmap.buffer + row * slot->bitmap.pitch,
                      static_cast<size_t>(rg.width));
        }
      }
    } else {
      rg.advanceX = static_cast<int>(pixelSize * 0.5f);
    }
    renderedGlyphs.push_back(std::move(rg));
  }

  // Calibrate ascender to cap-height across standard uppercase glyphs
  // so text.y aligns to the top of standard capital letters.
  float capHeight = 0.0f;
  for (char32_t c : { U'H', U'M', U'A', U'E', U'X', U'0' }) {
    for (const RenderedGlyph& rg : renderedGlyphs) {
      if (rg.codepoint == c && static_cast<float>(rg.bearingY) > capHeight) {
        capHeight = static_cast<float>(rg.bearingY);
      }
    }
  }
  if (capHeight > 0.0f) {
    metrics.ascender = capHeight;
  }

  // Pack glyphs into atlas
  atlasWidth = 512;
  atlasHeight = 512;
  const int padding = 1;
  int cursorX = padding;
  int cursorY = padding;
  int rowHeight = 0;

  // Check if 512x512 is sufficient, otherwise enlarge
  int requiredArea = 0;
  for (const RenderedGlyph& rg : renderedGlyphs) {
    requiredArea += (rg.width + padding * 2) * (rg.height + padding * 2);
  }
  while (atlasWidth * atlasHeight < requiredArea * 2 && atlasWidth < 2048) {
    atlasWidth *= 2;
    atlasHeight *= 2;
  }

  atlasPixels.assign(static_cast<size_t>(atlasWidth * atlasHeight * 4), 0);
  glyphs.clear();

  for (const RenderedGlyph& rg : renderedGlyphs) {
    if (cursorX + rg.width + padding > atlasWidth) {
      cursorX = padding;
      cursorY += rowHeight + padding;
      rowHeight = 0;
    }

    if (cursorY + rg.height + padding > atlasHeight) {
      // Atlas overflow protection
      break;
    }

    if (rg.width > 0 && rg.height > 0) {
      for (int y = 0; y < rg.height; ++y) {
        for (int x = 0; x < rg.width; ++x) {
          unsigned char alpha = rg.bitmap[y * rg.width + x];
          size_t dst =
            (static_cast<size_t>(cursorY + y) * atlasWidth + (cursorX + x)) * 4;
          atlasPixels[dst + 0] = 255;
          atlasPixels[dst + 1] = 255;
          atlasPixels[dst + 2] = 255;
          atlasPixels[dst + 3] = alpha;
        }
      }
    }

    GlyphInfo gi;
    gi.codepoint = rg.codepoint;
    gi.u0 = static_cast<float>(cursorX) / static_cast<float>(atlasWidth);
    gi.v0 = static_cast<float>(cursorY) / static_cast<float>(atlasHeight);
    gi.u1 =
      static_cast<float>(cursorX + rg.width) / static_cast<float>(atlasWidth);
    gi.v1 =
      static_cast<float>(cursorY + rg.height) / static_cast<float>(atlasHeight);
    gi.width = static_cast<float>(rg.width);
    gi.height = static_cast<float>(rg.height);
    gi.bearingX = static_cast<float>(rg.bearingX);
    gi.bearingY = static_cast<float>(rg.bearingY);
    gi.advanceX = static_cast<float>(rg.advanceX);
    gi.visible = (rg.codepoint != 32 && rg.width > 0 && rg.height > 0);

    glyphs[rg.codepoint] = gi;

    cursorX += rg.width + padding * 2;
    rowHeight = std::max(rowHeight, rg.height);
  }

  valid = !glyphs.empty();
  return valid;
}

void
Font::buildFallbackAtlas(float pixelSize)
{
  atlasWidth = 256;
  atlasHeight = 256;
  atlasPixels.assign(static_cast<size_t>(atlasWidth * atlasHeight * 4), 0);
  glyphs.clear();

  metrics.pixelSize = pixelSize;
  metrics.ascender = pixelSize * 0.8f;
  metrics.descender = -pixelSize * 0.2f;
  metrics.lineHeight = pixelSize * 1.25f;
  metrics.maxAdvance = pixelSize * 0.6f;

  const int charW = static_cast<int>(pixelSize * 0.5f);
  const int charH = static_cast<int>(pixelSize * 0.7f);
  const int padding = 2;
  int cursorX = padding;
  int cursorY = padding;
  int rowHeight = charH;

  for (char32_t cp = 32; cp <= 126; ++cp) {
    if (cursorX + charW + padding > atlasWidth) {
      cursorX = padding;
      cursorY += rowHeight + padding;
    }
    if (cursorY + charH + padding > atlasHeight) {
      break;
    }

    if (cp != 32) {
      // Fill a simple rectangle for fallback glyphs
      for (int y = 0; y < charH; ++y) {
        for (int x = 0; x < charW; ++x) {
          const bool edge =
            (x == 0 || x == charW - 1 || y == 0 || y == charH - 1);
          unsigned char alpha = edge ? 255 : 180;
          size_t dst =
            (static_cast<size_t>(cursorY + y) * atlasWidth + (cursorX + x)) * 4;
          atlasPixels[dst + 0] = 255;
          atlasPixels[dst + 1] = 255;
          atlasPixels[dst + 2] = 255;
          atlasPixels[dst + 3] = alpha;
        }
      }
    }

    GlyphInfo gi;
    gi.codepoint = cp;
    gi.u0 = static_cast<float>(cursorX) / static_cast<float>(atlasWidth);
    gi.v0 = static_cast<float>(cursorY) / static_cast<float>(atlasHeight);
    gi.u1 =
      static_cast<float>(cursorX + charW) / static_cast<float>(atlasWidth);
    gi.v1 =
      static_cast<float>(cursorY + charH) / static_cast<float>(atlasHeight);
    gi.width = static_cast<float>(charW);
    gi.height = static_cast<float>(charH);
    gi.bearingX = 0.0f;
    gi.bearingY = static_cast<float>(charH);
    gi.advanceX = static_cast<float>(charW + 2);
    gi.visible = (cp != 32);

    glyphs[cp] = gi;
    cursorX += charW + padding;
  }

  valid = true;
}

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

TextureHandle
Font::getTextureHandle(Renderer* renderer)
{
  if (renderer == nullptr) {
    return textureHandle;
  }
  if (enrolledRenderer != renderer || !textureHandle.isValid()) {
    TextureOptions options;
    options.filter = TextureFilter::Linear;
    options.wrapX = TextureWrap::ClampToEdge;
    options.wrapY = TextureWrap::ClampToEdge;
    textureHandle = renderer->enrollTexture(
      atlasPixels.data(), atlasWidth, atlasHeight, 4, options);
    enrolledRenderer = renderer;
  }
  return textureHandle;
}
