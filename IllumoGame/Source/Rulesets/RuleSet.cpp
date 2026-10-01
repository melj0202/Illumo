#include "RuleSet.h"
#include <Illumo/Foundation/Profile.h>

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
  ILLUMO_PROFILE_ZONE("Rule.buildTransitionTable");
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
