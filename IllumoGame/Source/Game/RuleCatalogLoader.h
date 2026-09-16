#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct RuleFamilyDefinition;
struct RuleSetDefinition;

class RuleSetRegistry;

class RuleCatalogLoader
{
public:
  static bool loadFromFile(RuleSetRegistry& registry,
                           const std::filesystem::path& path);
  static bool loadFromCatalogFiles(RuleSetRegistry& registry,
                                   const std::filesystem::path& familiesPath,
                                   const std::filesystem::path& rulesPath);
  static bool loadFromLocations(
    RuleSetRegistry& registry,
    const std::filesystem::path& executableDirectory,
    const std::filesystem::path& workingDirectory);
  static bool loadFromDefaultLocations(RuleSetRegistry& registry);
  static bool saveUserFamily(const std::filesystem::path& workingDirectory,
                             const RuleFamilyDefinition& definition,
                             std::string* error = nullptr);
  static bool saveUserFamilies(
    const std::filesystem::path& workingDirectory,
    const std::vector<RuleFamilyDefinition>& definitions,
    std::string* error = nullptr);
  static bool saveUserRule(const std::filesystem::path& workingDirectory,
                           const RuleSetDefinition& definition,
                           std::string* error = nullptr);
  static bool saveUserRules(const std::filesystem::path& workingDirectory,
                            const std::vector<RuleSetDefinition>& definitions,
                            std::string* error = nullptr);
  static bool saveCatalog(const std::filesystem::path& path,
                          const RuleFamilyDefinition& family,
                          const RuleSetDefinition& definition,
                          std::string* error = nullptr);
};
