#pragma once

#include "RuleSet.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

enum class RuleFamily
{
  LifeLike,
  Generations,
  MooreTable,
  Cyclic,
  SpeciesLife,
  LargerThanLife,
  Hodgepodge,
  Turmite,
  LatticeGas,
  Dominance,
  Elementary1D
};

enum class RuleSeedPattern
{
  Automatic,
  Glider,
  SingleCell,
  Wire,
  ActiveSoup,
  PhaseSoup,
  SpeciesSoup,
  ExcitableBreak,
  TurmiteSwarm,
  ParticleCloud
};

struct RuleSetDefinition
{
  // A ruleset owns transition behavior and references one cell family.
  std::string id;
  std::string name;
  std::string familyId;
  bool builtIn = false;
  std::string rule; // e.g. "B3/S23"
  unsigned int birthMask = 0u;
  unsigned int surviveMask = 0u;
  unsigned int ruleNumber = 0u;
  unsigned int cyclicThreshold = 1u;
  unsigned int cyclicStep = 1u;
  unsigned int neighborhoodRadius = 1u;
  unsigned int birthMinimum = 3u;
  unsigned int birthMaximum = 3u;
  unsigned int survivalMinimum = 2u;
  unsigned int survivalMaximum = 3u;
  unsigned int infectionDivisor = 2u;
  unsigned int illDivisor = 3u;
  unsigned int infectionIncrement = 1u;
  unsigned int dominanceThreshold = 1u;
  std::vector<unsigned int> dominancePreyOffsets;
  std::string turnSequence;
  RuleSet::ExtendedNeighborhoodShape extendedNeighborhoodShape =
    RuleSet::ExtendedNeighborhoodShape::Square;
  bool includeCenter = false;
  RuleSeedPattern seedPattern = RuleSeedPattern::Automatic;
  unsigned int seedRadius = 18u;
  unsigned int seedDensity = 42u;
  // Compiled transition artifacts; these are not serialized into ruleset data.
  bool hasTransitionTable = false;
  unsigned int transitionTableStateCount = 0u;
  RuleSet::TransitionTable transitionTable{};
  std::array<unsigned char, 8> elementaryTransitions{};
};

struct RuleFamilyDefinition
{
  std::string id;
  std::string name;
  bool builtIn = false;
  RuleFamily kind = RuleFamily::LifeLike;
  unsigned int stateCount = 2u;
  std::vector<std::string> stateNames;
  std::vector<std::array<unsigned char, 3>> stateColors;
};

class RuleSetRegistry
{
public:
  static RuleSetRegistry& instance();

  static std::string normalizeId(std::string id);
  static bool parseFamily(const std::string& value, RuleFamily& family);
  static const char* familyName(RuleFamily family);
  static bool parseSeedPattern(const std::string& value,
                               RuleSeedPattern& pattern);
  static const char* seedPatternName(RuleSeedPattern pattern);
  static bool parseLifeLikeRuleString(const std::string& ruleStr,
                                      unsigned int& outBirth,
                                      unsigned int& outSurvive);

  RuleSetRegistry();
  RuleSetRegistry(const RuleSetRegistry&) = default;
  RuleSetRegistry& operator=(const RuleSetRegistry&) = default;
  RuleSetRegistry(RuleSetRegistry&&) noexcept = default;
  RuleSetRegistry& operator=(RuleSetRegistry&&) noexcept = default;

  bool registerFamily(const RuleFamilyDefinition& definition);
  bool registerRule(const RuleSetDefinition& def);
  bool loadFromText(const std::string& text);
  bool loadFamiliesFromText(const std::string& text);
  bool loadFromCatalogTexts(const std::string& familiesText,
                            const std::string& rulesText);
  bool loadRulePackage(const std::string& text);
  void clear();

  bool isKnownFamily(const std::string& id) const;
  bool isKnownRule(const std::string& id) const;
  std::vector<std::string> getKnownFamilies() const;
  std::vector<std::string> getKnownRules() const;
  std::vector<std::string> getKnownRules(const std::string& familyId) const;
  const RuleFamilyDefinition* getFamilyDefinition(const std::string& id) const;
  const RuleSetDefinition* getRuleSetDefinition(const std::string& id) const;
  const std::vector<RuleFamilyDefinition>& getFamilyDefinitions() const
  {
    return families;
  }
  const std::vector<RuleSetDefinition>& getDefinitions() const { return rules; }
  static std::string serializeFamilies(
    const std::vector<RuleFamilyDefinition>& definitions);
  static std::string serializeCatalog(
    const std::vector<RuleFamilyDefinition>& families,
    const std::vector<RuleSetDefinition>& definitions);
  static std::string serializeRulePackage(const RuleFamilyDefinition& family,
                                          const RuleSetDefinition& definition);

  std::unique_ptr<RuleSet> createRuleSet(const std::string& id) const;

private:
  std::vector<RuleFamilyDefinition> families;
  std::vector<RuleSetDefinition> rules;
};
