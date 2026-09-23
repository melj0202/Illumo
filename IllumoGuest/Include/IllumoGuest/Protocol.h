#pragma once

#include <IllumoGuest/Wire.h>

// ABI 1 uses little-endian fields, never the in-memory layout of these types.
// Every call carries a host-assigned session and strictly increasing sequence.
enum class GuestRole : std::uint32_t
{
  Game = 1,
  Mod = 2,
  Worker = 3
};

enum class GuestCapability : std::uint32_t
{
  Render = 1u << 0u,
  Assets = 1u << 1u,
  Storage = 1u << 2u,
  SelectedFiles = 1u << 3u,
  Clipboard = 1u << 4u,
  Console = 1u << 5u,
  Display = 1u << 6u,
  Jobs = 1u << 7u,
  Messages = 1u << 8u
};

enum class GuestCall : std::uint32_t
{
  Init = 1,
  Update = 2,
  Frame = 3,
  Close = 4,
  Shutdown = 5,
  Receive = 6,
  Services = 7
};

// Update response flags (ABI 1). The host rejects unknown bits. A close
// request asks the host to begin its normal close sequence, which still calls
// illumo_guest_close before the store is shut down.
struct GuestUpdateFlags
{
  static constexpr std::uint32_t RequestClose = 1u;
  // The update queued service requests; the host runs one more services
  // exchange before the frame so work (such as compute lanes) starts now.
  static constexpr std::uint32_t ServicesPending = 2u;
  static constexpr std::uint32_t Known = RequestClose | ServicesPending;
};

struct GuestEnvelope
{
  static constexpr std::uint32_t Magic = 0x31474c49u; // ILG1
  static constexpr std::uint32_t Version = 1;
  static constexpr std::uint32_t KnownCapabilities = (1u << 9u) - 1u;
  static constexpr std::size_t HeaderBytes = 32;

  GuestCall call = GuestCall::Init;
  std::uint64_t session = 0;
  std::uint64_t sequence = 0;
  std::span<const std::byte> payload;

  void write(GuestWireWriter& output) const
  {
    if (payload.size() > UINT32_MAX) {
      throw std::length_error("Guest message exceeds ABI range");
    }
    output.u32(Magic);
    output.u32(Version);
    output.u32(static_cast<std::uint32_t>(call));
    output.u32(static_cast<std::uint32_t>(payload.size()));
    output.u64(session);
    output.u64(sequence);
    output.bytes(payload);
  }

  static bool read(std::span<const std::byte> input, GuestEnvelope& value)
  {
    GuestWireReader reader(input);
    const std::uint32_t magic = reader.u32();
    const std::uint32_t version = reader.u32();
    const std::uint32_t operation = reader.u32();
    const std::uint32_t size = reader.u32();
    value.session = reader.u64();
    value.sequence = reader.u64();
    value.payload = reader.bytes(size);
    value.call = static_cast<GuestCall>(operation);
    return reader.finished() && magic == Magic && version == Version &&
           operation >= static_cast<std::uint32_t>(GuestCall::Init) &&
           operation <= static_cast<std::uint32_t>(GuestCall::Services) &&
           value.session != 0 && value.sequence != 0;
  }
};

struct GuestDescriptor
{
  GuestRole role = GuestRole::Game;
  std::uint32_t requiredCapabilities = 0;
  std::string id;
  std::string extensionApi;

  void write(GuestWireWriter& output) const
  {
    output.u32(GuestEnvelope::Magic);
    output.u32(GuestEnvelope::Version);
    output.u32(static_cast<std::uint32_t>(role));
    output.u32(requiredCapabilities);
    output.text(id);
    output.text(extensionApi);
  }

  static bool read(std::span<const std::byte> input, GuestDescriptor& value)
  {
    GuestWireReader reader(input);
    const std::uint32_t magic = reader.u32();
    const std::uint32_t version = reader.u32();
    const std::uint32_t role = reader.u32();
    value.role = static_cast<GuestRole>(role);
    value.requiredCapabilities = reader.u32();
    value.id = reader.text(128);
    value.extensionApi = reader.text(128);
    if (!reader.finished() || magic != GuestEnvelope::Magic ||
        version != GuestEnvelope::Version || role < 1 || role > 3 ||
        (value.requiredCapabilities & ~GuestEnvelope::KnownCapabilities) != 0 ||
        value.id.empty()) {
      return false;
    }
    for (const char character : value.id) {
      if (!((character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '.' ||
            character == '-' || character == '_')) {
        return false;
      }
    }
    for (const char character : value.extensionApi) {
      if (!((character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '.' ||
            character == '-' || character == '_')) {
        return false;
      }
    }
    if (value.role == GuestRole::Mod && value.extensionApi.empty()) {
      return false;
    }
    return true;
  }
};
