#pragma once
#include "RuleSet.h"

class Rule184RuleSet : public RuleSet
{
public:
  Rule184RuleSet(CellGrid* targetCanvas)
    : RuleSet(targetCanvas)
  {
  }
  ~Rule184RuleSet() override = default;

  std::string getRuleTag() override { return "RULE_184"; }
  NeighborhoodKind getNeighborhoodKind() const override
  {
    return NeighborhoodKind::Elementary1D;
  }
  unsigned char nextElementary(unsigned char left,
                               unsigned char center,
                               unsigned char right) const override
  {
    const unsigned int index = ((left == 0) ? 4u : 0u) |
                               ((center == 0) ? 2u : 0u) |
                               ((right == 0) ? 1u : 0u);
    const unsigned int bit = (184u >> index) & 1u;
    return (bit != 0u) ? 0 : 1;
  }

  void evalCell(const unsigned char& target,
                unsigned char dest[3]) const override;
};
