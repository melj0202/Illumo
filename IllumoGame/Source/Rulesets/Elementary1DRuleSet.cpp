#include "Elementary1DRuleSet.h"
#include <cstring>
#include <utility>

Elementary1DRuleSet::Elementary1DRuleSet(
  std::string tag,
  unsigned int ruleNumber,
  std::array<unsigned char, 3> aliveColor,
  std::array<unsigned char, 3> deadColor)
  : RuleSet()
  , ruleTag(std::move(tag))
  , ruleNumber(ruleNumber & 0xFFu)
  , aliveColor(aliveColor)
  , deadColor(deadColor)
{
}

unsigned char
Elementary1DRuleSet::nextElementary(unsigned char left,
                                    unsigned char center,
                                    unsigned char right) const
{
  const unsigned int index = ((left == 0) ? 4u : 0u) |
                             ((center == 0) ? 2u : 0u) |
                             ((right == 0) ? 1u : 0u);
  const unsigned int bit = (ruleNumber >> index) & 1u;
  return (bit != 0u) ? 0 : 1;
}

void
Elementary1DRuleSet::evalCell(const unsigned char& target,
                              unsigned char dest[3]) const
{
  if (target == 1) {
    std::memcpy(dest, deadColor.data(), 3);
  } else {
    std::memcpy(dest, aliveColor.data(), 3);
  }
}
