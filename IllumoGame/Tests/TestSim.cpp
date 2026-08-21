// Simulation performance and generation-path correctness tests.

#include "Game/Canvas.h"
#include "Game/CellGrid.h"
#include "Game/SimulationRunner.h"
#include "Game/SparseCellGrid.h"
#include "Rulesets/GameOfLifeRuleSet.h"
#include "Rulesets/WireworldRuleSet.h"
#include <Illumo/Foundation/RollingMetric.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

static TestCounters g;

static void
seedRandom(CellGrid& grid, unsigned seed)
{
  // Simple LCG so benches are deterministic without <random> global state.
  unsigned state = seed;
  const int w = grid.getWidth();
  const int h = grid.getHeight();
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      state = state * 1664525u + 1013904223u;
      const unsigned char live = (state >> 28) == 0 ? 0 : 1;
      grid.setCanvasPixel(x, y, live);
    }
  }
}

static void
seedGlider(CellGrid& grid, int ox, int oy)
{
  grid.clearCells();
  // Standard GoL glider (alive = 0).
  grid.setCanvasPixel(ox + 1, oy + 0, 0);
  grid.setCanvasPixel(ox + 2, oy + 1, 0);
  grid.setCanvasPixel(ox + 0, oy + 2, 0);
  grid.setCanvasPixel(ox + 1, oy + 2, 0);
  grid.setCanvasPixel(ox + 2, oy + 2, 0);
}

static std::vector<unsigned char>
snapshot(const CellGrid& grid)
{
  const size_t total = static_cast<size_t>(grid.getWidth()) *
                       static_cast<size_t>(grid.getHeight());
  std::vector<unsigned char> out(total);
  std::memcpy(out.data(), grid.lifeCanvas, total);
  return out;
}

static void
testDoubleBufferAndStillLife()
{
  testSection("Sim: double-buffer present; still life does not re-dirty");
  CellGrid grid(8, 8);
  testTrue(g, grid.lifeCanvas != nullptr, "front buffer");
  testTrue(g, grid.getLifeBackBuffer() != nullptr, "back buffer");
  testTrue(g, grid.lifeCanvas != grid.getLifeBackBuffer(), "distinct buffers");

  // 2x2 block still life.
  grid.clearCells();
  grid.setCanvasPixel(3, 3, 0);
  grid.setCanvasPixel(4, 3, 0);
  grid.setCanvasPixel(3, 4, 0);
  grid.setCanvasPixel(4, 4, 0);

  // Consume paint dirty via Canvas-free path: mark region then clear flags by
  // stepping after capturing; use a fresh grid for the dirty check.
  Canvas canvas(8, 8, nullptr, nullptr, nullptr);
  canvas.clearCells();
  canvas.setCanvasPixel(3, 3, 0);
  canvas.setCanvasPixel(4, 3, 0);
  canvas.setCanvasPixel(3, 4, 0);
  canvas.setCanvasPixel(4, 4, 0);
  canvas.onTargetsRebuilt();
  testTrue(g, !canvas.isCellsDirty(), "targets rebuilt clears life dirty");

  GameOfLifeRuleSet rules(&canvas);
  RuleSet::setWorkerOverride(1);
  rules.calcGeneration(0, 0, 8, 8);
  testTrue(g, !canvas.isCellsDirty(), "still life leaves cells clean");
  testEqUChar(g, canvas.getCanvasPixel(3, 3), 0, "block stable TL");
  testEqUChar(g, canvas.getCanvasPixel(4, 4), 0, "block stable BR");
  RuleSet::setWorkerOverride(0);
}

static void
testDirtyAabbTightness()
{
  testSection("Sim: blinker change produces bounded dirty AABB");
  Canvas canvas(16, 16, nullptr, nullptr, nullptr);
  canvas.clearCells();
  // Vertical blinker at (8,7)-(8,9).
  canvas.setCanvasPixel(8, 7, 0);
  canvas.setCanvasPixel(8, 8, 0);
  canvas.setCanvasPixel(8, 9, 0);
  canvas.onTargetsRebuilt();

  GameOfLifeRuleSet rules(&canvas);
  RuleSet::setWorkerOverride(1);
  rules.calcGeneration(0, 0, 16, 16);
  testTrue(g, canvas.isCellsDirty(), "blinker step dirties");
  testTrue(g, canvas.hasCellsDirtyRegion(), "dirty region valid");
  const DirtyRect& r = canvas.getCellsDirtyRegion();
  // Horizontal blinker cells: (7,8)(8,8)(9,8) — AABB at most a small
  // neighborhood.
  testTrue(g, r.minX >= 6 && r.maxX <= 10, "dirty X bounded near blinker");
  testTrue(g, r.minY >= 6 && r.maxY <= 10, "dirty Y bounded near blinker");
  testTrue(g, r.width() * r.height() < 16 * 16, "dirty smaller than full grid");
  RuleSet::setWorkerOverride(0);
}

static void
testToroidalEdge()
{
  testSection("Sim: toroidal wrap still works after interior split");
  CellGrid grid(5, 5);
  grid.clearCells();
  // Three alive cells wrapping across left/right of middle row → should birth.
  grid.setCanvasPixel(0, 2, 0);
  grid.setCanvasPixel(1, 2, 0);
  grid.setCanvasPixel(4, 2, 0);

  GameOfLifeRuleSet rules(&grid);
  RuleSet::setWorkerOverride(1);
  rules.calcGeneration(0, 0, 5, 5);
  // Neighbors of (0,1)/(0,3) etc. — just ensure we did not crash and grid is
  // still readable; a vertical neighbor of the wrap trio should see 3.
  // Cell (0,1): neighbors include (0,2)(1,2)(4,2) along bottom of its Moore set
  // when counting carefully — verify at least one cell changed from all-dead
  // pattern edges.
  int alive = 0;
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 5; ++x) {
      if (grid.getCanvasPixel(x, y) == 0) {
        alive += 1;
      }
    }
  }
  testTrue(g, alive > 0, "toroidal generation produced live cells");
  RuleSet::setWorkerOverride(0);
}

