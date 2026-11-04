#pragma once

#include <Illumo/Services/KeyCode.h>
#include <array>
#include <cstddef>

enum class GamepadButton : unsigned char
{
  A = 0,
  B = 1,
  X = 2,
  Y = 3,
  LeftBumper = 4,
  RightBumper = 5,
  Back = 6,
  Start = 7,
  Guide = 8,
  LeftThumb = 9,
  RightThumb = 10,
  DpadUp = 11,
  DpadRight = 12,
  DpadDown = 13,
  DpadLeft = 14,
  Count = 15
};

enum class GamepadAxis : unsigned char
{
  LeftX = 0,
  LeftY = 1,
  RightX = 2,
  RightY = 3,
  LeftTrigger = 4,
  RightTrigger = 5,
  Count = 6
};

struct GamepadState
{
  static constexpr std::size_t ButtonCount = 15;
  static constexpr std::size_t AxisCount = 6;

  bool connected = false;
  std::array<float, AxisCount> axes{};
  std::array<InputAction, ButtonCount> buttons{
    InputAction::None, InputAction::None, InputAction::None, InputAction::None,
    InputAction::None, InputAction::None, InputAction::None, InputAction::None,
    InputAction::None, InputAction::None, InputAction::None, InputAction::None,
    InputAction::None, InputAction::None, InputAction::None
  };
};
