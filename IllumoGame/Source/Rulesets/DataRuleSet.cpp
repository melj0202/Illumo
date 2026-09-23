#include "DataRuleSet.h"

namespace {

unsigned int
decodeBackgroundZero(unsigned char state)
{
  if (state == 1u) {
    return 0u;
  }
  if (state == 0u) {
    return 1u;
  }
  return state;
}

unsigned char
encodeBackgroundZero(unsigned int value)
{
  if (value == 0u) {
    return 1u;
  }
  if (value == 1u) {
    return 0u;
  }
  return static_cast<unsigned char>(value);
}

unsigned char
speciesState(unsigned int ordinal)
{
  return static_cast<unsigned char>(ordinal == 0u ? 0u : ordinal + 1u);
}

unsigned int
speciesOrdinal(unsigned char state)
{
  return state == 0u ? 0u : static_cast<unsigned int>(state - 1u);
}

unsigned int
collideHpp(unsigned int mask)
{
  if (mask == 0x5u) {
    return 0xAu;
  }
  if (mask == 0xAu) {
    return 0x5u;
  }
  return mask;
}

} // namespace

DataRuleSet::DataRuleSet(const RuleSetDefinition& sourceDefinition,
                         const RuleFamilyDefinition& sourceFamily)
  : RuleSet(sourceDefinition.transitionTable)
  , definition(sourceDefinition)
  , family(sourceFamily)
{
}

std::string
DataRuleSet::getStateName(unsigned char state) const
{
  if (static_cast<std::size_t>(state) >= family.stateNames.size()) {
    return "Unknown";
  }
  return family.stateNames[state];
}

RuleSet::NeighborhoodKind
DataRuleSet::getNeighborhoodKind() const
{
  if (family.kind == RuleFamily::Elementary1D) {
    return NeighborhoodKind::Elementary1D;
  }
  if (family.kind == RuleFamily::Cyclic) {
    const bool extended =
      definition.neighborhoodRadius > 1u ||
      definition.extendedNeighborhoodShape != ExtendedNeighborhoodShape::Square;
    return extended ? NeighborhoodKind::ExtendedRange
                    : NeighborhoodKind::MooreStateCounts;
  }
  if (family.kind == RuleFamily::VonNeumannTable ||
      family.kind == RuleFamily::Sandpile) {
    return NeighborhoodKind::VonNeumannDirectional;
  }
  if (family.kind == RuleFamily::SpeciesLife) {
    return NeighborhoodKind::MooreStateCounts;
  }
  if (family.kind == RuleFamily::Hodgepodge ||
      family.kind == RuleFamily::Dominance) {
    return NeighborhoodKind::MooreStateCounts;
  }
  if (family.kind == RuleFamily::Turmite ||
      family.kind == RuleFamily::LatticeGas) {
    return NeighborhoodKind::VonNeumannDirectional;
  }
  if (family.kind == RuleFamily::LargerThanLife) {
    return NeighborhoodKind::ExtendedRange;
  }
  return NeighborhoodKind::MooreCount;
}

unsigned char
DataRuleSet::nextState(unsigned char cell, unsigned char neighborCount) const
{
  if (static_cast<unsigned int>(cell) >= family.stateCount ||
      neighborCount >= kNeighborCountCount) {
    return 1u;
  }
  return definition.transitionTable[transitionIndex(cell, neighborCount)];
}