static void
testSerialParallelIdentical()
{
  testSection("Sim: serial and parallel produce identical grids");
  const int w = 96;
  const int h = 96;
  const int gens = 8;

  CellGrid serialGrid(w, h);
  seedRandom(serialGrid, 42u);
  const std::vector<unsigned char> start = snapshot(serialGrid);

  GameOfLifeRuleSet serialRules(&serialGrid);
  RuleSet::setWorkerOverride(1);
  for (int i = 0; i < gens; ++i) {
    serialRules.calcGeneration(0, 0, w, h);
  }
  const std::vector<unsigned char> serialResult = snapshot(serialGrid);

  CellGrid parallelGrid(w, h);
  std::memcpy(parallelGrid.lifeCanvas, start.data(), start.size());
  GameOfLifeRuleSet parallelRules(&parallelGrid);
  RuleSet::setWorkerOverride(4);
  for (int i = 0; i < gens; ++i) {
    parallelRules.calcGeneration(0, 0, w, h);
  }
  const std::vector<unsigned char> parallelResult = snapshot(parallelGrid);

  testTrue(g,
           serialResult.size() == parallelResult.size() &&
             std::memcmp(serialResult.data(),
                         parallelResult.data(),
                         serialResult.size()) == 0,
           "serial == parallel after N gens");
  RuleSet::setWorkerOverride(0);
}

static double
benchGensPerSecond(int width, int height, int generations, unsigned seed)
{
  CellGrid grid(width, height);
  seedRandom(grid, seed);
  GameOfLifeRuleSet rules(&grid);
  RuleSet::setWorkerOverride(1);

  // Warmup
  rules.calcGeneration(0, 0, width, height);

  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < generations; ++i) {
    rules.calcGeneration(0, 0, width, height);
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(t1 - t0).count();
  RuleSet::setWorkerOverride(0);
  if (seconds <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(generations) / seconds;
}

static double
benchVisualTickMs(int width, int height, int frames)
{
  Canvas canvas(width, height, nullptr, nullptr, nullptr);
  seedRandom(canvas, 7u);
  canvas.rebuildTargetsFromLife();
  canvas.setFadeSpeed(8.0f);

  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < frames; ++i) {
    canvas.tickVisual(1.0f / 60.0f);
  }
  const auto t1 = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(t1 - t0).count();
  if (frames <= 0) {
    return 0.0;
  }
  return (seconds * 1000.0) / static_cast<double>(frames);
}

static void
seedSparseBenchmark(SparseCellGrid* grid, int chunksPerSide, unsigned int seed)
{
  if (grid == nullptr) {
    return;
  }

  unsigned int state = seed;
  const int firstChunk = -chunksPerSide / 2;
  for (int chunkY = 0; chunkY < chunksPerSide; ++chunkY) {
    for (int chunkX = 0; chunkX < chunksPerSide; ++chunkX) {
      SparseChunkRecord record;
      record.chunkX = firstChunk + chunkX;
      record.chunkY = firstChunk + chunkY;
      record.cells.fill(SparseCellGrid::BackgroundState);
      for (std::size_t index = 0; index < record.cells.size(); ++index) {
        state = state * 1664525u + 1013904223u;
        if ((state >> 29) < 2u) {
          record.cells[index] = SparseCellGrid::CountedNeighborState;
        }
      }
      grid->assignChunk(record);
    }
  }
}

static void
seedSparseWideBlinkers(SparseCellGrid* grid, int count)
{
  if (grid == nullptr) {
    return;
  }
  for (int index = 0; index < count; ++index) {
    const std::int64_t x = static_cast<std::int64_t>(index) * 32;
    grid->setCell(CellAddress{ x, -1 }, 0);
    grid->setCell(CellAddress{ x, 0 }, 0);
    grid->setCell(CellAddress{ x, 1 }, 0);
  }
}

static void
seedSparseStableBlocksAndBlinker(SparseCellGrid* grid, int blockCount)
{
  if (grid == nullptr) {
    return;
  }
  for (int block = 0; block < blockCount; ++block) {
    const std::int64_t x = static_cast<std::int64_t>(block) * 64 + 4;
    const std::int64_t y = 64;
    grid->setCell(CellAddress{ x, y }, 0);
    grid->setCell(CellAddress{ x + 1, y }, 0);
    grid->setCell(CellAddress{ x, y + 1 }, 0);
    grid->setCell(CellAddress{ x + 1, y + 1 }, 0);
  }
  grid->setCell(CellAddress{ 0, -1 }, 0);
  grid->setCell(CellAddress{ 0, 0 }, 0);
  grid->setCell(CellAddress{ 0, 1 }, 0);
}

