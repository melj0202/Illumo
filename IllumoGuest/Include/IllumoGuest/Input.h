#pragma once

#include <IllumoGuest/Wire.h>
#include <array>
#include <cmath>

enum class GuestKey : std::uint32_t
{
#define ILLUMO_GUEST_KEY(name, number) name = (number),
#include <IllumoGuest/Keys.inc>
#undef ILLUMO_GUEST_KEY
  Count = 72
};

enum class GuestKeyAction : std::uint32_t
{
  None = 0,
  Press = 1,
  Release = 2,
  Hold = 3
};

struct GuestKeyEvent
{
  GuestKey key = GuestKey::Space;
  GuestKeyAction action = GuestKeyAction::None;
  std::uint32_t modifiers = 0; // shift=1, control=2, alt=4, super=8
};

// Input v2 (Windows capability): lifecycle of surface windows.
enum class GuestWindowEventKind : std::uint32_t
{
  Closed = 1,  // the user asked to close the window
  Resized = 2, // x, y: the new client size
  Moved = 3,   // x, y: the new client origin in screen coordinates
  FocusGained = 4,
  FocusLost = 5
};

struct GuestWindowEvent
{
  GuestWindowEventKind kind = GuestWindowEventKind::Closed;
  std::uint32_t surface = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
};

// Input v2: one open surface window as the host sees it this frame.
struct GuestSurfaceInput
{
  static constexpr std::uint32_t LeftButton = 1u;
  static constexpr std::uint32_t RightButton = 2u;
  static constexpr std::uint32_t MiddleButton = 4u;
  std::uint32_t surface = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::int32_t originX = 0; // client origin, screen coordinates
  std::int32_t originY = 0;
  double mouseX = 0; // client coordinates; may lie outside while dragging
  double mouseY = 0;
  std::uint32_t buttons = 0;
  double scroll = 0;
  bool focused = false;
};

struct GuestInput
{
  static constexpr std::uint32_t MaximumEvents = 256;
  static constexpr std::uint32_t MaximumSurfaces = 8;
  static constexpr std::uint32_t MaximumWindowEvents = 64;
  // Version 1 describes the main window only. Version 2 appends the main
  // window's screen origin, the surface windows, which window each key and
  // character event came from, the focused window and window events.
  std::uint32_t version = 1;
  double elapsed = 0;
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
  double mouseX = 0;
  double mouseY = 0;
  double scroll = 0;
  std::uint32_t modifiers = 0;
  bool consoleOpen = false;
  std::array<GuestKeyAction, static_cast<std::size_t>(GuestKey::Count)> keys{};
  std::vector<GuestKeyEvent> events;
  std::vector<std::uint32_t> characters;
  // Version 2 only.
  std::int32_t originX = 0;
  std::int32_t originY = 0;
  std::uint32_t focusedSurface = 0;             // 0: the main window
  std::vector<std::uint32_t> eventSurfaces;     // one per event
  std::vector<std::uint32_t> characterSurfaces; // one per character
  std::vector<GuestSurfaceInput> surfaces;
  std::vector<GuestWindowEvent> windowEvents;

  const GuestSurfaceInput* surface(std::uint32_t id) const
  {
    for (const GuestSurfaceInput& value : surfaces) {
      if (value.surface == id) {
        return &value;
      }
    }
    return nullptr;
  }

