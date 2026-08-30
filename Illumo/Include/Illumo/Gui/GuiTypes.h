#pragma once

#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <Illumo/Services/KeyCode.h>
#include <cstdint>
#include <string>

// Button interaction state.
enum class GuiButtonState
{
  Normal,
  Hover,
  Pressed,
  Disabled
};

// Text alignment within bounding box.
enum class GuiAlignment
{
  Left,
  Center,
  Right
};

// Chrome styling parameters for panels, cards, and windows.
struct GuiPanelChrome
{
  ColorRgba background{ 14, 21, 32, 255 };
  ColorRgba border{ 70, 94, 119, 225 };
  ColorRgba shadow{ 0, 0, 0, 150 };
  ColorRgba accent{ 66, 214, 210, 255 };
  float borderWidth = 1.0f;
  float shadowOffset = 4.0f;
  float accentWidth = 0.0f;
  bool drawShadow = true;
  bool drawAccent = false;
};

// Definition of a button in a dialog or toolbar.
struct GuiButtonDef
{
  std::string label;
  std::string shortcut;
  int actionId = 0;
  KeyCode shortcutKey = KeyCode::None;
  ColorRgba customAccent{ 0, 0, 0, 0 };
  bool isDefault = false;
  bool isCancel = false;
  bool isDestructive = false;
};

// Definition of a property/settings row item.
struct GuiPropertyRowDef
{
  std::string label;
  std::string value;
  bool isSelected = false;
  bool isHovered = false;
  bool isEditable = false;
};
