#pragma once
#include <cstdint>
#include <string>

// The machine the process runs on, for startup logs and bug reports. Fields
// the platform cannot supply stay empty or zero.
struct SystemInfo
{
  std::string operatingSystem;
  std::string architecture;
  std::string cpuName;
  std::uint32_t physicalCores = 0;
  std::uint32_t logicalProcessors = 0;
  std::uint64_t totalMemoryBytes = 0;
  std::uint64_t availableMemoryBytes = 0;
};

// Queries the operating system. Does not log; throws only std::bad_alloc.
SystemInfo
QuerySystemInfo();
