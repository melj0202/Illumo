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

  // Living-glass chrome: gradient panel surfaces, lit rims, glows and keys.
  static ColorRgba glassTop() { return ColorRgba{ 21, 33, 54, 250 }; }
  static ColorRgba glassBottom() { return ColorRgba{ 9, 15, 29, 252 }; }
  static ColorRgba glassRim() { return ColorRgba{ 58, 92, 124, 255 }; }
  static ColorRgba glassRimLit() { return ColorRgba{ 150, 222, 246, 255 }; }
  static ColorRgba cardTop() { return ColorRgba{ 30, 45, 69, 255 }; }
  static ColorRgba cardBottom() { return ColorRgba{ 19, 30, 48, 255 }; }
  static ColorRgba cardRim() { return ColorRgba{ 50, 73, 102, 255 }; }
  static ColorRgba selectionTop() { return ColorRgba{ 34, 104, 132, 255 }; }
  static ColorRgba selectionBottom() { return ColorRgba{ 22, 66, 94, 255 }; }
  static ColorRgba glowShadow() { return ColorRgba{ 0, 3, 10, 150 }; }
  static ColorRgba scrimCenter() { return ColorRgba{ 4, 9, 20, 178 }; }
  static ColorRgba scrimEdge() { return ColorRgba{ 1, 3, 9, 238 }; }
  static ColorRgba keycapFace() { return ColorRgba{ 34, 50, 75, 255 }; }
  static ColorRgba keycapEdge() { return ColorRgba{ 10, 17, 29, 255 }; }
  static ColorRgba textSecondary() { return ColorRgba{ 182, 204, 224, 255 }; }

  // Same color at zero alpha. Straight-alpha fades must target this rather
  // than transparent black, which would darken the interpolated edge.
  static ColorRgba transparentOf(ColorRgba color)
  {
    color.a = 0;
    return color;
  }

  // Per-channel linear blend from `from` (t = 0) to `to` (t = 1).
  static ColorRgba mix(ColorRgba from, ColorRgba to, float t)
  {
    const float amount = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float keep = 1.0f - amount;
    ColorRgba result;
    result.r = static_cast<unsigned char>(static_cast<float>(from.r) * keep +
                                          static_cast<float>(to.r) * amount);
    result.g = static_cast<unsigned char>(static_cast<float>(from.g) * keep +
                                          static_cast<float>(to.g) * amount);
    result.b = static_cast<unsigned char>(static_cast<float>(from.b) * keep +
                                          static_cast<float>(to.b) * amount);
    result.a = static_cast<unsigned char>(static_cast<float>(from.a) * keep +
                                          static_cast<float>(to.a) * amount);
    return result;
  }

  // Scales alpha by a 0..1 factor (clamped).
  static ColorRgba fade(ColorRgba color, float factor)
  {
    const float amount = factor < 0.0f ? 0.0f : (factor > 1.0f ? 1.0f : factor);
    color.a = static_cast<unsigned char>(static_cast<float>(color.a) * amount);
    return color;
  }

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
