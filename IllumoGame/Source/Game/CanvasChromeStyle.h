#pragma once

#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <cmath>

// The settings (hamburger) button's look, shared by the canvas chrome that
// wears it: the edit toolbar (CanvasActionBar) and the collapsed paint bubble.
// An opaque tile with a rim lit teal at the top and indigo at the bottom, a
// deep teal-to-indigo face, both brightening toward the accents on hover, and
// a violet-to-cyan halo behind it that breathes at rest and blooms on hover.
// `hover` runs 0..1.
class CanvasChromeStyle
{
public:
  static ColorRgba rimTop(float hover)
  {
    return UiTheme::mix(
      ColorRgba{ 70, 160, 196, 255 }, UiTheme::accentCool(), hover);
  }
  static ColorRgba rimBottom(float hover)
  {
    return UiTheme::mix(
      ColorRgba{ 114, 88, 204, 255 }, UiTheme::accentViolet(), hover);
  }
  static ColorRgba faceTop(float hover)
  {
    return UiTheme::mix(
      ColorRgba{ 30, 74, 104, 255 }, ColorRgba{ 44, 128, 160, 255 }, hover);
  }
  static ColorRgba faceBottom(float hover)
  {
    return UiTheme::mix(
      ColorRgba{ 46, 34, 104, 255 }, ColorRgba{ 74, 54, 156, 255 }, hover);
  }
  static ColorRgba halo(float hover)
  {
    return UiTheme::mix(
      UiTheme::accentViolet(), UiTheme::accentCool(), 0.4f + 0.6f * hover);
  }
  // How far the halo reaches past the tile, and its inner opacity.
  static float haloWidth(float breathe, float hover)
  {
    return 7.0f + 3.0f * breathe + 5.0f * hover;
  }
  static float haloOpacity(float breathe, float hover)
  {
    return 0.14f + 0.08f * breathe + 0.26f * hover;
  }
  // The idle breath (0..1) on a 3.2 s cycle of a seconds clock; a steady
  // half breath with reduced motion.
  static float breathe(float clockSeconds, bool reducedMotion)
  {
    return reducedMotion
             ? 0.5f
             : 0.5f + 0.5f * std::sin(clockSeconds * 6.2831853f / 3.2f);
  }
  // Jelly squash from the springs driving a piece: positive is wider and
  // shorter, negative taller and narrower. Growing stretches it tall, the
  // recoil bulges it wide, and a held press squashes it flat.
  static float squash(float popVelocity, float hoverVelocity, bool pressed)
  {
    const float value =
      -0.004f * popVelocity - 0.008f * hoverVelocity + (pressed ? 0.08f : 0.0f);
    return value < -0.14f ? -0.14f : (value > 0.14f ? 0.14f : value);
  }
};
