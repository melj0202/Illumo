#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <Illumo/Platform/ProcessRelaunch.h>
#include <string>
#include <vector>

bool
RelaunchCurrentProcess(int argc, char** argv, std::string* error)
{
  // The original wide command line is replayed as-is, so quoting and
  // non-ASCII arguments survive; argc/argv are only for other platforms.
  (void)argc;
  (void)argv;
  std::vector<wchar_t> executable(32768u, L'\0');
  const DWORD length = GetModuleFileNameW(
    nullptr, executable.data(), static_cast<DWORD>(executable.size()));
  if (length == 0u || length >= executable.size()) {
    if (error != nullptr) {
      *error = "the program path could not be read";
    }
    return false;
  }
  const wchar_t* original = GetCommandLineW();
  std::vector<wchar_t> commandLine;
  for (const wchar_t* character = original; *character != L'\0'; ++character) {
    commandLine.push_back(*character);
  }
  // CreateProcessW may modify the command-line buffer in place.
  commandLine.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable.data(),
                      commandLine.data(),
                      nullptr,
                      nullptr,
                      FALSE,
                      0,
                      nullptr,
                      nullptr,
                      &startup,
                      &process)) {
    if (error != nullptr) {
      *error = "CreateProcess failed with error " +
               std::to_string(static_cast<unsigned long>(GetLastError()));
    }
    return false;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}
