#pragma once

#include <string>
#include <vector>

struct RuleFamilyDefinition;
struct RuleSetDefinition;
class RuleSetRegistry;

// Portable user-overlay policy shared by the native file loader and the WASM
// storage adapter. Callers stage onto a registry that already holds the
// shipped catalog plus any existing user overlay; nothing here touches files.
class RuleCatalogOverlay
{
public:
  static bool stageFamilies(RuleSetRegistry& staged,
                            const std::vector<RuleFamilyDefinition>& families,
                            std::string* error);
  static bool stageRules(RuleSetRegistry& staged,
                         const std::vector<RuleSetDefinition>& rules,
                         std::string* error);
  // Serialized user-only overlays (`families.user.json`/`rulesets.user.json`).
  static std::string familiesText(const RuleSetRegistry& staged);
  static std::string rulesText(const RuleSetRegistry& staged);
  // One exported family plus ruleset package.
  static bool packageText(const RuleFamilyDefinition& family,
                          const RuleSetDefinition& definition,
                          std::string* text,
                          std::string* error);
};
