#pragma once

#include <Illumo/Rendering/Font.h>
#include <IllumoGuest/RecordingBackend.h>

// Binds portable Font layout to asynchronous host rasterization. A pending Font
// has no glyphs/texture; pump installs one immutable completion before layout.
class GuestFontProvider
{
public:
  GuestFontProvider(GuestServiceQueue& services,
                    GuestRecordingBackend& backend);
  ~GuestFontProvider();
  GuestFontProvider(const GuestFontProvider&) = delete;
  GuestFontProvider& operator=(const GuestFontProvider&) = delete;
  GuestFontProvider(GuestFontProvider&&) = delete;
  GuestFontProvider& operator=(GuestFontProvider&&) = delete;
  std::shared_ptr<Font> acquire(const std::string& name, float pixelSize);
  void pump();
  void clear();
  static GuestFontProvider* active();

private:
  struct Entry
  {
    std::shared_ptr<Font> font;
    std::uint64_t pending = 0;
  };
  GuestServiceQueue& m_services;
  GuestRecordingBackend& m_backend;
  std::map<std::string, Entry> m_fonts;
  std::vector<Entry> m_retired;
  void complete(Entry& entry);
};
