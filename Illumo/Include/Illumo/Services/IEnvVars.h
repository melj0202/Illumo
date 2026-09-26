#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

struct EnvVar
{
  std::string value;
  long valueAsLong;
  double valueAsDouble;
  bool valueAsBool;
};

// Transparent hashing: lookups by std::string_view or a literal build no
// temporary std::string. Settings are read every frame, and wasm32 strings
// keep only about 10 characters inline.
struct EnvVarKeyHash
{
  using is_transparent = void;
  std::size_t operator()(std::string_view key) const noexcept
  {
    return std::hash<std::string_view>{}(key);
  }
};
using EnvVarMap =
  std::unordered_map<std::string, EnvVar, EnvVarKeyHash, std::equal_to<>>;

class IEnvVars
{
public:
  IEnvVars() = default;
  virtual ~IEnvVars() = default;

  virtual void load() = 0;
  virtual void save() = 0;

  virtual void setVar(const std::string& key, const std::string& value) = 0;
  virtual void setVar(const std::string& key, const long& value) = 0;
  virtual void setVar(const std::string& key, const double& value) = 0;
  virtual void setVar(const std::string& key, const bool& value) = 0;
  virtual void setVar(const std::string& key, const unsigned int& value) = 0;
  virtual void setVar(const std::string& key, const unsigned long& value) = 0;
  virtual void setVar(const std::string& key,
                      const unsigned long long& value) = 0;
  virtual void setVar(const std::string& key, const char& value) = 0;
  virtual void setVar(const std::string& key, const char* value) = 0;
  virtual void setVar(const std::string& key, const int& value) = 0;
  virtual const EnvVar& getVar(std::string_view key) = 0;
  virtual const EnvVarMap& getVars() const = 0;
};
