#include "RuleCatalogOverlay.h"
#include "Rulesets/RuleSetRegistry.h"

bool
RuleCatalogOverlay::stageFamilies(
  RuleSetRegistry& staged,
  const std::vector<RuleFamilyDefinition>& families,
  std::string* error)
{
  for (RuleFamilyDefinition definition : families) {
    const RuleFamilyDefinition* shippedFamily =
      RuleSetRegistry::instance().getFamilyDefinition(definition.id);
    if (shippedFamily != nullptr && shippedFamily->builtIn) {
      if (error != nullptr) {
        *error = "Built-in family IDs cannot be written to the user overlay.";
      }
      return false;
    }
    definition.builtIn = false;
    if (!staged.registerFamily(definition)) {
      if (error != nullptr) {
        *error =
          "A family definition is invalid or incompatible with its rules.";
      }
      return false;
    }
  }
  return true;
}

bool
RuleCatalogOverlay::stageRules(RuleSetRegistry& staged,
                               const std::vector<RuleSetDefinition>& rules,
                               std::string* error)
{
  for (const RuleSetDefinition& definition : rules) {
    const RuleSetDefinition* shippedRule =
      RuleSetRegistry::instance().getRuleSetDefinition(definition.id);
    if (shippedRule != nullptr && shippedRule->builtIn) {
      if (error != nullptr) {
        *error = "Built-in ruleset IDs cannot be written to the user overlay.";
      }
      return false;
    }
    if (!staged.registerRule(definition)) {
      if (error != nullptr) {
        *error = "A rule definition failed validation.";
      }
      return false;
    }
  }
  return true;
}

std::string
RuleCatalogOverlay::familiesText(const RuleSetRegistry& staged)
{
  std::vector<RuleFamilyDefinition> userFamilies;
  for (const RuleFamilyDefinition& family : staged.getFamilyDefinitions()) {
    if (!family.builtIn) {
      userFamilies.push_back(family);
    }
  }
  return RuleSetRegistry::serializeFamilies(userFamilies);
}

std::string
RuleCatalogOverlay::rulesText(const RuleSetRegistry& staged)
{
  std::vector<RuleSetDefinition> userRules;
  for (const RuleSetDefinition& rule : staged.getDefinitions()) {
    if (!rule.builtIn) {
      userRules.push_back(rule);
    }
  }
  return RuleSetRegistry::serializeCatalog(staged.getFamilyDefinitions(),
                                           userRules);
}

bool
RuleCatalogOverlay::packageText(const RuleFamilyDefinition& family,
                                const RuleSetDefinition& definition,
                                std::string* text,
                                std::string* error)
{
  RuleSetRegistry catalog;
  if (!catalog.registerFamily(family) || !catalog.registerRule(definition)) {
    if (error != nullptr) {
      *error = "The family and ruleset definitions failed validation.";
    }
    return false;
  }
  *text = RuleSetRegistry::serializeRulePackage(
    *catalog.getFamilyDefinition(family.id),
    *catalog.getRuleSetDefinition(definition.id));
  return true;
}
