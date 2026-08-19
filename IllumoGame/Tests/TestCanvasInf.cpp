#include "Game/CanvasView.h"
#include "Game/Cursor.h"
#include "Game/SimulationRunner.h"
#include "Game/SparseCellGrid.h"
#include "Rulesets/BrainsBrainRuleSet.h"
#include "Rulesets/DayAndNightRuleSet.h"
#include "Rulesets/GameOfLifeRuleSet.h"
#include "Rulesets/HighlifeRuleSet.h"
#include "Rulesets/LifeWithoutDeathRuleSet.h"
#include "Rulesets/Rule90RuleSet.h"
#include "Rulesets/RuleSet.h"
#include "Rulesets/SeedsRuleSet.h"
#include "Rulesets/WireworldRuleSet.h"
#include "TestHarness.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <vector>

static TestCounters g;

static void
testNegativeChunkMapping()
{
  testSection("SparseCellGrid: negative coordinates and chunk boundaries");
  testEqInt(g,
            static_cast<int>(SparseCellGrid::floorDivide(-1, 16)),
            -1,
            "-1 floors into chunk -1");
  testEqInt(g,
            static_cast<int>(SparseCellGrid::floorModulo(-1, 16)),
            15,
            "-1 maps to local 15");
  testEqInt(g,
            static_cast<int>(SparseCellGrid::floorDivide(-16, 16)),
            -1,
            "-16 stays in chunk -1");
  testEqInt(g,
            static_cast<int>(SparseCellGrid::floorModulo(-16, 16)),
            0,
            "-16 maps to local 0");

  SparseCellGrid grid;
  grid.setCell(CellAddress{ -1, -1 }, 0);
  grid.setCell(CellAddress{ -16, -16 }, 2);
  grid.setCell(CellAddress{ -17, -17 }, 3);
  testEqUChar(g, grid.getCell(CellAddress{ -1, -1 }), 0, "negative cell read");
  testEqUChar(
    g, grid.getCell(CellAddress{ -16, -16 }), 2, "boundary cell read");
  testEqUChar(
    g, grid.getCell(CellAddress{ -17, -17 }), 3, "next negative chunk read");
  testEqSize(
    g, grid.getAllocatedChunkCount(), 2, "negative writes allocate chunks");
}

static void
testUnboundedChunks()
{
  testSection("SparseCellGrid: no fixed chunk pool cap");
  SparseCellGrid grid;
  for (std::int64_t i = 0; i < 256; ++i) {
    grid.setCell(CellAddress{ i * 32, -i * 32 }, 0);
  }
  testTrue(g,
           grid.getAllocatedChunkCount() > 64,
           "far-apart chunks exceed the old pool capacity");
  testEqUChar(g,
              grid.getCell(CellAddress{ 255 * 32, -255 * 32 }),
              0,
              "far chunk remains addressable");
}

static void
testSparseSimulationBoundaries()
{
  testSection("SparseCellGrid: serial halo stepping");
  GameOfLifeRuleSet rules(nullptr);
  SparseCellGrid grid;
  grid.setCell(CellAddress{ 15, 0 }, 0);
  grid.setCell(CellAddress{ 15, 1 }, 0);
  grid.setCell(CellAddress{ 15, 2 }, 0);
  testTrue(g, grid.advance(rules), "cross-boundary generation advances");
  testEqUChar(
    g, grid.getCell(CellAddress{ 14, 1 }), 0, "left birth across chunk");
  testEqUChar(g, grid.getCell(CellAddress{ 15, 1 }), 0, "boundary survivor");
  testEqUChar(
    g, grid.getCell(CellAddress{ 16, 1 }), 0, "right birth across chunk");
  testEqUChar(g,
              grid.getCell(CellAddress{ 0, 1 }),
              SparseCellGrid::BackgroundState,
              "no toroidal wrapping");

  SparseCellGrid cornerGrid;
  cornerGrid.setCell(CellAddress{ 15, 15 }, 0);
  cornerGrid.setCell(CellAddress{ 16, 15 }, 0);
  cornerGrid.setCell(CellAddress{ 17, 15 }, 0);
  testTrue(g, cornerGrid.advance(rules), "corner-crossing generation advances");
  testEqUChar(g,
              cornerGrid.getCell(CellAddress{ 16, 14 }),
              0,
              "upper birth crosses chunks");
  testEqUChar(g,
              cornerGrid.getCell(CellAddress{ 16, 15 }),
              0,
              "center survivor crosses chunks");
  testEqUChar(g,
              cornerGrid.getCell(CellAddress{ 16, 16 }),
              0,
              "lower birth crosses chunks");

  SparseCellGrid first;
  SparseCellGrid second;
  first.setCell(CellAddress{ -17, 0 }, 0);
  first.setCell(CellAddress{ -16, 0 }, 0);
  first.setCell(CellAddress{ -15, 0 }, 0);
  second.setCell(CellAddress{ -17, 0 }, 0);
  second.setCell(CellAddress{ -16, 0 }, 0);
  second.setCell(CellAddress{ -15, 0 }, 0);
  first.advance(rules);
  second.advance(rules);
  const std::vector<SparseChunkRecord> firstRecords =
    first.collectChunkRecords();
  const std::vector<SparseChunkRecord> secondRecords =
    second.collectChunkRecords();
  testTrue(g,
           firstRecords.size() == secondRecords.size(),
           "serial output size deterministic");
  bool same = firstRecords.size() == secondRecords.size();
  if (same) {
    for (std::size_t i = 0; i < firstRecords.size(); ++i) {
      same = same && firstRecords[i].chunkX == secondRecords[i].chunkX &&
             firstRecords[i].chunkY == secondRecords[i].chunkY &&
             firstRecords[i].cells == secondRecords[i].cells;
    }
  }
  testTrue(g, same, "serial output bytes deterministic");
}

static void
testSparseRevisionAndBoundedVisit()
{
  testSection("SparseCellGrid: revisions and bounded chunk visits");
  GameOfLifeRuleSet rules(nullptr);
  SparseCellGrid grid;
  const std::uint64_t emptyRevision = grid.getRevision();
  testTrue(g, grid.advance(rules), "empty generation advances");
  testTrue(g,
           grid.getRevision() == emptyRevision,
           "empty generation does not change revision");

  grid.setCell(CellAddress{ 0, 0 }, 0);
  grid.setCell(CellAddress{ 1, 0 }, 0);
  grid.setCell(CellAddress{ 0, 1 }, 0);
  grid.setCell(CellAddress{ 1, 1 }, 0);
  const std::uint64_t stillLifeRevision = grid.getRevision();
  testTrue(g, grid.advance(rules), "still life generation advances");
  testTrue(g,
           grid.getRevision() == stillLifeRevision,
           "still life generation does not change revision");

  grid.setCell(CellAddress{ -17, 0 }, 0);
  grid.setCell(CellAddress{ 160, 0 }, 0);
  int visited = 0;
  bool visitedOutsideBounds = false;
  grid.visitChunksInBounds(
    ChunkAddress{ -2, 0 },
    ChunkAddress{ 0, 0 },
    [&visited, &visitedOutsideBounds](const ChunkAddress& address,
                                      const SparseCellGrid::ChunkCells& cells) {
      visited += 1;
      visitedOutsideBounds = visitedOutsideBounds || address.x < -2 ||
                             address.x > 0 || cells[0] == 255;
    });
  testEqInt(
    g, visited, 2, "bounded visit returns only allocated chunks in range");
  testTrue(g, !visitedOutsideBounds, "bounded visit excludes distant chunk");
}

static void
seedSparseRandom(SparseCellGrid* grid, int dimension, unsigned int seed)
{
  if (grid == nullptr) {
    return;
  }

  unsigned int state = seed;
  const int first = -dimension / 2;
  const int last = first + dimension;
  for (int y = first; y < last; ++y) {
    for (int x = first; x < last; ++x) {
      state = state * 1664525u + 1013904223u;
      if ((state >> 29) < 2u) {
        grid->setCell(CellAddress{ x, y }, 0);
      }
    }
  }
}

static bool
sameSparseRecords(const std::vector<SparseChunkRecord>& left,
                  const std::vector<SparseChunkRecord>& right)
{
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].chunkX != right[index].chunkX ||
        left[index].chunkY != right[index].chunkY ||
        left[index].cells != right[index].cells) {
      return false;
    }
  }
  return true;
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

static void
testSparseParallelDeterminism()
{
  testSection("SparseCellGrid: serial and parallel target stepping");
  GameOfLifeRuleSet rules(nullptr);
  SparseCellGrid serial;
  SparseCellGrid parallel;
  seedSparseRandom(&serial, 160, 17u);
  seedSparseRandom(&parallel, 160, 17u);

  SparseCellGrid::setCellCandidateOverrideForTesting(-1);
  SparseCellGrid::setWorkerOverrideForTesting(1);
  for (int generation = 0; generation < 6; ++generation) {
    testTrue(g, serial.advance(rules), "serial sparse generation advances");
  }
  testEqInt(g,
            static_cast<int>(serial.getLastAdvanceStats().workerCount),
            1,
            "serial override uses one worker");

  SparseCellGrid::setWorkerOverrideForTesting(4);
  for (int generation = 0; generation < 6; ++generation) {
    testTrue(g, parallel.advance(rules), "parallel sparse generation advances");
    if (generation == 0) {
      testEqInt(g,
                static_cast<int>(parallel.getLastAdvanceStats().workerCount),
                4,
                "dense sparse generation uses four workers");
    }
  }
  testTrue(g,
           parallel.getLastAdvanceStats().targetChunkCount >= 32u,
           "large region crosses parallel target threshold");
  testTrue(g,
           sameSparseRecords(serial.collectChunkRecords(),
                             parallel.collectChunkRecords()),
           "parallel sparse results are byte-identical to serial");

  SparseCellGrid small;
  small.setCell(CellAddress{ 0, 0 }, 0);
  small.setCell(CellAddress{ 1, 0 }, 0);
  small.setCell(CellAddress{ 2, 0 }, 0);
  SparseCellGrid::setWorkerOverrideForTesting(0);
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  testTrue(g, small.advance(rules), "small sparse generation advances");
  testEqInt(g,
            static_cast<int>(small.getLastAdvanceStats().workerCount),
            1,
            "small target set stays on the serial path");
}

