#pragma once
#include "LifeLikeRuleSet.h"

class DayAndNightRuleSet : public LifeLikeRuleSet
{
public:
  DayAndNightRuleSet(CellGrid* targetCanvas)
    : LifeLikeRuleSet(targetCanvas,
                      "DAY_AND_NIGHT",
                      (1u << 3) | (1u << 6) | (1u << 7) | (1u << 8),
                      (1u << 3) | (1u << 4) | (1u << 6) | (1u << 7) | (1u << 8))
  {
  }
};
