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

struct GuestInput
{
  static constexpr std::uint32_t MaximumEvents = 256;
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

  bool held(GuestKey key) const
  {
    const std::size_t index = static_cast<std::size_t>(key);
    return index < keys.size() && (keys[index] == GuestKeyAction::Press ||
                                   keys[index] == GuestKeyAction::Hold);
  }
  void write(GuestWireWriter& output) const
  {
    if (events.size() > MaximumEvents || characters.size() > MaximumEvents) {
      throw std::length_error("Guest input event quota exceeded");
    }
    output.u32(1);
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
    input.modifiers = reader.u32();
    const std::uint32_t console = reader.u32();
    input.consoleOpen = console != 0;
    if (version != 1 || !std::isfinite(input.elapsed) || input.elapsed < 0 ||
        input.width == 0 || input.width > 65536 || input.height == 0 ||
        input.height > 65536 || !std::isfinite(input.mouseX) ||
        !std::isfinite(input.mouseY) || !std::isfinite(input.scroll) ||
        input.modifiers > 15 || console > 1) {
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
    if (!reader.finished()) {
      return false;
    }
    output = std::move(input);
    return true;
  }
};
