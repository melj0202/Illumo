#include "../../Wasm/WasmCompiler.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

class CompilerHandle
{
public:
  explicit CompilerHandle(HANDLE value = nullptr)
    : m_value(value)
  {
  }
  ~CompilerHandle()
  {
    if (m_value != nullptr && m_value != INVALID_HANDLE_VALUE) {
      CloseHandle(m_value);
    }
  }
  CompilerHandle(const CompilerHandle&) = delete;
  CompilerHandle& operator=(const CompilerHandle&) = delete;
  CompilerHandle(CompilerHandle&&) = delete;
  CompilerHandle& operator=(CompilerHandle&&) = delete;
  HANDLE get() const { return m_value; }

private:
  HANDLE m_value;
};

bool
compileWasmIsolated(std::span<const std::byte> input,
                    std::uint64_t memoryLimit,
                    std::uint32_t timeoutMilliseconds,
                    std::uint32_t engineOptions,
                    std::vector<std::byte>& artifact,
                    std::string& error)
{
  constexpr DWORD kCapacity = 256u * 1024u * 1024u;
  constexpr DWORD kHeaderBytes = 16u;
  if (input.empty() || input.size() > kCapacity - kHeaderBytes ||
      memoryLimit < 16u * 1024u * 1024u || timeoutMilliseconds == 0) {
    error = "Invalid isolated compiler limits";
    return false;
  }
  std::wstring executable(32768, L'\0');
  const DWORD length = GetModuleFileNameW(
    nullptr, executable.data(), static_cast<DWORD>(executable.size()));
  if (length == 0 || length >= executable.size()) {
    error = "Cannot locate the bundled WASM compiler";
    return false;
  }
  executable.resize(length);
  executable.resize(executable.find_last_of(L"\\/") + 1u);
  executable += L"IllumoWasmCompiler.exe";
  SECURITY_ATTRIBUTES security{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
  CompilerHandle mapping(CreateFileMappingW(
    INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0, kCapacity, nullptr));
  CompilerHandle job(CreateJobObjectW(nullptr, nullptr));
  if (mapping.get() == nullptr || job.get() == nullptr) {
    error = "Cannot allocate isolated compiler resources";
    return false;
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                            JOB_OBJECT_LIMIT_PROCESS_MEMORY |
                                            JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
  limits.BasicLimitInformation.ActiveProcessLimit = 1;
  limits.ProcessMemoryLimit = static_cast<SIZE_T>(memoryLimit);
  if (!SetInformationJobObject(job.get(),
                               JobObjectExtendedLimitInformation,
                               &limits,
                               sizeof(limits))) {
    error = "Cannot enforce compiler process limits";
    return false;
  }
  std::byte* shared = static_cast<std::byte*>(
    MapViewOfFile(mapping.get(), FILE_MAP_ALL_ACCESS, 0, 0, kCapacity));
  if (shared == nullptr) {
    error = "Cannot map compiler input";
    return false;
  }
  const std::uint64_t inputSize = input.size();
  std::memcpy(shared, &inputSize, sizeof(inputSize));
  std::memcpy(shared + kHeaderBytes, input.data(), input.size());
  UnmapViewOfFile(shared);

  SIZE_T attributeBytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
  std::vector<std::byte> attributeStorage(attributeBytes);
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.lpAttributeList =
    reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
  if (!InitializeProcThreadAttributeList(
        startup.lpAttributeList, 1, 0, &attributeBytes)) {
    error = "Cannot initialize compiler handle isolation";
    return false;
  }
  HANDLE inherited = mapping.get();
  if (!UpdateProcThreadAttribute(startup.lpAttributeList,
                                 0,
                                 PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                 static_cast<void*>(&inherited),
                                 sizeof(inherited),
                                 nullptr,
                                 nullptr)) {
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    error = "Cannot restrict inherited compiler handles";
    return false;
  }
  std::wstring command =
    L"\"" + executable + L"\" " +
    std::to_wstring(reinterpret_cast<std::uintptr_t>(mapping.get())) + L" " +
    std::to_wstring(engineOptions);
  PROCESS_INFORMATION process{};
  const BOOL created = CreateProcessW(executable.c_str(),
                                      command.data(),
                                      nullptr,
                                      nullptr,
                                      TRUE,
                                      CREATE_NO_WINDOW | CREATE_SUSPENDED |
                                        EXTENDED_STARTUPINFO_PRESENT,
                                      nullptr,
                                      nullptr,
                                      &startup.StartupInfo,
                                      &process);
  DeleteProcThreadAttributeList(startup.lpAttributeList);
  if (!created) {
    error = "Cannot launch bundled IllumoWasmCompiler.exe";
    return false;
  }
  CompilerHandle processHandle(process.hProcess);
  CompilerHandle threadHandle(process.hThread);
  if (!AssignProcessToJobObject(job.get(), processHandle.get())) {
    TerminateProcess(processHandle.get(), 1);
    WaitForSingleObject(processHandle.get(), INFINITE);
    error = "Cannot contain compiler process in a job";
    return false;
  }
  if (ResumeThread(threadHandle.get()) == static_cast<DWORD>(-1) ||
      WaitForSingleObject(processHandle.get(), timeoutMilliseconds) !=
        WAIT_OBJECT_0) {
    TerminateJobObject(job.get(), 1);
    WaitForSingleObject(processHandle.get(), INFINITE);
    error = "WASM compiler exceeded its deadline";
    return false;
  }
  DWORD exitCode = 1;
  if (!GetExitCodeProcess(processHandle.get(), &exitCode) || exitCode != 0) {
    // Codes 2-4 are the helper's own verdicts; others are terminations.
    if (exitCode == 3) {
      error = "WASM module failed to compile";
    } else if (exitCode == 4) {
      error = "Compiled WASM artifact exceeds the compiler buffer";
    } else {
      char code[16] = {};
      std::snprintf(code, sizeof(code), "0x%08lX", exitCode);
      error = std::string("WASM compiler terminated (") + code +
              "); it may have exceeded its " +
              std::to_string(memoryLimit / (1024u * 1024u)) + " MiB limit";
    }
    return false;
  }
  shared = static_cast<std::byte*>(
    MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, kCapacity));
  if (shared == nullptr) {
    error = "Cannot read compiler artifact";
    return false;
  }
  std::uint64_t outputSize = 0;
  std::memcpy(&outputSize, shared + sizeof(inputSize), sizeof(outputSize));
  if (outputSize == 0 || outputSize > kCapacity - kHeaderBytes) {
    UnmapViewOfFile(shared);
    error = "Invalid compiler artifact size";
    return false;
  }
  try {
    artifact.assign(shared + kHeaderBytes, shared + kHeaderBytes + outputSize);
  } catch (...) {
    UnmapViewOfFile(shared);
    throw;
  }
  UnmapViewOfFile(shared);
  return true;
}
