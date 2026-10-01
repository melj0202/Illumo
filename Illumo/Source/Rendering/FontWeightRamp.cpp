#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/FontWeightRamp.h>
#include <Illumo/Rendering/Primitives/TextPrimitive.h>
#include <algorithm>
#include <cmath>
#include <utility>

static FontWeightRamp&
uiRamp()
{
  static FontWeightRamp ramp;
  return ramp;
}

FontWeightRamp::FontWeightRamp(std::vector<Sample> samples,
                               float restWeight,
                               float emphasisWeight)
  : m_restWeight(restWeight)
  , m_emphasisWeight(emphasisWeight)
{
  for (Sample& sample : samples) {
    if (sample.font != nullptr && std::isfinite(sample.weight)) {
      m_samples.push_back(std::move(sample));
    }
  }
  std::stable_sort(
    m_samples.begin(), m_samples.end(), [](const Sample& a, const Sample& b) {
      return a.weight < b.weight;
    });
}

bool
FontWeightRamp::ready() const
{
  for (const Sample& sample : m_samples) {
    if (!sample.font->isValid()) {
      return false;
    }
  }
  return !m_samples.empty();
}

float
FontWeightRamp::weightFor(float emphasis) const
{
  const float e = std::isfinite(emphasis) ? emphasis : 0.0f;
  return m_restWeight + (m_emphasisWeight - m_restWeight) * e;
}

FontWeightRamp::Pick
FontWeightRamp::pick(float weight) const
{
  Pick result;
  for (const Sample& sample : m_samples) {
    if (!sample.font->isValid()) {
      continue;
    }
    if (sample.weight <= weight) {
      result.light = &sample;
    } else if (result.heavy == nullptr) {
      result.heavy = &sample;
    }
  }
  if (result.light == nullptr) {
    // Below the lightest loaded sample.
    result.light = result.heavy;
    result.heavy = nullptr;
  } else if (result.heavy != nullptr) {
    result.blend = (weight - result.light->weight) /
                   (result.heavy->weight - result.light->weight);
  }
  return result;
}

bool
FontWeightRamp::apply(TextPrimitive& text, float weight) const
{
  if (m_samples.empty()) {
    return false;
  }
  const Pick chosen = pick(std::isfinite(weight) ? weight : m_restWeight);
  text.heavyFont.reset();
  text.heavyBlend = 0.0f;
  if (chosen.light == nullptr) {
    text.font = m_samples.front().font;
    return false;
  }
  text.font = chosen.light->font;
  if (chosen.heavy != nullptr && chosen.blend > 0.0f) {
    text.heavyFont = chosen.heavy->font;
    text.heavyBlend = chosen.blend;
  }
  return true;
}

float
FontWeightRamp::measure(const std::string& text,
                        float sizePt,
                        float weight) const
{
  const Pick chosen = pick(std::isfinite(weight) ? weight : m_restWeight);
  if (chosen.light == nullptr) {
    return -1.0f;
  }
  const float light = chosen.light->font->measureText(text, sizePt).width;
  if (chosen.heavy == nullptr) {
    return light;
  }
  const float heavy = chosen.heavy->font->measureText(text, sizePt).width;
  return light + (heavy - light) * chosen.blend;
}

const FontWeightRamp&
FontWeightRamp::ui()
{
  return uiRamp();
}

void
FontWeightRamp::setUi(FontWeightRamp ramp)
{
  uiRamp() = std::move(ramp);
}