static void
seedWideBlinkers(SparseCellGrid* grid, int count)
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
testSparseCellCandidates()
{
  testSection("SparseCellGrid: adaptive cell candidates");
  GameOfLifeRuleSet life(nullptr);
  SparseCellGrid candidates;
  SparseCellGrid fullChunks;
  seedWideBlinkers(&candidates, 96);
  seedWideBlinkers(&fullChunks, 96);

  SparseCellGrid::setWorkerOverrideForTesting(1);
  SparseCellGrid::setCellCandidateOverrideForTesting(1);
  for (int generation = 0; generation < 6; ++generation) {
    testTrue(g, candidates.advance(life), "cell-candidate generation advances");
  }
  const SparseAdvanceStats candidateStats = candidates.getLastAdvanceStats();
  testTrue(g,
           candidateStats.usedCellCandidates,
           "wide sparse colony uses cell candidates");
  testTrue(g,
           candidateStats.candidateCellCount <
             candidateStats.targetChunkCount * SparseCellGrid::kChunkCellCount,
           "wide sparse colony avoids whole-chunk cell evaluation");

  SparseCellGrid::setCellCandidateOverrideForTesting(-1);
  for (int generation = 0; generation < 6; ++generation) {
    testTrue(g, fullChunks.advance(life), "full-chunk generation advances");
  }
  const SparseAdvanceStats fullChunkStats = fullChunks.getLastAdvanceStats();
  testTrue(g,
           !fullChunkStats.usedCellCandidates,
           "test override retains full-chunk evaluator");
  testTrue(g,
           sameSparseRecords(candidates.collectChunkRecords(),
                             fullChunks.collectChunkRecords()),
           "cell candidates remain byte-identical to full chunks");

  WireworldRuleSet wireworld(nullptr);
  SparseCellGrid dense;
  SparseCellGrid denseReference;
  bool denseAssigned = true;
  for (int chunkY = 0; chunkY < 9; ++chunkY) {
    for (int chunkX = 0; chunkX < 9; ++chunkX) {
      SparseChunkRecord record;
      record.chunkX = chunkX;
      record.chunkY = chunkY;
      record.cells.fill(WireworldRuleSet::CELL_CONDUCTOR);
      denseAssigned = dense.assignChunk(record) && denseAssigned;
      denseAssigned = denseReference.assignChunk(record) && denseAssigned;
    }
  }
  testTrue(g, denseAssigned, "dense conductor chunks assign");
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  SparseCellGrid::setWorkerOverrideForTesting(4);
  testTrue(g, dense.advance(wireworld), "dense generation advances");
  testTrue(g,
           dense.getLastAdvanceStats().usedCellCandidates,
           "dense conductors use cell candidates without counted heads");
  testEqSize(g,
             dense.getLastAdvanceStats().countedCellCount,
             0u,
             "Wireworld conductors are stored but not neighbor-counted");
  testEqSize(g,
             dense.getLastAdvanceStats().haloTargetCount,
             0u,
             "conductor-only targets avoid halo evaluation");
  testEqInt(g,
            static_cast<int>(dense.getLastAdvanceStats().workerCount),
            4,
            "dense conductor candidates retain bounded parallel workers");
  SparseCellGrid::setCellCandidateOverrideForTesting(-1);
  testTrue(g,
           denseReference.advance(wireworld),
           "dense reference generation advances");
  testTrue(g,
           sameSparseRecords(dense.collectChunkRecords(),
                             denseReference.collectChunkRecords()),
           "conductor candidate output matches full chunks");
}

static void
seedMixedTargetWorld(SparseCellGrid* grid)
{
  if (grid == nullptr) {
    return;
  }
  SparseChunkRecord dense;
  dense.chunkX = 0;
  dense.chunkY = 0;
  dense.cells.fill(0);
  dense.cells[0] = 2;
  grid->assignChunk(dense);
  for (int index = 0; index < 64; ++index) {
    const std::int64_t x = static_cast<std::int64_t>(index + 2) * 32;
    grid->setCell(CellAddress{ x, -1 }, 0);
    grid->setCell(CellAddress{ x, 0 }, 0);
    grid->setCell(CellAddress{ x, 1 }, 0);
  }
}

static void
testSparsePerTargetAdaptiveEvaluation()
{
  testSection("SparseCellGrid: per-target adaptive evaluation");
  GameOfLifeRuleSet rules(nullptr);
  SparseCellGrid adaptive;
  SparseCellGrid fullChunks;
  seedMixedTargetWorld(&adaptive);
  seedMixedTargetWorld(&fullChunks);

  SparseCellGrid::setWorkerOverrideForTesting(1);
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  testTrue(g, adaptive.advance(rules), "mixed adaptive generation advances");
  const SparseAdvanceStats adaptiveStats = adaptive.getLastAdvanceStats();
  testTrue(g,
           adaptiveStats.usedMixedTargets,
           "mixed world selects candidate and halo targets independently");
  testTrue(g,
           adaptiveStats.candidateTargetCount > 0u,
           "sparse targets select candidate evaluation");
  testTrue(g,
           adaptiveStats.haloTargetCount > 0u,
           "dense targets select halo evaluation");
  testTrue(g,
           adaptiveStats.countedCellCount < adaptiveStats.activeCellCount,
           "counted-state diagnostics remain distinct from stored cells");

  SparseCellGrid::setCellCandidateOverrideForTesting(-1);
  testTrue(g, fullChunks.advance(rules), "mixed full generation advances");
  testTrue(g,
           sameSparseRecords(adaptive.collectChunkRecords(),
                             fullChunks.collectChunkRecords()),
           "per-target adaptive output matches complete halo evaluation");
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
}

static void
testSparseCandidateParallelDeterminism()
{
  testSection("SparseCellGrid: coarse parallel candidate evaluation");
  GameOfLifeRuleSet rules(nullptr);
  SparseCellGrid serial;
  SparseCellGrid parallel;
  const int colonyCount = 512;
  seedWideBlinkers(&serial, colonyCount);
  seedWideBlinkers(&parallel, colonyCount);

  SparseCellGrid::setCellCandidateOverrideForTesting(1);
  SparseCellGrid::setWorkerOverrideForTesting(1);
  for (int generation = 0; generation < 4; ++generation) {
    testTrue(g, serial.advance(rules), "serial candidate generation advances");
  }

  SparseCellGrid::setWorkerOverrideForTesting(4);
  for (int generation = 0; generation < 4; ++generation) {
    testTrue(
      g, parallel.advance(rules), "parallel candidate generation advances");
  }
  const SparseAdvanceStats parallelStats = parallel.getLastAdvanceStats();
  testEqInt(g,
            static_cast<int>(parallelStats.workerCount),
            4,
            "candidate evaluation uses the requested worker count");
  testEqInt(g,
            static_cast<int>(parallelStats.candidatePreparationWorkerCount),
            4,
            "candidate preparation uses the requested worker count");
  testTrue(g,
           parallelStats.candidateWorkRangeCount >= 4u,
           "candidate work provides at least one coarse range per worker");
  testTrue(g,
           parallelStats.candidatePreparationRangeCount >= 4u &&
             parallelStats.candidatePreparationRangeCount <
               parallelStats.targetChunkCount,
           "candidate preparation claims coarse target ranges");
  testTrue(g,
           parallelStats.candidateWorkRangeCount * 32u <
             parallelStats.targetChunkCount,
           "candidate workers claim coarse ranges instead of target chunks");
  testTrue(g,
           sameSparseRecords(serial.collectChunkRecords(),
                             parallel.collectChunkRecords()),
           "parallel candidates remain byte-identical to serial candidates");
}

static void
seedChunkBoundaryBlocks(SparseCellGrid* grid, int count)
{
  if (grid == nullptr) {
    return;
  }
  for (int index = 0; index < count; ++index) {
    const std::int64_t boundaryX = static_cast<std::int64_t>(index) * 64;
    const std::int64_t boundaryY = index % 2 == 0 ? 0 : -16;
    grid->setCell(CellAddress{ boundaryX - 1, boundaryY - 1 }, 0);
    grid->setCell(CellAddress{ boundaryX, boundaryY - 1 }, 0);
    grid->setCell(CellAddress{ boundaryX - 1, boundaryY }, 0);
    grid->setCell(CellAddress{ boundaryX, boundaryY }, 0);
  }
}

static void
testSparseCandidatePreparation()
{
  testSection("SparseCellGrid: parallel candidate preparation");
  GameOfLifeRuleSet rules(nullptr);
  SparseCellGrid serial;
  SparseCellGrid parallel;
  SparseCellGrid fullChunks;
  seedChunkBoundaryBlocks(&serial, 128);
  seedChunkBoundaryBlocks(&parallel, 128);
  seedChunkBoundaryBlocks(&fullChunks, 128);

  SparseCellGrid::setCellCandidateOverrideForTesting(1);
  SparseCellGrid::setWorkerOverrideForTesting(1);
  for (int generation = 0; generation < 3; ++generation) {
    testTrue(g, serial.advance(rules), "serial preparation advances");
  }
  testEqInt(g,
            static_cast<int>(
              serial.getLastAdvanceStats().candidatePreparationWorkerCount),
            1,
            "serial preparation remains direct");

  SparseCellGrid::setWorkerOverrideForTesting(4);
  for (int generation = 0; generation < 3; ++generation) {
    testTrue(g, parallel.advance(rules), "parallel preparation advances");
  }
  testEqInt(g,
            static_cast<int>(
              parallel.getLastAdvanceStats().candidatePreparationWorkerCount),
            4,
            "target-centric preparation uses four workers");

  SparseCellGrid::setCellCandidateOverrideForTesting(-1);
  SparseCellGrid::setWorkerOverrideForTesting(1);
  for (int generation = 0; generation < 3; ++generation) {
    testTrue(g, fullChunks.advance(rules), "full-halo reference advances");
  }
  testTrue(g,
           sameSparseRecords(serial.collectChunkRecords(),
                             parallel.collectChunkRecords()),
           "parallel preparation matches serial preparation at boundaries");
  testTrue(g,
           sameSparseRecords(serial.collectChunkRecords(),
                             fullChunks.collectChunkRecords()),
           "candidate preparation matches full halos at boundaries");
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  SparseCellGrid::setWorkerOverrideForTesting(0);
}

static void
seedStableBlocksAndBlinker(SparseCellGrid* grid, int blockCount)
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

static void
testSparseChangedFrontier()
{
  testSection("SparseCellGrid: retained changed-region frontier");
  GameOfLifeRuleSet life(nullptr);
  SparseCellGrid stable;
  stable.setCell(CellAddress{ 3, 3 }, 0);
  stable.setCell(CellAddress{ 4, 3 }, 0);
  stable.setCell(CellAddress{ 3, 4 }, 0);
  stable.setCell(CellAddress{ 4, 4 }, 0);

  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  testTrue(g, stable.advance(life), "still life frontier settles");
  testTrue(g,
           stable.getLastAdvanceStats().usedChangedFrontier ||
             stable.getLastAdvanceStats().frontierEstimatedWork >
               stable.getLastAdvanceStats().completeEstimatedWork,
           "small edited region is evaluated or cost-rejected");
  const std::uint64_t stableRevision = stable.getRevision();
  testTrue(g, stable.advance(life), "settled frontier advances");
  testTrue(g,
           stable.getLastAdvanceStats().usedChangedFrontier,
           "settled world stays on the frontier path");
  testEqSize(g,
             stable.getLastAdvanceStats().frontierTargetCount,
             0u,
             "settled world evaluates zero target chunks");
  testEqSize(g,
             static_cast<std::size_t>(stable.getRevision()),
             static_cast<std::size_t>(stableRevision),
             "settled frontier keeps its revision");

  SparseCellGrid optimized;
  SparseCellGrid reference;
  seedStableBlocksAndBlinker(&optimized, 128);
  seedStableBlocksAndBlinker(&reference, 128);
  bool observedLocalFrontier = false;
  for (int generation = 0; generation < 6; ++generation) {
    SparseCellGrid::setCellCandidateOverrideForTesting(0);
    testTrue(g, optimized.advance(life), "frontier generation advances");
    if (generation > 0 && optimized.getLastAdvanceStats().usedChangedFrontier) {
      observedLocalFrontier = true;
      testTrue(g,
               optimized.getLastAdvanceStats().frontierTargetCount <
                 optimized.getLastAdvanceStats().activeChunkCount,
               "localized oscillator evaluates less than the static world");
    }
    SparseCellGrid::setCellCandidateOverrideForTesting(-1);
    testTrue(g, reference.advance(life), "full reference generation advances");
  }
  testTrue(g,
           observedLocalFrontier,
           "localized activity enters the changed-region frontier");
  testTrue(g,
           sameSparseRecords(optimized.collectChunkRecords(),
                             reference.collectChunkRecords()),
           "frontier output matches complete full-chunk stepping");

  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  stable.setCell(CellAddress{ 20, 20 }, 0);
  testTrue(g, stable.advance(life), "edited settled world advances");
  testTrue(g,
           (stable.getLastAdvanceStats().usedChangedFrontier &&
            stable.getLastAdvanceStats().frontierTargetCount > 0u) ||
             stable.getLastAdvanceStats().frontierEstimatedWork >
               stable.getLastAdvanceStats().completeEstimatedWork,
           "editing evaluates or cost-rejects the local frontier");

  SparseCellGrid ruleChange;
  ruleChange.setCell(CellAddress{ 0, 0 }, 0);
  ruleChange.setCell(CellAddress{ 1, 0 }, 0);
  ruleChange.setCell(CellAddress{ 0, 1 }, 0);
  ruleChange.setCell(CellAddress{ 1, 1 }, 0);
  testTrue(g, ruleChange.advance(life), "life block settles");
  SeedsRuleSet seeds(nullptr);
  const std::uint64_t lifeRevision = ruleChange.getRevision();
  testTrue(g, ruleChange.advance(seeds), "ruleset change advances");
  testTrue(g,
           ruleChange.getLastAdvanceStats().usedChangedFrontier ||
             ruleChange.getLastAdvanceStats().frontierEstimatedWork >
               ruleChange.getLastAdvanceStats().completeEstimatedWork,
           "ruleset change evaluates or cost-rejects the invalidated region");
  testTrue(g,
           ruleChange.getRevision() > lifeRevision,
           "ruleset change produces a new generation");
}

