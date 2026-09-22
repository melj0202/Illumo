#include "RuleCatalogLoader.h"
#include "RuleCatalogOverlay.h"
#include "Rulesets/RuleSetRegistry.h"
#include <Illumo/Platform/AtomicFile.h>
#include <Illumo/Services/EnvVars.h>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

static bool
readTextFile(const std::filesystem::path& path, std::string& text)
{
  try {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
      return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    if (file.bad() || stream.fail()) {
      return false;
    }
    text = stream.str();
    return true;
  } catch (...) {
    return false;
  }
}

bool
RuleCatalogLoader::loadFromFile(RuleSetRegistry& registry,
                                const std::filesystem::path& path)
{
  std::string text;
  return readTextFile(path, text) && registry.loadRulePackage(text);
}

bool
RuleCatalogLoader::loadFromCatalogFiles(
  RuleSetRegistry& registry,
  const std::filesystem::path& familiesPath,
  const std::filesystem::path& rulesPath)
{
  std::string familiesText;
  std::string rulesText;
  if (!readTextFile(rulesPath, rulesText)) {
    return false;
  }
  std::error_code error;
  if (std::filesystem::exists(familiesPath, error)) {
    if (error || !readTextFile(familiesPath, familiesText)) {
      return false;
    }
  } else if (error) {
    return false;
  }
  return registry.loadFromCatalogTexts(familiesText, rulesText);
}

