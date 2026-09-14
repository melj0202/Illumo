#pragma once

#include <filesystem>

class RuleSetRegistry;

class RuleCatalogLoader
{
public:
  static bool loadFromFile(RuleSetRegistry& registry,
                           const std::filesystem::path& path);
  static bool loadFromLocations(
    RuleSetRegistry& registry,
    const std::filesystem::path& executableDirectory,
    const std::filesystem::path& workingDirectory);
  static bool loadFromDefaultLocations(RuleSetRegistry& registry);
};
