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
  int shadowCascades = 3;
  float shadowRadius = 2.5f;
  float lightDistance = 8.0f;
  float shadowBias = 0.001f;
  float shadowSlopeScale = 0.004f;
  float shadowNormalOffset = 0.015f;
  bool motionBlurEnabled = false;
  float motionBlurAmount = 0.5f;
  float motionBlurMax = 0.2f;
  int motionBlurSamples = 8;
  bool hdrEnabled = true;
  // 0 = None (clamp), 1 = Reinhard, 2 = AgX Base, 3 = Khronos PBR Neutral,
  // 4 = AgX Punchy, 5 = ACES Filmic, 6 = Uchimura / GT
  static constexpr int kMaxTonemapper = 6;
  int tonemapper = 2;
  float exposure = 0.0f;
  bool bloomEnabled = true;
  float bloomIntensity = 0.04f;
  int bloomMipCount = 5;
  bool fxaaEnabled = true;
  bool canvasBloomEnabled = false;
  bool sobelEnabled = false;
  // 0 = Outline overlay over scene, 1 = Edges Only
  int sobelMode = 0;
  // 0 = Color, 1 = Depth, 2 = Combined (Color + Depth)
  int sobelSource = 2;
  float sobelStrength = 0.85f;
  float sobelThreshold = 0.12f;
  float sobelThickness = 1.0f;
  // Matrix-style temporal phosphor motion effects:
  // 0 = Off, 1 = Matrix, 2 = Cascade, 3 = Rain, 4 = Glyph, 5 = ChromaBit
  static constexpr int kMaxMotionFxMode = 5;
  int motionFxMode = 0;
  float motionFxStrength = 0.65f;
  float motionFxSpread = 1.0f;
  // Posterization / color quantization post-process:
  bool posterizeEnabled = false;
  // 0 = RGB, 1 = Luma (hue-preserving), 2 = HSV
  static constexpr int kMaxPosterizeMode = 2;
  int posterizeMode = 0;
  int posterizeLevels = 6;
  float posterizeGamma = 1.0f;
  // 0 = Off, 1 = Bayer 4x4, 2 = Bayer 8x8, 3 = Interleaved Gradient Noise
  static constexpr int kMaxPosterizeDitherMode = 3;
  int posterizeDitherMode = 0;
  float posterizeDitherAmount = 0.5f;
  int posterizeDitherScale = 1;
  float posterizeStrength = 1.0f;

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
      { "shadowCascades", "3" },
      { "shadowRadius", "2.5" },
      { "lightDistance", "8" },
      { "shadowBias", "0.001" },
      { "shadowSlopeScale", "0.004" },
      { "shadowNormalOffset", "0.015" },
      { "motionBlurEnabled", "0" },
      { "motionBlurAmount", "0.5" },
      { "motionBlurMax", "0.2" },
      { "motionBlurSamples", "8" },
      { "hdrEnabled", "1" },
      { "tonemapper", "2" },
      { "exposure", "0" },
      { "bloomEnabled", "1" },
      { "bloomIntensity", "0.04" },
      { "bloomMipCount", "5" },
      { "fxaaEnabled", "1" },
      { "canvasBloomEnabled", "0" },
      { "sobelEnabled", "0" },
      { "sobelMode", "0" },
      { "sobelSource", "2" },
      { "sobelStrength", "0.85" },
      { "sobelThreshold", "0.12" },
      { "sobelThickness", "1" },
      { "motionFxMode", "0" },
      { "motionFxStrength", "0.65" },
      { "motionFxSpread", "1" },
      { "posterizeEnabled", "0" },
      { "posterizeMode", "0" },
      { "posterizeLevels", "6" },
      { "posterizeGamma", "1" },
      { "posterizeDitherMode", "0" },
      { "posterizeDitherAmount", "0.5" },
      { "posterizeDitherScale", "1" },
      { "posterizeStrength", "1" },
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
    const EnvVar& cascades = environment->getVar("shadowCascades");
    if (!cascades.value.empty() && cascades.valueAsLong >= 1) {
      config.shadowCascades =
        std::clamp(static_cast<int>(cascades.valueAsLong), 1, 3);
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
    const EnvVar& hdr = environment->getVar("hdrEnabled");
    if (!hdr.value.empty()) {
      config.hdrEnabled = hdr.valueAsBool;
    }
    const EnvVar& tonemap = environment->getVar("tonemapper");
    if (!tonemap.value.empty()) {
      config.tonemapper =
        std::clamp(static_cast<int>(tonemap.valueAsLong), 0, kMaxTonemapper);
    }
    const EnvVar& exp = environment->getVar("exposure");
    if (!exp.value.empty() && std::isfinite(exp.valueAsDouble)) {
      config.exposure =
        std::clamp(static_cast<float>(exp.valueAsDouble), -8.0f, 8.0f);
    }
    const EnvVar& bloom = environment->getVar("bloomEnabled");
    if (!bloom.value.empty()) {
      config.bloomEnabled = bloom.valueAsBool;
    }
    const EnvVar& bloomInt = environment->getVar("bloomIntensity");
    if (!bloomInt.value.empty() && std::isfinite(bloomInt.valueAsDouble)) {
      config.bloomIntensity =
        std::clamp(static_cast<float>(bloomInt.valueAsDouble), 0.0f, 1.0f);
    }
    const EnvVar& bloomMips = environment->getVar("bloomMipCount");
    if (!bloomMips.value.empty() && bloomMips.valueAsLong >= 1) {
      config.bloomMipCount =
        std::clamp(static_cast<int>(bloomMips.valueAsLong), 1, 5);
    }
    const EnvVar& fxaa = environment->getVar("fxaaEnabled");
    if (!fxaa.value.empty()) {
      config.fxaaEnabled = fxaa.valueAsBool;
    }
    const EnvVar& canvasBloom = environment->getVar("canvasBloomEnabled");
    if (!canvasBloom.value.empty()) {
      config.canvasBloomEnabled = canvasBloom.valueAsBool;
    }
    const EnvVar& sobel = environment->getVar("sobelEnabled");
    if (!sobel.value.empty()) {
      config.sobelEnabled = sobel.valueAsBool;
    }
    const EnvVar& sobelModeVar = environment->getVar("sobelMode");
    if (!sobelModeVar.value.empty()) {
      config.sobelMode =
        std::clamp(static_cast<int>(sobelModeVar.valueAsLong), 0, 1);
    }
    const EnvVar& sobelSourceVar = environment->getVar("sobelSource");
    if (!sobelSourceVar.value.empty()) {
      config.sobelSource =
        std::clamp(static_cast<int>(sobelSourceVar.valueAsLong), 0, 2);
    }
    const EnvVar& sobelStrengthVar = environment->getVar("sobelStrength");
    if (!sobelStrengthVar.value.empty() &&
        std::isfinite(sobelStrengthVar.valueAsDouble)) {
      config.sobelStrength = std::clamp(
        static_cast<float>(sobelStrengthVar.valueAsDouble), 0.0f, 1.0f);
    }
    const EnvVar& sobelThresholdVar = environment->getVar("sobelThreshold");
    if (!sobelThresholdVar.value.empty() &&
        std::isfinite(sobelThresholdVar.valueAsDouble)) {
      config.sobelThreshold = std::clamp(
        static_cast<float>(sobelThresholdVar.valueAsDouble), 0.005f, 1.0f);
    }
    const EnvVar& sobelThicknessVar = environment->getVar("sobelThickness");
    if (!sobelThicknessVar.value.empty() &&
        std::isfinite(sobelThicknessVar.valueAsDouble)) {
      config.sobelThickness = std::clamp(
        static_cast<float>(sobelThicknessVar.valueAsDouble), 0.25f, 4.0f);
    }
    const EnvVar& motionFxModeVar = environment->getVar("motionFxMode");
    if (!motionFxModeVar.value.empty()) {
      config.motionFxMode = std::clamp(
        static_cast<int>(motionFxModeVar.valueAsLong), 0, kMaxMotionFxMode);
    }
    const EnvVar& motionFxStrengthVar = environment->getVar("motionFxStrength");
    if (!motionFxStrengthVar.value.empty() &&
        std::isfinite(motionFxStrengthVar.valueAsDouble)) {
      config.motionFxStrength = std::clamp(
        static_cast<float>(motionFxStrengthVar.valueAsDouble), 0.0f, 1.0f);
    }
    const EnvVar& motionFxSpreadVar = environment->getVar("motionFxSpread");
    if (!motionFxSpreadVar.value.empty() &&
        std::isfinite(motionFxSpreadVar.valueAsDouble)) {
      config.motionFxSpread = std::clamp(
        static_cast<float>(motionFxSpreadVar.valueAsDouble), 0.1f, 3.0f);
    }
    const EnvVar& posterizeVar = environment->getVar("posterizeEnabled");
    if (!posterizeVar.value.empty()) {
      config.posterizeEnabled = posterizeVar.valueAsBool;
    }
    const EnvVar& posterizeModeVar = environment->getVar("posterizeMode");
    if (!posterizeModeVar.value.empty()) {
      config.posterizeMode = std::clamp(
        static_cast<int>(posterizeModeVar.valueAsLong), 0, kMaxPosterizeMode);
    }
    const EnvVar& posterizeLevelsVar = environment->getVar("posterizeLevels");
    if (!posterizeLevelsVar.value.empty() &&
        posterizeLevelsVar.valueAsLong > 0) {
      config.posterizeLevels =
        std::clamp(static_cast<int>(posterizeLevelsVar.valueAsLong), 2, 32);
    }
    const EnvVar& posterizeGammaVar = environment->getVar("posterizeGamma");
    if (!posterizeGammaVar.value.empty() &&
        std::isfinite(posterizeGammaVar.valueAsDouble)) {
      config.posterizeGamma = std::clamp(
        static_cast<float>(posterizeGammaVar.valueAsDouble), 0.4f, 2.5f);
    }
    const EnvVar& posterizeDitherModeVar =
      environment->getVar("posterizeDitherMode");
    if (!posterizeDitherModeVar.value.empty()) {
      config.posterizeDitherMode =
        std::clamp(static_cast<int>(posterizeDitherModeVar.valueAsLong),
                   0,
                   kMaxPosterizeDitherMode);
    }
    const EnvVar& posterizeDitherAmountVar =
      environment->getVar("posterizeDitherAmount");
    if (!posterizeDitherAmountVar.value.empty() &&
        std::isfinite(posterizeDitherAmountVar.valueAsDouble)) {
      config.posterizeDitherAmount = std::clamp(
        static_cast<float>(posterizeDitherAmountVar.valueAsDouble), 0.0f, 1.5f);
    }
    const EnvVar& posterizeDitherScaleVar =
      environment->getVar("posterizeDitherScale");
    if (!posterizeDitherScaleVar.value.empty() &&
        posterizeDitherScaleVar.valueAsLong > 0) {
      config.posterizeDitherScale =
        std::clamp(static_cast<int>(posterizeDitherScaleVar.valueAsLong), 1, 4);
    }
    const EnvVar& posterizeStrengthVar =
      environment->getVar("posterizeStrength");
    if (!posterizeStrengthVar.value.empty() &&
        std::isfinite(posterizeStrengthVar.valueAsDouble)) {
      config.posterizeStrength = std::clamp(
        static_cast<float>(posterizeStrengthVar.valueAsDouble), 0.0f, 1.0f);
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
    environment->setVar("shadowCascades",
                        static_cast<long>(std::clamp(shadowCascades, 1, 3)));
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
    environment->setVar("hdrEnabled", hdrEnabled);
    environment->setVar(
      "tonemapper",
      static_cast<long>(std::clamp(tonemapper, 0, kMaxTonemapper)));
    environment->setVar("exposure", formatFloat(exposure));
    environment->setVar("bloomEnabled", bloomEnabled);
    environment->setVar("bloomIntensity", formatFloat(bloomIntensity));
    environment->setVar("bloomMipCount",
                        static_cast<long>(std::clamp(bloomMipCount, 1, 5)));
    environment->setVar("fxaaEnabled", fxaaEnabled);
    environment->setVar("canvasBloomEnabled", canvasBloomEnabled);
    environment->setVar("sobelEnabled", sobelEnabled);
    environment->setVar("sobelMode",
                        static_cast<long>(std::clamp(sobelMode, 0, 1)));
    environment->setVar("sobelSource",
                        static_cast<long>(std::clamp(sobelSource, 0, 2)));
    environment->setVar("sobelStrength",
                        formatFloat(std::clamp(sobelStrength, 0.0f, 1.0f)));
    environment->setVar("sobelThreshold",
                        formatFloat(std::clamp(sobelThreshold, 0.005f, 1.0f)));
    environment->setVar("sobelThickness",
                        formatFloat(std::clamp(sobelThickness, 0.25f, 4.0f)));
    environment->setVar(
      "motionFxMode",
      static_cast<long>(std::clamp(motionFxMode, 0, kMaxMotionFxMode)));
    environment->setVar("motionFxStrength",
                        formatFloat(std::clamp(motionFxStrength, 0.0f, 1.0f)));
    environment->setVar("motionFxSpread",
                        formatFloat(std::clamp(motionFxSpread, 0.1f, 3.0f)));
    environment->setVar("posterizeEnabled", posterizeEnabled);
    environment->setVar(
      "posterizeMode",
      static_cast<long>(std::clamp(posterizeMode, 0, kMaxPosterizeMode)));
    environment->setVar("posterizeLevels",
                        static_cast<long>(std::clamp(posterizeLevels, 2, 32)));
    environment->setVar("posterizeGamma",
                        formatFloat(std::clamp(posterizeGamma, 0.4f, 2.5f)));
    environment->setVar("posterizeDitherMode",
                        static_cast<long>(std::clamp(
                          posterizeDitherMode, 0, kMaxPosterizeDitherMode)));
    environment->setVar(
      "posterizeDitherAmount",
      formatFloat(std::clamp(posterizeDitherAmount, 0.0f, 1.5f)));
    environment->setVar(
      "posterizeDitherScale",
      static_cast<long>(std::clamp(posterizeDitherScale, 1, 4)));
    environment->setVar("posterizeStrength",
                        formatFloat(std::clamp(posterizeStrength, 0.0f, 1.0f)));
  }
};
