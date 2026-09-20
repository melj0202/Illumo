#pragma once
#include <IllumoGuest/Services.h>

struct GuestDisplayState
{
  bool fullscreen = false;
  bool vsync = true;
  std::uint32_t fps = 60;
  std::uint32_t uiScale = 1;
  bool operator==(const GuestDisplayState&) const = default;
  void write(GuestWireWriter& writer) const
  {
    writer.u32(fullscreen ? 1 : 0);
    writer.u32(vsync ? 1 : 0);
    writer.u32(fps);
    writer.u32(uiScale);
  }
  static bool read(GuestWireReader& reader, GuestDisplayState& state)
  {
    const std::uint32_t fullscreen = reader.u32();
    const std::uint32_t vsync = reader.u32();
    state = { fullscreen != 0, vsync != 0, reader.u32(), reader.u32() };
    return reader.valid() && fullscreen <= 1 && vsync <= 1 &&
           state.fps <= 1000 && state.uiScale >= 1 && state.uiScale <= 4;
  }
};

struct GuestDisplayRequest
{
  bool apply = false;
  GuestDisplayState state;
  void write(GuestWireWriter& writer) const
  {
    writer.u32(1);
    writer.u32(apply ? 1 : 0);
    state.write(writer);
  }
  static bool read(std::span<const std::byte> bytes,
                   GuestDisplayRequest& output)
  {
    GuestWireReader reader(bytes);
    const std::uint32_t version = reader.u32();
    const std::uint32_t apply = reader.u32();
    GuestDisplayRequest candidate;
    candidate.apply = apply != 0;
    if (!GuestDisplayState::read(reader, candidate.state) ||
        !reader.finished() || version != 1 || apply > 1 ||
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
