#pragma once

#include <Illumo/Platform/PlatformTimer.h>
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

inline bool
shouldPace(bool vsyncEnabled, int refreshRate, long targetFps)
{
  if (targetFps <= 0) {
    return false;
  }
  if (vsyncEnabled) {
    const int effectiveRefresh = refreshRate > 0 ? refreshRate : 60;
    return targetFps < effectiveRefresh;
  }
  return true;
}

class FramePacer
{
public:
  FramePacer() = default;

  void reset()
  {
    m_hasTarget = false;
    m_lastTargetFps = 0;
    m_lastVsyncEnabled = false;
  }

  bool pace(long targetFps, bool vsyncEnabled = false, int refreshRate = 60)
  {
    if (targetFps != m_lastTargetFps || vsyncEnabled != m_lastVsyncEnabled) {
      m_hasTarget = false;
      m_lastTargetFps = targetFps;
      m_lastVsyncEnabled = vsyncEnabled;
    }

    if (!shouldPace(vsyncEnabled, refreshRate, targetFps)) {
      m_hasTarget = false;
      return false;
    }

    const std::chrono::nanoseconds targetDuration =
      calculateTargetFrameDuration(targetFps);
    if (targetDuration <= std::chrono::nanoseconds::zero()) {
      m_hasTarget = false;
      return false;
    }

    const std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now();

    if (!m_hasTarget) {
      m_nextDeadline = now + targetDuration;
      m_hasTarget = true;
      return true;
    }

    // Hard reset on hitch: If behind deadline by more than 1.5 frame durations,
    // immediately reset deadline to now + targetDuration to prevent catch-up
    // bursts.
    const std::chrono::nanoseconds hitchThreshold =
      targetDuration + (targetDuration / 2);
    if (now > m_nextDeadline + hitchThreshold) {
      m_nextDeadline = now + targetDuration;
    }

    // Coarse sleep if sufficient time remains (> 3ms) to minimize CPU load
    // while avoiding sleep-overshoot spikes.
    std::chrono::steady_clock::time_point current =
      std::chrono::steady_clock::now();
    while (m_nextDeadline - current > std::chrono::milliseconds(3)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      current = std::chrono::steady_clock::now();
    }

    // Fine spin-pause for sub-millisecond precision.
    while (std::chrono::steady_clock::now() < m_nextDeadline) {
      PlatformCpuPause();
    }

    m_nextDeadline += targetDuration;
    return true;
  }

  void paceFrom(std::chrono::steady_clock::time_point frameStartTime,
                long targetFps)
  {
    const std::chrono::nanoseconds targetDuration =
      calculateTargetFrameDuration(targetFps);
    if (targetDuration <= std::chrono::nanoseconds::zero()) {
      return;
    }

    const std::chrono::steady_clock::time_point targetEndTime =
      frameStartTime + targetDuration;
    std::chrono::steady_clock::time_point now =
      std::chrono::steady_clock::now();

    if (now >= targetEndTime) {
      return;
    }

    while (targetEndTime - now > std::chrono::milliseconds(3)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      now = std::chrono::steady_clock::now();
    }

    while (std::chrono::steady_clock::now() < targetEndTime) {
      PlatformCpuPause();
    }
  }

  std::chrono::steady_clock::time_point nextDeadline() const
  {
    return m_nextDeadline;
  }

  bool hasTarget() const { return m_hasTarget; }

private:
  std::chrono::steady_clock::time_point m_nextDeadline;
  long m_lastTargetFps = 0;
  bool m_lastVsyncEnabled = false;
  bool m_hasTarget = false;
};

inline void
paceFrame(std::chrono::steady_clock::time_point frameStartTime, long targetFps)
{
  FramePacer pacer;
  pacer.paceFrom(frameStartTime, targetFps);
}

inline std::string
buildFrameRateLabel(bool framePaced, int pacedFps, int submitFps)
{
  const std::string pacedValue =
    framePaced ? std::to_string(pacedFps) : std::string("off");
  return "Paced FPS: " + pacedValue +
         " | Submit FPS: " + std::to_string(submitFps);
}