static void
seedAdaptiveFrontierWorld(SparseCellGrid* grid,
                          int stableBlockCount,
                          int blinkerCount)
{
  if (grid == nullptr) {
    return;
  }
  for (int block = 0; block < stableBlockCount; ++block) {
    const std::int64_t x = static_cast<std::int64_t>(block) * 64 + 4;
    const std::int64_t y = 64;
    grid->setCell(CellAddress{ x, y }, 0);
    grid->setCell(CellAddress{ x + 1, y }, 0);
    grid->setCell(CellAddress{ x, y + 1 }, 0);
    grid->setCell(CellAddress{ x + 1, y + 1 }, 0);
  }
  for (int blinker = 0; blinker < blinkerCount; ++blinker) {
    const std::int64_t x = static_cast<std::int64_t>(blinker) * 64 + 15;
    grid->setCell(CellAddress{ x, -1 }, 0);
    grid->setCell(CellAddress{ x, 0 }, 0);
    grid->setCell(CellAddress{ x, 1 }, 0);
  }
}

static void
testSparseAdaptiveFrontierCost()
{
  testSection("SparseCellGrid: adaptive frontier cost and candidates");
  GameOfLifeRuleSet life(nullptr);
  SparseCellGrid optimized;
  SparseCellGrid reference;
  seedAdaptiveFrontierWorld(&optimized, 1024, 8);
  seedAdaptiveFrontierWorld(&reference, 1024, 8);

  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  testTrue(g, optimized.advance(life), "adaptive baseline generation advances");
  SparseCellGrid::setCellCandidateOverrideForTesting(1);
  testTrue(g, reference.advance(life), "complete baseline generation advances");

  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  testTrue(g, optimized.advance(life), "wide local frontier advances");
  const SparseAdvanceStats frontierStats = optimized.getLastAdvanceStats();
  testTrue(g,
           frontierStats.usedChangedFrontier,
           "cost model keeps the localized frontier");
  testTrue(g,
           frontierStats.frontierTargetCount > 0u &&
             frontierStats.frontierTargetCount < 64u,
           "cell-precise changes avoid the former chunk-wide expansion");
  testTrue(g,
           frontierStats.candidateTargetCount > 0u &&
             frontierStats.usedCellCandidates,
           "sparse frontier evaluates candidate masks");
  testTrue(
    g,
    frontierStats.frontierEstimatedWork <= frontierStats.completeEstimatedWork,
    "selected frontier has no more estimated work than complete stepping");
  testTrue(g,
           frontierStats.frontierSourceChunkCount <
             frontierStats.activeChunkCount,
           "frontier candidate preparation visits only local source chunks");

  SparseCellGrid::setCellCandidateOverrideForTesting(1);
  testTrue(
    g, reference.advance(life), "complete comparison generation advances");
  testTrue(g,
           sameSparseRecords(optimized.collectChunkRecords(),
                             reference.collectChunkRecords()),
           "adaptive candidate frontier matches complete candidate stepping");

  SparseCellGrid broad;
  seedWideBlinkers(&broad, 128);
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  testTrue(g, broad.advance(life), "broad-change generation advances");
  const SparseAdvanceStats broadStats = broad.getLastAdvanceStats();
  testTrue(g,
           !broadStats.usedChangedFrontier,
           "cost model rejects a broad separated frontier");
  testTrue(g,
           broadStats.frontierEstimatedWork > broadStats.completeEstimatedWork,
           "broad frontier rejection is explained by estimated work");
}

static void
testSparsePreciseActivityMasks()
{
  testSection("SparseCellGrid: cell-precise activity gating");
  GameOfLifeRuleSet life(nullptr);

  SparseCellGrid interior;
  seedStableBlocksAndBlinker(&interior, 128);
  interior.setCell(CellAddress{ 0, -1 }, SparseCellGrid::BackgroundState);
  interior.setCell(CellAddress{ 0, 0 }, SparseCellGrid::BackgroundState);
  interior.setCell(CellAddress{ 0, 1 }, SparseCellGrid::BackgroundState);
  interior.setCell(CellAddress{ 7, 6 }, 0);
  interior.setCell(CellAddress{ 7, 7 }, 0);
  interior.setCell(CellAddress{ 7, 8 }, 0);
  testTrue(g, interior.advance(life), "interior blinker advances");
  testTrue(g, interior.advance(life), "interior blinker advances again");
  const SparseAdvanceStats interiorStats = interior.getLastAdvanceStats();
  testTrue(g,
           interiorStats.usedChangedFrontier,
           "interior change uses the precise frontier");
  testEqSize(g,
             interiorStats.frontierTargetCount,
             1u,
             "interior changes process only their own chunk");
  testEqSize(g,
             interiorStats.changedCellCount,
             4u,
             "blinker reports its four exact changed cells");
  testEqSize(g,
             interiorStats.countedChangedCellCount,
             4u,
             "binary blinker reports the same counting changes");

  SparseCellGrid boundary;
  seedStableBlocksAndBlinker(&boundary, 128);
  boundary.setCell(CellAddress{ 0, -1 }, SparseCellGrid::BackgroundState);
  boundary.setCell(CellAddress{ 0, 0 }, SparseCellGrid::BackgroundState);
  boundary.setCell(CellAddress{ 0, 1 }, SparseCellGrid::BackgroundState);
  boundary.setCell(CellAddress{ 15, 6 }, 0);
  boundary.setCell(CellAddress{ 15, 7 }, 0);
  boundary.setCell(CellAddress{ 15, 8 }, 0);
  testTrue(g, boundary.advance(life), "boundary blinker advances");
  testTrue(g, boundary.advance(life), "boundary blinker advances again");
  const SparseAdvanceStats boundaryStats = boundary.getLastAdvanceStats();
  testTrue(g,
           boundaryStats.frontierTargetCount > 1u &&
             boundaryStats.frontierTargetCount <= 2u,
           "edge changes enroll only the touching neighbor chunk");

  WireworldRuleSet wireworld(nullptr);
  SparseCellGrid conductors;
  SparseChunkRecord conductor;
  conductor.cells.fill(WireworldRuleSet::CELL_CONDUCTOR);
  testTrue(g, conductors.assignChunk(conductor), "conductor chunk assigns");
  SparseCellGrid::setCellCandidateOverrideForTesting(-1);
  testTrue(g, conductors.advance(wireworld), "conductor chunk advances");
  const SparseAdvanceStats conductorStats = conductors.getLastAdvanceStats();
  testEqSize(g,
             conductorStats.countedCellCount,
             0u,
             "conductors do not contribute neighbor activity");
  testEqSize(g,
             conductorStats.targetChunkCount,
             1u,
             "non-counting stored cells do not enroll neighbor chunks");
}

static void
testSparseChunkMemoization()
{
  testSection("SparseCellGrid: exact on-demand chunk memoization");
  GameOfLifeRuleSet life(nullptr);
  SparseCellGrid cached;
  SparseCellGrid reference;
  seedRepeatedDenseChunks(&cached, 128);
  seedRepeatedDenseChunks(&reference, 128);

  SparseCellGrid::setWorkerOverrideForTesting(4);
  SparseCellGrid::setCellCandidateOverrideForTesting(-1);
  for (int generation = 0; generation < 4; ++generation) {
    SparseCellGrid::setChunkMemoOverrideForTesting(1);
    testTrue(g, cached.advance(life), "memoized generation advances");
    const SparseAdvanceStats cachedStats = cached.getLastAdvanceStats();
    testTrue(g,
             cachedStats.memoProbeCount > 0u &&
               cachedStats.memoHitCount > cachedStats.memoMissCount,
             "repeated neighborhoods produce exact cache hits");
    testTrue(g,
             cachedStats.memoMemoryBytes <= 4u * 1024u * 1024u,
             "per-grid memo storage remains within the bounded budget");

    SparseCellGrid::setChunkMemoOverrideForTesting(-1);
    testTrue(g, reference.advance(life), "uncached generation advances");
    testTrue(g,
             sameSparseRecords(cached.collectChunkRecords(),
                               reference.collectChunkRecords()),
             "memoized output is byte-identical to uncached output");
  }

  SeedsRuleSet seeds(nullptr);
  SparseCellGrid::setChunkMemoOverrideForTesting(1);
  testTrue(g, cached.advance(seeds), "memoized ruleset switch advances");
  SparseCellGrid::setChunkMemoOverrideForTesting(-1);
  testTrue(g, reference.advance(seeds), "uncached ruleset switch advances");
  testTrue(g,
           sameSparseRecords(cached.collectChunkRecords(),
                             reference.collectChunkRecords()),
           "ruleset changes invalidate cached transitions exactly");

  SparseCellGrid adaptive;
  seedRepeatedDenseChunks(&adaptive, 128);
  SparseCellGrid::setChunkMemoOverrideForTesting(0);
  testTrue(g, adaptive.advance(life), "adaptive memo probe advances");
  testTrue(g,
           adaptive.getLastAdvanceStats().memoProbeCount >= 16u &&
             adaptive.getLastAdvanceStats().memoHitCount > 0u,
           "adaptive memo samples repeated neighborhoods");
  testTrue(
    g, adaptive.advance(life), "adaptive memo active generation advances");
  testTrue(g,
           adaptive.getLastAdvanceStats().chunkMemoActive &&
             adaptive.getLastAdvanceStats().memoHitCount >
               adaptive.getLastAdvanceStats().memoMissCount,
           "profitable sampling activates full memo probing");

  SparseCellGrid sparseCandidate;
  seedWideBlinkers(&sparseCandidate, 96);
  SparseCellGrid::setCellCandidateOverrideForTesting(1);
  testTrue(
    g, sparseCandidate.advance(life), "candidate-only generation advances");
  testEqSize(g,
             sparseCandidate.getLastAdvanceStats().memoProbeCount,
             0u,
             "candidate-only evaluation bypasses halo memoization");
}

