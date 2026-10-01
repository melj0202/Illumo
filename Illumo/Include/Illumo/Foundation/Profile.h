#ifndef ILLUMO_FOUNDATION_PROFILE_H
#define ILLUMO_FOUNDATION_PROFILE_H

// Profiling markers shared by the host, the engine sources compiled into WASM
// guests, and product code. Natively they are Tracy zones, plots and frame
// marks when TRACY_ENABLE is defined. Inside a guest built with
// ILLUMO_GUEST_PROFILE they call the host's `illumo_profile` imports, which
// emit the same Tracy events on the thread running the guest (see
// docs/tracy-profiling.md). Otherwise every marker compiles to nothing.
//
// Names must be string literals. Zones are scoped to the enclosing block;
// keep them around meaningful work, not per-cell or per-vertex loops.

#define ILLUMO_PROFILE_CONCAT_INNER(left, right) left##right
#define ILLUMO_PROFILE_CONCAT(left, right)                                     \
  ILLUMO_PROFILE_CONCAT_INNER(left, right)

#if defined(__wasm__) && defined(ILLUMO_GUEST_PROFILE)

#include <cstdint>

// Site kinds for illumo_profile_register. They are part of the guest ABI.
#define ILLUMO_PROFILE_SITE_ZONE 0
#define ILLUMO_PROFILE_SITE_PLOT 1
#define ILLUMO_PROFILE_SITE_FRAME_MARK 2

extern "C"
{
  // Registers one static marker site and returns its id, or -1 when the host
  // records nothing (no profiler compiled in, or a bound was reached).
  __attribute__((import_module("illumo_profile"),
                 import_name("register"))) std::int32_t
  illumo_profile_register(std::int32_t kind,
                          const char* name,
                          std::int32_t nameLength,
                          const char* function,
                          std::int32_t functionLength,
                          const char* file,
                          std::int32_t fileLength,
                          std::int32_t line);
  __attribute__((import_module("illumo_profile"),
                 import_name("zone_begin"))) void
  illumo_profile_zone_begin(std::int32_t site);
  __attribute__((import_module("illumo_profile"), import_name("zone_end"))) void
  illumo_profile_zone_end();
  __attribute__((import_module("illumo_profile"), import_name("plot"))) void
  illumo_profile_plot(std::int32_t site, double value);
  __attribute__((import_module("illumo_profile"),
                 import_name("frame_mark"))) void
  illumo_profile_frame_mark(std::int32_t site);
}

constexpr std::int32_t
illumoProfileLength(const char* text)
{
  std::int32_t length = 0;
  while (text[length] != '\0') {
    ++length;
  }
  return length;
}

inline std::int32_t
illumoProfileRegister(std::int32_t kind,
                      const char* name,
                      const char* function,
                      const char* file,
                      std::int32_t line)
{
  return illumo_profile_register(kind,
                                 name,
                                 illumoProfileLength(name),
                                 function,
                                 illumoProfileLength(function),
                                 file,
                                 illumoProfileLength(file),
                                 line);
}

// Begins the zone on construction and ends it on destruction. A site the
// host refused (-1) records nothing and makes no further import calls.
class IllumoProfileZone
{
public:
  explicit IllumoProfileZone(std::int32_t site)
    : m_site(site)
  {
    if (m_site >= 0) {
      illumo_profile_zone_begin(m_site);
    }
  }
  ~IllumoProfileZone()
  {
    if (m_site >= 0) {
      illumo_profile_zone_end();
    }
  }
  IllumoProfileZone(const IllumoProfileZone&) = delete;
  IllumoProfileZone& operator=(const IllumoProfileZone&) = delete;
  IllumoProfileZone(IllumoProfileZone&&) = delete;
  IllumoProfileZone& operator=(IllumoProfileZone&&) = delete;

private:
  std::int32_t m_site;
};

#define ILLUMO_PROFILE_ZONE(name)                                              \
  static const std::int32_t ILLUMO_PROFILE_CONCAT(illumoProfileSite,           \
                                                  __LINE__) =                  \
    illumoProfileRegister(                                                     \
      ILLUMO_PROFILE_SITE_ZONE, name, __func__, __FILE__, __LINE__);           \
  const IllumoProfileZone ILLUMO_PROFILE_CONCAT(illumoProfileZone, __LINE__)(  \
    ILLUMO_PROFILE_CONCAT(illumoProfileSite, __LINE__))

#define ILLUMO_PROFILE_PLOT(name, value)                                       \
  do {                                                                         \
    static const std::int32_t illumoProfilePlotSite = illumoProfileRegister(   \
      ILLUMO_PROFILE_SITE_PLOT, name, __func__, __FILE__, __LINE__);           \
    if (illumoProfilePlotSite >= 0) {                                          \
      illumo_profile_plot(illumoProfilePlotSite, static_cast<double>(value));  \
    }                                                                          \
  } while (false)

#define ILLUMO_PROFILE_FRAME_MARK(name)                                        \
  do {                                                                         \
    static const std::int32_t illumoProfileMarkSite = illumoProfileRegister(   \
      ILLUMO_PROFILE_SITE_FRAME_MARK, name, __func__, __FILE__, __LINE__);     \
    if (illumoProfileMarkSite >= 0) {                                          \
      illumo_profile_frame_mark(illumoProfileMarkSite);                        \
    }                                                                          \
  } while (false)

#define ILLUMO_PROFILE_THREAD(name)                                            \
  do {                                                                         \
  } while (false)

#elif !defined(__wasm__) && defined(TRACY_ENABLE) && __has_include(<tracy/Tracy.hpp>)

#include <tracy/Tracy.hpp>

// Named per line (as in guests) so two zones may share a scope; the later one
// nests in the earlier until the scope ends.
#define ILLUMO_PROFILE_ZONE(name)                                              \
  ZoneNamedN(ILLUMO_PROFILE_CONCAT(illumoProfileZone, __LINE__), name, true)
#define ILLUMO_PROFILE_PLOT(name, value)                                       \
  TracyPlot(name, static_cast<double>(value))
#define ILLUMO_PROFILE_FRAME_MARK(name) FrameMarkNamed(name)
#define ILLUMO_PROFILE_THREAD(name) tracy::SetThreadName(name)

#else

#define ILLUMO_PROFILE_ZONE(name)                                              \
  do {                                                                         \
  } while (false)
#define ILLUMO_PROFILE_PLOT(name, value)                                       \
  do {                                                                         \
    (void)sizeof(value);                                                       \
  } while (false)
#define ILLUMO_PROFILE_FRAME_MARK(name)                                        \
  do {                                                                         \
  } while (false)
#define ILLUMO_PROFILE_THREAD(name)                                            \
  do {                                                                         \
  } while (false)

#endif

#endif
