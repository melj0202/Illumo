#include "BriansBrainRuleSet.h"
#include <cstring>

namespace {
const unsigned char CELL_DEAD = 1;
const unsigned char CELL_DYING = 2;
const unsigned char CELL_ALIVE = 0;
}

unsigned char
BriansBrainRuleSet::nextState(unsigned char cell,
                              unsigned char aliveNeighbors) const
{
  // Dead + 2 live → alive; alive → dying; dying → dead
  if (cell == CELL_DEAD && aliveNeighbors == 2) {
    return CELL_ALIVE;
  }
  if (cell == CELL_ALIVE) {
    return CELL_DYING;
  }
  if (cell == CELL_DYING) {
    return CELL_DEAD;
  }
  return CELL_DEAD;
}

void
BriansBrainRuleSet::evalCell(const unsigned char& target,
                             unsigned char dest[3]) const
{
  if (target == CELL_DEAD) {
    std::memset(dest, 255, 3);
  } else if (target == CELL_ALIVE) {
    std::memset(dest, 0, 3);
  } else {
    dest[0] = 0;
    dest[1] = 164;
    dest[2] = 128;
  }
}
