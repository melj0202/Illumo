#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <Illumo/Platform/ProcessRelaunch.h>
#include <string>
#include <utility>
#include <vector>

static std::vector<std::string>&
relaunchArguments()
{
  static std::vector<std::string> arguments;
  return arguments;
}

void
SetRelaunchArguments(std::vector<std::string> arguments)
{
  relaunchArguments() = std::move(arguments);
}

const std::vector<std::string>&
RelaunchArguments()
{
  return relaunchArguments();
}

static std::wstring
wideArgument(const std::string& text)
{
  if (text.empty()) {
    return {};
  }
  const int size = MultiByteToWideChar(
    CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring result(static_cast<size_t>(size > 0 ? size : 0), L'\0');
  if (size > 0) {
    MultiByteToWideChar(CP_UTF8,
                        0,
                        text.data(),
                        static_cast<int>(text.size()),
                        result.data(),
                        size);
  }
  return result;
}

// Quotes one argument the way CommandLineToArgvW undoes it: backslashes
// before a quote (or the closing quote) are doubled, quotes escaped.
static void
appendQuoted(std::vector<wchar_t>& commandLine, const std::wstring& argument)
{
  commandLine.push_back(L'"');
  std::size_t backslashes = 0;
  for (wchar_t character : argument) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }
    if (character == L'"') {
      commandLine.insert(commandLine.end(), backslashes * 2 + 1, L'\\');
      backslashes = 0;
      commandLine.push_back(L'"');
      continue;
    }
    commandLine.insert(commandLine.end(), backslashes, L'\\');
    backslashes = 0;
    commandLine.push_back(character);
  }
  commandLine.insert(commandLine.end(), backslashes * 2, L'\\');
  commandLine.push_back(L'"');
}

bool
RelaunchCurrentProcess(int argc, char** argv, std::string* error)
{
  // The original wide command line is replayed as-is, so quoting and
  // non-ASCII arguments survive; argc/argv are only for other platforms.
  // Replacement arguments are quoted for CommandLineToArgvW.
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
  std::vector<wchar_t> commandLine;
  if (relaunchArguments().empty()) {
    const wchar_t* original = GetCommandLineW();
    for (const wchar_t* character = original; *character != L'\0';
         ++character) {
      commandLine.push_back(*character);
    }
  } else {
    appendQuoted(commandLine, std::wstring(executable.data()));
    for (const std::string& argument : relaunchArguments()) {
      commandLine.push_back(L' ');
      appendQuoted(commandLine, wideArgument(argument));
    }
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
