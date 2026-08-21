#pragma once
#include "LifeLikeRuleSet.h"

class LifeWithoutDeathRuleSet : public LifeLikeRuleSet
{
public:
  LifeWithoutDeathRuleSet(CellGrid* targetCanvas)
    : LifeLikeRuleSet(targetCanvas,
                      "LIFE_WITHOUT_DEATH",
                      1u << 3,
                      (1u << 9) - 1u)
  {
  }
};
