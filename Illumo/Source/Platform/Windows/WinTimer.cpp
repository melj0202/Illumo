#include <Illumo/Platform/PlatformTimer.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <timeapi.h>
// clang-format on
#include <immintrin.h>
#include <thread>

PlatformTimerScope::PlatformTimerScope()
{
  timeBeginPeriod(1);
}

PlatformTimerScope::~PlatformTimerScope()
{
  timeEndPeriod(1);
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