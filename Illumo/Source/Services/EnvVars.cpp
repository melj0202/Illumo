#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

std::filesystem::path
EnvVars::ApplicationConfigPath()
{
#ifdef _WIN32
  std::wstring executablePath(32768, L'\0');
  const unsigned long pathLength =
    GetModuleFileNameW(nullptr,
                       executablePath.data(),
                       static_cast<unsigned long>(executablePath.size()));
  if (pathLength > 0 &&
      static_cast<size_t>(pathLength) < executablePath.size()) {
    executablePath.resize(pathLength);
    return std::filesystem::path(executablePath).parent_path() / "envvars.json";
  }
#elif defined(__linux__)
  char executablePath[4096];
  const ssize_t pathLength =
    ::readlink("/proc/self/exe", executablePath, sizeof(executablePath) - 1);
  if (pathLength > 0 &&
      static_cast<size_t>(pathLength) < sizeof(executablePath) - 1) {
    executablePath[pathLength] = '\0';
    return std::filesystem::path(executablePath).parent_path() / "envvars.json";
  }
#endif
  return std::filesystem::current_path() / "envvars.json";
}

// Settings load before, and save after, the logger's lifetime; without a
// logger the problem still reaches stderr.
static void
reportSettingsProblem(const std::string& text)
{
  if (Logger::getLogFilePath().empty()) {
    std::fputs(("EnvVars: " + text + "\n").c_str(), stderr);
    return;
  }
  Logger::LogWarning(text);
}

void
EnvVars::load()
{
  m_persistenceEligible = false;
  try {
    std::error_code error;
    const bool exists = std::filesystem::exists(m_filePath, error);
    if (!exists && !error) {
      m_persistenceEligible = true;
      return;
    }
    std::ifstream file(m_filePath);
    if (error || !file.is_open()) {
      reportSettingsProblem("Settings file " + m_filePath.string() +
                            " is unreadable; using defaults and preserving it");
      return;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad() || contents.bad() || !loadText(contents.str())) {
      reportSettingsProblem("Settings file " + m_filePath.string() +
                            " is not a valid settings object; using defaults "
                            "and preserving it");
      return;
    }
    m_persistenceEligible = true;
  } catch (...) {
    std::fputs("EnvVars: configuration load failed; preserving original file\n",
               stderr);
  }
}

void
EnvVars::save()
{
  if (!m_persistenceEligible) {
    return;
  }
  try {
    const std::string serialized = saveText();
    std::ofstream file(m_filePath);
    file << serialized;
    file.close();
    if (!file) {
      reportSettingsProblem("Settings could not be saved to " +
                            m_filePath.string());
    }
  } catch (...) {
    // Runs during teardown: nothing may escape.
    std::fputs("EnvVars: configuration save failed\n", stderr);
  }
}
