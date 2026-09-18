#include <Illumo/Services/EnvVars.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

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
      std::fputs(
        "EnvVars: configuration is unreadable; preserving original file\n",
        stderr);
      return;
    }
    const nlohmann::json j = nlohmann::json::parse(file);
    if (!j.is_object() || file.bad()) {
      std::fputs(
        "EnvVars: invalid configuration object; preserving original file\n",
        stderr);
      return;
    }
    std::unordered_map<std::string, EnvVar> staged = m_vars;
    for (nlohmann::json::const_iterator it = j.cbegin(); it != j.cend(); ++it) {
      const nlohmann::json& item = it.value();
      if (item.is_string()) {
        staged[it.key()] = parseValue(item.get<std::string>());
      } else if (item.is_object()) {
        staged[it.key()] = parseValue(item.value("value", ""));
      }
    }
    m_vars.swap(staged);
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
    nlohmann::json j = nlohmann::json::object();
    for (const std::pair<const std::string, EnvVar>& pair : m_vars) {
      j[pair.first] = pair.second.value;
    }
    const std::string serialized = j.dump(1);
    std::ofstream file(m_filePath);
    file << serialized;
    file.close();
    if (!file) {
      std::fputs("EnvVars: configuration save failed\n", stderr);
    }
  } catch (...) {
    std::fputs("EnvVars: configuration save failed\n", stderr);
  }
}

EnvVar
EnvVars::parseValue(const std::string& value)
{
  EnvVar var;
  var.value = value;

  try {
    var.valueAsLong = std::stol(value);
  } catch (...) {
    var.valueAsLong = 0L;
  }

  try {
    var.valueAsDouble = std::stod(value);
  } catch (...) {
    var.valueAsDouble = 0.0;
  }

  std::string lowerValue = value;
  std::transform(lowerValue.begin(),
                 lowerValue.end(),
                 lowerValue.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  var.valueAsBool = (lowerValue == "true" || lowerValue == "1" ||
                     lowerValue == "yes" || lowerValue == "on");

  return var;
}

void
EnvVars::setVar(const std::string& key, const std::string& value)
{
  m_vars[key] = parseValue(value);
}

void
EnvVars::setVar(const std::string& key, const double& value)
{
  setVar(key, std::to_string(value));
}

void
EnvVars::setVar(const std::string& key, const int& value)
{
  setVar(key, std::to_string(value));
}

void
EnvVars::setVar(const std::string& key, const long& value)
{
  setVar(key, std::to_string(value));
}

void
EnvVars::setVar(const std::string& key, const bool& value)
{
  setVar(key, std::to_string(value));
}

void
EnvVars::setVar(const std::string& key, const unsigned int& value)
{
  setVar(key, std::to_string(value));
}

void
EnvVars::setVar(const std::string& key, const unsigned long& value)
{
  setVar(key, std::to_string(value));
}

void
EnvVars::setVar(const std::string& key, const unsigned long long& value)
{
  setVar(key, std::to_string(value));
}

void
EnvVars::setVar(const std::string& key, const char& value)
{
  setVar(key, std::to_string(value));
}

void
EnvVars::setVar(const std::string& key, const char* value)
{
  setVar(key, std::string(value));
}
const EnvVar&
EnvVars::getVar(const std::string& key)
{
  auto it = m_vars.find(key);
  if (it != m_vars.end()) {
    return it->second;
  }
  static const EnvVar defaultVar = { "", 0L, 0.0, false };
  return defaultVar;
}
