#include "Rulesets/RuleSet.h"
#include "SimulationRunner.h"
#include <chrono>
#include <tracy/Tracy.hpp>
#include <utility>

// Shared by the native worker thread and the serial WASM guest runner.
bool
SimulationRunner::runGeneration(SparseCellGrid* workingGrid,
                                const SparseCellGrid* publishedGrid,
                                const RuleSet* ruleSet,
                                SparseGenerationDelta&& mirrorDelta,
                                bool useMirrorDelta,
                                SparseGenerationDelta* completedDelta,
                                SimulationRunnerTimings* timings)
{
  ZoneScopedN("SimulationRunner.generation");
  const std::chrono::steady_clock::time_point startTime =
    std::chrono::steady_clock::now();
  bool advanceSucceeded = false;
  bool captured = false;
  try {
    const std::chrono::steady_clock::time_point mirrorStart =
      std::chrono::steady_clock::now();
    bool synchronized = false;
    const bool useDirectSourceAdvance = useMirrorDelta &&
                                        mirrorDelta.fullReplacement &&
                                        mirrorDelta.fullChunks.empty();
    if (useMirrorDelta && !useDirectSourceAdvance) {
      ZoneScopedN("SimulationRunner.applyMirrorDelta");
      synchronized = workingGrid->applyGenerationDelta(mirrorDelta);
      timings->usedMirrorDelta = synchronized;
    }
    if (!synchronized && !useDirectSourceAdvance) {
      ZoneScopedN("SimulationRunner.copyPublishedGrid");
      workingGrid->copyStateFrom(*publishedGrid);
      timings->usedFullCopy = true;
    }
    timings->usedDirectSourceAdvance = useDirectSourceAdvance;
    timings->mirrorMilliseconds =
      std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - mirrorStart)
        .count();
    *completedDelta = std::move(mirrorDelta);
    const std::uint64_t previousRevision = useDirectSourceAdvance
                                             ? publishedGrid->getRevision()
                                             : workingGrid->getRevision();
    const std::chrono::steady_clock::time_point advanceStart =
      std::chrono::steady_clock::now();
    advanceSucceeded = useDirectSourceAdvance
                         ? workingGrid->advanceFrom(*publishedGrid, *ruleSet)
                         : workingGrid->advance(*ruleSet);
    timings->advanceMilliseconds =
      std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - advanceStart)
        .count();
    const std::chrono::steady_clock::time_point captureStart =
      std::chrono::steady_clock::now();
    captured = advanceSucceeded && workingGrid->captureGenerationDelta(
                                     previousRevision, completedDelta, false);
    if (captured && !useDirectSourceAdvance &&
        !completedDelta->fullReplacement) {
      workingGrid->rememberInactiveGenerationDelta(*completedDelta);
    }
    timings->captureMilliseconds =
      std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - captureStart)
        .count();
  } catch (...) {
    advanceSucceeded = false;
    captured = false;
    completedDelta->clear();
  }
  timings->totalMilliseconds = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - startTime)
                                 .count();
  return advanceSucceeded && captured;
}