static void
testSparseCachedStatistics()
{
  testSection("SparseCellGrid: transactional cached statistics");
  Rule90RuleSet identity(nullptr);
  SparseCellGrid grid;
  grid.setCell(CellAddress{ 0, 0 }, 0);
  grid.setCell(CellAddress{ 1, 0 }, 3);
  grid.setCell(CellAddress{ 16, 0 }, 0);

  testTrue(g, grid.advance(identity), "initial identity generation advances");
  const SparseAdvanceStats initial = grid.getLastAdvanceStats();
  testEqSize(g, initial.activeChunkCount, 2u, "two source chunks are cached");
  testEqSize(g, initial.activeCellCount, 3u, "stored cells are cached");
  testEqSize(g, initial.countedCellCount, 2u, "counted cells are cached");
  testEqSize(g,
             initial.candidatePreferredChunkCount,
             2u,
             "candidate-preferred chunks are cached");

  testTrue(g, grid.advance(identity), "settled identity generation advances");
  const SparseAdvanceStats settled = grid.getLastAdvanceStats();
  testTrue(g,
           settled.usedChangedFrontier && settled.frontierTargetCount == 0u,
           "settled generation takes the zero-target frontier path");
  testEqSize(g,
             settled.activeCellCount,
             3u,
             "settled generation reports cached stored cells");
  testEqSize(g,
             settled.countedCellCount,
             2u,
             "settled generation reports cached counted cells");

  grid.setCell(CellAddress{ 0, 0 }, 3);
  grid.setCell(CellAddress{ 16, 0 }, SparseCellGrid::BackgroundState);
  testTrue(g, grid.advance(identity), "edited identity generation advances");
  const SparseAdvanceStats edited = grid.getLastAdvanceStats();
  testEqSize(g, edited.activeChunkCount, 1u, "empty chunk leaves the cache");
  testEqSize(g, edited.activeCellCount, 2u, "state edits update stored cells");
  testEqSize(
    g, edited.countedCellCount, 0u, "state edits update counted cells");
  testEqSize(g,
             edited.candidatePreferredChunkCount,
             1u,
             "state edits update candidate preference");

  SparseChunkRecord replacement;
  replacement.chunkX = 0;
  replacement.chunkY = 0;
  replacement.cells.fill(SparseCellGrid::BackgroundState);
  replacement.cells[0] = 0;
  replacement.cells[1] = 2;
  testTrue(g, grid.assignChunk(replacement), "existing chunk is replaced");

  SparseChunkRecord denseCounted;
  denseCounted.chunkX = 3;
  denseCounted.chunkY = 0;
  denseCounted.cells.fill(SparseCellGrid::BackgroundState);
  for (std::size_t index = 0u; index < 48u; ++index) {
    denseCounted.cells[index] = 0;
  }
  testTrue(
    g, grid.assignChunk(denseCounted), "dense counted chunk is assigned");
  testTrue(g, grid.advance(identity), "assigned chunks advance");
  const SparseAdvanceStats assigned = grid.getLastAdvanceStats();
  testEqSize(
    g, assigned.activeCellCount, 50u, "assignment updates stored cache");
  testEqSize(
    g, assigned.countedCellCount, 49u, "assignment updates counted cache");
  testEqSize(g,
             assigned.candidatePreferredChunkCount,
             1u,
             "threshold-equal chunk is not candidate-preferred");

  grid.setCell(CellAddress{ 48, 0 }, 2);
  testTrue(g, grid.advance(identity), "threshold crossing advances");
  testEqSize(g,
             grid.getLastAdvanceStats().candidatePreferredChunkCount,
             2u,
             "counted-cell edit updates cached candidate preference");

  SparseCellGrid other;
  other.setCell(CellAddress{ -32, 0 }, 0);
  testTrue(g, other.advance(identity), "swap peer is synchronized");
  grid.swap(other);
  testTrue(g, grid.advance(identity), "swapped small grid advances");
  testEqSize(g,
             grid.getLastAdvanceStats().activeCellCount,
             1u,
             "swap moves stored cache");
  testEqSize(g,
             grid.getLastAdvanceStats().countedCellCount,
             1u,
             "swap moves counted cache");
  testTrue(g, other.advance(identity), "swapped large grid advances");
  testEqSize(g,
             other.getLastAdvanceStats().activeCellCount,
             50u,
             "peer receives stored cache");
  testEqSize(g,
             other.getLastAdvanceStats().countedCellCount,
             48u,
             "peer receives counted cache");
  other.clear();
  testTrue(g, other.advance(identity), "cleared grid advances");
  testEqSize(g,
             other.getLastAdvanceStats().activeCellCount,
             0u,
             "clear resets stored cache");
  testEqSize(g,
             other.getLastAdvanceStats().countedCellCount,
             0u,
             "clear resets counted cache");

  GameOfLifeRuleSet life(nullptr);
  SparseCellGrid complete;
  complete.setCell(CellAddress{ 0, 0 }, 0);
  SparseCellGrid::setCellCandidateOverrideForTesting(-1);
  testTrue(g, complete.advance(life), "complete generation removes lone cell");
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  testTrue(g, complete.advance(life), "empty complete result advances");
  testEqSize(g,
             complete.getLastAdvanceStats().activeCellCount,
             0u,
             "complete-map swap publishes cached empty totals");

  SparseCellGrid frontier;
  frontier.setCell(CellAddress{ 3, 3 }, 0);
  frontier.setCell(CellAddress{ 4, 3 }, 0);
  frontier.setCell(CellAddress{ 3, 4 }, 0);
  frontier.setCell(CellAddress{ 4, 4 }, 0);
  testTrue(g, frontier.advance(life), "frontier still life synchronizes");
  frontier.setCell(CellAddress{ 64, 0 }, 0);
  testTrue(g, frontier.advance(life), "frontier removes edited lone cell");
  testTrue(g, frontier.advance(life), "frontier result settles");
  testEqSize(g,
             frontier.getLastAdvanceStats().activeCellCount,
             4u,
             "frontier-map swap publishes cached stored totals");
  testEqSize(g,
             frontier.getLastAdvanceStats().countedCellCount,
             4u,
             "frontier-map swap publishes cached counted totals");
}

static void
testSparseCandidateScratchReuse()
{
  testSection("SparseCellGrid: retained candidate scratch storage");
  const int colonyCount = 64;
  GameOfLifeRuleSet rules(nullptr);
  SparseCellGrid grid;
  seedWideBlinkers(&grid, colonyCount);

  SparseCellGrid::setWorkerOverrideForTesting(1);
  SparseCellGrid::setCellCandidateOverrideForTesting(1);
  testTrue(g, grid.advance(rules), "first candidate generation advances");
  const SparseAdvanceStats firstStats = grid.getLastAdvanceStats();
  const std::size_t firstIndexCapacity =
    grid.getCandidateIndexCapacityForTesting();
  const std::size_t firstScratchCapacity =
    grid.getCandidateScratchCapacityForTesting();
  testEqSize(g,
             firstStats.targetChunkCount,
             static_cast<std::size_t>(colonyCount) * 4u,
             "wide blinkers use exact candidate chunks");
  testEqSize(g,
             firstStats.candidateCellCount,
             static_cast<std::size_t>(colonyCount) * 15u,
             "wide blinkers use exact candidate cells");
  testTrue(g,
           firstIndexCapacity >= firstStats.targetChunkCount,
           "candidate flat index owns retained capacity");
  testTrue(g,
           firstStats.targetChunkCount <=
             firstIndexCapacity - firstIndexCapacity / 4u,
           "candidate flat index preserves its maximum load boundary");
  testTrue(g,
           firstStats.candidateIndexGrowthCount > 0u,
           "first candidate generation grows the flat index on insertion");
  testTrue(g,
           firstStats.candidateEnrollmentAttemptCount >
             firstStats.targetChunkCount,
           "duplicate target enrollments are tracked separately");
  testEqSize(g,
             firstStats.producedChunkCount,
             grid.getAllocatedChunkCount(),
             "candidate output count matches the authoritative chunk map");
  testTrue(g,
           firstScratchCapacity >= firstStats.targetChunkCount,
           "candidate scratch vector owns retained capacity");

  testTrue(g, grid.advance(rules), "second candidate generation advances");
  testEqSize(g,
             grid.getCandidateIndexCapacityForTesting(),
             firstIndexCapacity,
             "candidate flat-index capacity is reused");
  testEqSize(g,
             grid.getCandidateScratchCapacityForTesting(),
             firstScratchCapacity,
             "candidate scratch capacity is reused");
  testEqSize(g,
             grid.getLastAdvanceStats().candidateCellCount,
             static_cast<std::size_t>(colonyCount) * 15u,
             "reused scratch is reset before the next generation");
  testEqSize(g,
             grid.getLastAdvanceStats().candidateIndexGrowthCount,
             0u,
             "duplicate enrollment does not regrow a retained candidate index");
  testEqSize(g,
             grid.getLastAdvanceStats().producedChunkCount,
             grid.getAllocatedChunkCount(),
             "reused candidate output count remains exact");
}

static void
testSparseCandidateFlatIndex()
{
  testSection("SparseCellGrid: flat candidate index generations");
  GameOfLifeRuleSet rules(nullptr);
  SparseCellGrid candidates;
  SparseCellGrid fullChunks;
  seedWideBlinkers(&candidates, 128);

  SparseCellGrid::setWorkerOverrideForTesting(1);
  SparseCellGrid::setCellCandidateOverrideForTesting(1);
  testTrue(g, candidates.advance(rules), "large candidate generation advances");
  const std::size_t retainedCapacity =
    candidates.getCandidateIndexCapacityForTesting();

  candidates.clear();
  const std::int64_t x = -1024;
  for (std::int64_t y = -1; y <= 1; ++y) {
    candidates.setCell(CellAddress{ x, y }, 0);
    fullChunks.setCell(CellAddress{ x, y }, 0);
  }
  testTrue(g,
           candidates.advance(rules),
           "new negative-coordinate index generation advances");
  testEqSize(g,
             candidates.getLastAdvanceStats().targetChunkCount,
             4u,
             "old generation slots do not remain active");
  testEqSize(g,
             candidates.getLastAdvanceStats().candidateCellCount,
             15u,
             "flat index deduplicates negative-coordinate candidates");
  testEqSize(g,
             candidates.getCandidateIndexCapacityForTesting(),
             retainedCapacity,
             "flat index retains its high-water capacity");

  SparseCellGrid::setCellCandidateOverrideForTesting(-1);
  testTrue(g, fullChunks.advance(rules), "full-chunk reference advances");
  testTrue(g,
           sameSparseRecords(candidates.collectChunkRecords(),
                             fullChunks.collectChunkRecords()),
           "flat-index generation matches full chunks");
}