static double
benchSparseGensPerSecond(int chunksPerSide,
                         int generations,
                         int workers,
                         SparseAdvanceStats* stats,
                         int memoMode = 0)
{
  SparseCellGrid grid;
  seedSparseBenchmark(&grid, chunksPerSide, 101u);
  GameOfLifeRuleSet rules(nullptr);
  SparseCellGrid::setWorkerOverrideForTesting(workers);
  SparseCellGrid::setCellCandidateOverrideForTesting(-1);
  SparseCellGrid::setChunkMemoOverrideForTesting(memoMode);

  grid.advance(rules);
  const auto t0 = std::chrono::steady_clock::now();
  for (int generation = 0; generation < generations; ++generation) {
    grid.advance(rules);
  }
  const auto t1 = std::chrono::steady_clock::now();
  if (stats != nullptr) {
    *stats = grid.getLastAdvanceStats();
  }
  SparseCellGrid::setWorkerOverrideForTesting(0);
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  SparseCellGrid::setChunkMemoOverrideForTesting(0);

  const double seconds = std::chrono::duration<double>(t1 - t0).count();
  if (seconds <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(generations) / seconds;
}

static double
benchSparseWideGensPerSecond(int colonyCount,
                             int generations,
                             int candidateMode,
                             SparseAdvanceStats* stats,
                             bool reuseChunkNodes = true,
                             int workers = 1,
                             int memoMode = 0)
{
  SparseCellGrid grid;
  seedSparseWideBlinkers(&grid, colonyCount);
  GameOfLifeRuleSet rules(nullptr);
  SparseCellGrid::setWorkerOverrideForTesting(workers);
  SparseCellGrid::setCellCandidateOverrideForTesting(candidateMode);
  SparseCellGrid::setChunkNodeReuseOverrideForTesting(reuseChunkNodes);
  SparseCellGrid::setChunkMemoOverrideForTesting(memoMode);

  grid.advance(rules);
  const std::chrono::steady_clock::time_point t0 =
    std::chrono::steady_clock::now();
  for (int generation = 0; generation < generations; ++generation) {
    grid.advance(rules);
  }
  const std::chrono::steady_clock::time_point t1 =
    std::chrono::steady_clock::now();
  if (stats != nullptr) {
    *stats = grid.getLastAdvanceStats();
  }
  SparseCellGrid::setWorkerOverrideForTesting(0);
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  SparseCellGrid::setChunkNodeReuseOverrideForTesting(true);
  SparseCellGrid::setChunkMemoOverrideForTesting(0);

  const double seconds = std::chrono::duration<double>(t1 - t0).count();
  if (seconds <= 0.0) {
    return 0.0;
  }
  return static_cast<double>(generations) / seconds;
}

static double
benchSparseFrontierGensPerSecond(int blockCount,
                                 int generations,
                                 int candidateMode,
                                 SparseAdvanceStats* stats)
{
  SparseCellGrid grid;
  seedSparseStableBlocksAndBlinker(&grid, blockCount);
  GameOfLifeRuleSet rules(nullptr);
  SparseCellGrid::setWorkerOverrideForTesting(1);
  SparseCellGrid::setCellCandidateOverrideForTesting(candidateMode);

  grid.advance(rules);
  const std::chrono::steady_clock::time_point t0 =
    std::chrono::steady_clock::now();
  for (int generation = 0; generation < generations; ++generation) {
    grid.advance(rules);
  }
  const std::chrono::steady_clock::time_point t1 =
    std::chrono::steady_clock::now();
  if (stats != nullptr) {
    *stats = grid.getLastAdvanceStats();
  }
  SparseCellGrid::setWorkerOverrideForTesting(0);
  SparseCellGrid::setCellCandidateOverrideForTesting(0);

  const double seconds = std::chrono::duration<double>(t1 - t0).count();
  return seconds > 0.0 ? static_cast<double>(generations) / seconds : 0.0;
}

static void
seedRepeatedDenseChunks(SparseCellGrid* grid, int chunkCount)
{
  if (grid == nullptr) {
    return;
  }

  SparseChunkRecord record;
  record.cells.fill(SparseCellGrid::BackgroundState);
  for (int localY = 2; localY < SparseCellGrid::kChunkDim - 2; ++localY) {
    for (int localX = 2; localX < SparseCellGrid::kChunkDim - 2; ++localX) {
      const unsigned int value = static_cast<unsigned int>(
        localX * 17 + localY * 31 + localX * localY * 7);
      if ((value % 11u) < 5u) {
        record.cells[static_cast<std::size_t>(
          localY * SparseCellGrid::kChunkDim + localX)] = 0;
      }
    }
  }

  for (int index = 0; index < chunkCount; ++index) {
    record.chunkX = static_cast<std::int64_t>(index) * 4;
    record.chunkY = static_cast<std::int64_t>(index % 3) * 4;
    grid->assignChunk(record);
  }
}

static double
benchRepeatedDenseGensPerSecond(int chunkCount,
                                int generations,
                                int memoMode,
                                SparseAdvanceStats* stats)
{
  SparseCellGrid grid;
  seedRepeatedDenseChunks(&grid, chunkCount);
  GameOfLifeRuleSet rules(nullptr);
  SparseCellGrid::setWorkerOverrideForTesting(4);
  SparseCellGrid::setCellCandidateOverrideForTesting(-1);
  SparseCellGrid::setChunkMemoOverrideForTesting(memoMode);

  grid.advance(rules);
  const std::chrono::steady_clock::time_point start =
    std::chrono::steady_clock::now();
  for (int generation = 0; generation < generations; ++generation) {
    grid.advance(rules);
  }
  const double seconds =
    std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
      .count();
  if (stats != nullptr) {
    *stats = grid.getLastAdvanceStats();
  }
  SparseCellGrid::setWorkerOverrideForTesting(0);
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  SparseCellGrid::setChunkMemoOverrideForTesting(0);
  return seconds > 0.0 ? static_cast<double>(generations) / seconds : 0.0;
}

static double
benchWireworldConductorsGensPerSecond(int chunksPerSide,
                                      int generations,
                                      int candidateMode,
                                      SparseAdvanceStats* stats)
{
  SparseCellGrid grid;
  for (int chunkY = 0; chunkY < chunksPerSide; ++chunkY) {
    for (int chunkX = 0; chunkX < chunksPerSide; ++chunkX) {
      SparseChunkRecord record;
      record.chunkX = chunkX;
      record.chunkY = chunkY;
      record.cells.fill(WireworldRuleSet::CELL_CONDUCTOR);
      grid.assignChunk(record);
    }
  }
  WireworldRuleSet rules(nullptr);
  SparseCellGrid::setWorkerOverrideForTesting(1);
  SparseCellGrid::setCellCandidateOverrideForTesting(candidateMode);

  grid.advance(rules);
  const std::chrono::steady_clock::time_point t0 =
    std::chrono::steady_clock::now();
  for (int generation = 0; generation < generations; ++generation) {
    grid.advance(rules);
  }
  const std::chrono::steady_clock::time_point t1 =
    std::chrono::steady_clock::now();
  if (stats != nullptr) {
    *stats = grid.getLastAdvanceStats();
  }
  SparseCellGrid::setWorkerOverrideForTesting(0);
  SparseCellGrid::setCellCandidateOverrideForTesting(0);

  const double seconds = std::chrono::duration<double>(t1 - t0).count();
  return seconds > 0.0 ? static_cast<double>(generations) / seconds : 0.0;
}

static void
testMicroBenchReport()
{
  testSection("Sim: micro-bench report (informational)");
  struct Case
  {
    int w;
    int h;
    int gens;
  };
  const Case cases[] = { { 80, 60, 200 }, { 256, 256, 40 }, { 512, 512, 15 } };

  for (const Case& c : cases) {
    const double gps = benchGensPerSecond(c.w, c.h, c.gens, 99u);
    std::printf(
      "BENCH: GoL serial %dx%d  gens/s=%.1f  (N=%d)\n", c.w, c.h, gps, c.gens);
    testTrue(g, gps > 0.0, "bench produced positive gens/s");
  }

  // Parallel comparison on a large grid where eval can amortize spawn cost.
  {
    const int dim = 512;
    CellGrid grid(dim, dim);
    seedRandom(grid, 11u);
    GameOfLifeRuleSet rules(&grid);
    const int gens = 12;

    RuleSet::setWorkerOverride(1);
    rules.calcGeneration(0, 0, dim, dim);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < gens; ++i) {
      rules.calcGeneration(0, 0, dim, dim);
    }
    auto t1 = std::chrono::steady_clock::now();
    const double serialSec = std::chrono::duration<double>(t1 - t0).count();

    seedRandom(grid, 11u);
    RuleSet::setWorkerOverride(4);
    rules.calcGeneration(0, 0, dim, dim);
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < gens; ++i) {
      rules.calcGeneration(0, 0, dim, dim);
    }
    t1 = std::chrono::steady_clock::now();
    const double parallelSec = std::chrono::duration<double>(t1 - t0).count();
    RuleSet::setWorkerOverride(0);

    const double serialGps =
      serialSec > 0.0 ? static_cast<double>(gens) / serialSec : 0.0;
    const double parallelGps =
      parallelSec > 0.0 ? static_cast<double>(gens) / parallelSec : 0.0;
    std::printf(
      "BENCH: GoL %dx%d serial gens/s=%.1f  parallel(4) gens/s=%.1f\n",
      dim,
      dim,
      serialGps,
      parallelGps);
    testTrue(g, serialGps > 0.0 && parallelGps > 0.0, "parallel bench ran");
  }

  {
    const double ms = benchVisualTickMs(256, 256, 30);
    std::printf("BENCH: Canvas.tickVisual 256x256  ms/frame=%.3f\n", ms);
    testTrue(g, ms >= 0.0, "visual tick bench ran");
  }

  // Glider stability smoke on default-ish size.
  {
    CellGrid grid(80, 60);
    seedGlider(grid, 10, 10);
    GameOfLifeRuleSet rules(&grid);
    RuleSet::setWorkerOverride(1);
    for (int i = 0; i < 4; ++i) {
      rules.calcGeneration(0, 0, 80, 60);
    }
    int alive = 0;
    for (int y = 0; y < 60; ++y) {
      for (int x = 0; x < 80; ++x) {
        if (grid.getCanvasPixel(x, y) == 0) {
          alive += 1;
        }
      }
    }
    testEqInt(g, alive, 5, "glider still has 5 live cells after 4 gens");
    RuleSet::setWorkerOverride(0);
  }
}

