#pragma once
#include <Illumo/Services/IEnvVars.h>
#include <filesystem>

class EnvVars : public IEnvVars
{
public:
  explicit EnvVars(const std::filesystem::path& filePath = "envvars.json")
    : m_filePath(filePath)
  {
    load();
  }
  ~EnvVars() override { save(); }

  static std::filesystem::path ApplicationConfigPath();

  void load() override;
  void save() override;

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
  std::filesystem::path m_filePath;
  std::unordered_map<std::string, EnvVar> m_vars;
  bool m_persistenceEligible = false;
};