unsigned char
DataRuleSet::nextStateFromNeighborhood(
  unsigned char cell,
  const NeighborStateCounts& neighborStateCounts) const
{
  if (family.kind == RuleFamily::Hodgepodge) {
    if (static_cast<unsigned int>(cell) >= family.stateCount) {
      return 1u;
    }
    const unsigned int maximumLevel = family.stateCount - 1u;
    const unsigned int currentLevel = decodeBackgroundZero(cell);
    unsigned int infectedNeighbors = 0u;
    unsigned int illNeighbors = 0u;
    unsigned int activeNeighbors = 0u;
    unsigned int neighborhoodSum = currentLevel;
    for (unsigned int state = 0u; state < family.stateCount; ++state) {
      const unsigned int level =
        decodeBackgroundZero(static_cast<unsigned char>(state));
      const unsigned int count = neighborStateCounts[state];
      neighborhoodSum += level * count;
      if (level == maximumLevel) {
        illNeighbors += count;
      } else if (level != 0u) {
        infectedNeighbors += count;
      }
      if (level != 0u) {
        activeNeighbors += count;
      }
    }
    if (currentLevel == 0u) {
      const unsigned int nextLevel =
        infectedNeighbors / definition.infectionDivisor +
        illNeighbors / definition.illDivisor;
      return encodeBackgroundZero(nextLevel > maximumLevel ? maximumLevel
                                                           : nextLevel);
    }
    if (currentLevel == maximumLevel) {
      return 1u;
    }
    unsigned int nextLevel =
      neighborhoodSum / (activeNeighbors + 1u) + definition.infectionIncrement;
    if (nextLevel > maximumLevel) {
      nextLevel = maximumLevel;
    }
    return encodeBackgroundZero(nextLevel);
  }
  if (family.kind == RuleFamily::Dominance) {
    if (cell == 1u || static_cast<unsigned int>(cell) >= family.stateCount) {
      return 1u;
    }
    const unsigned int speciesCount = family.stateCount - 1u;
    const unsigned int currentOrdinal = speciesOrdinal(cell);
    unsigned int winningOrdinal = currentOrdinal;
    unsigned int winningCount = 0u;
    for (unsigned int candidate = 0u; candidate < speciesCount; ++candidate) {
      bool preysOnCurrent = false;
      for (unsigned int offset : definition.dominancePreyOffsets) {
        if ((candidate + offset) % speciesCount == currentOrdinal) {
          preysOnCurrent = true;
          break;
        }
      }
      if (!preysOnCurrent) {
        continue;
      }
      const unsigned int count = neighborStateCounts[speciesState(candidate)];
      if (count > winningCount) {
        winningOrdinal = candidate;
        winningCount = count;
      }
    }
    return winningCount >= definition.dominanceThreshold
             ? speciesState(winningOrdinal)
             : cell;
  }
  if (family.kind == RuleFamily::SpeciesLife) {
    if (static_cast<unsigned int>(cell) >= family.stateCount) {
      return 1u;
    }
    unsigned int liveNeighbors = 0u;
    for (unsigned int state = 0u; state < family.stateCount; ++state) {
      if (state != 1u) {
        liveNeighbors += neighborStateCounts[state];
      }
    }
    if (cell != 1u) {
      return ((definition.surviveMask >> liveNeighbors) & 1u) != 0u ? cell : 1u;
    }
    if (((definition.birthMask >> liveNeighbors) & 1u) == 0u) {
      return 1u;
    }

    // QuadLife's distinctive three-color birth selects the absent fourth
    // species. Other births, including two-color Immigration, inherit the
    // most numerous parent color.
    if (family.stateCount == 5u && liveNeighbors == 3u) {
      unsigned int distinctSpecies = 0u;
      unsigned int missingSpecies = 0u;
      for (unsigned int state = 0u; state < family.stateCount; ++state) {
        if (state == 1u) {
          continue;
        }
        if (neighborStateCounts[state] != 0u) {
          distinctSpecies += 1u;
        } else {
          missingSpecies = state;
        }
      }
      if (distinctSpecies == 3u) {
        return static_cast<unsigned char>(missingSpecies);
      }
    }

    unsigned int bestState = 0u;
    unsigned int bestCount = 0u;
    for (unsigned int state = 0u; state < family.stateCount; ++state) {
      if (state != 1u && neighborStateCounts[state] > bestCount) {
        bestState = state;
        bestCount = neighborStateCounts[state];
      }
    }
    return static_cast<unsigned char>(bestState);
  }
  if (family.kind != RuleFamily::Cyclic) {
    return RuleSet::nextStateFromNeighborhood(cell, neighborStateCounts);
  }
  if (static_cast<unsigned int>(cell) >= family.stateCount) {
    return 1u;
  }
  if (definition.inertBackground && cell == 1u) {
    return cell;
  }
  const unsigned char successor = getExtendedCountedState(cell);
  return neighborStateCounts[successor] >= definition.cyclicThreshold
           ? successor
           : cell;
}