static void
testSparseMicroBenchReport()
{
  testSection("Sim: sparse micro-bench report (informational)");
  const int chunksPerSide = 24;
  const int generations = 6;
  SparseAdvanceStats serialStats;
  SparseAdvanceStats parallelStats;
  const double serialGps =
    benchSparseGensPerSecond(chunksPerSide, generations, 1, &serialStats);
  const double parallelGps =
    benchSparseGensPerSecond(chunksPerSide, generations, 4, &parallelStats);
  std::printf("BENCH: Sparse GoL %dx%d chunks serial gens/s=%.1f parallel(4) "
              "gens/s=%.1f targets=%zu workers=%u\n",
              chunksPerSide,
              chunksPerSide,
              serialGps,
              parallelGps,
              parallelStats.targetChunkCount,
              parallelStats.workerCount);
  testTrue(g,
           serialGps > 0.0 && parallelGps > 0.0,
           "sparse serial and parallel bench ran");
  testTrue(g,
           serialStats.workerCount == 1u && parallelStats.workerCount == 4u,
           "sparse bench honored worker overrides");

  const int colonyCount = 256;
  SparseAdvanceStats cellCandidateStats;
  SparseAdvanceStats fullChunkStats;
  const double candidateGps = benchSparseWideGensPerSecond(
    colonyCount, generations, 1, &cellCandidateStats);
  const double fullChunkGps =
    benchSparseWideGensPerSecond(colonyCount, generations, -1, &fullChunkStats);
  std::printf("BENCH: Sparse wide blinkers count=%d candidates gens/s=%.1f "
              "full-chunks gens/s=%.1f candidate-cells=%zu\n",
              colonyCount,
              candidateGps,
              fullChunkGps,
              cellCandidateStats.candidateCellCount);
  testTrue(g,
           candidateGps > 0.0 && fullChunkGps > 0.0,
           "wide sparse candidate and full-chunk bench ran");
  testTrue(g,
           cellCandidateStats.usedCellCandidates &&
             !fullChunkStats.usedCellCandidates,
           "wide sparse bench exercised both evaluation paths");

  const int largeColonyCount = 16384;
  SparseAdvanceStats allocatingCandidateStats;
  SparseAdvanceStats largeCandidateStats;
  SparseAdvanceStats parallelCandidateStats;
  SparseAdvanceStats parallelEightCandidateStats;
  const double allocatingCandidateGps = benchSparseWideGensPerSecond(
    largeColonyCount, 10, 1, &allocatingCandidateStats, false);
  const double largeCandidateGps =
    benchSparseWideGensPerSecond(largeColonyCount, 10, 1, &largeCandidateStats);
  const double parallelCandidateGps = benchSparseWideGensPerSecond(
    largeColonyCount, 10, 1, &parallelCandidateStats, true, 4);
  const double parallelEightCandidateGps = benchSparseWideGensPerSecond(
    largeColonyCount, 10, 1, &parallelEightCandidateStats, true, 8);
  std::printf("BENCH: Sparse large-wide blinkers count=%d candidates "
              "allocating gens/s=%.1f retained serial gens/s=%.1f "
              "parallel(4) gens/s=%.1f parallel(8) gens/s=%.1f "
              "candidate-cells=%zu targets=%zu "
              "prep/eval-ranges=%zu/%zu chunk-node-allocs=%zu reused=%zu "
              "retained=%zu enrollments/growth/output=%zu/%zu/%zu "
              "stages=%.3f/%.3f/%.3f/%.3f/%.3f/%.3f/%.3f ms\n",
              largeColonyCount,
              allocatingCandidateGps,
              largeCandidateGps,
              parallelCandidateGps,
              parallelEightCandidateGps,
              largeCandidateStats.candidateCellCount,
              largeCandidateStats.targetChunkCount,
              parallelCandidateStats.candidatePreparationRangeCount,
              parallelCandidateStats.candidateWorkRangeCount,
              largeCandidateStats.allocatedChunkNodeCount,
              largeCandidateStats.reusedChunkNodeCount,
              largeCandidateStats.retainedChunkNodeCount,
              parallelCandidateStats.candidateEnrollmentAttemptCount,
              parallelCandidateStats.candidateIndexGrowthCount,
              parallelCandidateStats.producedChunkCount,
              parallelCandidateStats.candidateDiscoveryMilliseconds,
              parallelCandidateStats.candidatePreparationMilliseconds,
              parallelCandidateStats.candidateEvaluationMilliseconds,
              parallelCandidateStats.candidateChangeTrackingMilliseconds,
              parallelCandidateStats.candidateRecycleMilliseconds,
              parallelCandidateStats.candidateOutputMilliseconds,
              parallelCandidateStats.candidateMergeMilliseconds);
  testTrue(g, largeCandidateGps > 0.0, "large wide candidate bench ran");
  testTrue(g, parallelCandidateGps > 0.0, "large parallel candidate bench ran");
  testTrue(g,
           parallelEightCandidateGps > 0.0,
           "large eight-worker candidate bench ran");
  testEqInt(g,
            static_cast<int>(parallelCandidateStats.workerCount),
            4,
            "large parallel candidate bench uses four workers");
  testEqInt(g,
            static_cast<int>(parallelEightCandidateStats.workerCount),
            8,
            "large parallel candidate bench uses eight workers");
  testTrue(g,
           allocatingCandidateGps > 0.0,
           "large allocating candidate reference bench ran");
  testEqSize(g,
             largeCandidateStats.candidateCellCount,
             static_cast<std::size_t>(largeColonyCount) * 15u,
             "large wide candidate count remains exact");
  testEqSize(g,
             largeCandidateStats.allocatedChunkNodeCount,
             0u,
             "large steady candidate generation allocates no chunk nodes");

  const int stableBlockCount = 1024;
  SparseAdvanceStats frontierStats;
  SparseAdvanceStats completeStats;
  const double frontierGps =
    benchSparseFrontierGensPerSecond(stableBlockCount, 200, 0, &frontierStats);
  const double completeGps =
    benchSparseFrontierGensPerSecond(stableBlockCount, 10, 1, &completeStats);
  std::printf("BENCH: Sparse local frontier static-blocks=%d frontier "
              "gens/s=%.1f complete-candidates gens/s=%.1f targets=%zu "
              "active-chunks=%zu\n",
              stableBlockCount,
              frontierGps,
              completeGps,
              frontierStats.frontierTargetCount,
              frontierStats.activeChunkCount);
  testTrue(g,
           frontierGps > 0.0 && completeGps > 0.0,
           "local frontier comparison bench ran");
  testTrue(g,
           frontierStats.usedChangedFrontier,
           "local frontier bench uses changed-region stepping");
  testTrue(g,
           frontierStats.frontierTargetCount < frontierStats.activeChunkCount,
           "local frontier bench skips the static majority");

  SparseAdvanceStats conductorCandidateStats;
  SparseAdvanceStats conductorHaloStats;
  const double conductorCandidateGps =
    benchWireworldConductorsGensPerSecond(9, 10, 1, &conductorCandidateStats);
  const double conductorHaloGps =
    benchWireworldConductorsGensPerSecond(9, 10, -1, &conductorHaloStats);
  std::printf("BENCH: Sparse Wireworld 9x9 conductor chunks candidates "
              "gens/s=%.1f halos gens/s=%.1f stored=%zu counted=%zu "
              "halo-targets=%zu halo-workers=%u\n",
              conductorCandidateGps,
              conductorHaloGps,
              conductorCandidateStats.activeCellCount,
              conductorCandidateStats.countedCellCount,
              conductorHaloStats.targetChunkCount,
              conductorHaloStats.workerCount);
  testTrue(g,
           conductorCandidateGps > 0.0 && conductorHaloGps > 0.0,
           "Wireworld candidate and halo comparison bench ran");
  testEqSize(g,
             conductorCandidateStats.countedCellCount,
             0u,
             "Wireworld conductor bench counts no head neighbors");

  SparseAdvanceStats uncachedRepeatedStats;
  SparseAdvanceStats cachedRepeatedStats;
  const double uncachedRepeatedGps =
    benchRepeatedDenseGensPerSecond(256, 8, -1, &uncachedRepeatedStats);
  const double cachedRepeatedGps =
    benchRepeatedDenseGensPerSecond(256, 8, 0, &cachedRepeatedStats);
  const double repeatedImprovement =
    uncachedRepeatedGps > 0.0
      ? (cachedRepeatedGps / uncachedRepeatedGps - 1.0) * 100.0
      : 0.0;
  std::printf("BENCH: Sparse repeated dense chunks=256 uncached gens/s=%.1f "
              "adaptive-memo gens/s=%.1f improvement=%.1f%% "
              "hits/probes=%zu/%zu memory=%zu\n",
              uncachedRepeatedGps,
              cachedRepeatedGps,
              repeatedImprovement,
              cachedRepeatedStats.memoHitCount,
              cachedRepeatedStats.memoProbeCount,
              cachedRepeatedStats.memoMemoryBytes);
  testTrue(g,
           uncachedRepeatedGps > 0.0 && cachedRepeatedGps > 0.0,
           "repeated dense memo comparison ran");

  SparseAdvanceStats uncachedRandomStats;
  SparseAdvanceStats cachedRandomStats;
  const double uncachedRandomGps =
    benchSparseGensPerSecond(24, 20, 4, &uncachedRandomStats, -1);
  const double cachedRandomGps =
    benchSparseGensPerSecond(24, 20, 4, &cachedRandomStats, 0);
  const double randomChange =
    uncachedRandomGps > 0.0
      ? (cachedRandomGps / uncachedRandomGps - 1.0) * 100.0
      : 0.0;
  std::printf("BENCH: Sparse random dense 24x24 uncached gens/s=%.1f "
              "adaptive-memo gens/s=%.1f change=%.1f%% probes=%zu\n",
              uncachedRandomGps,
              cachedRandomGps,
              randomChange,
              cachedRandomStats.memoProbeCount);

  SparseAdvanceStats uncachedSparseStats;
  SparseAdvanceStats cachedSparseStats;
  const double uncachedSparseGps = benchSparseWideGensPerSecond(
    4096, 20, 1, &uncachedSparseStats, true, 4, -1);
  const double cachedSparseGps =
    benchSparseWideGensPerSecond(4096, 20, 1, &cachedSparseStats, true, 4, 0);
  const double sparseChange =
    uncachedSparseGps > 0.0
      ? (cachedSparseGps / uncachedSparseGps - 1.0) * 100.0
      : 0.0;
  std::printf("BENCH: Sparse candidate-only colonies=4096 uncached "
              "gens/s=%.1f adaptive-memo gens/s=%.1f change=%.1f%% "
              "probes=%zu\n",
              uncachedSparseGps,
              cachedSparseGps,
              sparseChange,
              cachedSparseStats.memoProbeCount);

  {
    const int denseChunksPerSide = 65;
    const int denseGenerations = 12;
    SparseCellGrid grid;
    seedSparseBenchmark(&grid, denseChunksPerSide, 303u);
    GameOfLifeRuleSet rules(nullptr);
    SparseCellGrid::setWorkerOverrideForTesting(4);
    SparseCellGrid::setCellCandidateOverrideForTesting(0);
    grid.advance(rules);
    RollingMetric stepMetric;
    SparseAdvanceStats lastStats;
    for (int generation = 0; generation < denseGenerations; ++generation) {
      const std::chrono::steady_clock::time_point t0 =
        std::chrono::steady_clock::now();
      grid.advance(rules);
      stepMetric.add(std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - t0)
                       .count());
      lastStats = grid.getLastAdvanceStats();
    }
    SparseCellGrid::setWorkerOverrideForTesting(0);
    SparseCellGrid::setCellCandidateOverrideForTesting(0);
    const char* path = lastStats.usedChangedFrontier
                         ? "frontier"
                         : (lastStats.usedCellCandidates ? "cells" : "chunks");
    std::printf("BENCH: Sparse dense soup %dx%d chunks N=%d p50/p95/max="
                "%.3f/%.3f/%.3f ms path=%s changed=%zu frontier-work=%zu "
                "complete-work=%zu workers=%u stages=disc/prep/eval/change/"
                "out/merge=%.3f/%.3f/%.3f/%.3f/%.3f/%.3f ms\n",
                denseChunksPerSide,
                denseChunksPerSide,
                denseGenerations,
                stepMetric.median(),
                stepMetric.p95(),
                stepMetric.maximum(),
                path,
                lastStats.changedChunkCount,
                lastStats.frontierEstimatedWork,
                lastStats.completeEstimatedWork,
                lastStats.workerCount,
                lastStats.candidateDiscoveryMilliseconds,
                lastStats.candidatePreparationMilliseconds,
                lastStats.candidateEvaluationMilliseconds,
                lastStats.candidateChangeTrackingMilliseconds,
                lastStats.candidateOutputMilliseconds,
                lastStats.candidateMergeMilliseconds);
    testTrue(g,
             stepMetric.size() == static_cast<std::size_t>(denseGenerations),
             "dense soup bench retains every measured generation");
    testTrue(g,
             lastStats.changedChunkCount > 4096u,
             "dense soup keeps a journal above the former 4,096-chunk cap");
    SparseGenerationDelta denseDelta;
    const std::uint64_t denseRevision = grid.getRevision();
    testTrue(
      g,
      denseRevision > 0u &&
        grid.captureGenerationDelta(denseRevision - 1u, &denseDelta, false) &&
        denseDelta.fullReplacement && denseDelta.changedChunks.empty() &&
        denseDelta.fullChunks.empty(),
      "dense soup captures a lightweight replacement marker");

    SparseCellGrid asyncPublished;
    SparseCellGrid asyncWorking;
    seedSparseBenchmark(&asyncPublished, denseChunksPerSide, 303u);
    GameOfLifeRuleSet asyncRules(nullptr);
    SimulationRunner runner;
    SparseGenerationDelta mirrorDelta;
    bool mirrorDeltaValid = false;
    RollingMetric runnerMetric;
    RollingMetric mirrorMetric;
    RollingMetric advanceMetric;
    RollingMetric captureMetric;
    const int runnerWarmup = 4;
    const int runnerMeasured = 12;
    bool runnerSucceeded = true;
    SparseCellGrid* published = &asyncPublished;
    SparseCellGrid* working = &asyncWorking;
    for (int generation = 0;
         generation < runnerWarmup + runnerMeasured && runnerSucceeded;
         ++generation) {
      SparseGenerationDelta transferDelta;
      if (mirrorDeltaValid) {
        transferDelta = std::move(mirrorDelta);
      }
      runnerSucceeded = runner.start(working,
                                     published,
                                     &asyncRules,
                                     std::move(transferDelta),
                                     mirrorDeltaValid);
      SparseCellGrid* completedGrid = nullptr;
      double elapsedMilliseconds = 0.0;
      bool advanceSucceeded = false;
      SimulationRunnerTimings timings;
      if (runnerSucceeded) {
        runnerSucceeded = runner.waitAndTakeCompleted(&completedGrid,
                                                      &mirrorDelta,
                                                      &elapsedMilliseconds,
                                                      &advanceSucceeded,
                                                      &timings) &&
                          advanceSucceeded && completedGrid == working;
      }
      if (!runnerSucceeded) {
        break;
      }
      SparseCellGrid* previousPublished = published;
      published = working;
      working = previousPublished;
      mirrorDeltaValid = true;
      if (generation >= runnerWarmup) {
        runnerMetric.add(elapsedMilliseconds);
        mirrorMetric.add(timings.mirrorMilliseconds);
        advanceMetric.add(timings.advanceMilliseconds);
        captureMetric.add(timings.captureMilliseconds);
      }
    }
    runner.shutdown();
    std::printf("BENCH: Sparse dense soup async %dx%d N=%d p50/p95="
                "%.3f/%.3f ms mirror=%.3f/%.3f ms advance=%.3f/%.3f ms "
                "capture=%.3f/%.3f ms\n",
                denseChunksPerSide,
                denseChunksPerSide,
                runnerMeasured,
                runnerMetric.median(),
                runnerMetric.p95(),
                mirrorMetric.median(),
                mirrorMetric.p95(),
                advanceMetric.median(),
                advanceMetric.p95(),
                captureMetric.median(),
                captureMetric.p95());
    testTrue(g, runnerSucceeded, "dense soup async runner published");
    testEqSize(g,
               runnerMetric.size(),
               static_cast<std::size_t>(runnerMeasured),
               "dense soup async bench retains every measured generation");
  }
}

