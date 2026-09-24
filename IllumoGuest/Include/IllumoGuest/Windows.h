#pragma once

#include <IllumoGuest/Wire.h>
#include <cstdint>
#include <string>

// Window service requests (Windows capability). A surface is a guest-drawn
// top-level window: its content arrives in the frame's surfaces section
// (frame schema v5) and its pointer, focus and lifecycle arrive in input v2.
// Surface 0 is the main window and is never opened or closed here.
enum class GuestWindowAction : std::uint32_t
{
  Open = 1,
  Close = 2,
  SetTitle = 3
};

struct GuestWindowRequest
{
  static constexpr std::uint32_t Version = 1;
  static constexpr std::uint32_t MaximumSurfaces = 8;
  static constexpr std::uint32_t MaximumSurfaceId = 0x7fffffffu;
  static constexpr std::uint32_t MaximumTitleBytes = 128;
  static constexpr std::uint32_t MinimumWidth = 160;
  static constexpr std::uint32_t MinimumHeight = 120;
  static constexpr std::uint32_t MaximumSize = 4096;
  // Offsets from the main window's client origin stay within this range.
  static constexpr std::int32_t MaximumOffset = 16384;

  GuestWindowAction action = GuestWindowAction::Open;
  std::uint32_t surface = 0;
  std::string title;
  // Open only: client rectangle in pixels relative to the main window's
  // client origin.
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  static bool validTitle(const std::string& text)
  {
    if (text.size() > MaximumTitleBytes || !guestUtf8(text)) {
      return false;
    }
    for (char character : text) {
      if (static_cast<unsigned char>(character) < 0x20u ||
          character == '\x7f') {
        return false;
      }
    }
    return true;
  }
  void write(GuestWireWriter& output) const
  {
    output.u32(Version);
    output.u32(static_cast<std::uint32_t>(action));
    output.u32(surface);
    output.text(title);
    output.u32(static_cast<std::uint32_t>(x));
    output.u32(static_cast<std::uint32_t>(y));
    output.u32(width);
    output.u32(height);
  }
  static bool read(std::span<const std::byte> bytes, GuestWindowRequest& output)
  {
    GuestWireReader reader(bytes);
    GuestWindowRequest candidate;
    const std::uint32_t version = reader.u32();
    const std::uint32_t action = reader.u32();
    candidate.surface = reader.u32();
    candidate.title = reader.text(MaximumTitleBytes);
    candidate.x = static_cast<std::int32_t>(reader.u32());
    candidate.y = static_cast<std::int32_t>(reader.u32());
    candidate.width = reader.u32();
    candidate.height = reader.u32();
    if (!reader.finished() || version != Version || action < 1 || action > 3 ||
        candidate.surface == 0 || candidate.surface > MaximumSurfaceId ||
        !validTitle(candidate.title)) {
      return false;
    }
    candidate.action = static_cast<GuestWindowAction>(action);
    const bool geometryEmpty = candidate.x == 0 && candidate.y == 0 &&
                               candidate.width == 0 && candidate.height == 0;
    if (candidate.action == GuestWindowAction::Open) {
      if (candidate.width < MinimumWidth || candidate.width > MaximumSize ||
          candidate.height < MinimumHeight || candidate.height > MaximumSize ||
          candidate.x < -MaximumOffset || candidate.x > MaximumOffset ||
          candidate.y < -MaximumOffset || candidate.y > MaximumOffset) {
        return false;
      }
    } else if (!geometryEmpty ||
               (candidate.action == GuestWindowAction::Close &&
                !candidate.title.empty())) {
      return false;
    }
    output = std::move(candidate);
    return true;
  }
};

// Completion payload of an accepted Open: the client size the window got.
struct GuestWindowOpened
{
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  void write(GuestWireWriter& output) const
  {
    output.u32(width);
    output.u32(height);
  }
  static bool read(std::span<const std::byte> bytes, GuestWindowOpened& output)
  {
    GuestWireReader reader(bytes);
    GuestWindowOpened candidate{ reader.u32(), reader.u32() };
    if (!reader.finished() || candidate.width == 0 ||
        candidate.width > GuestWindowRequest::MaximumSize ||
        candidate.height == 0 ||
        candidate.height > GuestWindowRequest::MaximumSize) {
      return false;
    }
    output = candidate;
    return true;
  }
};
