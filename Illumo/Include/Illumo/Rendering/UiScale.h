#pragma once

#include <Illumo/Services/IEnvVars.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

// The persisted "uiScale" setting. It is either a factor (fractions such as
// 1.25 are allowed) or automatic: "auto" (or 0), which follows the window so
// a larger window gets a larger interface. Stateless; every reader resolves
// the setting against the window it lays out for, so a resize takes effect
// on the next frame.
class UiScale final
{
public:
  // Automatic scale is 1x at the reference window and grows in whole steps
  // with the smaller of the two axis ratios, clamped to [minimum, maximum].
  static constexpr float kAutomaticMinimum = 1.0f;
  static constexpr float kAutomaticMaximum = 4.0f;
  static constexpr float kAutomaticStep = 0.25f;
  static constexpr float kReferenceWidth = 1280.0f;
  static constexpr float kReferenceHeight = 720.0f;

  static bool isAutomatic(const EnvVar& setting)
  {
    if (setting.value.empty()) {
      return false;
    }
    std::string lower = setting.value;
    std::transform(
      lower.begin(), lower.end(), lower.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
      });
    return lower == "auto" ||
           (setting.valueAsDouble == 0.0 && lower.front() == '0');
  }

  static float automatic(int width, int height)
  {
    if (width <= 0 || height <= 0) {
      return kAutomaticMinimum;
    }
    const float ratio = std::min(static_cast<float>(width) / kReferenceWidth,
                                 static_cast<float>(height) / kReferenceHeight);
    // A small tolerance keeps exact multiples (1920x1080 -> 1.5) on their
    // step despite float rounding.
    const float stepped =
      std::floor(ratio / kAutomaticStep + 1.0e-3f) * kAutomaticStep;
    return std::clamp(stepped, kAutomaticMinimum, kAutomaticMaximum);
  }

  // The factor to lay out with. An unset or unreadable setting is 1x; other
  // explicit factors pass through unchanged.
  static float resolve(const EnvVar& setting, int width, int height)
  {
    if (isAutomatic(setting)) {
      return automatic(width, height);
    }
    if (setting.value.empty() || !(setting.valueAsDouble > 0.0) ||
        !std::isfinite(setting.valueAsDouble)) {
      return 1.0f;
    }
    return static_cast<float>(setting.valueAsDouble);
  }

  // The stored preference: 0 for automatic, else the explicit factor (1x
  // when unset or unreadable).
  static float stored(const EnvVar& setting)
  {
    if (isAutomatic(setting)) {
      return 0.0f;
    }
    return resolve(setting, 0, 0);
  }

  // Text to persist for a preference (0 is automatic): "auto", "1.25", "2".
  static std::string text(float preference)
  {
    if (!(preference > 0.0f)) {
      return "auto";
    }
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.2f", preference);
    std::string result = buffer;
    while (!result.empty() && result.back() == '0') {
      result.pop_back();
    }
    if (!result.empty() && result.back() == '.') {
      result.pop_back();
    }
    return result;
  }
};
