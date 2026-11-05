#pragma once

#include <Illumo/Services/IEnvVars.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

// Full rendering pipeline configuration exposed to every application.
// Products may configure these flags in code, in their packaged envvars.json,
// or through a front-end settings menu; the engine and guest runtime apply
// them across SceneInstance, RenderWorld, MeshVisual and host post-processing.
struct RenderPipelineConfig
{
  bool lightingEnabled = true;
  bool shadowsEnabled = true;
  bool shadowPcf = true;
  int shadowMapSize = 1024;
  float shadowRadius = 2.5f;
  float lightDistance = 8.0f;
  float shadowBias = 0.001f;
  float shadowSlopeScale = 0.004f;
  float shadowNormalOffset = 0.015f;
  bool motionBlurEnabled = false;
  float motionBlurAmount = 0.5f;
  float motionBlurMax = 0.2f;
  int motionBlurSamples = 8;

  bool operator==(const RenderPipelineConfig&) const = default;

  static std::string formatFloat(float value)
  {
    if (!std::isfinite(value)) {
      return "0";
    }
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.4f", value);
    std::string result = buffer;
    while (result.size() > 1 && result.back() == '0') {
      result.pop_back();
    }
    if (!result.empty() && result.back() == '.') {
      result.pop_back();
    }
    return result.empty() || result == "-0" ? "0" : result;
  }

  // Seeds any unset pipeline keys so every application has the full pipeline
  // configuration available in its environment and console.
  static void seedDefaults(IEnvVars* environment)
  {
    if (environment == nullptr) {
      return;
    }
    struct DefaultEntry
    {
      const char* key;
      const char* value;
    };
    static constexpr DefaultEntry kDefaults[] = {
      { "lightingEnabled", "1" },
      { "shadowsEnabled", "1" },
      { "shadowPcf", "1" },
      { "shadowMapSize", "1024" },
      { "shadowRadius", "2.5" },
      { "lightDistance", "8" },
      { "shadowBias", "0.001" },
      { "shadowSlopeScale", "0.004" },
      { "shadowNormalOffset", "0.015" },
      { "motionBlurEnabled", "0" },
      { "motionBlurAmount", "0.5" },
      { "motionBlurMax", "0.2" },
      { "motionBlurSamples", "8" },
    };
    for (const DefaultEntry& entry : kDefaults) {
      if (environment->getVar(entry.key).value.empty()) {
        environment->setVar(entry.key, entry.value);
      }
    }
  }

  static RenderPipelineConfig fromEnv(IEnvVars* environment)
  {
    RenderPipelineConfig config;
    if (environment == nullptr) {
      return config;
    }
    const EnvVar& lighting = environment->getVar("lightingEnabled");
    if (!lighting.value.empty()) {
      config.lightingEnabled = lighting.valueAsBool;
    }
    const EnvVar& shadows = environment->getVar("shadowsEnabled");
    if (!shadows.value.empty()) {
      config.shadowsEnabled = shadows.valueAsBool;
    }
    const EnvVar& pcf = environment->getVar("shadowPcf");
    if (!pcf.value.empty()) {
      config.shadowPcf = pcf.valueAsBool;
    }
    const EnvVar& mapSize = environment->getVar("shadowMapSize");
    if (!mapSize.value.empty() && mapSize.valueAsLong > 0) {
      config.shadowMapSize = static_cast<int>(mapSize.valueAsLong);
    }
    const EnvVar& radius = environment->getVar("shadowRadius");
    if (!radius.value.empty() && std::isfinite(radius.valueAsDouble) &&
        radius.valueAsDouble > 0.0) {
      config.shadowRadius = static_cast<float>(radius.valueAsDouble);
    }
    const EnvVar& distance = environment->getVar("lightDistance");
    if (!distance.value.empty() && std::isfinite(distance.valueAsDouble) &&
        distance.valueAsDouble > 0.0) {
      config.lightDistance = static_cast<float>(distance.valueAsDouble);
    }
    const EnvVar& bias = environment->getVar("shadowBias");
    if (!bias.value.empty() && std::isfinite(bias.valueAsDouble)) {
      config.shadowBias = static_cast<float>(bias.valueAsDouble);
    }
    const EnvVar& slopeScale = environment->getVar("shadowSlopeScale");
    if (!slopeScale.value.empty() && std::isfinite(slopeScale.valueAsDouble)) {
      config.shadowSlopeScale = static_cast<float>(slopeScale.valueAsDouble);
    }
    const EnvVar& normalOffset = environment->getVar("shadowNormalOffset");
    if (!normalOffset.value.empty() &&
        std::isfinite(normalOffset.valueAsDouble)) {
      config.shadowNormalOffset =
        static_cast<float>(normalOffset.valueAsDouble);
    }
    const EnvVar& blur = environment->getVar("motionBlurEnabled");
    if (!blur.value.empty()) {
      config.motionBlurEnabled = blur.valueAsBool;
    }
    const EnvVar& blurAmount = environment->getVar("motionBlurAmount");
    if (!blurAmount.value.empty() && std::isfinite(blurAmount.valueAsDouble)) {
      config.motionBlurAmount = static_cast<float>(blurAmount.valueAsDouble);
    }
    const EnvVar& blurMax = environment->getVar("motionBlurMax");
    if (!blurMax.value.empty() && std::isfinite(blurMax.valueAsDouble)) {
      config.motionBlurMax = static_cast<float>(blurMax.valueAsDouble);
    }
    const EnvVar& blurSamples = environment->getVar("motionBlurSamples");
    if (!blurSamples.value.empty() && blurSamples.valueAsLong > 0) {
      config.motionBlurSamples = static_cast<int>(blurSamples.valueAsLong);
    }
    return config;
  }

  void writeToEnv(IEnvVars* environment) const
  {
    if (environment == nullptr) {
      return;
    }
    environment->setVar("lightingEnabled", lightingEnabled);
    environment->setVar("shadowsEnabled", shadowsEnabled);
    environment->setVar("shadowPcf", shadowPcf);
    environment->setVar("shadowMapSize", static_cast<long>(shadowMapSize));
    environment->setVar("shadowRadius", formatFloat(shadowRadius));
    environment->setVar("lightDistance", formatFloat(lightDistance));
    environment->setVar("shadowBias", formatFloat(shadowBias));
    environment->setVar("shadowSlopeScale", formatFloat(shadowSlopeScale));
    environment->setVar("shadowNormalOffset", formatFloat(shadowNormalOffset));
    environment->setVar("motionBlurEnabled", motionBlurEnabled);
    environment->setVar("motionBlurAmount", formatFloat(motionBlurAmount));
    environment->setVar("motionBlurMax", formatFloat(motionBlurMax));
    environment->setVar("motionBlurSamples",
                        static_cast<long>(motionBlurSamples));
  }
};
