#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

class CellGrid;

constexpr auto MAX_RULETAG_SIZE = 128;

// Base cellular-automaton ruleset.
// Generation is double-buffered on CellGrid: neighbors are read from the
// front lifeCanvas, next states are written to lifeCanvasBack, then buffers
// are swapped (D-P5). Operates only on CellGrid domain storage — no
// Renderer / OpenGL.
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

  CellGrid* canvas;

  RuleSet(CellGrid* targetCanvas);

protected:
  RuleSet(CellGrid* targetCanvas,
          const TransitionTable& precompiledTransitions);

public:
  virtual ~RuleSet() = default;

  // Advance one generation over the full canvas (rect args kept for API
  // compatibility; toroidal full-grid is always evaluated).
  void calcGeneration(const int& x_start,
                      const int& y_start,
                      const int& x_end,
                      const int& y_end) const;

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

  // Worker count for calcGeneration: 0 = auto (size threshold + HW), 1 =
  // force serial, N = force up to N workers. Used by tests and optional
  // parallel path (D-P7).
  static void setWorkerOverride(int workers);
  static int getWorkerOverride();

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

  // Toroidal Moore count of cells with value 0 (project "alive" encoding).
  static int countAliveNeighbors(const unsigned char* grid,
                                 int w,
                                 int h,
                                 int x,
                                 int y);

  // Interior Moore count (no wrap). Requires 0 < x < w-1 and 0 < y < h-1.
  static int countAliveNeighborsInterior(const unsigned char* grid,
                                         int w,
                                         int x,
                                         int y);

private:
  // Auto-parallel threshold: grids at or above this cell count may use
  // multiple workers when override is 0. Kept high enough that per-generation
  // thread spawn is amortized (256² is still spawn-bound on typical CPUs).
  static const int kParallelCellThreshold = 512 * 512;

  static int workerOverride;
  mutable TransitionTable transitionTable{};
  mutable bool transitionTableReady = false;
  mutable std::uint64_t transitionRevision = 0u;

  void evalRows(const unsigned char* src,
                unsigned char* dst,
                const unsigned char* transitions,
                int width,
                int height,
                int yBegin,
                int yEnd,
                int* outMinX,
                int* outMinY,
                int* outMaxX,
                int* outMaxY,
                bool* outAnyChange) const;

  int resolveWorkerCount(int width, int height) const;
};
