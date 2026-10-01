#pragma once

#include "RuleSet.h"
#include <array>
#include <string>

class LifeLikeRuleSet : public RuleSet
{
public:
  LifeLikeRuleSet(std::string tag = "GAME_OF_LIFE",
                  unsigned int birthMask = (1u << 3),
                  unsigned int surviveMask = (1u << 2) | (1u << 3),
                  std::array<unsigned char, 3> aliveColor = { 0, 0, 0 },
                  std::array<unsigned char, 3> deadColor = { 255, 255, 255 });
  ~LifeLikeRuleSet() override = default;

  unsigned char nextState(unsigned char cell,
                          unsigned char aliveNeighbors) const override final;

  void evalCell(const unsigned char& target,
                unsigned char dest[3]) const override;

  std::string getRuleTag() const override { return ruleTag; }

  unsigned int getBirthMask() const { return birthMask; }
  unsigned int getSurviveMask() const { return surviveMask; }

private:
  std::string ruleTag;
  unsigned int birthMask;
  unsigned int surviveMask;
  std::array<unsigned char, 3> aliveColor;
  std::array<unsigned char, 3> deadColor;
};
