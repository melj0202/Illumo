#pragma once
#include "RuleSet.h"

// Wireworld (Brian Silverman, 1987): four-state CA for digital circuits.
// Encoding chosen so value 0 = electron head — RuleSet::countAliveNeighbors
// already counts heads for the conductor birth rule.
//
// States:
//   HEAD (0)       electron head
//   EMPTY (1)      empty / background
//   TAIL (2)       electron tail
//   CONDUCTOR (3)  wire / copper
class WireworldRuleSet : public RuleSet
{
public:
  static constexpr unsigned char CELL_HEAD = 0;
  static constexpr unsigned char CELL_EMPTY = 1;
  static constexpr unsigned char CELL_TAIL = 2;
  static constexpr unsigned char CELL_CONDUCTOR = 3;

  WireworldRuleSet(CellGrid* targetCanvas)
    : RuleSet(targetCanvas)
  {
  }
  ~WireworldRuleSet() override = default;

  unsigned char nextState(unsigned char cell,
                          unsigned char headNeighbors) const override final;
  void evalCell(const unsigned char& target,
                unsigned char dest[3]) const override;
  std::string getRuleTag() const override { return "WIREWORLD"; }
  unsigned int getStateCount() const override { return 4u; }
  std::string getStateName(unsigned char state) const override
  {
    switch (state) {
      case CELL_HEAD:
        return "Head";
      case CELL_EMPTY:
        return "Empty";
      case CELL_TAIL:
        return "Tail";
      case CELL_CONDUCTOR:
        return "Conductor";
      default:
        return "Unknown";
    }
  }
};