unsigned char
DataRuleSet::nextStateFromDirectionalNeighborhood(
  unsigned char cell,
  const DirectionalNeighbors& neighbors) const
{
  if (family.kind == RuleFamily::Turmite) {
    if (static_cast<unsigned int>(cell) >= family.stateCount) {
      return 1u;
    }
    const unsigned int tapeColorCount = family.stateCount / 5u;
    if (static_cast<unsigned int>(cell) >= tapeColorCount) {
      const unsigned int encodedAgent =
        static_cast<unsigned int>(cell) - tapeColorCount;
      const unsigned int tapeColor = encodedAgent / 4u;
      return encodeBackgroundZero((tapeColor + 1u) % tapeColorCount);
    }

    const unsigned int targetTapeColor = decodeBackgroundZero(cell);
    unsigned int arrivalCount = 0u;
    unsigned int arrivalDirection = 0u;
    for (unsigned int neighborIndex = 0u; neighborIndex < neighbors.size();
         ++neighborIndex) {
      const unsigned int sourceState = neighbors[neighborIndex];
      if (sourceState < tapeColorCount || sourceState >= family.stateCount) {
        continue;
      }
      const unsigned int encodedAgent = sourceState - tapeColorCount;
      const unsigned int sourceTapeColor = encodedAgent / 4u;
      const unsigned int sourceDirection = encodedAgent % 4u;
      const char turn = definition.turnSequence[sourceTapeColor];
      const unsigned int nextDirection =
        (sourceDirection + (turn == 'R' ? 1u : 3u)) % 4u;
      if (nextDirection == (neighborIndex + 2u) % 4u) {
        arrivalCount += 1u;
        arrivalDirection = nextDirection;
      }
    }
    if (arrivalCount != 1u) {
      return cell;
    }
    return static_cast<unsigned char>(tapeColorCount + targetTapeColor * 4u +
                                      arrivalDirection);
  }

  if (family.kind == RuleFamily::VonNeumannTable) {
    const unsigned int stateCount = family.stateCount;
    if (static_cast<unsigned int>(cell) >= stateCount) {
      return 1u;
    }
    std::size_t index = cell;
    for (unsigned char neighbor : neighbors) {
      if (static_cast<unsigned int>(neighbor) >= stateCount) {
        return cell;
      }
      index = index * stateCount + neighbor;
    }
    return definition.vonNeumannTransitions[index];
  }

  if (family.kind == RuleFamily::Sandpile) {
    // Heights 0..7 use the background-zero encoding. A cell holding four or
    // more grains topples one grain to each neighbor; a source phase-zero
    // cell emits the same four grains without depleting.
    const unsigned int heightCount = 8u;
    if (static_cast<unsigned int>(cell) >= family.stateCount) {
      return 1u;
    }
    if (static_cast<unsigned int>(cell) >= heightCount) {
      const unsigned int phase = static_cast<unsigned int>(cell) - heightCount;
      const unsigned int period = family.stateCount - heightCount;
      return static_cast<unsigned char>(heightCount + (phase + 1u) % period);
    }
    unsigned int height = decodeBackgroundZero(cell);
    if (height >= 4u) {
      height -= 4u;
    }
    for (unsigned char neighbor : neighbors) {
      const unsigned int state = neighbor;
      if (state == heightCount ||
          (state < heightCount && decodeBackgroundZero(neighbor) >= 4u)) {
        height += 1u;
      }
    }
    return encodeBackgroundZero(height);
  }

  if (family.kind == RuleFamily::LatticeGas) {
    unsigned int incomingMask = 0u;
    for (unsigned int neighborIndex = 0u; neighborIndex < neighbors.size();
         ++neighborIndex) {
      if (static_cast<unsigned int>(neighbors[neighborIndex]) >=
          family.stateCount) {
        continue;
      }
      const unsigned int sourceMask =
        collideHpp(decodeBackgroundZero(neighbors[neighborIndex]));
      const unsigned int incomingDirection = (neighborIndex + 2u) % 4u;
      const unsigned int incomingBit = 1u << incomingDirection;
      if ((sourceMask & incomingBit) != 0u) {
        incomingMask |= incomingBit;
      }
    }
    return encodeBackgroundZero(incomingMask);
  }

  return RuleSet::nextStateFromDirectionalNeighborhood(cell, neighbors);
}

