#pragma once
#include "LifeLikeRuleSet.h"

class GameOfLifeRuleSet : public LifeLikeRuleSet
{
public:
  GameOfLifeRuleSet(CellGrid* targetCanvas)
    : LifeLikeRuleSet(targetCanvas,
                      "GAME_OF_LIFE",
                      1u << 3,
                      (1u << 2) | (1u << 3))
  {
  }
};