static void
testSparseCompleteHaloScratchReuse()
{
  testSection("SparseCellGrid: retained complete-halo scratch storage");
  WireworldRuleSet rules(nullptr);
  SparseCellGrid grid;
  bool assigned = true;
  for (int chunkY = 0; chunkY < 9; ++chunkY) {
    for (int chunkX = 0; chunkX < 9; ++chunkX) {
      SparseChunkRecord record;
      record.chunkX = chunkX;
      record.chunkY = chunkY;
      record.cells.fill(WireworldRuleSet::CELL_CONDUCTOR);
      assigned = grid.assignChunk(record) && assigned;
    }
  }
  testTrue(g, assigned, "complete-halo source chunks assign");

  SparseCellGrid::setWorkerOverrideForTesting(1);
  SparseCellGrid::setCellCandidateOverrideForTesting(-1);
  testTrue(g, grid.advance(rules), "first complete-halo generation advances");
  testEqSize(g,
             grid.getLastAdvanceStats().targetChunkCount,
             9u * 9u,
             "non-counting complete targets do not expand to neighbors");
  const std::size_t targetCapacity = grid.getCompleteTargetCapacityForTesting();
  const std::size_t indexCapacity =
    grid.getCompleteTargetIndexCapacityForTesting();
  const std::size_t resultCapacity = grid.getCompleteResultCapacityForTesting();
  testTrue(g,
           targetCapacity >= grid.getLastAdvanceStats().targetChunkCount,
           "complete target vector owns retained capacity");
  testTrue(g,
           indexCapacity >= grid.getLastAdvanceStats().targetChunkCount,
           "complete target index owns retained capacity");
  testTrue(g,
           resultCapacity >= grid.getLastAdvanceStats().targetChunkCount,
           "complete result vector owns retained capacity");

  testTrue(g, grid.advance(rules), "second complete-halo generation advances");
  testEqSize(g,
             grid.getCompleteTargetCapacityForTesting(),
             targetCapacity,
             "complete target capacity is reused");
  testEqSize(g,
             grid.getCompleteTargetIndexCapacityForTesting(),
             indexCapacity,
             "complete target-index capacity is reused");
  testEqSize(g,
             grid.getCompleteResultCapacityForTesting(),
             resultCapacity,
             "complete result capacity is reused");

  grid.clear();
  SparseChunkRecord replacement;
  replacement.chunkX = -5;
  replacement.chunkY = -5;
  replacement.cells.fill(WireworldRuleSet::CELL_CONDUCTOR);
  testTrue(g, grid.assignChunk(replacement), "replacement halo chunk assigns");
  testTrue(g, grid.advance(rules), "new complete-target generation advances");
  testEqSize(g,
             grid.getLastAdvanceStats().targetChunkCount,
             1u,
             "old complete-target generation does not remain active");
  testEqSize(g,
             grid.getCompleteTargetIndexCapacityForTesting(),
             indexCapacity,
             "smaller complete world retains its target-index high-water");
}

static void
testSparseChunkNodeReuse()
{
  testSection("SparseCellGrid: retained generation chunk nodes");
  GameOfLifeRuleSet rules(nullptr);
  SparseCellGrid candidates;
  candidates.setCell(CellAddress{ 4, 3 }, 0);
  candidates.setCell(CellAddress{ 4, 4 }, 0);
  candidates.setCell(CellAddress{ 4, 5 }, 0);

  SparseCellGrid::setWorkerOverrideForTesting(1);
  SparseCellGrid::setCellCandidateOverrideForTesting(1);
  testTrue(g, candidates.advance(rules), "first candidate step advances");
  testEqSize(g,
             candidates.getLastAdvanceStats().allocatedChunkNodeCount,
             1u,
             "first candidate output allocates one chunk node");
  testTrue(g, candidates.advance(rules), "second candidate step advances");
  testEqSize(g,
             candidates.getLastAdvanceStats().allocatedChunkNodeCount,
             0u,
             "steady candidate output allocates no chunk nodes");
  testEqSize(g,
             candidates.getLastAdvanceStats().reusedChunkNodeCount,
             1u,
             "steady candidate output reuses an inactive chunk node");
  testEqSize(g,
             candidates.getLastAdvanceStats().retainedChunkNodeCount,
             1u,
             "candidate path retains the inactive generation node");

  SparseCellGrid stable;
  stable.setCell(CellAddress{ -2, -2 }, 0);
  stable.setCell(CellAddress{ -1, -2 }, 0);
  stable.setCell(CellAddress{ -2, -1 }, 0);
  stable.setCell(CellAddress{ -1, -1 }, 0);
  const std::uint64_t stableRevision = stable.getRevision();
  testTrue(g, stable.advance(rules), "unchanged candidate step advances");
  testEqSize(g,
             static_cast<std::size_t>(stable.getRevision()),
             static_cast<std::size_t>(stableRevision),
             "unchanged generation keeps its revision");
  testTrue(g, stable.advance(rules), "reused unchanged step advances");
  testEqSize(g,
             stable.getLastAdvanceStats().allocatedChunkNodeCount,
             0u,
             "unchanged steady generation allocates no chunk nodes");
  testEqSize(g,
             stable.getLastAdvanceStats().reusedChunkNodeCount,
             1u,
             "unchanged steady generation reuses its retained node");

  SparseCellGrid dense;
  dense.setCell(CellAddress{ 7, 7 }, 0);
  dense.setCell(CellAddress{ 8, 7 }, 0);
  dense.setCell(CellAddress{ 7, 8 }, 0);
  dense.setCell(CellAddress{ 8, 8 }, 0);
  SparseCellGrid::setCellCandidateOverrideForTesting(-1);
  testTrue(g, dense.advance(rules), "first full-chunk step advances");
  testTrue(g, dense.advance(rules), "second full-chunk step advances");
  testEqSize(g,
             dense.getLastAdvanceStats().allocatedChunkNodeCount,
             0u,
             "steady full-chunk output allocates no chunk nodes");
  testEqSize(g,
             dense.getLastAdvanceStats().reusedChunkNodeCount,
             1u,
             "full-chunk output uses the shared node recycler");
}

static void
seedSparseRulePattern(SparseCellGrid* grid)
{
  if (grid == nullptr) {
    return;
  }
  grid->setCell(CellAddress{ -32, -1 }, 0);
  grid->setCell(CellAddress{ -32, 0 }, 0);
  grid->setCell(CellAddress{ -32, 1 }, 0);
  grid->setCell(CellAddress{ 0, 0 }, 0);
}

static void
testSparseCellCandidateRuleEquivalence()
{
  testSection("SparseCellGrid: candidate rule equivalence");
  GameOfLifeRuleSet life(nullptr);
  SeedsRuleSet seeds(nullptr);
  BrainsBrainRuleSet brains(nullptr);
  HighlifeRuleSet highlife(nullptr);
  DayAndNightRuleSet dayAndNight(nullptr);
  LifeWithoutDeathRuleSet lifeWithoutDeath(nullptr);
  WireworldRuleSet wireworld(nullptr);
  RuleSet* ruleSets[] = { &life,     &seeds,       &brains,
                          &highlife, &dayAndNight, &lifeWithoutDeath,
                          &wireworld };

  for (RuleSet* rules : ruleSets) {
    SparseCellGrid candidates;
    SparseCellGrid fullChunks;
    seedSparseRulePattern(&candidates);
    seedSparseRulePattern(&fullChunks);
    if (rules == &brains) {
      candidates.setCell(CellAddress{ 1, 0 }, 2);
      fullChunks.setCell(CellAddress{ 1, 0 }, 2);
    }
    if (rules == &wireworld) {
      candidates.setCell(CellAddress{ 1, 0 }, WireworldRuleSet::CELL_TAIL);
      candidates.setCell(CellAddress{ 2, 0 }, WireworldRuleSet::CELL_CONDUCTOR);
      fullChunks.setCell(CellAddress{ 1, 0 }, WireworldRuleSet::CELL_TAIL);
      fullChunks.setCell(CellAddress{ 2, 0 }, WireworldRuleSet::CELL_CONDUCTOR);
    }

    SparseCellGrid::setCellCandidateOverrideForTesting(1);
    for (int generation = 0; generation < 4; ++generation) {
      testTrue(
        g, candidates.advance(*rules), "candidate rules generation advances");
    }
    SparseCellGrid::setCellCandidateOverrideForTesting(-1);
    for (int generation = 0; generation < 4; ++generation) {
      testTrue(g, fullChunks.advance(*rules), "full rules generation advances");
    }
    testTrue(g,
             sameSparseRecords(candidates.collectChunkRecords(),
                               fullChunks.collectChunkRecords()),
             "candidate rules match full chunks");
  }
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
}

static void
testMultiStateTransitions()
{
  testSection("SparseCellGrid: multi-state transitions");
  WireworldRuleSet rules(nullptr);
  SparseCellGrid grid;
  grid.setCell(CellAddress{ 0, 0 }, WireworldRuleSet::CELL_HEAD);
  grid.setCell(CellAddress{ 1, 0 }, WireworldRuleSet::CELL_TAIL);
  grid.setCell(CellAddress{ 2, 0 }, WireworldRuleSet::CELL_CONDUCTOR);
  grid.advance(rules);
  testEqUChar(g,
              grid.getCell(CellAddress{ 0, 0 }),
              WireworldRuleSet::CELL_TAIL,
              "head becomes tail");
  testEqUChar(g,
              grid.getCell(CellAddress{ 1, 0 }),
              WireworldRuleSet::CELL_CONDUCTOR,
              "tail becomes conductor");
  testEqUChar(g,
              grid.getCell(CellAddress{ 2, 0 }),
              WireworldRuleSet::CELL_CONDUCTOR,
              "conductor preserves state");

  BrainsBrainRuleSet brainRules(nullptr);
  SparseCellGrid brain;
  brain.setCell(CellAddress{ 0, 0 }, 0);
  brain.setCell(CellAddress{ 1, 0 }, 2);
  brain.advance(brainRules);
  testEqUChar(
    g, brain.getCell(CellAddress{ 0, 0 }), 2, "brain alive becomes dying");
  testEqUChar(
    g, brain.getCell(CellAddress{ 1, 0 }), 1, "brain dying becomes empty");
}

static void
testBoundedCanvasView()
{
  testSection("CanvasView: visible region, snapping, and fade");
  NullRenderWindow window(64, 64);
  EnvVars env;
  env.setVar("WinX", 64);
  env.setVar("WinY", 64);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  SparseCellGrid grid;
  CanvasView view(4, 4, &grid, &window, &camera, nullptr);
  view.rebuildDefaultPalette();
  grid.setCell(CellAddress{ 0, 0 }, 0);
  view.rebuildTargetsFromGrid();
  view.snapVisualToTargets();
  testTrue(g,
           view.getVisibleCell(3, 2) == CellAddress{ 0, 0 },
           "view samples the centered origin");
  const unsigned char* pixels = view.getDisplayTexBuffer();
  const CellAddress cacheFirst = view.getCacheFirstCell();
  const int originX =
    static_cast<int>((0 - cacheFirst.x) / view.getCellsPerTexel());
  const int originY =
    static_cast<int>((cacheFirst.y - 0) / view.getCellsPerTexel());
  testEqUChar(g,
              pixels[(originY * view.getTextureWidth() + originX) * 3],
              0,
              "visible alive cell is staged black");

  grid.setCell(CellAddress{ 0, 0 }, SparseCellGrid::BackgroundState);
  view.rebuildTargetsFromGrid();
  testTrue(g, view.isFadeActive(), "target changes start visible-area fade");
  view.setFadeSpeed(0.0f);
  testTrue(g, !view.isFadeActive(), "zero fade speed snaps immediately");
  grid.setCell(CellAddress{ 2, 0 }, 0);
  camera.SetPosition(glm::vec2(32.0f, 0.0f));
  view.syncVisibleRegion();
  testTrue(g,
           view.getVisibleCell(3, 2) == CellAddress{ 2, 0 },
           "camera movement reveals a new world region");
  testTrue(
    g, !view.isFadeActive(), "newly revealed cells snap instead of fading");
}

