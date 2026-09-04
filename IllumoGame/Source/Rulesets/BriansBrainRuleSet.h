#pragma once
#include "RuleSet.h"

class BriansBrainRuleSet : public RuleSet
{
public:
  BriansBrainRuleSet(CellGrid* targetCanvas)
    : RuleSet(targetCanvas)
  {
  }
  ~BriansBrainRuleSet() override = default;

  unsigned char nextState(unsigned char cell,
                          unsigned char aliveNeighbors) const override final;
  void evalCell(const unsigned char& target,
                unsigned char dest[3]) const override;
  std::string getRuleTag() override { return "BRIANS_BRAIN"; }
};

using BrainsBrainRuleSet = BriansBrainRuleSet;
