#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <Illumo/Platform/ChildProcess.h>
#include <string>
#include <vector>

// The child runs inside a job object that kills it when the job's last handle
// closes, so it never outlives this process, even when this one crashes.
struct ChildProcess::State
{
  HANDLE job = nullptr;
  HANDLE process = nullptr;
};

static std::wstring
wide(const std::string& text)
{
  if (text.empty()) {
    return std::wstring();
  }
  const int length = MultiByteToWideChar(
    CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring output(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8,
                      0,
                      text.data(),
                      static_cast<int>(text.size()),
                      output.data(),
                      length);
  return output;
}

// Quotes one argument so CommandLineToArgvW reads it back unchanged.
static void
appendArgument(std::wstring& line, const std::wstring& argument)
{
  if (!line.empty()) {
    line += L' ';
  }
  if (!argument.empty() &&
      argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
    line += argument;
    return;
  }
  line += L'"';
  std::size_t backslashes = 0;
  for (const wchar_t character : argument) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }
    if (character == L'"') {
      line.append(backslashes * 2u + 1u, L'\\');
    } else {
      line.append(backslashes, L'\\');
    }
    backslashes = 0;
    line += character;
  }
  line.append(backslashes * 2u, L'\\');
  line += L'"';
}

ChildProcess::ChildProcess()
  : m_state(std::make_unique<State>())
{
}

ChildProcess::~ChildProcess()
{
  stop();
}

bool
ChildProcess::start(const std::vector<std::string>& arguments,
                    std::string* error)
{
  stop();
  std::vector<wchar_t> executable(32768u, L'\0');
  const DWORD length = GetModuleFileNameW(
    nullptr, executable.data(), static_cast<DWORD>(executable.size()));
  if (length == 0u || length >= executable.size()) {
    if (error != nullptr) {
      *error = "the program path could not be read";
    }
    return false;
  }
  std::wstring line;
  appendArgument(line, std::wstring(executable.data(), length));
  for (const std::string& argument : arguments) {
    appendArgument(line, wide(argument));
  }
  std::vector<wchar_t> commandLine(line.begin(), line.end());
  commandLine.push_back(L'\0');
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (job == nullptr ||
      !SetInformationJobObject(
        job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
    if (job != nullptr) {
      CloseHandle(job);
    }
    if (error != nullptr) {
      *error = "a job object could not be created";
    }
    return false;
  }
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  // Suspended until it is in the job, so it cannot escape it.
  if (!CreateProcessW(executable.data(),
                      commandLine.data(),
                      nullptr,
                      nullptr,
                      FALSE,
                      CREATE_SUSPENDED,
                      nullptr,
                      nullptr,
                      &startup,
                      &process)) {
    CloseHandle(job);
    if (error != nullptr) {
      *error = "CreateProcess failed with error " +
               std::to_string(static_cast<unsigned long>(GetLastError()));
    }
    return false;
  }
  if (!AssignProcessToJobObject(job, process.hProcess)) {
    TerminateProcess(process.hProcess, 1);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    if (error != nullptr) {
      *error = "the process could not join its job object";
    }
    return false;
  }
  ResumeThread(process.hThread);
  CloseHandle(process.hThread);
  m_state->job = job;
  m_state->process = process.hProcess;
  return true;
}

bool
ChildProcess::running()
{
  return m_state->process != nullptr &&
         WaitForSingleObject(m_state->process, 0) == WAIT_TIMEOUT;
}

void
ChildProcess::stop()
{
  if (m_state->job != nullptr) {
    TerminateJobObject(m_state->job, 0);
    CloseHandle(m_state->job);
    m_state->job = nullptr;
  }
  if (m_state->process != nullptr) {
    WaitForSingleObject(m_state->process, 5000);
    CloseHandle(m_state->process);
    m_state->process = nullptr;
  }
}
