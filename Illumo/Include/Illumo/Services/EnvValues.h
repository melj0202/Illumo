#pragma once
#include <Illumo/Services/IEnvVars.h>
#include <string_view>

// Shared in-memory values and JSON format. Persistence remains a host or guest
// policy; this abstract base never opens files or saves during destruction.
class EnvValues : public IEnvVars
{
public:
  EnvValues() = default;
  ~EnvValues() override = default;
  EnvValues(const EnvValues&) = delete;
  EnvValues& operator=(const EnvValues&) = delete;
  EnvValues(EnvValues&&) = delete;
  EnvValues& operator=(EnvValues&&) = delete;

  bool loadText(std::string_view text);
  std::string saveText() const;

  void setVar(const std::string& key, const std::string& value) override;
  void setVar(const std::string& key, const long& value) override;
  void setVar(const std::string& key, const double& value) override;
  void setVar(const std::string& key, const bool& value) override;
  void setVar(const std::string& key, const unsigned int& value) override;
  void setVar(const std::string& key, const unsigned long& value) override;
  void setVar(const std::string& key, const unsigned long long& value) override;
  void setVar(const std::string& key, const char& value) override;
  void setVar(const std::string& key, const char* value) override;
  void setVar(const std::string& key, const int& value) override;
  const EnvVar& getVar(const std::string& key) override;
  const std::unordered_map<std::string, EnvVar>& getVars() const override
  {
    return m_vars;
  }

private:
  static EnvVar parseValue(const std::string& value);
  std::unordered_map<std::string, EnvVar> m_vars;
};
