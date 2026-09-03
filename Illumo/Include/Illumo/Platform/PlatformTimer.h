#pragma once

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

// Emits an architecture-appropriate, low-latency pause instruction
// (e.g. x86 pause or ARM yield) during fine spin-waits without releasing the
// thread's OS scheduling quantum.
void
PlatformCpuPause();