#include <Illumo/Foundation/ParseNumber.h>
#include <Illumo/Services/EnvValues.h>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

EnvVar
EnvValues::parseValue(const std::string& value)
{
  EnvVar var;
  var.value = value;

  // Leading numbers count, as with std::stol and std::stod: "12px" is 12.
  if (!parseInteger(value, &var.valueAsLong)) {
    var.valueAsLong = 0L;
  }
  if (!parseFloating(value, &var.valueAsDouble)) {
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
EnvValues::getVar(std::string_view key)
{
  EnvVarMap::const_iterator it = m_vars.find(key);
  if (it != m_vars.end()) {
    return it->second;
  }
  static const EnvVar defaultVar = { "", 0L, 0.0, false };
  return defaultVar;
}

bool
EnvValues::loadText(std::string_view text)
{
  const nlohmann::json document = nlohmann::json::parse(text, nullptr, false);
  if (document.is_discarded() || !document.is_object()) {
    return false;
  }
  EnvVarMap staged = m_vars;
  for (nlohmann::json::const_iterator item = document.cbegin();
       item != document.cend();
       ++item) {
    if (item.value().is_string()) {
      staged[item.key()] = parseValue(item.value().get<std::string>());
    } else if (item.value().is_object()) {
      const nlohmann::json& object = item.value();
      const nlohmann::json::const_iterator field = object.find("value");
      if (field == object.end()) {
        staged[item.key()] = parseValue("");
      } else if (field->is_string()) {
        staged[item.key()] = parseValue(field->get<std::string>());
      } else {
        return false;
      }
    }
  }
  m_vars.swap(staged);
  return true;
}

std::string
EnvValues::saveText() const
{
  nlohmann::json document = nlohmann::json::object();
  for (const std::pair<const std::string, EnvVar>& item : m_vars) {
    document[item.first] = item.second.value;
  }
  return document.dump(1, ' ', false, nlohmann::json::error_handler_t::replace);
}
