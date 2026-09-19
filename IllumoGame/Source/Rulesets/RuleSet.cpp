#include "RuleSet.h"
#include <tracy/Tracy.hpp>

RuleSet::RuleSet(const TransitionTable& precompiledTransitions)
  : transitionTable(precompiledTransitions)
  , transitionTableReady(true)
  , transitionRevision(1u)
{
}

const RuleSet::TransitionTable&
RuleSet::getTransitionTable() const
{
  if (transitionTableReady) {
    return transitionTable;
  }
  ZoneScopedN("Rule.buildTransitionTable");
  for (std::size_t state = 0u; state < kCellStateCount; ++state) {
    for (std::size_t neighbors = 0u; neighbors < kNeighborCountCount;
         ++neighbors) {
      transitionTable[state * kNeighborCountCount + neighbors] =
        nextState(static_cast<unsigned char>(state),
                  static_cast<unsigned char>(neighbors));
    }
  }
  transitionTableReady = true;
  return transitionTable;
}
