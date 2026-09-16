#include "DataRuleSet.h"

DataRuleSet::DataRuleSet(const RuleSetDefinition& sourceDefinition,
                         const RuleFamilyDefinition& sourceFamily,
                         CellGrid* canvas)
  : RuleSet(canvas, sourceDefinition.transitionTable)
  , definition(sourceDefinition)
  , family(sourceFamily)
{
}

std::string
DataRuleSet::getStateName(unsigned char state) const
{
  if (static_cast<std::size_t>(state) >= family.stateNames.size()) {
    return "Unknown";
  }
  return family.stateNames[state];
}

RuleSet::NeighborhoodKind
DataRuleSet::getNeighborhoodKind() const
{
  return family.kind == RuleFamily::Elementary1D
           ? NeighborhoodKind::Elementary1D
           : NeighborhoodKind::MooreCount;
}

unsigned char
DataRuleSet::nextState(unsigned char cell, unsigned char neighborCount) const
{
  if (static_cast<unsigned int>(cell) >= family.stateCount ||
      neighborCount >= kNeighborCountCount) {
    return 1u;
  }
  return definition.transitionTable[transitionIndex(cell, neighborCount)];
}

unsigned char
DataRuleSet::nextElementary(unsigned char left,
                            unsigned char center,
                            unsigned char right) const
{
  const unsigned int index = ((left == 0u) ? 4u : 0u) |
                             ((center == 0u) ? 2u : 0u) |
                             ((right == 0u) ? 1u : 0u);
  return definition.elementaryTransitions[index];
}

void
DataRuleSet::evalCell(const unsigned char& target, unsigned char dest[3]) const
{
  if (dest == nullptr) {
    return;
  }
  if (static_cast<std::size_t>(target) < family.stateColors.size()) {
    const std::array<unsigned char, 3>& color = family.stateColors[target];
    dest[0] = color[0];
    dest[1] = color[1];
    dest[2] = color[2];
    return;
  }
  dest[0] = 160u;
  dest[1] = 160u;
  dest[2] = 160u;
}
