#pragma once

#include <chrono>

// Platform-neutral timer resolution scope.
// On platforms requiring explicit timer precision requests (e.g., Windows),
// constructing this scope requests high-precision (1 ms) system timer
// interrupts, and destruction restores the previous system timer configuration.
class PlatformTimerScope
{
public:
  PlatformTimerScope();
  ~PlatformTimerScope();

  PlatformTimerScope(const PlatformTimerScope&) = delete;
  PlatformTimerScope& operator=(const PlatformTimerScope&) = delete;
  PlatformTimerScope(PlatformTimerScope&&) = delete;
  PlatformTimerScope& operator=(PlatformTimerScope&&) = delete;
};

// Blocks the calling thread for short, precise waits such as frame pacing.
// On Windows it owns a high-resolution waitable timer, whose wake-ups do not
// depend on the process timer resolution: Windows 11 stops honouring
// PlatformTimerScope's request while a process's windows are occluded or
// minimized, and a 1 ms sleep can then last a whole 15.6 ms timer tick.
// Where that timer is unavailable (Windows before 10 1803, other platforms)
// it falls back to std::this_thread::sleep_for. Use it from one thread at a
// time.
class PlatformWaitTimer
{
public:
  PlatformWaitTimer();
  ~PlatformWaitTimer();

  PlatformWaitTimer(const PlatformWaitTimer&) = delete;
  PlatformWaitTimer& operator=(const PlatformWaitTimer&) = delete;
  PlatformWaitTimer(PlatformWaitTimer&&) = delete;
  PlatformWaitTimer& operator=(PlatformWaitTimer&&) = delete;

  // Sleeps for about `duration`; a non-positive duration returns at once.
  // The wake-up can come up to wakeLatency() late, and later under load.
  void sleepFor(std::chrono::nanoseconds duration);

  // How late a wake-up can typically be. Callers that must not miss a
  // deadline sleep until this margin before it and spin the rest.
  std::chrono::nanoseconds wakeLatency() const;

  // True when sleeps use the high-resolution timer rather than the fallback.
  bool isHighResolution() const;

private:
  void* m_timer = nullptr; // Platform timer handle; null uses the fallback.
};

// Emits an architecture-appropriate, low-latency pause instruction
// (e.g. x86 pause or ARM yield) during fine spin-waits without releasing the
// thread's OS scheduling quantum.
void
PlatformCpuPause();
