#pragma once

#include "RuleSet.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

struct RuleDefinition
{
  std::string id;   // e.g. "GAME_OF_LIFE"
  std::string name; // e.g. "Conway's Game of Life"
  std::string
    family;         // "life_like", "elementary_1d", "generations", "wireworld"
  std::string rule; // e.g. "B3/S23"
  unsigned int birthMask = 0u;
  unsigned int surviveMask = 0u;
  unsigned int ruleNumber = 0u;
  bool hasCustomPalette = false;
  std::array<unsigned char, 3> aliveColor = { 0, 0, 0 };
  std::array<unsigned char, 3> deadColor = { 255, 255, 255 };
};

class RuleSetRegistry
{
public:
  static RuleSetRegistry& instance();

  static std::string normalizeId(std::string id);
  static bool parseLifeLikeRuleString(const std::string& ruleStr,
                                      unsigned int& outBirth,
                                      unsigned int& outSurvive);

  RuleSetRegistry();

  bool registerRule(const RuleDefinition& def);
  bool loadFromText(const std::string& text);
  void loadBuiltinDefaults();
  void clear();

  bool isKnownRule(const std::string& id) const;
  std::vector<std::string> getKnownRules() const;
  const RuleDefinition* getRuleDefinition(const std::string& id) const;

  std::unique_ptr<RuleSet> createRuleSet(const std::string& id,
                                         CellGrid* canvas = nullptr) const;

private:
  std::vector<RuleDefinition> rules;
};
