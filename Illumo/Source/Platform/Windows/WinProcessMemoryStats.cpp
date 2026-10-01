#include <Illumo/Platform/ProcessMemoryStats.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on

bool
QueryProcessMemoryStats(ProcessMemoryStats& stats) noexcept
{
  stats = {};
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (!K32GetProcessMemoryInfo(
        GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
        sizeof(counters))) {
    return false;
  }
  stats.residentBytes = counters.WorkingSetSize;
  stats.peakResidentBytes = counters.PeakWorkingSetSize;
  stats.privateCommitBytes = counters.PrivateUsage;
  return true;
}