static double
reportSparseFrameLatency(const char* name,
                         SparseCellGrid* grid,
                         const RuleSet& rules)
{
  const int warmupGenerations = 30;
  const int measuredGenerations = 300;
  for (int generation = 0; generation < warmupGenerations; ++generation) {
    grid->advance(rules);
  }

  RollingMetric metric;
  for (int generation = 0; generation < measuredGenerations; ++generation) {
    const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
    grid->advance(rules);
    const double milliseconds = std::chrono::duration<double, std::milli>(
                                  std::chrono::steady_clock::now() - start)
                                  .count();
    metric.add(milliseconds);
  }
  std::printf("BENCH: Frame latency %s N=%d p50/p95/max=%.3f/%.3f/%.3f ms\n",
              name,
              measuredGenerations,
              metric.median(),
              metric.p95(),
              metric.maximum());
  testEqSize(g,
             metric.size(),
             RollingMetric::kCapacity,
             "frame-latency bench retains the latest 256 samples");
  return metric.p95();
}

static double
reportAsyncSparseFrameLatency(const char* name,
                              SparseCellGrid* published,
                              SparseCellGrid* working,
                              const RuleSet& rules)
{
  const int warmupGenerations = 30;
  const int measuredGenerations = 300;
  SimulationRunner runner;
  SparseGenerationDelta mirrorDelta;
  bool mirrorDeltaValid = false;
  RollingMetric metric;
  RollingMetric mirrorMetric;
  RollingMetric advanceMetric;
  RollingMetric captureMetric;
  RollingMetric discoveryMetric;
  RollingMetric preparationMetric;
  RollingMetric evaluationMetric;
  RollingMetric changeMetric;
  RollingMetric recycleMetric;
  RollingMetric outputMetric;
  int topologyReuseCount = 0;
  bool succeeded = true;
  for (int generation = 0; generation < warmupGenerations + measuredGenerations;
       ++generation) {
    SparseGenerationDelta transferDelta;
    if (mirrorDeltaValid) {
      transferDelta = std::move(mirrorDelta);
    }
    succeeded = runner.start(
      working, published, &rules, std::move(transferDelta), mirrorDeltaValid);
    SparseCellGrid* completedGrid = nullptr;
    double elapsedMilliseconds = 0.0;
    bool advanceSucceeded = false;
    SimulationRunnerTimings timings;
    if (succeeded) {
      succeeded = runner.waitAndTakeCompleted(&completedGrid,
                                              &mirrorDelta,
                                              &elapsedMilliseconds,
                                              &advanceSucceeded,
                                              &timings) &&
                  advanceSucceeded && completedGrid == working;
    }
    if (!succeeded) {
      break;
    }
    SparseCellGrid* previousPublished = published;
    published = working;
    working = previousPublished;
    mirrorDeltaValid = true;
    if (published->getLastAdvanceStats().reusedCandidateTopology) {
      topologyReuseCount += 1;
    }
    if (generation >= warmupGenerations) {
      const SparseAdvanceStats& stats = published->getLastAdvanceStats();
      metric.add(elapsedMilliseconds);
      mirrorMetric.add(timings.mirrorMilliseconds);
      advanceMetric.add(timings.advanceMilliseconds);
      captureMetric.add(timings.captureMilliseconds);
      discoveryMetric.add(stats.candidateDiscoveryMilliseconds);
      preparationMetric.add(stats.candidatePreparationMilliseconds);
      evaluationMetric.add(stats.candidateEvaluationMilliseconds);
      changeMetric.add(stats.candidateChangeTrackingMilliseconds);
      recycleMetric.add(stats.candidateRecycleMilliseconds);
      outputMetric.add(stats.candidateOutputMilliseconds);
    }
  }
  runner.shutdown();
  std::printf("BENCH: Frame latency %s N=%d p50/p95/max=%.3f/%.3f/%.3f ms\n",
              name,
              measuredGenerations,
              metric.median(),
              metric.p95(),
              metric.maximum());
  std::printf("BENCH: Async stages %s mirror p50/p95=%.3f/%.3f ms "
              "advance=%.3f/%.3f ms capture=%.3f/%.3f ms topology-reuse=%d\n",
              name,
              mirrorMetric.median(),
              mirrorMetric.p95(),
              advanceMetric.median(),
              advanceMetric.p95(),
              captureMetric.median(),
              captureMetric.p95(),
              topologyReuseCount);
  std::printf("BENCH: Async candidate stages %s p50 discovery/prep/eval/"
              "change/recycle/output=%.3f/%.3f/%.3f/%.3f/%.3f/%.3f ms\n",
              name,
              discoveryMetric.median(),
              preparationMetric.median(),
              evaluationMetric.median(),
              changeMetric.median(),
              recycleMetric.median(),
              outputMetric.median());
  testTrue(
    g, succeeded, "async frame-latency bench publishes every generation");
  testEqSize(g,
             metric.size(),
             RollingMetric::kCapacity,
             "async frame-latency bench retains the latest 256 samples");
  return metric.p95();
}

