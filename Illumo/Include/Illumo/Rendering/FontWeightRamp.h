#pragma once

#include <memory>
#include <string>
#include <vector>

class Font;
struct TextPrimitive;

// One variable-weight family rasterized at a few weights (samples) of one
// raster size. Any weight between two loaded samples draws the lighter one
// with the heavier overlaid at the fraction between them
// (TextPrimitive::heavyFont), so weight animates continuously. Samples still
// loading are skipped; weights outside the loaded range clamp to it.
// Main-thread value type; the static UI ramp mirrors Font::setDefaultFont.
class FontWeightRamp
{
public:
  struct Sample
  {
    float weight = 0.0f;
    std::shared_ptr<Font> font;
  };

  FontWeightRamp() = default;
  // Samples in any order; null fonts are dropped. `restWeight` is the weight
  // of unemphasized text and `emphasisWeight` that of fully emphasized text.
  FontWeightRamp(std::vector<Sample> samples,
                 float restWeight,
                 float emphasisWeight);

  bool empty() const { return m_samples.empty(); }
  // Every sample has finished loading.
  bool ready() const;
  const std::vector<Sample>& samples() const { return m_samples; }
  float restWeight() const { return m_restWeight; }
  float emphasisWeight() const { return m_emphasisWeight; }
  // Weight for emphasis `e`: rest at 0, emphasis at 1, and past it for a
  // spring's overshoot.
  float weightFor(float emphasis) const;

  // Points `text` at the samples around `weight`. Returns false (leaving the
  // text on the first sample, which draws once it loads) when none has
  // loaded, and false without touching the text when the ramp is empty.
  bool apply(TextPrimitive& text, float weight) const;
  // Advance width of `text` drawn at `sizePt` and `weight`, matching apply;
  // negative when no sample has loaded.
  float measure(const std::string& text, float sizePt, float weight) const;

  // The product's emphasis ramp for UI text; empty unless one is installed.
  static const FontWeightRamp& ui();
  static void setUi(FontWeightRamp ramp);

private:
  struct Pick
  {
    const Sample* light = nullptr;
    const Sample* heavy = nullptr;
    float blend = 0.0f;
  };
  Pick pick(float weight) const;

  std::vector<Sample> m_samples;
  float m_restWeight = 400.0f;
  float m_emphasisWeight = 700.0f;
};
