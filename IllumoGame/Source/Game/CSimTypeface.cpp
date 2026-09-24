#include "CSimTypeface.h"

#include <Illumo/Rendering/Font.h>
#include <utility>
#include <vector>

static bool s_installed = false;

// UI text is sampled from rest to past emphasis, so a spring's overshoot has
// somewhere to go. The title spans thin (falling letters) to black.
static const float kUiWeights[4] = { 400.0f, 600.0f, 800.0f, 1000.0f };
static const float kTitleWeights[5] = { 200.0f,
                                        450.0f,
                                        700.0f,
                                        850.0f,
                                        1000.0f };

static std::string
faceName(float weight, const std::string& glyphs)
{
  std::string name = "kikuta:" + std::to_string(static_cast<int>(weight));
  if (!glyphs.empty()) {
    name += ":" + glyphs;
  }
  return name;
}

void
CSimTypeface::install()
{
  std::vector<FontWeightRamp::Sample> samples;
  for (float weight : kUiWeights) {
    samples.push_back({ weight,
                        Font::loadFromFile(faceName(weight, std::string()),
                                           Font::kDefaultPixelSize) });
  }
  if (samples.front().font == nullptr) {
    return; // no font service: keep the engine default
  }
  Font::setDefaultFont(samples.front().font);
  FontWeightRamp::setUi(
    FontWeightRamp(std::move(samples), kRestWeight, kEmphasisWeight));
  s_installed = true;
}

bool
CSimTypeface::installed()
{
  return s_installed;
}

bool
CSimTypeface::ready()
{
  if (!s_installed) {
    return true;
  }
  const std::shared_ptr<Font> font = Font::getDefaultFont();
  return font != nullptr && font->isValid();
}

FontWeightRamp
CSimTypeface::titleRamp(int rasterSize, const std::string& glyphs)
{
  if (!s_installed) {
    return FontWeightRamp();
  }
  std::vector<FontWeightRamp::Sample> samples;
  for (float weight : kTitleWeights) {
    samples.push_back({ weight,
                        Font::loadFromFile(faceName(weight, glyphs),
                                           static_cast<float>(rasterSize)) });
  }
  return FontWeightRamp(std::move(samples), kTitleRestWeight, 1000.0f);
}
