#include <IllumoGuest/FontProvider.h>
#include <stdexcept>

static GuestFontProvider* activeProvider = nullptr;
static std::shared_ptr<Font> defaultFont;
const float Font::kDefaultPixelSize = 32;
Font::Font() = default;
Font::~Font() = default;

GuestFontProvider::GuestFontProvider(GuestServiceQueue& services,
                                     GuestRecordingBackend& backend)
  : m_services(services)
  , m_backend(backend)
{
  if (activeProvider != nullptr) {
    throw std::logic_error("Only one font provider per control store");
  }
  activeProvider = this;
}
GuestFontProvider::~GuestFontProvider()
{
  defaultFont.reset();
  activeProvider = nullptr;
}
GuestFontProvider*
GuestFontProvider::active()
{
  return activeProvider;
}
std::shared_ptr<Font>
GuestFontProvider::acquire(const std::string& name, float pixelSize)
{
  const std::string key = name + "@" + std::to_string(pixelSize);
  const std::map<std::string, Entry>::const_iterator existing =
    m_fonts.find(key);
  if (existing != m_fonts.end()) {
    return existing->second.font;
  }
  if (m_fonts.size() + m_retired.size() >= 32 || !std::isfinite(pixelSize) ||
      pixelSize < 8 || pixelSize > 256) {
    return nullptr;
  }
  GuestWireWriter request;
  GuestFontRequest{ name, pixelSize }.write(request);
  const std::uint64_t id =
    m_services.enqueue(GuestService::LoadFont, request.take());
  if (id == 0) {
    return nullptr;
  }
  std::shared_ptr<Font> font = std::make_shared<Font>();
  font->sourcePath = name;
  font->metrics.pixelSize = pixelSize;
  m_fonts.emplace(key, Entry{ font, id });
  return font;
}
void
GuestFontProvider::pump()
{
  for (std::pair<const std::string, Entry>& pair : m_fonts) {
    complete(pair.second);
  }
  for (std::vector<Entry>::iterator it = m_retired.begin();
       it != m_retired.end();) {
    complete(*it);
    if (it->pending == 0 && it->font.use_count() == 1) {
      m_backend.DestroyTexture(it->font->textureHandle);
      it = m_retired.erase(it);
    } else {
      ++it;
    }
  }
}
void
GuestFontProvider::complete(Entry& entry)
{
  GuestServiceRecord completion;
  if (entry.pending == 0 || !m_services.take(entry.pending, completion)) {
    return;
  }
  GuestFont description;
  if (completion.status != GuestServiceStatus::Complete ||
      !GuestFont::read(completion.payload, description)) {
    throw std::runtime_error("Font resource acquisition failed");
  }
  Font& font = *entry.font;
  font.metrics = { description.metrics[0],
                   description.metrics[1],
                   description.metrics[2],
                   description.metrics[3],
                   description.metrics[4] };
  for (const GuestGlyph& glyph : description.glyphs) {
    const std::array<float, 9>& v = glyph.values;
    font.glyphs.emplace(static_cast<char32_t>(glyph.codepoint),
                        GlyphInfo{ static_cast<char32_t>(glyph.codepoint),
                                   v[0],
                                   v[1],
                                   v[2],
                                   v[3],
                                   v[4],
                                   v[5],
                                   v[6],
                                   v[7],
                                   v[8],
                                   glyph.visible });
  }
  font.textureHandle = m_backend.importTexture(description.atlas);
  if (!font.textureHandle.isValid()) {
    throw std::runtime_error("Font atlas enrollment failed");
  }
  font.valid = true;
  entry.pending = 0;
}
void
GuestFontProvider::clear()
{
  // Retain request ownership and borrowed Font objects through completion.
  for (std::pair<const std::string, Entry>& pair : m_fonts) {
    m_retired.push_back(std::move(pair.second));
  }
  m_fonts.clear();
  defaultFont.reset();
}
std::shared_ptr<Font>
Font::loadFromFile(const std::string& path, float pixelSize)
{
  return activeProvider ? activeProvider->acquire(path, pixelSize) : nullptr;
}
std::shared_ptr<Font>
Font::getDefaultFont()
{
  if (!defaultFont && activeProvider) {
    defaultFont = activeProvider->acquire("default", kDefaultPixelSize);
  }
  return defaultFont;
}
void
Font::setDefaultFont(std::shared_ptr<Font> font)
{
  defaultFont = std::move(font);
}
void
Font::clearCache()
{
  if (activeProvider) {
    activeProvider->clear();
  }
}
TextureHandle
Font::getTextureHandle(Renderer*)
{
  return textureHandle;
}
