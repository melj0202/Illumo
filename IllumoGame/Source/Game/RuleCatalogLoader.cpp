#include "RuleCatalogLoader.h"
#include "Rulesets/RuleSetRegistry.h"
#include <Illumo/Services/EnvVars.h>
#include <fstream>
#include <sstream>

bool
RuleCatalogLoader::loadFromFile(RuleSetRegistry& registry,
                                const std::filesystem::path& path)
{
  try {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
      return false;
    }
    std::ostringstream text;
    text << file.rdbuf();
    return !file.bad() && !text.fail() && registry.loadFromText(text.str());
  } catch (...) {
    return false;
  }
}

bool
RuleCatalogLoader::loadFromLocations(
  RuleSetRegistry& registry,
  const std::filesystem::path& executableDirectory,
  const std::filesystem::path& workingDirectory)
{
  if (!executableDirectory.empty() &&
      loadFromFile(registry, executableDirectory / "rulesets.json")) {
    return true;
  }
  if (workingDirectory.empty()) {
    return false;
  }
  return loadFromFile(registry, workingDirectory / "rulesets.json") ||
         loadFromFile(registry,
                      workingDirectory / "IllumoGame" / "rulesets.json");
}

bool
RuleCatalogLoader::loadFromDefaultLocations(RuleSetRegistry& registry)
{
  std::filesystem::path executableDirectory;
  try {
    // The engine owns executable-path discovery; only catalog policy lives
    // here.
    executableDirectory = EnvVars::ApplicationConfigPath().parent_path();
  } catch (...) {
    // A failed executable lookup must not prevent working-directory fallback.
  }
  std::error_code error;
  const std::filesystem::path workingDirectory =
    std::filesystem::current_path(error);
  return loadFromLocations(registry,
                           executableDirectory,
                           error ? std::filesystem::path{} : workingDirectory);
}