static void
testFrameLatencyBenchReport()
{
  testSection("Sim: warmed frame-latency report (informational)");
  GameOfLifeRuleSet rules(nullptr);
  SparseCellGrid::setWorkerOverrideForTesting(0);
  SparseCellGrid::setCellCandidateOverrideForTesting(0);

  SparseCellGrid dense;
  seedSparseBenchmark(&dense, 24, 901u);
  reportSparseFrameLatency("dense-24x24-chunks", &dense, rules);

  SparseCellGrid sparseWide;
  seedSparseWideBlinkers(&sparseWide, 256);
  reportSparseFrameLatency("sparse-wide-256-colonies", &sparseWide, rules);

  SparseCellGrid dispersed;
  seedSparseWideBlinkers(&dispersed, 16384);
  const double synchronousP95 =
    reportSparseFrameLatency("dispersed-16384-colonies", &dispersed, rules);

  SparseCellGrid asyncPublished;
  SparseCellGrid asyncWorking;
  seedSparseWideBlinkers(&asyncPublished, 16384);
  const double asynchronousP95 = reportAsyncSparseFrameLatency(
    "async-dispersed-16384-colonies", &asyncPublished, &asyncWorking, rules);
  const double overheadPercent =
    synchronousP95 > 0.0 ? (asynchronousP95 / synchronousP95 - 1.0) * 100.0
                         : 0.0;
  std::printf("BENCH: Async dispersed p95 overhead=%.1f%% (informational)\n",
              overheadPercent);
}

