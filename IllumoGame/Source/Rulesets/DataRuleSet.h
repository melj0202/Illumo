#pragma once

#include "RuleSet.h"
#include "RuleSetRegistry.h"
#include <array>
#include <string>
#include <vector>

class DataRuleSet : public RuleSet
{
public:
  DataRuleSet(const RuleSetDefinition& definition,
              const RuleFamilyDefinition& family,
              CellGrid* canvas);
  ~DataRuleSet() override = default;

  std::string getRuleTag() const override { return definition.id; }
  std::string getFamilyTag() const override { return family.id; }
  unsigned int getStateCount() const override { return family.stateCount; }
  std::string getStateName(unsigned char state) const override;
  bool isValidState(unsigned char state) const override
  {
    return static_cast<unsigned int>(state) < family.stateCount;
  }
  NeighborhoodKind getNeighborhoodKind() const override;
  unsigned char nextState(unsigned char cell,
                          unsigned char neighborCount) const override;
  unsigned char nextElementary(unsigned char left,
                               unsigned char center,
                               unsigned char right) const override;
  void evalCell(const unsigned char& target,
                unsigned char dest[3]) const override;

private:
  RuleSetDefinition definition;
  RuleFamilyDefinition family;
};
