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
  std::string getRuleTag() const override { return "BRIANS_BRAIN"; }
  unsigned int getStateCount() const override { return 3u; }
  std::string getStateName(unsigned char state) const override
  {
    return state == 0u
             ? "Firing"
             : (state == 1u ? "Background"
                            : (state == 2u ? "Refractory" : "Unknown"));
  }
};

using BrainsBrainRuleSet = BriansBrainRuleSet;
