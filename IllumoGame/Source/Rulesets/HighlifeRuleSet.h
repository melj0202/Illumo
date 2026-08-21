#pragma once
#include "LifeLikeRuleSet.h"

class HighlifeRuleSet : public LifeLikeRuleSet
{
public:
  HighlifeRuleSet(CellGrid* targetCanvas)
    : LifeLikeRuleSet(targetCanvas,
                      "HIGHLIFE",
                      (1u << 3) | (1u << 6),
                      (1u << 2) | (1u << 3))
  {
  }
};
