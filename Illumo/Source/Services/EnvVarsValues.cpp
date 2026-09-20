#include <Illumo/Services/EnvValues.h>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

EnvVar
EnvValues::parseValue(const std::string& value)
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
EnvValues::setVar(const std::string& key, const std::string& value)
{
  m_vars[key] = parseValue(value);
}

void
EnvValues::setVar(const std::string& key, const double& value)
{
  setVar(key, std::to_string(value));
}

void
EnvValues::setVar(const std::string& key, const int& value)
{
  setVar(key, std::to_string(value));
}

void
EnvValues::setVar(const std::string& key, const long& value)
{
  setVar(key, std::to_string(value));
}

void
EnvValues::setVar(const std::string& key, const bool& value)
{
  setVar(key, std::to_string(value));
}

void
EnvValues::setVar(const std::string& key, const unsigned int& value)
{
  setVar(key, std::to_string(value));
}

void
EnvValues::setVar(const std::string& key, const unsigned long& value)
{
  setVar(key, std::to_string(value));
}

void
EnvValues::setVar(const std::string& key, const unsigned long long& value)
{
  setVar(key, std::to_string(value));
}

void
EnvValues::setVar(const std::string& key, const char& value)
{
  setVar(key, std::to_string(value));
}

void
EnvValues::setVar(const std::string& key, const char* value)
{
  setVar(key, std::string(value));
}
const EnvVar&
EnvValues::getVar(const std::string& key)
{
  std::unordered_map<std::string, EnvVar>::const_iterator it = m_vars.find(key);
  if (it != m_vars.end()) {
    return it->second;
  }
  static const EnvVar defaultVar = { "", 0L, 0.0, false };
  return defaultVar;
}

bool
EnvValues::loadText(std::string_view text)
{
  try {
    const nlohmann::json document = nlohmann::json::parse(text);
    if (!document.is_object()) {
      return false;
    }
    std::unordered_map<std::string, EnvVar> staged = m_vars;
    for (nlohmann::json::const_iterator item = document.cbegin();
         item != document.cend();
         ++item) {
      if (item.value().is_string()) {
        staged[item.key()] = parseValue(item.value().get<std::string>());
      } else if (item.value().is_object()) {
        staged[item.key()] = parseValue(item.value().value("value", ""));
      }
    }
    m_vars.swap(staged);
    return true;
  } catch (...) {
    return false;
  }
}

std::string
EnvValues::saveText() const
{
  nlohmann::json document = nlohmann::json::object();
  for (const std::pair<const std::string, EnvVar>& item : m_vars) {
    document[item.first] = item.second.value;
  }
  return document.dump(1);
}