static void
testAdaptiveOverviewAndRevisionGate()
{
  testSection("CanvasView: adaptive overview and revision-gated uploads");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 0.1f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  SparseCellGrid grid;
  grid.setCell(CellAddress{ 0, 0 }, 0);
  CanvasView view(80, 60, &grid, &window, &camera, &renderer);
  view.rebuildDefaultPalette();
  view.rebuildTargetsFromGrid();

  testEqInt(g, view.getVisibleCellWidth(), 802, "far view source width");
  testEqInt(g, view.getVisibleCellHeight(), 452, "far view source height");
  testEqInt(
    g, view.getCellsPerTexel(), 3, "far view selects stable density LOD");
  testEqInt(
    g, view.getViewWidth(), 268, "far view uses bounded overview width");
  testEqInt(
    g, view.getViewHeight(), 151, "far view uses bounded overview height");
  testEqInt(
    g, view.getTextureWidth(), 320, "overview texture width is bounded");
  testTrue(g,
           view.getTextureHeight() >= view.getCachedTexelHeight(),
           "overview texture contains the padded cache");

  const CellAddress firstCell = view.getCacheFirstCell();
  const int outputX =
    static_cast<int>((0 - firstCell.x) / view.getCellsPerTexel());
  const int outputY =
    static_cast<int>((firstCell.y - 0) / view.getCellsPerTexel());
  const unsigned char* pixels = view.getDisplayTexBuffer();
  const int pixelIndex = (outputY * view.getTextureWidth() + outputX) * 3;
  testTrue(g,
           pixels[pixelIndex] > 0 && pixels[pixelIndex] < 255,
           "overview pixel contains density-weighted live color");

  view.AppendCommands(&renderer);
  mock.SubmitCommandQueue();
  mock.ClearCommandQueue();
  view.rebuildTargetsFromGrid();
  view.AppendCommands(&renderer);
  mock.SubmitCommandQueue();
  bool unchangedFrameUpdatedTexture = false;
  for (std::size_t i = 0; i < mock.getLastSubmittedCount(); ++i) {
    unchangedFrameUpdatedTexture =
      unchangedFrameUpdatedTexture ||
      mock.getLastSubmitted(i).commandType == CommandType::UpdateTexture;
  }
  testTrue(g,
           !unchangedFrameUpdatedTexture,
           "unchanged view does not upload a texture");

  grid.setCell(CellAddress{ 1, 0 }, 0);
  view.rebuildTargetsFromGrid();
  testTrue(g,
           view.getLastSampledTexelCount() > 0u &&
             view.getLastSampledTexelCount() <
               static_cast<std::size_t>(view.getCachedTexelWidth()) *
                 static_cast<std::size_t>(view.getCachedTexelHeight()),
           "overview revision recomputes only affected density bins");
  view.setFadeSpeed(0.0f);
  mock.ClearCommandQueue();
  view.AppendCommands(&renderer);
  mock.SubmitCommandQueue();
  bool foundOverviewUpload = false;
  bool usesTextureStride = false;
  for (std::size_t i = 0; i < mock.getLastSubmittedCount(); ++i) {
    const RenderCommand& command = mock.getLastSubmitted(i);
    if (command.commandType == CommandType::UpdateTexture) {
      foundOverviewUpload = true;
      usesTextureStride =
        command.updateTexture.srcRowStride == view.getTextureWidth();
    }
  }
  testTrue(g, foundOverviewUpload, "changed overview emits one texture update");
  testTrue(
    g, usesTextureStride, "overview update keeps allocated texture stride");

  camera.SetZoom(1.0f);
  view.rebuildTargetsFromGrid();
  testEqInt(g, view.getViewWidth(), 82, "near view restores exact cell texels");
  testEqInt(g, view.getViewHeight(), 47, "near view restores exact cell rows");
  testEqInt(g, view.getCellsPerTexel(), 1, "near view restores exact-cell LOD");
  testEqInt(g,
            view.getTextureWidth(),
            320,
            "near view retains allocation without expanding active work");
  testEqInt(g,
            view.getTextureHeight(),
            192,
            "near view retains allocation without expanding active rows");
}

static std::size_t
countTextureCreates(const MockBackend& mock)
{
  std::size_t count = 0u;
  for (std::size_t i = 0; i < mock.getCreateCount(); ++i) {
    if (mock.getCreate(i).kind ==
        MockBackend::CreateRecord::Kind::TextureData) {
      count += 1u;
    }
  }
  return count;
}

static void
testTextureCapacityAndLifecycle()
{
  testSection("CanvasView: texture capacity and lifecycle");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  SparseCellGrid grid;
  std::uint32_t textureSlot = 0u;
  std::uint32_t textureGeneration = 0u;
  std::size_t replacementCount = 0u;

  {
    CanvasView view(80, 60, &grid, &window, &camera, &renderer);
    const std::size_t initialTextureCreates = countTextureCreates(mock);
    testEqSize(
      g, initialTextureCreates, 1u, "view enrolls one initial display texture");

    view.rebuildTargetsFromGrid();
    const std::size_t grownTextureCreates = countTextureCreates(mock);
    testEqSize(g,
               grownTextureCreates,
               initialTextureCreates,
               "capacity growth retains one typed display texture handle");
    testEqInt(g,
              view.getTextureWidth(),
              160,
              "padded cache grows texture capacity once");

    camera.SetZoom(0.95f);
    view.syncVisibleRegion();
    camera.SetZoom(0.90f);
    view.syncVisibleRegion();
    testEqSize(g,
               countTextureCreates(mock),
               grownTextureCreates,
               "nearby smooth-zoom sizes reuse retained texture capacity");

    for (std::size_t i = 0; i < mock.getCreateCount(); ++i) {
      const MockBackend::CreateRecord& record = mock.getCreate(i);
      if (record.kind == MockBackend::CreateRecord::Kind::TextureData) {
        textureSlot = record.slot;
        textureGeneration = record.generation;
      } else if (record.kind ==
                 MockBackend::CreateRecord::Kind::ReplaceTexture) {
        replacementCount += 1u;
        testTrue(g,
                 record.slot == textureSlot &&
                   record.generation == textureGeneration,
                 "capacity replacement preserves the typed texture handle");
      }
    }
    testEqSize(g,
               replacementCount,
               1u,
               "first capacity growth replaces the display texture once");
  }

  std::size_t destroyedTextureCount = 0u;
  for (std::size_t i = 0; i < mock.getCreateCount(); ++i) {
    const MockBackend::CreateRecord& record = mock.getCreate(i);
    if (record.kind != MockBackend::CreateRecord::Kind::DestroyTexture) {
      continue;
    }
    destroyedTextureCount += 1u;
    testTrue(g,
             record.slot == textureSlot &&
               record.generation == textureGeneration,
             "view releases the enrolled typed texture handle");
  }
  testEqSize(g,
             destroyedTextureCount,
             1u,
             "view destruction releases its backend texture");
}

static void
testIncrementalPresentationWork()
{
  testSection("CanvasView: changed-tile sampling and active fades");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  SparseCellGrid grid;
  grid.setCell(CellAddress{ 0, 0 }, 0);
  CanvasView view(80, 60, &grid, &window, &camera, nullptr);
  view.rebuildDefaultPalette();
  view.rebuildTargetsFromGrid();
  const std::size_t fullViewTexels =
    static_cast<std::size_t>(view.getCachedTexelWidth()) *
    static_cast<std::size_t>(view.getCachedTexelHeight());
  testEqSize(g,
             view.getLastSampledTexelCount(),
             fullViewTexels,
             "initial presentation samples the complete view");

  grid.setCell(CellAddress{ 0, 0 }, SparseCellGrid::BackgroundState);
  view.rebuildTargetsFromGrid();
  testEqSize(g,
             view.getLastSampledTexelCount(),
             SparseCellGrid::kChunkCellCount,
             "one changed chunk resamples one visible tile");
  testEqSize(g,
             view.getFadingTexelCount(),
             1u,
             "one changed cell enrolls one fading texel");
  view.tickVisual(0.01f);
  testEqSize(g,
             view.getLastFadeVisitCount(),
             1u,
             "fade tick visits only the active texel");

  view.setFadeSpeed(0.0f);
  testEqSize(g,
             view.getLastSnapVisitCountForTesting(),
             1u,
             "zero fade speed snaps only the active texel");
  testEqSize(
    g, view.getFadingTexelCount(), 0u, "snap clears the active fade set");
  view.setFadeSpeed(0.0f);
  testEqSize(g,
             view.getLastSnapVisitCountForTesting(),
             1u,
             "repeated zero fade configuration performs no snap scan");

  grid.setCell(CellAddress{ 0, 0 }, 0);
  grid.setCell(CellAddress{ 20, 0 }, 0);
  view.rebuildTargetsFromGrid();
  testEqSize(g,
             view.getLastSampledTexelCount(),
             fullViewTexels,
             "multiple unseen revisions fall back to a complete resample");

  grid.setCell(CellAddress{ 20, 0 }, SparseCellGrid::BackgroundState);
  view.rebuildTargetsFromGrid();
  testEqSize(g,
             view.getLastSampledTexelCount(),
             SparseCellGrid::kChunkCellCount,
             "single-cell removal recomputes its cached chunk");

  GameOfLifeRuleSet rules(nullptr);
  testTrue(g, grid.advance(rules), "presentation source generation advances");
  view.rebuildTargetsFromGrid();
  testEqSize(g,
             view.getLastSampledTexelCount(),
             SparseCellGrid::kChunkCellCount,
             "one simulation revision publishes its changed tile");

  SparseCellGrid replacement;
  replacement.setCell(CellAddress{ 1, 1 }, 0);
  grid.swap(replacement);
  view.rebuildTargetsFromGrid();
  testEqSize(g,
             view.getLastSampledTexelCount(),
             fullViewTexels,
             "whole-grid replacement invalidates incremental sampling");
}

static void
testCanvasViewUsesWorldCellQuad()
{
  testSection("CanvasView: world-space cell-aligned presentation");
  NullRenderWindow window(64, 64);
  EnvVars env;
  env.setVar("WinX", 64);
  env.setVar("WinY", 64);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  SparseCellGrid grid;
  CanvasView view(4, 4, &grid, &window, &camera, &renderer);
  view.rebuildTargetsFromGrid();

  bool foundCanvasTexture = false;
  bool usesNearest = true;
  for (std::size_t i = 0; i < mock.getCreateCount(); ++i) {
    const MockBackend::CreateRecord& record = mock.getCreate(i);
    if (record.kind == MockBackend::CreateRecord::Kind::TextureData) {
      foundCanvasTexture = true;
      usesNearest = usesNearest && record.filter == TextureFilter::Nearest;
    }
  }
  testTrue(g, foundCanvasTexture, "CanvasView enrolls a display texture");
  testTrue(g, usesNearest, "CanvasView keeps cell texture edges sharp");
  testTrue(g,
           view.getVisual().getSpace() == PrimitiveSpace::World,
           "CanvasView uses the camera world space");
  SpritePrimitive* sprite = view.getVisual().getSprite(0);
  testTrue(g, sprite != nullptr, "CanvasView owns one display sprite");
  if (sprite != nullptr) {
    const CellAddress cacheFirst = view.getCacheFirstCell();
    const float expectedX = static_cast<float>(cacheFirst.x) * 16.0f - 8.0f;
    const float expectedTop = static_cast<float>(cacheFirst.y) * 16.0f + 8.0f;
    const float expectedHeight =
      static_cast<float>(view.getCacheCellHeight()) * 16.0f;
    testTrue(g,
             sprite->rect.x == expectedX &&
               sprite->rect.y == expectedTop - expectedHeight &&
               sprite->rect.w ==
                 static_cast<float>(view.getCacheCellWidth()) * 16.0f &&
               sprite->rect.h == expectedHeight,
             "display sprite follows padded cache cell boundaries");
    testTrue(g,
             sprite->region.v0 == 1.0f && sprite->region.v1 == 0.0f,
             "display sprite keeps world-up rows upright");
  }
}

