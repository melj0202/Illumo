#include <Illumo/Platform/ProcessMemoryStats.h>

bool
QueryProcessMemoryStats(ProcessMemoryStats& stats) noexcept
{
  stats = {};
  return false;
}
