#pragma once
#include <Illumo/Services/EnvValues.h>
#include <filesystem>

class EnvVars : public EnvValues
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

private:
  std::filesystem::path m_filePath;
  bool m_persistenceEligible = false;
};
