#include <Illumo/Platform/PlatformTimer.h>

#include <thread>

PlatformTimerScope::PlatformTimerScope() = default;
PlatformTimerScope::~PlatformTimerScope() = default;

// Linux sleeps already use high-resolution timers; the wait timer keeps the
// portable sleep_for fallback and its conservative spin margin.
PlatformWaitTimer::PlatformWaitTimer() = default;
PlatformWaitTimer::~PlatformWaitTimer() = default;

void
PlatformWaitTimer::sleepFor(std::chrono::nanoseconds duration)
{
  if (duration > std::chrono::nanoseconds::zero()) {
    std::this_thread::sleep_for(duration);
  }
}

std::chrono::nanoseconds
PlatformWaitTimer::wakeLatency() const
{
  return std::chrono::milliseconds(3);
}

bool
PlatformWaitTimer::isHighResolution() const
{
  return m_timer != nullptr;
}

void
PlatformCpuPause()
{
#if defined(__i386__) || defined(__x86_64__)
  __builtin_ia32_pause();
#elif defined(__aarch64__)
  asm volatile("yield" ::: "memory");
#else
  std::this_thread::yield();
#endif
}
