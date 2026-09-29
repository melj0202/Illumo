#include "Rulesets/RuleSet.h"
#include "SimulationRunner.h"
#include <Illumo/Foundation/Profile.h>
#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

static bool
chunkAddressLess(const ChunkAddress& left, const ChunkAddress& right)
{
  return left.y < right.y || (left.y == right.y && left.x < right.x);
}

// Shared by the native worker thread and the serial WASM guest runner.
bool
SimulationRunner::runGeneration(SparseCellGrid* workingGrid,
                                const SparseCellGrid* publishedGrid,
                                const RuleSet* ruleSet,
                                SparseGenerationDelta&& mirrorDelta,
                                bool useMirrorDelta,
                                std::uint32_t generations,
                                SparseGenerationDelta* completedDelta,
                                SimulationRunnerTimings* timings)
{
  ILLUMO_PROFILE_ZONE("SimulationRunner.generation");
  const std::chrono::steady_clock::time_point startTime =
    std::chrono::steady_clock::now();
  generations = std::max(1u, generations);
  timings->generations = generations;
  bool advanceSucceeded = false;
  bool captured = false;
  const std::chrono::steady_clock::time_point mirrorStart =
    std::chrono::steady_clock::now();
  bool synchronized = false;
  const bool useDirectSourceAdvance = useMirrorDelta &&
                                      mirrorDelta.fullReplacement &&
                                      mirrorDelta.fullChunks.empty();
  if (useMirrorDelta && !useDirectSourceAdvance) {
    ILLUMO_PROFILE_ZONE("SimulationRunner.applyMirrorDelta");
    synchronized = workingGrid->applyGenerationDelta(mirrorDelta);
    timings->usedMirrorDelta = synchronized;
  }
  if (!synchronized && !useDirectSourceAdvance) {
    ILLUMO_PROFILE_ZONE("SimulationRunner.copyPublishedGrid");
    workingGrid->copyStateFrom(*publishedGrid);
    timings->usedFullCopy = true;
  }
  timings->usedDirectSourceAdvance = useDirectSourceAdvance;
  timings->mirrorMilliseconds =
    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                              mirrorStart)
      .count();
  *completedDelta = std::move(mirrorDelta);
  const std::uint64_t previousRevision = useDirectSourceAdvance
                                           ? publishedGrid->getRevision()
                                           : workingGrid->getRevision();
  const std::chrono::steady_clock::time_point advanceStart =
    std::chrono::steady_clock::now();
  // Several generations publish their net change: every chunk any of them
  // changed, compared against the published grid (the start state).
  std::vector<ChunkAddress> changed;
  bool journaled = true;
  std::uint64_t lastBefore = previousRevision;
  {
    ILLUMO_PROFILE_ZONE("SimulationRunner.advance");
    advanceSucceeded = true;
    for (std::uint32_t step = 0u; step < generations && advanceSucceeded;
         ++step) {
      const bool fromSource = useDirectSourceAdvance && step == 0u;
      lastBefore =
        fromSource ? publishedGrid->getRevision() : workingGrid->getRevision();
      advanceSucceeded = fromSource
                           ? workingGrid->advanceFrom(*publishedGrid, *ruleSet)
                           : workingGrid->advance(*ruleSet);
      if (advanceSucceeded && generations > 1u && journaled &&
          workingGrid->getRevision() != lastBefore) {
        journaled = workingGrid->visitChangedChunksSince(
          lastBefore,
          [&changed](const ChunkAddress& address,
                     const SparseCellGrid::ChunkCells*) {
            changed.push_back(address);
          });
      }
    }
  }
  timings->advanceMilliseconds =
    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                              advanceStart)
      .count();
  const std::chrono::steady_clock::time_point captureStart =
    std::chrono::steady_clock::now();
  {
    ILLUMO_PROFILE_ZONE("SimulationRunner.captureDelta");
    if (advanceSucceeded && generations == 1u) {
      captured = workingGrid->captureGenerationDelta(
        previousRevision, completedDelta, false);
      if (captured && !useDirectSourceAdvance &&
          !completedDelta->fullReplacement) {
        workingGrid->rememberInactiveGenerationDelta(*completedDelta);
      }
    } else if (advanceSucceeded) {
      // Only the last generation, always a self-advance here, describes the
      // inactive map, as after a single generation.
      SparseGenerationDelta last;
      if (workingGrid->captureGenerationDelta(lastBefore, &last, false) &&
          !last.fullReplacement) {
        workingGrid->rememberInactiveGenerationDelta(last);
      }
      if (journaled) {
        std::sort(changed.begin(), changed.end(), chunkAddressLess);
        changed.erase(std::unique(changed.begin(), changed.end()),
                      changed.end());
        std::vector<SparseChunkPatch> patches(changed.size());
        for (std::size_t index = 0; index < changed.size(); ++index) {
          SparseChunkPatch& patch = patches[index];
          patch.address = changed[index];
          const SparseCellGrid::ChunkCells* cells =
            workingGrid->findChunkCells(patch.address);
          patch.present = cells != nullptr;
          patch.cells.fill(SparseCellGrid::BackgroundState);
          if (cells != nullptr) {
            patch.cells = *cells;
          }
        }
        captured = publishedGrid->buildPatchDelta(patches, completedDelta);
      } else {
        // Too many changes to journal: a lightweight replacement makes the
        // presentation refill and the next start advance from the source.
        captured = workingGrid->captureGenerationDelta(
          publishedGrid->getRevision(), completedDelta, false);
      }
      if (captured) {
        completedDelta->fromRevision = publishedGrid->getRevision();
        completedDelta->toRevision = workingGrid->getRevision();
      }
    }
  }
  timings->captureMilliseconds =
    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                              captureStart)
      .count();
  timings->totalMilliseconds = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - startTime)
                                 .count();
  if (advanceSucceeded) {
    // Once per start, from counters the grid already keeps.
    ILLUMO_PROFILE_PLOT("Sim.generationMs",
                        timings->totalMilliseconds /
                          static_cast<double>(generations));
    ILLUMO_PROFILE_PLOT("Sim.serialGenerations", generations);
    ILLUMO_PROFILE_PLOT("Sim.advanceMs", timings->advanceMilliseconds);
    ILLUMO_PROFILE_PLOT("Sim.storedChunks",
                        workingGrid->getLastAdvanceStats().activeChunkCount);
    ILLUMO_PROFILE_PLOT("Sim.targetChunks",
                        workingGrid->getLastAdvanceStats().targetChunkCount);
    ILLUMO_PROFILE_PLOT(
      "Sim.candidateTargets",
      workingGrid->getLastAdvanceStats().candidateTargetCount);
    ILLUMO_PROFILE_PLOT("Sim.frontierTargets",
                        workingGrid->getLastAdvanceStats().frontierTargetCount);
    ILLUMO_PROFILE_PLOT("Sim.producedChunks",
                        workingGrid->getLastAdvanceStats().producedChunkCount);
    ILLUMO_PROFILE_PLOT("Sim.changedChunks",
                        completedDelta->changedChunks.size());
    ILLUMO_PROFILE_PLOT("Sim.workers",
                        workingGrid->getLastAdvanceStats().workerCount);
    ILLUMO_PROFILE_FRAME_MARK("Sim.serialGeneration");
  }
  return advanceSucceeded && captured;
}
