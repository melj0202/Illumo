#include <Illumo/Platform/PlatformTimer.h>

#include <thread>

PlatformTimerScope::PlatformTimerScope() = default;
PlatformTimerScope::~PlatformTimerScope() = default;

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