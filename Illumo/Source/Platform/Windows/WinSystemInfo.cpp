#include <Illumo/Platform/SystemInfo.h>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

// A REG_SZ value under HKEY_LOCAL_MACHINE as UTF-8, or empty.
static std::string
machineRegistryText(const wchar_t* key, const wchar_t* value)
{
  wchar_t buffer[256] = {};
  DWORD size = sizeof(buffer);
  if (RegGetValueW(HKEY_LOCAL_MACHINE,
                   key,
                   value,
                   RRF_RT_REG_SZ,
                   nullptr,
                   buffer,
                   &size) != ERROR_SUCCESS) {
    return {};
  }
  buffer[(sizeof(buffer) / sizeof(buffer[0])) - 1] = L'\0';
  const int bytes =
    WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
  if (bytes <= 1) {
    return {};
  }
  std::string text(static_cast<std::size_t>(bytes - 1), '\0');
  WideCharToMultiByte(
    CP_UTF8, 0, buffer, -1, text.data(), bytes, nullptr, nullptr);
  const std::size_t first = text.find_first_not_of(' ');
  const std::size_t last = text.find_last_not_of(' ');
  return first == std::string::npos ? std::string()
                                    : text.substr(first, last - first + 1);
}

// GetVersionEx reports the manifest-compatible version, so the true one
// comes from ntdll.
static std::string
windowsVersion()
{
  using RtlGetVersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (ntdll == nullptr) {
    return "Windows";
  }
  const RtlGetVersionFunction rtlGetVersion =
    reinterpret_cast<RtlGetVersionFunction>(
      reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
  OSVERSIONINFOW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  if (rtlGetVersion == nullptr || rtlGetVersion(&version) != 0) {
    return "Windows";
  }
  // Windows 11 still reports 10.0; its builds start at 22000.
  std::string name =
    version.dwMajorVersion == 10 && version.dwBuildNumber >= 22000
      ? "Windows 11"
      : "Windows " + std::to_string(version.dwMajorVersion);
  name += " " + std::to_string(version.dwMajorVersion) + "." +
          std::to_string(version.dwMinorVersion) + "." +
          std::to_string(version.dwBuildNumber);
  const std::string release = machineRegistryText(
    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion");
  if (!release.empty()) {
    name += " (" + release + ")";
  }
  return name;
}

static std::uint32_t
physicalCoreCount()
{
  DWORD bytes = 0;
  GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
  if (bytes == 0) {
    return 0;
  }
  std::vector<unsigned char> buffer(bytes);
  if (!GetLogicalProcessorInformationEx(
        RelationProcessorCore,
        reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
          buffer.data()),
        &bytes)) {
    return 0;
  }
  std::uint32_t cores = 0;
  DWORD offset = 0;
  while (offset < bytes) {
    const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX* entry =
      reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
        buffer.data() + offset);
    if (entry->Size == 0) {
      break;
    }
    if (entry->Relationship == RelationProcessorCore) {
      ++cores;
    }
    offset += entry->Size;
  }
  return cores;
}

SystemInfo
QuerySystemInfo()
{
  SystemInfo info;
  info.operatingSystem = windowsVersion();

  SYSTEM_INFO system{};
  GetNativeSystemInfo(&system);
  switch (system.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
      info.architecture = "x64";
      break;
    case PROCESSOR_ARCHITECTURE_ARM64:
      info.architecture = "arm64";
      break;
    case PROCESSOR_ARCHITECTURE_INTEL:
      info.architecture = "x86";
      break;
    default:
      info.architecture = "unknown";
      break;
  }

  info.cpuName =
    machineRegistryText(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                        L"ProcessorNameString");
  info.physicalCores = physicalCoreCount();
  info.logicalProcessors =
    static_cast<std::uint32_t>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));

  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof(memory);
  if (GlobalMemoryStatusEx(&memory)) {
    info.totalMemoryBytes = memory.ullTotalPhys;
    info.availableMemoryBytes = memory.ullAvailPhys;
  }
  return info;
}
