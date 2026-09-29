#include <Illumo/Platform/PlatformTimer.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <timeapi.h>
// clang-format on
#include <immintrin.h>
#include <thread>

// Older SDK headers lack the flag; Windows 10 1803 and later accept it.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// A high-resolution timer typically fires about half a millisecond late; the
// fallback sleep follows the timer resolution PlatformTimerScope asked for
// (1 ms), when Windows honours it.
static constexpr std::chrono::nanoseconds kHighResolutionWakeLatency =
  std::chrono::milliseconds(1);
static constexpr std::chrono::nanoseconds kFallbackWakeLatency =
  std::chrono::milliseconds(3);

PlatformTimerScope::PlatformTimerScope()
{
  timeBeginPeriod(1);
}

PlatformTimerScope::~PlatformTimerScope()
{
  timeEndPeriod(1);
}

PlatformWaitTimer::PlatformWaitTimer()
{
  // Fails before Windows 10 1803, leaving the sleep_for fallback.
  m_timer = CreateWaitableTimerExW(nullptr,
                                   nullptr,
                                   CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                   SYNCHRONIZE | TIMER_MODIFY_STATE);
}

PlatformWaitTimer::~PlatformWaitTimer()
{
  if (m_timer != nullptr) {
    CloseHandle(static_cast<HANDLE>(m_timer));
  }
}

void
PlatformWaitTimer::sleepFor(std::chrono::nanoseconds duration)
{
  if (duration <= std::chrono::nanoseconds::zero()) {
    return;
  }
  if (m_timer != nullptr) {
    // A negative due time is relative, in 100 ns units; round up so the
    // timer never fires early.
    LARGE_INTEGER dueTime;
    dueTime.QuadPart = -static_cast<LONGLONG>((duration.count() + 99) / 100);
    if (SetWaitableTimer(
          static_cast<HANDLE>(m_timer), &dueTime, 0, nullptr, nullptr, FALSE) !=
          0 &&
        WaitForSingleObject(static_cast<HANDLE>(m_timer), INFINITE) ==
          WAIT_OBJECT_0) {
      return;
    }
  }
  std::this_thread::sleep_for(duration);
}

std::chrono::nanoseconds
PlatformWaitTimer::wakeLatency() const
{
  return m_timer != nullptr ? kHighResolutionWakeLatency : kFallbackWakeLatency;
}

bool
PlatformWaitTimer::isHighResolution() const
{
  return m_timer != nullptr;
}

void
PlatformCpuPause()
{
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) ||                \
  defined(__x86_64__)
  _mm_pause();
#elif defined(_M_ARM64) || defined(__aarch64__)
  __yield();
#else
  std::this_thread::yield();
#endif
}
