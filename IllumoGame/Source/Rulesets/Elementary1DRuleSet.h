#pragma once

#include "RuleSet.h"
#include <array>
#include <string>

class Elementary1DRuleSet : public RuleSet
{
public:
  Elementary1DRuleSet(std::string tag,
                      unsigned int ruleNumber,
                      std::array<unsigned char, 3> aliveColor = { 0, 0, 0 },
                      std::array<unsigned char, 3> deadColor = { 255,
                                                                 255,
                                                                 255 });
  ~Elementary1DRuleSet() override = default;

  std::string getRuleTag() const override { return ruleTag; }

  NeighborhoodKind getNeighborhoodKind() const override
  {
    return NeighborhoodKind::Elementary1D;
  }

  unsigned char nextElementary(unsigned char left,
                               unsigned char center,
                               unsigned char right) const override;

  void evalCell(const unsigned char& target,
                unsigned char dest[3]) const override;

  unsigned int getRuleNumber() const { return ruleNumber; }

private:
  std::string ruleTag;
  unsigned int ruleNumber;
  std::array<unsigned char, 3> aliveColor;
  std::array<unsigned char, 3> deadColor;
};
