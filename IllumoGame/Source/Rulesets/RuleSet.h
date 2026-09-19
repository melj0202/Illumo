#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

constexpr auto MAX_RULETAG_SIZE = 128;

// Pure transition and palette contract, independent of storage and scheduling.
class RuleSet
{
public:
  static constexpr std::size_t kCellStateCount = 256u;
  static constexpr std::size_t kNeighborCountCount = 9u;
  using TransitionTable =
    std::array<unsigned char, kCellStateCount * kNeighborCountCount>;
  using NeighborStateCounts = std::array<unsigned char, kCellStateCount>;
  // North, east, south, west. Direction is preserved for mobile-agent and
  // lattice-gas rules that cannot be expressed as an unordered histogram.
  using DirectionalNeighbors = std::array<unsigned char, 4>;

  enum class NeighborhoodKind
  {
    MooreCount,
    MooreStateCounts,
    VonNeumannDirectional,
    ExtendedRange,
    Elementary1D
  };

  enum class ExtendedNeighborhoodShape
  {
    Square,
    Circular
  };

  RuleSet() = default;

protected:
  explicit RuleSet(const TransitionTable& precompiledTransitions);

public:
  virtual ~RuleSet() = default;

  // Map logical cell value → RGB display color.
  virtual void evalCell(const unsigned char& target,
                        unsigned char dest[3]) const
  {
    (void)target;
    (void)dest;
  }

  virtual std::string getRuleTag() const { return "BASE_CLASS"; }

  virtual std::string getFamilyTag() const { return "BASE_FAMILY"; }

  virtual unsigned int getStateCount() const { return 2u; }

  virtual std::string getStateName(unsigned char state) const
  {
    return state == 0u ? "Active" : (state == 1u ? "Background" : "Unknown");
  }

  virtual bool isValidState(unsigned char state) const
  {
    return state < getStateCount();
  }

  // Pure transition: old cell + Moore neighbor count of *alive* (value==0)
  // cells. Does not write the canvas. Public so sparse / alternate domains can
  // evaluate the same rules without going through dense Canvas.
  virtual unsigned char nextState(unsigned char cell,
                                  unsigned char aliveNeighbors) const
  {
    (void)aliveNeighbors;
    return cell;
  }

  // Full Moore-neighborhood histogram for models whose states interact with
  // one another. Existing rules remain compatible through the state-zero
  // count used by the historical MooreCount contract.
  virtual unsigned char nextStateFromNeighborhood(
    unsigned char cell,
    const NeighborStateCounts& neighborStateCounts) const
  {
    return nextState(cell, neighborStateCounts[0]);
  }

  virtual unsigned char nextStateFromDirectionalNeighborhood(
    unsigned char cell,
    const DirectionalNeighbors& neighbors) const
  {
    NeighborStateCounts counts{};
    for (unsigned char neighbor : neighbors) {
      counts[neighbor] += 1u;
    }
    return nextStateFromNeighborhood(cell, counts);
  }

  virtual NeighborhoodKind getNeighborhoodKind() const
  {
    return NeighborhoodKind::MooreCount;
  }

  virtual unsigned int getNeighborhoodRadius() const { return 1u; }

  virtual ExtendedNeighborhoodShape getExtendedNeighborhoodShape() const
  {
    return ExtendedNeighborhoodShape::Square;
  }

  virtual bool includesCenterInNeighborCount() const { return false; }

  virtual unsigned char nextStateFromExtendedCount(
    unsigned char cell,
    unsigned int aliveCount) const
  {
    return nextState(cell, static_cast<unsigned char>(aliveCount));
  }

  virtual unsigned char nextElementary(unsigned char left,
                                       unsigned char center,
                                       unsigned char right) const
  {
    (void)left;
    (void)right;
    return center;
  }

  // Built once per ruleset instance before worker dispatch. Hot simulation
  // loops index this table instead of repeating virtual calls and rule
  // branches.
  const TransitionTable& getTransitionTable() const;
  std::uint64_t getTransitionRevision() const { return transitionRevision; }

  static std::size_t transitionIndex(unsigned char cell,
                                     unsigned char aliveNeighbors)
  {
    return static_cast<std::size_t>(cell) * kNeighborCountCount +
           static_cast<std::size_t>(aliveNeighbors);
  }

protected:
  void invalidateTransitionTable() const
  {
    transitionTableReady = false;
    transitionRevision += 1u;
  }

private:
  mutable TransitionTable transitionTable{};
  mutable bool transitionTableReady = false;
  mutable std::uint64_t transitionRevision = 0u;
};