static int
runSimCase(void (*testFunction)())
{
  g.failures = 0;
  RuleSet::setWorkerOverride(0);
  SparseCellGrid::setWorkerOverrideForTesting(0);
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  SparseCellGrid::setChunkNodeReuseOverrideForTesting(true);
  SparseCellGrid::setChunkMemoOverrideForTesting(0);
  testFunction();
  RuleSet::setWorkerOverride(0);
  SparseCellGrid::setWorkerOverrideForTesting(0);
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  SparseCellGrid::setChunkNodeReuseOverrideForTesting(true);
  SparseCellGrid::setChunkMemoOverrideForTesting(0);
  return g.failures;
}

void
registerSimTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.Sim.DoubleBufferStillLife",
               []() { return runSimCase(testDoubleBufferAndStillLife); });
  registry.add("IllumoGame.Sim.DirtyAabbTightness",
               []() { return runSimCase(testDirtyAabbTightness); });
  registry.add("IllumoGame.Sim.ToroidalEdge",
               []() { return runSimCase(testToroidalEdge); });
  registry.add("IllumoGame.Sim.SerialParallelIdentical",
               []() { return runSimCase(testSerialParallelIdentical); });
  registry.add("IllumoGame.Sim.MicroBench",
               []() { return runSimCase(testMicroBenchReport); });
  registry.add("IllumoGame.Sim.SparseMicroBench",
               []() { return runSimCase(testSparseMicroBenchReport); });
  registry.add(
    "IllumoGame.Sim.FrameLatencyBench",
    []() { return runSimCase(testFrameLatencyBenchReport); },
    180);
}