bool
RuleCatalogLoader::loadFromLocations(
  RuleSetRegistry& registry,
  const std::filesystem::path& executableDirectory,
  const std::filesystem::path& workingDirectory)
{
  std::vector<std::filesystem::path> candidates;
  if (!executableDirectory.empty()) {
    candidates.push_back(executableDirectory);
  }
  if (!workingDirectory.empty()) {
    candidates.push_back(workingDirectory);
    candidates.push_back(workingDirectory / "IllumoGame");
  }
  RuleSetRegistry staged;
  bool foundBaseCatalog = false;
  for (const std::filesystem::path& directory : candidates) {
    RuleSetRegistry attempt;
    if (loadFromCatalogFiles(
          attempt, directory / "families.json", directory / "rulesets.json")) {
      staged = std::move(attempt);
      foundBaseCatalog = true;
      break;
    }
  }
  if (!foundBaseCatalog) {
    return false;
  }

  if (!workingDirectory.empty()) {
    const std::filesystem::path familiesOverlay =
      workingDirectory / "families.user.json";
    const std::filesystem::path rulesOverlay =
      workingDirectory / "rulesets.user.json";
    std::error_code error;
    if (std::filesystem::exists(familiesOverlay, error)) {
      std::string text;
      if (error || !readTextFile(familiesOverlay, text) ||
          !staged.loadFamiliesFromText(text)) {
        return false;
      }
    } else if (error) {
      return false;
    }
    error.clear();
    if (std::filesystem::exists(rulesOverlay, error)) {
      std::string text;
      if (error || !readTextFile(rulesOverlay, text) ||
          !staged.loadFromText(text)) {
        return false;
      }
    } else if (error) {
      return false;
    }
  }
  registry = std::move(staged);
  return true;
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

bool
RuleCatalogLoader::saveUserFamily(const std::filesystem::path& workingDirectory,
                                  const RuleFamilyDefinition& definition,
                                  std::string* error)
{
  return saveUserFamilies(
    workingDirectory, std::vector<RuleFamilyDefinition>{ definition }, error);
}

bool
RuleCatalogLoader::saveUserFamilies(
  const std::filesystem::path& workingDirectory,
  const std::vector<RuleFamilyDefinition>& definitions,
  std::string* error)
{
  if (workingDirectory.empty()) {
    if (error != nullptr) {
      *error = "The working directory is unavailable.";
    }
    return false;
  }
  RuleSetRegistry staged = RuleSetRegistry::instance();
  const std::filesystem::path path = workingDirectory / "families.user.json";
  std::error_code filesystemError;
  if (std::filesystem::exists(path, filesystemError)) {
    std::string text;
    if (filesystemError || !readTextFile(path, text) ||
        !staged.loadFamiliesFromText(text)) {
      if (error != nullptr) {
        *error = "The existing user families catalog is invalid or unreadable.";
      }
      return false;
    }
  } else if (filesystemError) {
    if (error != nullptr) {
      *error = "The user families catalog path could not be checked.";
    }
    return false;
  }
  if (!RuleCatalogOverlay::stageFamilies(staged, definitions, error)) {
    return false;
  }
  const std::string text = RuleCatalogOverlay::familiesText(staged);
  return AtomicFile::write(
    path,
    [&text](std::ostream& stream, std::string*) {
      stream << text;
      return static_cast<bool>(stream);
    },
    error);
}

bool
RuleCatalogLoader::saveUserRule(const std::filesystem::path& workingDirectory,
                                const RuleSetDefinition& definition,
                                std::string* error)
{
  return saveUserRules(
    workingDirectory, std::vector<RuleSetDefinition>{ definition }, error);
}

bool
RuleCatalogLoader::saveUserRules(
  const std::filesystem::path& workingDirectory,
  const std::vector<RuleSetDefinition>& definitions,
  std::string* error)
{
  if (workingDirectory.empty()) {
    if (error != nullptr) {
      *error = "The working directory is unavailable.";
    }
    return false;
  }
  RuleSetRegistry staged = RuleSetRegistry::instance();
  const std::filesystem::path familiesPath =
    workingDirectory / "families.user.json";
  const std::filesystem::path rulesPath =
    workingDirectory / "rulesets.user.json";
  std::error_code filesystemError;
  if (std::filesystem::exists(familiesPath, filesystemError)) {
    std::string text;
    if (filesystemError || !readTextFile(familiesPath, text) ||
        !staged.loadFamiliesFromText(text)) {
      if (error != nullptr) {
        *error = "The existing user families catalog is invalid or unreadable.";
      }
      return false;
    }
  } else if (filesystemError) {
    if (error != nullptr) {
      *error = "The user families catalog path could not be checked.";
    }
    return false;
  }
  filesystemError.clear();
  if (std::filesystem::exists(rulesPath, filesystemError)) {
    std::string text;
    if (filesystemError || !readTextFile(rulesPath, text) ||
        !staged.loadFromText(text)) {
      if (error != nullptr) {
        *error = "The existing user rules catalog is invalid or unreadable.";
      }
      return false;
    }
  } else if (filesystemError) {
    if (error != nullptr) {
      *error = "The user rules catalog path could not be checked.";
    }
    return false;
  }
  if (!RuleCatalogOverlay::stageRules(staged, definitions, error)) {
    return false;
  }
  const std::string familiesText = RuleCatalogOverlay::familiesText(staged);
  const std::string rulesText = RuleCatalogOverlay::rulesText(staged);
  if (!AtomicFile::write(
        familiesPath,
        [&familiesText](std::ostream& stream, std::string*) {
          stream << familiesText;
          return static_cast<bool>(stream);
        },
        error)) {
    return false;
  }
  return AtomicFile::write(
    rulesPath,
    [&rulesText](std::ostream& stream, std::string*) {
      stream << rulesText;
      return static_cast<bool>(stream);
    },
    error);
}

bool
RuleCatalogLoader::saveCatalog(const std::filesystem::path& path,
                               const RuleFamilyDefinition& family,
                               const RuleSetDefinition& definition,
                               std::string* error)
{
  std::string text;
  if (!RuleCatalogOverlay::packageText(family, definition, &text, error)) {
    return false;
  }
  return AtomicFile::write(
    path,
    [&text](std::ostream& stream, std::string*) {
      stream << text;
      return static_cast<bool>(stream);
    },
    error);
}
