#pragma once
#include "RuleSet.h"

class Rule90RuleSet : public RuleSet
{
public:
  Rule90RuleSet(CellGrid* targetCanvas)
    : RuleSet(targetCanvas)
  {
  }
  ~Rule90RuleSet() override = default;

  std::string getRuleTag() override { return "RULE_90"; }
  NeighborhoodKind getNeighborhoodKind() const override
  {
    return NeighborhoodKind::Elementary1D;
  }
  unsigned char nextElementary(unsigned char left,
                               unsigned char center,
                               unsigned char right) const override
  {
    (void)center;
    const unsigned char leftOn = (left == 0) ? 1 : 0;
    const unsigned char rightOn = (right == 0) ? 1 : 0;
    return (leftOn ^ rightOn) != 0 ? 0 : 1;
  }

  void evalCell(const unsigned char& target,
                unsigned char dest[3]) const override;
};
