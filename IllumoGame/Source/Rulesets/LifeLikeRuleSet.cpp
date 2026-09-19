#include "LifeLikeRuleSet.h"
#include <cstring>
#include <utility>

LifeLikeRuleSet::LifeLikeRuleSet(std::string tag,
                                 unsigned int birthMask,
                                 unsigned int surviveMask,
                                 std::array<unsigned char, 3> aliveColor,
                                 std::array<unsigned char, 3> deadColor)
  : RuleSet()
  , ruleTag(std::move(tag))
  , birthMask(birthMask)
  , surviveMask(surviveMask)
  , aliveColor(aliveColor)
  , deadColor(deadColor)
{
}

unsigned char
LifeLikeRuleSet::nextState(unsigned char cell,
                           unsigned char aliveNeighbors) const
{
  if (aliveNeighbors > 8) {
    return 1;
  }
  const unsigned int bit = 1u << aliveNeighbors;
  if (cell == 0) {
    return ((surviveMask & bit) != 0u) ? 0 : 1;
  }
  return ((birthMask & bit) != 0u) ? 0 : 1;
}

void
LifeLikeRuleSet::evalCell(const unsigned char& target,
                          unsigned char dest[3]) const
{
  if (target == 1) {
    std::memcpy(dest, deadColor.data(), 3);
  } else {
    std::memcpy(dest, aliveColor.data(), 3);
  }
}
