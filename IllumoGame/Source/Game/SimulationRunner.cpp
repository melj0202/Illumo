#include "SimulationRunner.h"
#include <utility>

SimulationRunner::SimulationRunner()
{
  // The worker can read request/result state as soon as it starts.
  // Launch only after every member has finished initialization.
  worker = std::thread(&SimulationRunner::workerLoop, this);
}

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
      ruleSet == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex);
  if (stopping || requestPending || running || completed) {
    return false;
  }
  requestWorkingGrid = workingGrid;
  requestPublishedGrid = publishedGrid;
  requestRuleSet = ruleSet;
  requestMirrorDelta = std::move(mirrorDelta);
  requestUsesMirror = useMirrorDelta;
  requestPending = true;
  condition.notify_all();
  return true;
}

bool
SimulationRunner::tryTakeCompleted(SparseCellGrid** completedGrid,
                                   SparseGenerationDelta* delta,
                                   double* elapsedMilliseconds,
                                   bool* advanceSucceeded,
                                   SimulationRunnerTimings* timings)
{
  std::lock_guard<std::mutex> lock(mutex);
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
  std::unique_lock<std::mutex> lock(mutex);
  if (!requestPending && !running && !completed) {
    return false;
  }
  condition.wait(lock, [this]() { return completed || stopping; });
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
SimulationRunner::isBusy() const
{
  std::lock_guard<std::mutex> lock(mutex);
  return requestPending || running || completed;
}

void
SimulationRunner::shutdown()
{
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (stopping) {
      return;
    }
    stopping = true;
    condition.notify_all();
  }
  if (worker.joinable()) {
    worker.join();
  }
  std::lock_guard<std::mutex> lock(mutex);
  requestPending = false;
  running = false;
  completed = false;
  requestWorkingGrid = nullptr;
  requestPublishedGrid = nullptr;
  requestRuleSet = nullptr;
  resultGrid = nullptr;
  requestMirrorDelta.clear();
  resultDelta.clear();
}

void
SimulationRunner::workerLoop()
{
  for (;;) {
    SparseCellGrid* workingGrid = nullptr;
    const SparseCellGrid* publishedGrid = nullptr;
    const RuleSet* ruleSet = nullptr;
    SparseGenerationDelta mirrorDelta;
    bool useMirrorDelta = false;
    {
      std::unique_lock<std::mutex> lock(mutex);
      condition.wait(lock, [this]() { return stopping || requestPending; });
      if (stopping && !requestPending) {
        return;
      }
      workingGrid = requestWorkingGrid;
      publishedGrid = requestPublishedGrid;
      ruleSet = requestRuleSet;
      mirrorDelta = std::move(requestMirrorDelta);
      useMirrorDelta = requestUsesMirror;
      requestPending = false;
      running = true;
    }

    SparseGenerationDelta completedDelta;
    SimulationRunnerTimings timings;
    const bool succeeded = runGeneration(workingGrid,
                                         publishedGrid,
                                         ruleSet,
                                         std::move(mirrorDelta),
                                         useMirrorDelta,
                                         &completedDelta,
                                         &timings);

    {
      std::lock_guard<std::mutex> lock(mutex);
      resultGrid = workingGrid;
      resultDelta = std::move(completedDelta);
      resultElapsedMilliseconds = timings.totalMilliseconds;
      resultTimings = timings;
      resultAdvanceSucceeded = succeeded;
      running = false;
      completed = true;
      condition.notify_all();
    }
  }
}
