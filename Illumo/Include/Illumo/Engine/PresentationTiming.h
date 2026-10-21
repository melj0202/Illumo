#pragma once

#include <Illumo/Platform/PlatformTimer.h>
#include <Illumo/Services/IEnvVars.h>
#include <chrono>
#include <iomanip>
#include <locale>
#include <sstream>
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

// The rate the loop should sleep toward. A cap below the refresh wins. With
// vsync on and no lower cap, hold the refresh anyway: a fullscreen present on
// Windows can return without waiting, and the loop would otherwise run ahead
// of the display. Zero means do not sleep.
inline long
presentationPaceTarget(bool vsyncEnabled, int refreshHz, long targetFps)
{
  if (shouldPace(vsyncEnabled, refreshHz, targetFps)) {
    return targetFps;
  }
  if (!vsyncEnabled) {
    return 0;
  }
  return refreshHz > 0 ? refreshHz : 60;
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

// Software frame-time cap, or "inf" when the limiter is not running.
// `vsync` is the swap interval (fifo); the refresh rate is in hertz.
inline std::string
frameLimiterText(bool vsync, int refreshHz, long targetFps)
{
  const int refresh = refreshHz > 0 ? refreshHz : 60;
  if (!shouldPace(vsync, refresh, targetFps)) {
    return "inf";
  }
  std::ostringstream text;
  text.imbue(std::locale::classic());
  text << std::fixed << std::setprecision(1)
       << (1000.0 / static_cast<double>(targetFps)) << "ms";
  return text.str();
}

// The frame time the graph's reference line marks: the software cap when it
// is running, otherwise one refresh.
inline double
frameBudgetSeconds(bool vsync, int refreshHz, long targetFps)
{
  const int refresh = refreshHz > 0 ? refreshHz : 60;
  if (shouldPace(vsync, refresh, targetFps) && targetFps > 0) {
    return 1.0 / static_cast<double>(targetFps);
  }
  return 1.0 / static_cast<double>(refresh);
}

// Achieved rate, limiter, present mode and refresh, then frame-time
// percentiles expressed as FPS (a higher percentile is a slower frame).
inline std::string
buildFrameRateLabel(int submitFps,
                    bool vsync,
                    int refreshHz,
                    long targetFps,
                    int p50Fps,
                    int p98Fps,
                    int p995Fps)
{
  const int refresh = refreshHz > 0 ? refreshHz : 0;
  std::ostringstream text;
  text.imbue(std::locale::classic());
  text << submitFps
       << " fps T: " << frameLimiterText(vsync, refreshHz, targetFps) << " ("
       << (vsync ? "fifo" : "mailbox") << ") @" << refresh << "Hz\n"
       << "p50=" << p50Fps << " p98=" << p98Fps << " p99.5=" << p995Fps
       << " fps";
  return text.str();
}
