#pragma once
#include <IllumoGuest/Services.h>
#include <algorithm>
#include <cmath>

// Display wire version. Version 2 appends hideSystemCursor to the state.
// Version 3 carries the UI scale in hundredths with 0 for automatic, where
// earlier versions carry a whole factor 1-4. Version 4 appends the MSAA
// preference (read when the host window is created) and the samples of the
// window now running. Hosts still accept versions 1-3 and answer each request
// in its own version.
inline constexpr std::uint32_t kGuestDisplayVersion = 4u;

struct GuestDisplayState
{
  // Version 3 bounds, in hundredths of the UI scale factor.
  static constexpr std::uint32_t kMinimumUiScaleHundredths = 100u;
  static constexpr std::uint32_t kMaximumUiScaleHundredths = 400u;

  bool fullscreen = false;
  bool vsync = true;
  std::uint32_t fps = 60;
  // UI scale factor 1-4 in hundredths steps, or 0 for automatic (the scale
  // follows the window size).
  float uiScale = 1.0f;
  // The product draws its own pointer, so the host hides the system cursor
  // over the main window. Runtime state, never persisted.
  bool hideSystemCursor = false;
  // Version 4: the MSAA sample count the host saves for its next window, and
  // (reports only) the count the running window was created with; that one
  // is 0xFFFFFFFF when unknown. Both are 0 or a power of two up to 16.
  std::uint32_t msaa = 4;
  std::uint32_t activeMsaa = kUnknownMsaa;
  bool operator==(const GuestDisplayState&) const = default;

  static constexpr std::uint32_t kUnknownMsaa = 0xFFFFFFFFu;
  static bool validMsaa(std::uint32_t samples)
  {
    return samples <= 16u && (samples & (samples - 1u)) == 0u;
  }

  static std::uint32_t uiScaleHundredths(float scale)
  {
    return scale > 0.0f && std::isfinite(scale)
             ? static_cast<std::uint32_t>(std::lround(scale * 100.0f))
             : 0u;
  }
  // The same factor after a version 3 round trip, so a guest's desired
  // value compares equal to the host's report of it.
  static float quantizedUiScale(float scale)
  {
    return static_cast<float>(uiScaleHundredths(scale)) / 100.0f;
  }

  void write(GuestWireWriter& writer,
             std::uint32_t version = kGuestDisplayVersion) const
  {
    writer.u32(fullscreen ? 1 : 0);
    writer.u32(vsync ? 1 : 0);
    writer.u32(fps);
    if (version >= 3u) {
      writer.u32(uiScaleHundredths(uiScale));
    } else {
      // Earlier versions only know whole factors; automatic reads as 1x.
      const long whole = uiScale > 0.0f ? std::lround(uiScale) : 1L;
      writer.u32(static_cast<std::uint32_t>(std::clamp(whole, 1L, 4L)));
    }
    if (version >= 2u) {
      writer.u32(hideSystemCursor ? 1 : 0);
    }
    if (version >= 4u) {
      writer.u32(msaa);
      writer.u32(activeMsaa);
    }
  }
  static bool read(GuestWireReader& reader,
                   GuestDisplayState& state,
                   std::uint32_t version = kGuestDisplayVersion)
  {
    const std::uint32_t fullscreen = reader.u32();
    const std::uint32_t vsync = reader.u32();
    const std::uint32_t fps = reader.u32();
    const std::uint32_t scale = reader.u32();
    std::uint32_t hideSystemCursor = 0u;
    if (version >= 2u) {
      hideSystemCursor = reader.u32();
    }
    std::uint32_t msaa = 4u;
    std::uint32_t activeMsaa = kUnknownMsaa;
    if (version >= 4u) {
      msaa = reader.u32();
      activeMsaa = reader.u32();
    }
    const bool scaleValid =
      version >= 3u ? scale == 0u || (scale >= kMinimumUiScaleHundredths &&
                                      scale <= kMaximumUiScaleHundredths)
                    : scale >= 1u && scale <= 4u;
    state = GuestDisplayState{};
    state.fullscreen = fullscreen != 0u;
    state.vsync = vsync != 0u;
    state.fps = fps;
    state.uiScale = version >= 3u ? static_cast<float>(scale) / 100.0f
                                  : static_cast<float>(scale);
    state.hideSystemCursor = hideSystemCursor != 0u;
    state.msaa = msaa;
    state.activeMsaa = activeMsaa;
    return reader.valid() && fullscreen <= 1 && vsync <= 1 &&
           hideSystemCursor <= 1 && fps <= 1000 && scaleValid &&
           validMsaa(msaa) &&
           (activeMsaa == kUnknownMsaa || validMsaa(activeMsaa));
  }
};

struct GuestDisplayRequest
{
  bool apply = false;
  GuestDisplayState state;
  std::uint32_t version = kGuestDisplayVersion;
  void write(GuestWireWriter& writer) const
  {
    writer.u32(version);
    writer.u32(apply ? 1 : 0);
    state.write(writer, version);
  }
  static bool read(std::span<const std::byte> bytes,
                   GuestDisplayRequest& output)
  {
    GuestWireReader reader(bytes);
    const std::uint32_t version = reader.u32();
    const std::uint32_t apply = reader.u32();
    GuestDisplayRequest candidate;
    candidate.apply = apply != 0;
    candidate.version = version;
    if (version < 1u || version > kGuestDisplayVersion ||
        !GuestDisplayState::read(reader, candidate.state, version) ||
        !reader.finished() || apply > 1 ||
        (!candidate.apply && candidate.state != GuestDisplayState{})) {
      return false;
    }
    output = candidate;
    return true;
  }
};

// Absolute requests are coalesced while an earlier change is pending. Native
// fullscreen toggles never get replayed from a stale guest-side assumption.
class GuestDisplay
{
public:
  explicit GuestDisplay(GuestServiceQueue& services)
    : m_services(services)
  {
  }
  GuestDisplay(const GuestDisplay&) = delete;
  GuestDisplay& operator=(const GuestDisplay&) = delete;
  GuestDisplay(GuestDisplay&&) = delete;
  GuestDisplay& operator=(GuestDisplay&&) = delete;
  void query()
  {
    m_next = GuestDisplayRequest{};
    m_pending = true;
  }
  void apply(GuestDisplayState state)
  {
    GuestWireWriter bytes;
    GuestDisplayRequest request{ true, state };
    request.write(bytes);
    GuestDisplayRequest checked;
    if (!GuestDisplayRequest::read(bytes.data(), checked)) {
      throw std::invalid_argument("Invalid display settings");
    }
    m_next = request;
    m_pending = true;
  }
  void pump();
  bool idle() const { return !m_pending && m_request == 0; }
  bool ready() const { return m_ready; }
  const GuestDisplayState& actual() const { return m_actual; }
  const std::string& error() const { return m_error; }

private:
  GuestServiceQueue& m_services;
  GuestDisplayRequest m_next;
  GuestDisplayState m_actual;
  std::uint64_t m_request = 0;
  bool m_pending = false;
  bool m_ready = false;
  std::string m_error;
};