static void
testCameraCacheReuseAndLodHysteresis()
{
  testSection("CanvasView: exact padded cache and stable overview LOD");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  SparseCellGrid grid;
  grid.setCell(CellAddress{ 0, 0 }, 0);
  CanvasView view(80, 60, &grid, &window, &camera, nullptr);
  view.rebuildTargetsFromGrid();
  const std::size_t initialRefills = view.getCacheRefillCount();

  camera.SetPositionPrecise(16.0 * 16.0, 0.0);
  view.syncVisibleRegion();
  testEqSize(g,
             view.getCacheRefillCount(),
             initialRefills,
             "one-chunk pan reuses the padded exact cache");

  camera.SetZoom(0.95f);
  view.syncVisibleRegion();
  testEqSize(g,
             view.getCacheRefillCount(),
             initialRefills,
             "near smooth zoom reuses the same exact-cell cache");

  camera.SetZoom(0.1f);
  view.syncVisibleRegion();
  testEqInt(g, view.getCellsPerTexel(), 3, "far zoom selects LOD 3");
  camera.SetZoom(0.13f);
  view.syncVisibleRegion();
  testEqInt(g,
            view.getCellsPerTexel(),
            3,
            "zoom-in hysteresis retains LOD near its boundary");
  camera.SetZoom(0.16f);
  view.syncVisibleRegion();
  testEqInt(g, view.getCellsPerTexel(), 2, "LOD refines after 80 percent fit");
}

static void
testBoundedDirtyUploadRectangles()
{
  testSection("CanvasView: bounded dirty upload rectangles");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  SparseCellGrid grid;
  CanvasView view(80, 60, &grid, &window, &camera, &renderer);
  view.setFadeSpeed(0.0f);
  view.rebuildTargetsFromGrid();
  view.AppendCommands(&renderer);
  mock.SubmitCommandQueue();
  mock.ClearCommandQueue();

  grid.setCell(CellAddress{ -25, 15 }, 0);
  grid.setCell(CellAddress{ 25, -15 }, 0);
  view.rebuildTargetsFromGrid();
  view.AppendCommands(&renderer);
  testEqSize(g,
             view.getLastUploadRectCount(),
             2u,
             "two distant dirty tiles avoid uploading their empty gap");
  testTrue(g,
           view.getLastUploadByteCount() < 51u * 31u * 3u,
           "split rectangles upload fewer bytes than the enclosing area");

  mock.ClearCommandQueue();
  for (int index = 0; index < 9; ++index) {
    const int x = -36 + (index % 3) * 36;
    const int y = 24 - (index / 3) * 24;
    grid.setCell(CellAddress{ x, y }, 0);
  }
  view.rebuildTargetsFromGrid();
  view.AppendCommands(&renderer);
  testEqSize(g,
             view.getLastUploadRectCount(),
             1u,
             "more than eight separated regions fall back to one bound");
}

static void
testAsyncSimulationPublication()
{
  testSection("Sparse simulation: dual-grid publication and delta mirroring");
  SparseCellGrid published;
  seedStableBlocksAndBlinker(&published, 96);
  SparseCellGrid expected;
  expected.copyStateFrom(published);
  GameOfLifeRuleSet rules(nullptr);
  expected.advance(rules);

  SparseCellGrid working;
  SimulationRunner runner;
  SparseGenerationDelta emptyDelta;
  testTrue(
    g,
    runner.start(&working, &published, &rules, std::move(emptyDelta), false),
    "runner accepts one generation");
  SparseCellGrid* completedGrid = nullptr;
  SparseGenerationDelta completedDelta;
  double elapsedMilliseconds = 0.0;
  bool advanceSucceeded = false;
  SimulationRunnerTimings firstTimings;
  testTrue(g,
           runner.waitAndTakeCompleted(&completedGrid,
                                       &completedDelta,
                                       &elapsedMilliseconds,
                                       &advanceSucceeded,
                                       &firstTimings),
           "runner publishes its completed generation");
  testTrue(g, advanceSucceeded, "background generation succeeds");
  testTrue(g,
           completedGrid == &working &&
             sameSparseRecords(working.collectChunkRecords(),
                               expected.collectChunkRecords()),
           "published background generation matches synchronous output");
  testTrue(g,
           published.getCell(CellAddress{ 4, 64 }) == 0 &&
             published.getCell(CellAddress{ 0, 0 }) == 0,
           "published input remains immutable while worker advances");

  SparseCellGrid mirror;
  mirror.copyStateFrom(published);
  testTrue(g,
           mirror.applyGenerationDelta(completedDelta),
           "generation delta updates the former published grid");
  testTrue(g,
           sameSparseRecords(mirror.collectChunkRecords(),
                             working.collectChunkRecords()),
           "delta mirror becomes the next equivalent worker input");
  testTrue(g, elapsedMilliseconds >= 0.0, "runner reports generation timing");
  testTrue(g,
           firstTimings.usedFullCopy && !firstTimings.usedMirrorDelta,
           "first generation reports its full-copy synchronization");

  expected.advance(rules);
  testTrue(
    g,
    runner.start(&published, &working, &rules, std::move(completedDelta), true),
    "former published grid accepts the next mirrored generation");
  SparseGenerationDelta secondDelta;
  SimulationRunnerTimings secondTimings;
  testTrue(g,
           runner.waitAndTakeCompleted(&completedGrid,
                                       &secondDelta,
                                       &elapsedMilliseconds,
                                       &advanceSucceeded,
                                       &secondTimings),
           "second generation publishes in order");
  testTrue(g,
           advanceSucceeded && completedGrid == &published &&
             sameSparseRecords(published.collectChunkRecords(),
                               expected.collectChunkRecords()),
           "alternating worker grids preserve unchanged sparse chunks and "
           "ordered deterministic output");
  testTrue(g,
           secondTimings.usedMirrorDelta && !secondTimings.usedFullCopy &&
             secondTimings.mirrorMilliseconds >= 0.0 &&
             secondTimings.advanceMilliseconds >= 0.0 &&
             secondTimings.captureMilliseconds >= 0.0,
           "subsequent generation reports delta, advance, and capture stages");
  runner.shutdown();

  SparseCellGrid broadExpected;
  SparseCellGrid broadPublished;
  SparseCellGrid broadWorking;
  seedSparseRandom(&broadExpected, 160, 73u);
  seedSparseRandom(&broadPublished, 160, 73u);
  SparseCellGrid* broadPublishedGrid = &broadPublished;
  SparseCellGrid* broadWorkingGrid = &broadWorking;
  SparseGenerationDelta broadDelta;
  bool broadDeltaValid = false;
  SimulationRunner broadRunner;
  bool broadEquivalent = true;
  for (int generation = 0; generation < 8; ++generation) {
    broadExpected.advance(rules);
    SparseGenerationDelta transferDelta;
    if (broadDeltaValid) {
      transferDelta = std::move(broadDelta);
    }
    broadEquivalent = broadRunner.start(broadWorkingGrid,
                                        broadPublishedGrid,
                                        &rules,
                                        std::move(transferDelta),
                                        broadDeltaValid) &&
                      broadEquivalent;
    if (!broadEquivalent) {
      break;
    }
    broadEquivalent =
      broadRunner.waitAndTakeCompleted(
        &completedGrid, &broadDelta, &elapsedMilliseconds, &advanceSucceeded) &&
      advanceSucceeded && completedGrid == broadWorkingGrid &&
      sameSparseRecords(broadExpected.collectChunkRecords(),
                        broadWorkingGrid->collectChunkRecords()) &&
      broadEquivalent;
    SparseCellGrid* previousPublished = broadPublishedGrid;
    broadPublishedGrid = broadWorkingGrid;
    broadWorkingGrid = previousPublished;
    broadDeltaValid = true;
  }
  broadRunner.shutdown();
  testTrue(g,
           broadEquivalent,
           "overlapping and non-overlapping mirror changes stay equivalent");

  SparseCellGrid replacementExpected;
  SparseCellGrid replacementPublished;
  SparseCellGrid replacementWorking;
  seedWideBlinkers(&replacementExpected, 2200);
  seedWideBlinkers(&replacementPublished, 2200);
  replacementExpected.advance(rules);
  SimulationRunner replacementRunner;
  SparseGenerationDelta replacementDelta;
  testTrue(g,
           replacementRunner.start(&replacementWorking,
                                   &replacementPublished,
                                   &rules,
                                   SparseGenerationDelta{},
                                   false),
           "broad replacement runner accepts its first generation");
  testTrue(g,
           replacementRunner.waitAndTakeCompleted(&completedGrid,
                                                  &replacementDelta,
                                                  &elapsedMilliseconds,
                                                  &advanceSucceeded) &&
             advanceSucceeded && replacementDelta.fullReplacement &&
             replacementDelta.fullChunks.empty(),
           "broad change publishes a lightweight replacement marker");
  replacementExpected.advance(rules);
  testTrue(g,
           replacementRunner.start(&replacementPublished,
                                   &replacementWorking,
                                   &rules,
                                   std::move(replacementDelta),
                                   true),
           "former broad published grid accepts recycled replacement nodes");
  SimulationRunnerTimings replacementTimings;
  testTrue(g,
           replacementRunner.waitAndTakeCompleted(&completedGrid,
                                                  &replacementDelta,
                                                  &elapsedMilliseconds,
                                                  &advanceSucceeded,
                                                  &replacementTimings) &&
             advanceSucceeded && completedGrid == &replacementPublished &&
             replacementTimings.usedDirectSourceAdvance &&
             !replacementTimings.usedMirrorDelta &&
             !replacementTimings.usedFullCopy &&
             sameSparseRecords(replacementExpected.collectChunkRecords(),
                               replacementPublished.collectChunkRecords()),
           "direct broad generation remains byte-identical without mirroring");

  replacementExpected.advance(rules);
  testTrue(g,
           replacementRunner.start(&replacementWorking,
                                   &replacementPublished,
                                   &rules,
                                   std::move(replacementDelta),
                                   true) &&
             replacementRunner.waitAndTakeCompleted(&completedGrid,
                                                    &replacementDelta,
                                                    &elapsedMilliseconds,
                                                    &advanceSucceeded) &&
             advanceSucceeded && completedGrid == &replacementWorking,
           "alternating direct grid prepares its retained topology");
  replacementExpected.advance(rules);
  testTrue(
    g,
    replacementRunner.start(&replacementPublished,
                            &replacementWorking,
                            &rules,
                            std::move(replacementDelta),
                            true) &&
      replacementRunner.waitAndTakeCompleted(&completedGrid,
                                             &replacementDelta,
                                             &elapsedMilliseconds,
                                             &advanceSucceeded) &&
      advanceSucceeded && completedGrid == &replacementPublished &&
      replacementPublished.getLastAdvanceStats().reusedCandidateTopology &&
      sameSparseRecords(replacementExpected.collectChunkRecords(),
                        replacementPublished.collectChunkRecords()),
    "unchanged alternating topology reuses exact candidate targets");

  const CellAddress topologyEdit{ 999999, 999999 };
  replacementExpected.setCell(topologyEdit, 0u);
  replacementPublished.setCell(topologyEdit, 0u);
  replacementExpected.advance(rules);
  testTrue(
    g,
    replacementRunner.start(&replacementWorking,
                            &replacementPublished,
                            &rules,
                            std::move(replacementDelta),
                            true) &&
      replacementRunner.waitAndTakeCompleted(&completedGrid,
                                             &replacementDelta,
                                             &elapsedMilliseconds,
                                             &advanceSucceeded) &&
      advanceSucceeded && completedGrid == &replacementWorking &&
      !replacementWorking.getLastAdvanceStats().reusedCandidateTopology &&
      sameSparseRecords(replacementExpected.collectChunkRecords(),
                        replacementWorking.collectChunkRecords()),
    "topology edits invalidate retained candidate targets exactly");
  replacementRunner.shutdown();

  SparseCellGrid shutdownPublished;
  seedStableBlocksAndBlinker(&shutdownPublished, 512);
  SparseCellGrid shutdownWorking;
  SimulationRunner shutdownRunner;
  SparseGenerationDelta shutdownDelta;
  testTrue(g,
           shutdownRunner.start(&shutdownWorking,
                                &shutdownPublished,
                                &rules,
                                std::move(shutdownDelta),
                                false),
           "runner accepts work immediately before shutdown");
  shutdownRunner.shutdown();
  testTrue(
    g, !shutdownRunner.isBusy(), "shutdown drains work and joins cleanly");
}

