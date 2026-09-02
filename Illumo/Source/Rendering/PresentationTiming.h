#pragma once

#include <Illumo/Services/IEnvVars.h>
#include <chrono>
#include <string>
#include <thread>

inline bool
isVsyncRequested(IEnvVars* envVars)
{
  if (envVars == nullptr) {
    return true;
  }

  const EnvVar& configured = envVars->getVar("vsync");
  return configured.value.empty() ? true : configured.valueAsBool;
}

inline long
getTargetFps(IEnvVars* envVars)
{
  if (envVars == nullptr) {
    return 60;
  }

  const EnvVar& configured = envVars->getVar("fps");
  if (configured.value.empty()) {
    return 60;
  }

  // A value <= 0 explicitly disables the limiter (uncapped).
  return configured.valueAsLong < 0 ? 0 : configured.valueAsLong;
}

inline std::chrono::nanoseconds
calculateTargetFrameDuration(long targetFps)
{
  if (targetFps <= 0) {
    return std::chrono::nanoseconds::zero();
  }
  return std::chrono::nanoseconds(1'000'000'000LL / targetFps);
}

inline void
paceFrame(std::chrono::steady_clock::time_point frameStartTime, long targetFps)
{
  const std::chrono::nanoseconds targetDuration =
    calculateTargetFrameDuration(targetFps);
  if (targetDuration <= std::chrono::nanoseconds::zero()) {
    return;
  }

  const std::chrono::steady_clock::time_point targetEndTime =
    frameStartTime + targetDuration;
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

  if (now >= targetEndTime) {
    return;
  }

  // Sleep coarse chunk if sufficient time remains (> 2ms) to avoid high CPU
  // usage.
  while (targetEndTime - now > std::chrono::milliseconds(2)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    now = std::chrono::steady_clock::now();
  }

  // Spin-wait / yield for final sub-millisecond precision.
  while (std::chrono::steady_clock::now() < targetEndTime) {
    std::this_thread::yield();
  }
}

inline std::string
buildFrameRateLabel(bool framePaced, int pacedFps, int submitFps)
{
  const std::string pacedValue =
    framePaced ? std::to_string(pacedFps) : std::string("off");
  return "Paced FPS: " + pacedValue +
         " | Submit FPS: " + std::to_string(submitFps);
}
