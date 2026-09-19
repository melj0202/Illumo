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
              const RuleFamilyDefinition& family);
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
  unsigned int getNeighborhoodRadius() const override
  {
    return definition.neighborhoodRadius;
  }
  ExtendedNeighborhoodShape getExtendedNeighborhoodShape() const override
  {
    return definition.extendedNeighborhoodShape;
  }
  bool includesCenterInNeighborCount() const override
  {
    return definition.includeCenter;
  }
  unsigned char nextState(unsigned char cell,
                          unsigned char neighborCount) const override;
  unsigned char nextStateFromNeighborhood(
    unsigned char cell,
    const NeighborStateCounts& neighborStateCounts) const override;
  unsigned char nextStateFromDirectionalNeighborhood(
    unsigned char cell,
    const DirectionalNeighbors& neighbors) const override;
  unsigned char nextElementary(unsigned char left,
                               unsigned char center,
                               unsigned char right) const override;
  unsigned char nextStateFromExtendedCount(
    unsigned char cell,
    unsigned int aliveCount) const override;
  void evalCell(const unsigned char& target,
                unsigned char dest[3]) const override;

private:
  RuleSetDefinition definition;
  RuleFamilyDefinition family;
};
