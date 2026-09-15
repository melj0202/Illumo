#pragma once

#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>

// Shared colors and compact panel chrome for Illumo's primitive-composed UI.
// This is intentionally a value-only theme, not a retained widget system.
struct UiPanelStyle
{
  bool enabled = false;
  ColorRgba background{ 12, 18, 28, 238 };
  ColorRgba border{ 74, 100, 126, 220 };
  ColorRgba accent{ 66, 214, 210, 255 };
  ColorRgba shadow{ 0, 0, 0, 120 };
  float paddingX = 10.0f;
  float paddingY = 6.0f;
  float borderWidth = 1.0f;
  float accentWidth = 3.0f;
  float shadowOffset = 3.0f;
};

class UiTheme final
{
public:
  static ColorRgba canvasShade() { return ColorRgba{ 2, 5, 9, 92 }; }
  static ColorRgba panelShadow() { return ColorRgba{ 0, 0, 0, 150 }; }
  static ColorRgba panelSurface() { return ColorRgba{ 10, 16, 25, 246 }; }
  static ColorRgba panelRaised() { return ColorRgba{ 17, 26, 39, 252 }; }
  static ColorRgba panelInset() { return ColorRgba{ 6, 10, 17, 230 }; }
  static ColorRgba panelBorder() { return ColorRgba{ 70, 94, 119, 225 }; }
  static ColorRgba divider() { return ColorRgba{ 54, 76, 98, 190 }; }
  static ColorRgba accent() { return ColorRgba{ 66, 214, 210, 255 }; }
  static ColorRgba accentSoft() { return ColorRgba{ 66, 214, 210, 105 }; }
  static ColorRgba textPrimary() { return ColorRgba{ 232, 239, 246, 255 }; }
  static ColorRgba textMuted() { return ColorRgba{ 139, 160, 180, 255 }; }
  static ColorRgba selection() { return ColorRgba{ 42, 111, 151, 205 }; }
  static ColorRgba success() { return ColorRgba{ 92, 224, 150, 255 }; }
  static ColorRgba warning() { return ColorRgba{ 246, 194, 82, 255 }; }
  static ColorRgba error() { return ColorRgba{ 245, 102, 112, 255 }; }
  static ColorRgba accentCool() { return ColorRgba{ 87, 221, 242, 255 }; }
  static ColorRgba accentViolet() { return ColorRgba{ 151, 128, 245, 255 }; }
  static ColorRgba menuSurface() { return ColorRgba{ 14, 23, 40, 255 }; }
  static ColorRgba menuCard() { return ColorRgba{ 26, 39, 59, 255 }; }
  static ColorRgba menuBorder() { return ColorRgba{ 69, 110, 142, 255 }; }

  static UiPanelStyle statusPanel()
  {
    UiPanelStyle style;
    style.enabled = true;
    style.background = ColorRgba{ 10, 19, 26, 232 };
    style.border = ColorRgba{ 65, 105, 116, 220 };
    style.accent = success();
    style.paddingX = 9.0f;
    style.paddingY = 5.0f;
    style.accentWidth = 3.0f;
    return style;
  }

  static UiPanelStyle noticePanel(ColorRgba accentColor)
  {
    UiPanelStyle style;
    style.enabled = true;
    style.background = ColorRgba{ 12, 19, 29, 238 };
    style.border = ColorRgba{ 79, 103, 128, 225 };
    style.accent = accentColor;
    style.paddingX = 12.0f;
    style.paddingY = 8.0f;
    style.borderWidth = 1.0f;
    style.accentWidth = 4.0f;
    style.shadowOffset = 4.0f;
    return style;
  }

  static ColorRgba applyOpacity(ColorRgba color, unsigned char opacity)
  {
    color.a = static_cast<unsigned char>(
      (static_cast<unsigned int>(color.a) * opacity) / 255u);
    return color;
  }
};