  bool held(GuestKey key) const
  {
    const std::size_t index = static_cast<std::size_t>(key);
    return index < keys.size() && (keys[index] == GuestKeyAction::Press ||
                                   keys[index] == GuestKeyAction::Hold);
  }
  void write(GuestWireWriter& output) const
  {
    if (events.size() > MaximumEvents || characters.size() > MaximumEvents ||
        (version != 1 && version != 2) ||
        (version == 2 && (eventSurfaces.size() != events.size() ||
                          characterSurfaces.size() != characters.size() ||
                          surfaces.size() > MaximumSurfaces ||
                          windowEvents.size() > MaximumWindowEvents))) {
      throw std::length_error("Guest input event quota exceeded");
    }
    output.u32(version);
    output.f64(elapsed);
    output.u32(width);
    output.u32(height);
    output.f64(mouseX);
    output.f64(mouseY);
    output.f64(scroll);
    output.u32(modifiers);
    output.u32(consoleOpen ? 1 : 0);
    for (GuestKeyAction action : keys) {
      output.u32(static_cast<std::uint32_t>(action));
    }
    output.u32(static_cast<std::uint32_t>(events.size()));
    for (const GuestKeyEvent& event : events) {
      output.u32(static_cast<std::uint32_t>(event.key));
      output.u32(static_cast<std::uint32_t>(event.action));
      output.u32(event.modifiers);
    }
    output.u32(static_cast<std::uint32_t>(characters.size()));
    for (std::uint32_t character : characters) {
      output.u32(character);
    }
    if (version == 1) {
      return;
    }
    for (std::uint32_t id : eventSurfaces) {
      output.u32(id);
    }
    for (std::uint32_t id : characterSurfaces) {
      output.u32(id);
    }
    output.u32(static_cast<std::uint32_t>(originX));
    output.u32(static_cast<std::uint32_t>(originY));
    output.u32(focusedSurface);
    output.u32(static_cast<std::uint32_t>(surfaces.size()));
    for (const GuestSurfaceInput& value : surfaces) {
      output.u32(value.surface);
      output.u32(value.width);
      output.u32(value.height);
      output.u32(static_cast<std::uint32_t>(value.originX));
      output.u32(static_cast<std::uint32_t>(value.originY));
      output.f64(value.mouseX);
      output.f64(value.mouseY);
      output.u32(value.buttons);
      output.f64(value.scroll);
      output.u32(value.focused ? 1 : 0);
    }
    output.u32(static_cast<std::uint32_t>(windowEvents.size()));
    for (const GuestWindowEvent& event : windowEvents) {
      output.u32(static_cast<std::uint32_t>(event.kind));
      output.u32(event.surface);
      output.u32(static_cast<std::uint32_t>(event.x));
      output.u32(static_cast<std::uint32_t>(event.y));
    }
  }
  // A surface id named by input: 0 (main) or one of the open surfaces.
  bool knownSurface(std::uint32_t id) const
  {
    return id == 0 || surface(id) != nullptr;
  }
  static bool read(std::span<const std::byte> bytes, GuestInput& output)
  {
    GuestWireReader reader(bytes);
    GuestInput input;
    const std::uint32_t version = reader.u32();
    input.elapsed = reader.f64();
    input.width = reader.u32();
    input.height = reader.u32();
    input.mouseX = reader.f64();
    input.mouseY = reader.f64();
    input.scroll = reader.f64();
    input.version = version;
    input.modifiers = reader.u32();
    const std::uint32_t console = reader.u32();
    input.consoleOpen = console != 0;
    if ((version != 1 && version != 2) || !std::isfinite(input.elapsed) ||
        input.elapsed < 0 || input.width == 0 || input.width > 65536 ||
        input.height == 0 || input.height > 65536 ||
        !std::isfinite(input.mouseX) || !std::isfinite(input.mouseY) ||
        !std::isfinite(input.scroll) || input.modifiers > 15 || console > 1) {
      return false;
    }
    for (GuestKeyAction& action : input.keys) {
      const std::uint32_t value = reader.u32();
      if (value > 3) {
        return false;
      }
      action = static_cast<GuestKeyAction>(value);
    }
    const std::uint32_t events = reader.u32();
    if (events > MaximumEvents || events > reader.remaining() / 12u) {
      return false;
    }
    input.events.resize(events);
    for (GuestKeyEvent& event : input.events) {
      const std::uint32_t key = reader.u32();
      const std::uint32_t action = reader.u32();
      event.modifiers = reader.u32();
      if (key >= static_cast<std::uint32_t>(GuestKey::Count) || action > 3 ||
          event.modifiers > 15) {
        return false;
      }
      event.key = static_cast<GuestKey>(key);
      event.action = static_cast<GuestKeyAction>(action);
    }
    const std::uint32_t characters = reader.u32();
    if (characters > MaximumEvents || characters > reader.remaining() / 4u) {
      return false;
    }
    input.characters.resize(characters);
    for (std::uint32_t& character : input.characters) {
      character = reader.u32();
      if (character > 0x10ffff ||
          (character >= 0xd800 && character <= 0xdfff)) {
        return false;
      }
    }
    if (version == 2 && !readVersion2(reader, input)) {
      return false;
    }
    if (!reader.finished()) {
      return false;
    }
    output = std::move(input);
    return true;
  }

private:
  static bool readVersion2(GuestWireReader& reader, GuestInput& input)
  {
    if (reader.remaining() / 4u <
        input.events.size() + input.characters.size()) {
      return false;
    }
    input.eventSurfaces.resize(input.events.size());
    for (std::uint32_t& id : input.eventSurfaces) {
      id = reader.u32();
    }
    input.characterSurfaces.resize(input.characters.size());
    for (std::uint32_t& id : input.characterSurfaces) {
      id = reader.u32();
    }
    input.originX = static_cast<std::int32_t>(reader.u32());
    input.originY = static_cast<std::int32_t>(reader.u32());
    input.focusedSurface = reader.u32();
    const std::uint32_t surfaces = reader.u32();
    // Each surface record is 56 bytes.
    if (!reader.valid() || surfaces > MaximumSurfaces ||
        surfaces > reader.remaining() / 56u) {
      return false;
    }
    input.surfaces.resize(surfaces);
    for (std::size_t index = 0; index < input.surfaces.size(); ++index) {
      GuestSurfaceInput& value = input.surfaces[index];
      value.surface = reader.u32();
      value.width = reader.u32();
      value.height = reader.u32();
      value.originX = static_cast<std::int32_t>(reader.u32());
      value.originY = static_cast<std::int32_t>(reader.u32());
      value.mouseX = reader.f64();
      value.mouseY = reader.f64();
      value.buttons = reader.u32();
      value.scroll = reader.f64();
      const std::uint32_t focused = reader.u32();
      value.focused = focused != 0;
      if (!reader.valid() || value.surface == 0 || value.width == 0 ||
          value.width > 65536 || value.height == 0 || value.height > 65536 ||
          !std::isfinite(value.mouseX) || !std::isfinite(value.mouseY) ||
          !std::isfinite(value.scroll) || value.buttons > 7 || focused > 1) {
        return false;
      }
      for (std::size_t earlier = 0; earlier < index; ++earlier) {
        if (input.surfaces[earlier].surface == value.surface) {
          return false;
        }
      }
    }
    for (std::uint32_t id : input.eventSurfaces) {
      if (!input.knownSurface(id)) {
        return false;
      }
    }
    for (std::uint32_t id : input.characterSurfaces) {
      if (!input.knownSurface(id)) {
        return false;
      }
    }
    if (!input.knownSurface(input.focusedSurface)) {
      return false;
    }
    const std::uint32_t events = reader.u32();
    if (!reader.valid() || events > MaximumWindowEvents ||
        events > reader.remaining() / 16u) {
      return false;
    }
    input.windowEvents.resize(events);
    for (GuestWindowEvent& event : input.windowEvents) {
      const std::uint32_t kind = reader.u32();
      event.surface = reader.u32();
      event.x = static_cast<std::int32_t>(reader.u32());
      event.y = static_cast<std::int32_t>(reader.u32());
      // Events may name a window closed this frame, never the main one.
      if (kind < 1 || kind > 5 || event.surface == 0) {
        return false;
      }
      event.kind = static_cast<GuestWindowEventKind>(kind);
    }
    return reader.valid();
  }
};
