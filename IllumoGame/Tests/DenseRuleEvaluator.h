#pragma once
#include "Game/CellGrid.h"
#include "Rulesets/RuleSet.h"

// Dense toroidal compatibility evaluator; never linked into the product core.
class DenseRuleEvaluator
{
public:
  DenseRuleEvaluator(CellGrid* grid, const RuleSet& rule)
    : canvas(grid)
    , rules(rule)
  {
  }
  void setWorkerOverride(int workers)
  {
    workerOverride = workers > 0 ? workers : 0;
  }
  int getWorkerOverride() const { return workerOverride; }
  void calcGeneration(const int& xStart,
                      const int& yStart,
                      const int& xEnd,
                      const int& yEnd) const;
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
  CellGrid* canvas;
  const RuleSet& rules;
  int workerOverride = 0;
  static constexpr int kParallelCellThreshold = 512 * 512;
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
