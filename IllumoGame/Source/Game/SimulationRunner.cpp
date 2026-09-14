#include "SimulationRunner.h"
#include "Rulesets/RuleSet.h"
#include <chrono>
#include <tracy/Tracy.hpp>
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

    ZoneScopedN("SimulationRunner.generation");
    const std::chrono::steady_clock::time_point startTime =
      std::chrono::steady_clock::now();
    bool advanceSucceeded = false;
    SparseGenerationDelta completedDelta;
    bool captured = false;
    SimulationRunnerTimings timings;
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
        timings.usedMirrorDelta = synchronized;
      }
      if (!synchronized && !useDirectSourceAdvance) {
        ZoneScopedN("SimulationRunner.copyPublishedGrid");
        workingGrid->copyStateFrom(*publishedGrid);
        timings.usedFullCopy = true;
      }
      timings.usedDirectSourceAdvance = useDirectSourceAdvance;
      timings.mirrorMilliseconds =
        std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - mirrorStart)
          .count();
      completedDelta = std::move(mirrorDelta);
      const std::uint64_t previousRevision = useDirectSourceAdvance
                                               ? publishedGrid->getRevision()
                                               : workingGrid->getRevision();
      const std::chrono::steady_clock::time_point advanceStart =
        std::chrono::steady_clock::now();
      advanceSucceeded = useDirectSourceAdvance
                           ? workingGrid->advanceFrom(*publishedGrid, *ruleSet)
                           : workingGrid->advance(*ruleSet);
      timings.advanceMilliseconds =
        std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - advanceStart)
          .count();
      const std::chrono::steady_clock::time_point captureStart =
        std::chrono::steady_clock::now();
      captured =
        advanceSucceeded && workingGrid->captureGenerationDelta(
                              previousRevision, &completedDelta, false);
      if (captured && !useDirectSourceAdvance &&
          !completedDelta.fullReplacement) {
        workingGrid->rememberInactiveGenerationDelta(completedDelta);
      }
      timings.captureMilliseconds =
        std::chrono::duration<double, std::milli>(
          std::chrono::steady_clock::now() - captureStart)
          .count();
    } catch (...) {
      advanceSucceeded = false;
      captured = false;
      completedDelta.clear();
    }
    const double elapsedMilliseconds =
      std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - startTime)
        .count();
    timings.totalMilliseconds = elapsedMilliseconds;

    {
      std::lock_guard<std::mutex> lock(mutex);
      resultGrid = workingGrid;
      resultDelta = std::move(completedDelta);
      resultElapsedMilliseconds = elapsedMilliseconds;
      resultTimings = timings;
      resultAdvanceSucceeded = advanceSucceeded && captured;
      running = false;
      completed = true;
      condition.notify_all();
    }
  }
}
