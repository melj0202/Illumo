#pragma once

#include <Illumo/Rendering/FontWeightRamp.h>
#include <string>

// CSim's typeface: Kikuta, a variable-weight face the host rasterizes at a
// few weights ("kikuta:<weight>[:<glyphs>]", glyphs it lacks drawn from Space
// Mono). install() makes its rest weight the default font and installs the
// emphasis ramp the glass screens read, so hovered and focused labels
// thicken. It needs the guest font service; the native test oracle keeps the
// engine default font and no ramp.
class CSimTypeface
{
public:
  static constexpr float kRestWeight = 400.0f;
  static constexpr float kEmphasisWeight = 800.0f;
  // The title word rests heavy and swings lighter and heavier around it.
  static constexpr float kTitleRestWeight = 700.0f;

  static void install();
  static bool installed();
  // The rest weight has loaded (or nothing was installed); startup waits for
  // it so the first menu frame has text.
  static bool ready();
  // The title word's weights at `rasterSize`, rasterized for `glyphs` only;
  // empty when the typeface is not installed.
  static FontWeightRamp titleRamp(int rasterSize, const std::string& glyphs);
};
