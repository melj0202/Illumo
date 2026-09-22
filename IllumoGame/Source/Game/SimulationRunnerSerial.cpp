#include "SimulationRunner.h"
#include <utility>

// WASM guest runner: no thread exists, so start() completes the generation
// before returning. Callers still take the result through try/wait, which
// keeps publication, mirror-delta and drain semantics identical to native.

SimulationRunner::SimulationRunner() = default;

SimulationRunner::~SimulationRunner()
{
  shutdown();
}

bool
SimulationRunner::start(SparseCellGrid* workingGrid,
                        const SparseCellGrid* publishedGrid,
                        const RuleSet* ruleSet,
                        SparseGenerationDelta&& mirrorDelta,
                        bool useMirrorDelta)
{
  if (workingGrid == nullptr || publishedGrid == nullptr ||
      ruleSet == nullptr || stopping || completed) {
    return false;
  }
  SimulationRunnerTimings timings;
  SparseGenerationDelta completedDelta;
  resultAdvanceSucceeded = runGeneration(workingGrid,
                                         publishedGrid,
                                         ruleSet,
                                         std::move(mirrorDelta),
                                         useMirrorDelta,
                                         &completedDelta,
                                         &timings);
  resultGrid = workingGrid;
  resultDelta = std::move(completedDelta);
  resultElapsedMilliseconds = timings.totalMilliseconds;
  resultTimings = timings;
  completed = true;
  return true;
}

bool
SimulationRunner::tryTakeCompleted(SparseCellGrid** completedGrid,
                                   SparseGenerationDelta* delta,
                                   double* elapsedMilliseconds,
                                   bool* advanceSucceeded,
                                   SimulationRunnerTimings* timings)
{
  if (!completed) {
    return false;
  }
  if (completedGrid != nullptr) {
    *completedGrid = resultGrid;
  }
  if (delta != nullptr) {
    *delta = std::move(resultDelta);
  }
  if (elapsedMilliseconds != nullptr) {
    *elapsedMilliseconds = resultElapsedMilliseconds;
  }
  if (advanceSucceeded != nullptr) {
    *advanceSucceeded = resultAdvanceSucceeded;
  }
  if (timings != nullptr) {
    *timings = resultTimings;
  }
  completed = false;
  resultGrid = nullptr;
  return true;
}

bool
SimulationRunner::waitAndTakeCompleted(SparseCellGrid** completedGrid,
                                       SparseGenerationDelta* delta,
                                       double* elapsedMilliseconds,
                                       bool* advanceSucceeded,
                                       SimulationRunnerTimings* timings)
{
  return tryTakeCompleted(
    completedGrid, delta, elapsedMilliseconds, advanceSucceeded, timings);
}

bool
SimulationRunner::isBusy() const
{
  return completed;
}

void
SimulationRunner::shutdown()
{
  stopping = true;
  completed = false;
  resultGrid = nullptr;
  resultDelta.clear();
}
