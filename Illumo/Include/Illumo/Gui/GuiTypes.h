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

// Soft "glass" panel chrome: soft shadow, breathing outer glow, a vertical
// gradient surface with a lit rim, and an animated accent hairline.
struct GuiGlassStyle
{
  float radius = 22.0f;
  unsigned char opacity = 255;
  // Seconds into GuiMenuAnimator's ambient cycle; drives the hairline glint.
  float ambientPhase = 0.0f;
  // Outer glow strength, 0..1 (callers breathe it with the ambient cycle).
  float glow = 0.6f;
  // Fraction of the accent hairline drawn, 0..1 (entrance reveal).
  float accentReveal = 1.0f;
  bool shadow = true;
  bool accentLine = true;
  // Pointer tilt, -1..1 per axis (positive toward the right and bottom), for
  // a pane that swivels toward the pointer: the shadow slides away, a broad
  // glare follows the pointer across the glass, and the edges nearest the
  // pointer catch the light. Zero draws the untilted panel exactly.
  float tiltX = 0.0f;
  float tiltY = 0.0f;
};

// A selection highlight drawn as a drop of liquid travelling between cells
// of a list (vertical) or a button row (horizontal). The head cell leads and
// keeps its full width; as the drop stretches toward the tail cell it necks
// in the middle and tapers to a smaller tail, like a teardrop. At rest (head
// and tail on the same cell, no squash) it is exactly a rounded rectangle.
struct GuiLiquidSelection
{
  // Across the travel: x and width for a vertical list, y and height for a
  // horizontal row.
  float crossStart = 0.0f;
  float crossSize = 0.0f;
  // Along the travel: where the head and tail cells begin, and one cell's
  // length (row height or button width).
  float headStart = 0.0f;
  float tailStart = 0.0f;
  float cellLength = 0.0f;
  float radius = 8.0f;
  // Jelly response in [-1, 1] (GuiSelectionSpan::squash plus any press
  // wobble): positive bulges wider and shorter, negative pulls thin and long.
  float squash = 0.0f;
  bool horizontal = false;
  // Soft outer glow; zero spread or alpha skips it.
  float glowSpread = 12.0f;
  ColorRgba glow{ 0, 0, 0, 0 };
  ColorRgba rim{ 0, 0, 0, 0 };
  ColorRgba faceTop{ 0, 0, 0, 0 };
  ColorRgba faceBottom{ 0, 0, 0, 0 };
  // GuiMenuAnimator::selectionSheen(); negative skips the sweep.
  float sheen = -1.0f;
  ColorRgba sheenColor{ 0, 0, 0, 0 };
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
