#pragma once
#include "LifeLikeRuleSet.h"

class SeedsRuleSet : public LifeLikeRuleSet
{
public:
  SeedsRuleSet(CellGrid* targetCanvas)
    : LifeLikeRuleSet(targetCanvas, "SEEDS", 1u << 2, 0u)
  {
  }
};