unsigned char
DataRuleSet::getExtendedCountedState(unsigned char cell) const
{
  if (family.kind != RuleFamily::Cyclic ||
      static_cast<unsigned int>(cell) >= family.stateCount) {
    return 0u;
  }
  if (!definition.inertBackground) {
    return static_cast<unsigned char>(
      (static_cast<unsigned int>(cell) + definition.cyclicStep) %
      family.stateCount);
  }
  // The cycle runs over states 0, 2, 3, ..., n-1; background has no
  // successor, and counting background neighbors never changes it.
  if (cell == 1u) {
    return 1u;
  }
  const unsigned int cycleLength = family.stateCount - 1u;
  const unsigned int ordinal = speciesOrdinal(cell);
  return speciesState((ordinal + definition.cyclicStep) % cycleLength);
}

unsigned char
DataRuleSet::nextStateFromExtendedCount(unsigned char cell,
                                        unsigned int aliveCount) const
{
  if (static_cast<unsigned int>(cell) >= family.stateCount) {
    return 1u;
  }
  if (family.kind == RuleFamily::Cyclic) {
    if (definition.inertBackground && cell == 1u) {
      return cell;
    }
    // aliveCount holds the neighbors in this cell's successor state.
    return aliveCount >= definition.cyclicThreshold
             ? getExtendedCountedState(cell)
             : cell;
  }
  if (family.kind != RuleFamily::LargerThanLife) {
    return 1u;
  }
  if (cell >= 2u) {
    // Decay trail: dying states neither count nor accept births.
    return static_cast<unsigned int>(cell) + 1u < family.stateCount
             ? static_cast<unsigned char>(cell + 1u)
             : 1u;
  }
  const bool active = cell == 0u;
  const unsigned int minimum =
    active ? definition.survivalMinimum : definition.birthMinimum;
  const unsigned int maximum =
    active ? definition.survivalMaximum : definition.birthMaximum;
  if (aliveCount >= minimum && aliveCount <= maximum) {
    return 0u;
  }
  return active && family.stateCount > 2u ? 2u : 1u;
}

unsigned char
DataRuleSet::nextElementary(unsigned char left,
                            unsigned char center,
                            unsigned char right) const
{
  const unsigned int index = ((left == 0u) ? 4u : 0u) |
                             ((center == 0u) ? 2u : 0u) |
                             ((right == 0u) ? 1u : 0u);
  return definition.elementaryTransitions[index];
}

void
DataRuleSet::evalCell(const unsigned char& target, unsigned char dest[3]) const
{
  if (dest == nullptr) {
    return;
  }
  if (static_cast<std::size_t>(target) < family.stateColors.size()) {
    const std::array<unsigned char, 3>& color = family.stateColors[target];
    dest[0] = color[0];
    dest[1] = color[1];
    dest[2] = color[2];
    return;
  }
  dest[0] = 160u;
  dest[1] = 160u;
  dest[2] = 160u;
}