static void
testPresentationFrameLatencyBench()
{
  testSection("CanvasView: warmed camera-cache report (informational)");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  SparseCellGrid grid;
  seedStableBlocksAndBlinker(&grid, 128);
  CanvasView view(80, 60, &grid, &window, &camera, nullptr);
  view.setFadeSpeed(0.0f);
  view.rebuildTargetsFromGrid();

  const int warmupFrames = 30;
  const int measuredFrames = 300;
  RollingMetric staticMetric;
  for (int frame = 0; frame < warmupFrames; ++frame) {
    view.syncVisibleRegion();
    view.rebuildTargetsFromGrid();
  }
  for (int frame = 0; frame < measuredFrames; ++frame) {
    const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
    view.syncVisibleRegion();
    view.rebuildTargetsFromGrid();
    staticMetric.add(std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - start)
                       .count());
  }

  camera.SetPositionPrecise(0.0, 0.0);
  camera.SetZoom(1.0f);
  view.syncVisibleRegion();
  double panWorldX = 0.0;
  for (int frame = 0; frame < warmupFrames; ++frame) {
    panWorldX += 4.0;
    camera.SetPositionPrecise(panWorldX, 0.0);
    view.syncVisibleRegion();
  }
  const std::size_t panRefillsBefore = view.getCacheRefillCount();
  RollingMetric panMetric;
  for (int frame = 0; frame < measuredFrames; ++frame) {
    panWorldX += 4.0;
    const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
    camera.SetPositionPrecise(panWorldX, 0.0);
    view.syncVisibleRegion();
    panMetric.add(std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start)
                    .count());
  }
  const std::size_t panRefills = view.getCacheRefillCount() - panRefillsBefore;

  camera.SetPositionPrecise(0.0, 0.0);
  camera.SetZoom(1.0f);
  view.syncVisibleRegion();
  for (int frame = 0; frame < warmupFrames; ++frame) {
    const float zoom =
      0.9f +
      0.1f * static_cast<float>(std::sin(static_cast<double>(frame) * 0.08));
    camera.SetZoom(zoom);
    view.syncVisibleRegion();
  }
  const std::size_t zoomRefillsBefore = view.getCacheRefillCount();
  RollingMetric zoomMetric;
  for (int frame = 0; frame < measuredFrames; ++frame) {
    const float zoom =
      0.9f + 0.1f * static_cast<float>(std::sin(
                      static_cast<double>(frame + warmupFrames) * 0.08));
    const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
    camera.SetZoom(zoom);
    view.syncVisibleRegion();
    zoomMetric.add(std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - start)
                     .count());
  }
  const std::size_t zoomRefills =
    view.getCacheRefillCount() - zoomRefillsBefore;

  std::printf("BENCH: Presentation static N=%d p50/p95/max="
              "%.3f/%.3f/%.3f ms\n",
              measuredFrames,
              staticMetric.median(),
              staticMetric.p95(),
              staticMetric.maximum());
  std::printf("BENCH: Presentation continuous-pan N=%d p50/p95/max="
              "%.3f/%.3f/%.3f ms refills=%zu\n",
              measuredFrames,
              panMetric.median(),
              panMetric.p95(),
              panMetric.maximum(),
              panRefills);
  std::printf("BENCH: Presentation smooth-zoom N=%d p50/p95/max="
              "%.3f/%.3f/%.3f ms refills=%zu\n",
              measuredFrames,
              zoomMetric.median(),
              zoomMetric.p95(),
              zoomMetric.maximum(),
              zoomRefills);
  testEqSize(g,
             staticMetric.size(),
             RollingMetric::kCapacity,
             "presentation bench retains the latest 256 static samples");
  testTrue(g,
           panRefills < static_cast<std::size_t>(measuredFrames),
           "padded cache avoids a refill on every pan frame");
  testTrue(g,
           zoomRefills < static_cast<std::size_t>(measuredFrames),
           "padded cache avoids a refill on every smooth-zoom frame");
}

static void
testCursorUsesCellBounds()
{
  testSection("CanvasView: cursor uses the same cell bounds");
  static_assert(!std::is_base_of<GameVisual, Cursor>::value,
                "game cursor must compose rather than inherit GameVisual");
  NullRenderWindow window(64, 64);
  EnvVars env;
  env.setVar("WinX", 64);
  env.setVar("WinY", 64);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  Cursor cursor;
  cursor.init(&renderer, &window, &camera);
  cursor.setFromCell(0, 0);

  ShapePrimitive* outline = cursor.getVisual().getShape(0);
  testTrue(g, outline != nullptr, "cursor creates a cell outline");
  if (outline != nullptr) {
    testTrue(g,
             outline->rect.x == -8.0f && outline->rect.y == -8.0f &&
               outline->rect.w == 16.0f && outline->rect.h == 16.0f,
             "cursor outline is centered on its selected cell");
  }
  mock.resetCounters();
  testTrue(g,
           cursor.AppendCommands(&renderer),
           "cursor delegates rendering to its composed visual");
  renderer.EndFrame();
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "cursor composition emits draw tokens");
}

static int
runCanvasInfCase(void (*testFunction)())
{
  g.failures = 0;
  SparseCellGrid::setWorkerOverrideForTesting(0);
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  SparseCellGrid::setChunkNodeReuseOverrideForTesting(true);
  SparseCellGrid::setChunkMemoOverrideForTesting(0);
  testFunction();
  SparseCellGrid::setWorkerOverrideForTesting(0);
  SparseCellGrid::setCellCandidateOverrideForTesting(0);
  SparseCellGrid::setChunkNodeReuseOverrideForTesting(true);
  SparseCellGrid::setChunkMemoOverrideForTesting(0);
  return g.failures;
}

void
registerCanvasInfTests(IllumoTestRegistry& registry)
{
  registry.add("IllumoGame.CanvasInf.NegativeChunkMapping",
               []() { return runCanvasInfCase(testNegativeChunkMapping); });
  registry.add("IllumoGame.CanvasInf.UnboundedChunks",
               []() { return runCanvasInfCase(testUnboundedChunks); });
  registry.add("IllumoGame.CanvasInf.SparseSimulationBoundaries", []() {
    return runCanvasInfCase(testSparseSimulationBoundaries);
  });
  registry.add("IllumoGame.CanvasInf.SparseRevisionAndBoundedVisit", []() {
    return runCanvasInfCase(testSparseRevisionAndBoundedVisit);
  });
  registry.add("IllumoGame.CanvasInf.SparseParallelDeterminism", []() {
    return runCanvasInfCase(testSparseParallelDeterminism);
  });
  registry.add("IllumoGame.CanvasInf.SparseCellCandidates",
               []() { return runCanvasInfCase(testSparseCellCandidates); });
  registry.add("IllumoGame.CanvasInf.SparsePerTargetAdaptiveEvaluation", []() {
    return runCanvasInfCase(testSparsePerTargetAdaptiveEvaluation);
  });
  registry.add("IllumoGame.CanvasInf.SparseCandidateParallelDeterminism", []() {
    return runCanvasInfCase(testSparseCandidateParallelDeterminism);
  });
  registry.add("IllumoGame.CanvasInf.SparseCandidatePreparation", []() {
    return runCanvasInfCase(testSparseCandidatePreparation);
  });
  registry.add("IllumoGame.CanvasInf.SparseChangedFrontier",
               []() { return runCanvasInfCase(testSparseChangedFrontier); });
  registry.add("IllumoGame.CanvasInf.SparseAdaptiveFrontierCost", []() {
    return runCanvasInfCase(testSparseAdaptiveFrontierCost);
  });
  registry.add("IllumoGame.CanvasInf.SparsePreciseActivityMasks", []() {
    return runCanvasInfCase(testSparsePreciseActivityMasks);
  });
  registry.add("IllumoGame.CanvasInf.SparseChunkMemoization",
               []() { return runCanvasInfCase(testSparseChunkMemoization); });
  registry.add("IllumoGame.CanvasInf.SparseCachedStatistics",
               []() { return runCanvasInfCase(testSparseCachedStatistics); });
  registry.add("IllumoGame.CanvasInf.SparseCandidateScratchReuse", []() {
    return runCanvasInfCase(testSparseCandidateScratchReuse);
  });
  registry.add("IllumoGame.CanvasInf.SparseCandidateFlatIndex",
               []() { return runCanvasInfCase(testSparseCandidateFlatIndex); });
  registry.add("IllumoGame.CanvasInf.SparseCompleteHaloScratchReuse", []() {
    return runCanvasInfCase(testSparseCompleteHaloScratchReuse);
  });
  registry.add("IllumoGame.CanvasInf.SparseChunkNodeReuse",
               []() { return runCanvasInfCase(testSparseChunkNodeReuse); });
  registry.add("IllumoGame.CanvasInf.SparseCellCandidateRuleEquivalence", []() {
    return runCanvasInfCase(testSparseCellCandidateRuleEquivalence);
  });
  registry.add("IllumoGame.CanvasInf.MultiStateTransitions",
               []() { return runCanvasInfCase(testMultiStateTransitions); });
  registry.add("IllumoGame.CanvasInf.BoundedView",
               []() { return runCanvasInfCase(testBoundedCanvasView); });
  registry.add("IllumoGame.CanvasInf.AdaptiveOverviewAndRevisionGate", []() {
    return runCanvasInfCase(testAdaptiveOverviewAndRevisionGate);
  });
  registry.add("IllumoGame.CanvasInf.TextureCapacityAndLifecycle", []() {
    return runCanvasInfCase(testTextureCapacityAndLifecycle);
  });
  registry.add("IllumoGame.CanvasInf.IncrementalPresentationWork", []() {
    return runCanvasInfCase(testIncrementalPresentationWork);
  });
  registry.add("IllumoGame.CanvasInf.WorldCellPresentation", []() {
    return runCanvasInfCase(testCanvasViewUsesWorldCellQuad);
  });
  registry.add("IllumoGame.CanvasInf.CameraCacheReuseAndLod", []() {
    return runCanvasInfCase(testCameraCacheReuseAndLodHysteresis);
  });
  registry.add("IllumoGame.CanvasInf.BoundedDirtyUploadRects", []() {
    return runCanvasInfCase(testBoundedDirtyUploadRectangles);
  });
  registry.add("IllumoGame.CanvasInf.AsyncSimulationPublication", []() {
    return runCanvasInfCase(testAsyncSimulationPublication);
  });
  registry.add("IllumoGame.CanvasInf.FrameLatencyBench", []() {
    return runCanvasInfCase(testPresentationFrameLatencyBench);
  });
  registry.add("IllumoGame.CanvasInf.CursorCellAlignment",
               []() { return runCanvasInfCase(testCursorUsesCellBounds); });
}
