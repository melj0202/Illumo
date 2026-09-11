#pragma once
#include <cstdint>

struct ProcessMemoryStats
{
  std::uint64_t residentBytes = 0;
  std::uint64_t peakResidentBytes = 0;
  std::uint64_t privateCommitBytes = 0;
};

// Whole-process counters. Peak resident memory covers the process lifetime.
// Returns false and clears stats if unavailable; does not log or throw.
bool
QueryProcessMemoryStats(ProcessMemoryStats& stats) noexcept;
