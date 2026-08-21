#pragma once
#include "RuleSet.h"
#include <cstring>
#include <string>
#include <utility>

class LifeLikeRuleSet : public RuleSet
{
public:
  LifeLikeRuleSet(CellGrid* targetCanvas,
                  std::string tag,
                  unsigned int birthMask,
                  unsigned int surviveMask)
    : RuleSet(targetCanvas)
    , ruleTag(std::move(tag))
    , birthMask(birthMask)
    , surviveMask(surviveMask)
  {
  }
  ~LifeLikeRuleSet() override = default;

  unsigned char nextState(unsigned char cell,
                          unsigned char aliveNeighbors) const override final
  {
    const unsigned int bit = 1u << aliveNeighbors;
    if (cell == 0) {
      return ((surviveMask & bit) != 0u) ? 0 : 1;
    }
    return ((birthMask & bit) != 0u) ? 0 : 1;
  }

  void evalCell(const unsigned char& target,
                unsigned char dest[3]) const override
  {
    if (target == 1) {
      std::memset(dest, 255, 3);
    } else {
      std::memset(dest, 0, 3);
    }
  }

  std::string getRuleTag() override { return ruleTag; }

private:
  std::string ruleTag;
  unsigned int birthMask;
  unsigned int surviveMask;
};
