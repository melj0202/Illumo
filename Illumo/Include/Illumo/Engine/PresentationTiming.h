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

// How much of `remaining` a pacer sleeps through before spinning to its
// deadline: all but the timer's wake latency, so a late wake-up still lands
// before the deadline.
inline std::chrono::nanoseconds
calculatePacingSleep(std::chrono::nanoseconds remaining,
                     std::chrono::nanoseconds wakeLatency)
{
  if (remaining <= wakeLatency) {
    return std::chrono::nanoseconds::zero();
  }
  return remaining - wakeLatency;
}

// Paces the main loop in software. It sleeps on a PlatformWaitTimer until
// its wake latency before the deadline, then spins with PlatformCpuPause for
// the final stretch.
class FramePacer
{
public:
  FramePacer() = default;

  FramePacer(const FramePacer&) = delete;
  FramePacer& operator=(const FramePacer&) = delete;
  FramePacer(FramePacer&&) = delete;
  FramePacer& operator=(FramePacer&&) = delete;

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

    waitUntil(m_nextDeadline);
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

    waitUntil(frameStartTime + targetDuration);
  }

  std::chrono::steady_clock::time_point nextDeadline() const
  {
    return m_nextDeadline;
  }

  bool hasTarget() const { return m_hasTarget; }

private:
  // Sleeps until the timer's wake latency before `deadline`, then spins. A
  // wake-up that comes early sleeps again.
  void waitUntil(std::chrono::steady_clock::time_point deadline)
  {
    const std::chrono::nanoseconds wakeLatency = m_waitTimer.wakeLatency();
    std::chrono::nanoseconds sleep = calculatePacingSleep(
      deadline - std::chrono::steady_clock::now(), wakeLatency);
    while (sleep > std::chrono::nanoseconds::zero()) {
      m_waitTimer.sleepFor(sleep);
      sleep = calculatePacingSleep(deadline - std::chrono::steady_clock::now(),
                                   wakeLatency);
    }

    while (std::chrono::steady_clock::now() < deadline) {
      PlatformCpuPause();
    }
  }

  PlatformWaitTimer m_waitTimer;
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
