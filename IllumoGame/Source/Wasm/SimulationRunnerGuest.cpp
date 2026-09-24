#include "Game/CSimPlatform.h"
#include "Game/SimulationRunner.h"
#include "Rulesets/RuleSet.h"
#include "SimulationLanes.h"
#include <Illumo/Services/Logger.h>
#include <string>
#include <utility>

// WASM guest runner. No thread exists in this store: with granted simulation
// lanes each generation is fanned out to isolated worker stores and taken by
// a later try call; otherwise (no grant, pending grant, a failed lane or an
// unpartitionable rule) start() completes the generation before returning.
// Lanes are used once serial generations exceed kUseLanesAbove (a quarter of
// a 60 Hz frame) and dropped when all lanes together work less than
// kLeaveLanesBelow (a small or settled world). Summed lane work includes
// per-lane overhead, so the exit threshold is deliberately low: frames stay
// light on lanes even where serial throughput would be similar.
static constexpr double kUseLanesAbove = 4.0;
static constexpr double kLeaveLanesBelow = 1.0;

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
      ruleSet == nullptr || stopping || completed || (lanes && lanes->busy())) {
    return false;
  }
  SimulationLaneTransport* transport =
    CSimPlatform::current().simulationLanes();
  if (transport != nullptr && !lanes) {
    lanes = std::make_unique<SimulationLaneCoordinator>(*transport);
    Logger::LogTrace("Simulation lane coordinator created");
  }
  reportLaneFailure();
  if (lanes) {
    if (!preferLanes && serialCost.size() >= 8u &&
        serialCost.median() > kUseLanesAbove) {
      preferLanes = true;
      Logger::LogTrace("Serial generations exceed 4 ms; preferring simulation "
                       "lanes");
    } else if (preferLanes && lanes->laneWorkMetric().size() >= 32u &&
               lanes->laneWorkMetric().median() < kLeaveLanesBelow &&
               !lanes->busy()) {
      preferLanes = false;
      serialCost = RollingMetric{};
      lanes->retire(); // any speculative generation is dropped
      Logger::LogTrace("Lane work is under 1 ms; generations return to the "
                       "game store");
    }
  }
  const SimulationLaneCoordinator::Availability availability =
    lanes && preferLanes ? lanes->availability(*ruleSet)
                         : SimulationLaneCoordinator::Availability::Unavailable;
  if (lanes && preferLanes &&
      availability == SimulationLaneCoordinator::Availability::Unavailable &&
      !lanes->failed() && transport != nullptr && transport->laneCountKnown() &&
      transport->laneCount() > 0u &&
      reportedSerialRule != ruleSet->getRuleTag()) {
    reportedSerialRule = ruleSet->getRuleTag();
    Logger::LogTrace("Ruleset " + reportedSerialRule +
                     " cannot be split across simulation lanes; it runs in "
                     "the game store");
  }
  if (availability == SimulationLaneCoordinator::Availability::Available &&
      reportedLaneCount != lanes->lanes()) {
    reportedLaneCount = lanes->lanes();
    Logger::LogInfo("Generations now run on " +
                    std::to_string(reportedLaneCount) +
                    " simulation lanes (serial cost over 4 ms)");
  }
  if (lanes && preferLanes &&
      availability == SimulationLaneCoordinator::Availability::Available) {
    if (lanes->start(workingGrid,
                     publishedGrid,
                     ruleSet,
                     std::move(mirrorDelta),
                     useMirrorDelta)) {
      requestWorkingGrid = workingGrid;
      requestPublishedGrid = publishedGrid;
      requestRuleSet = ruleSet;
      laneRequestPending = true;
      return true;
    }
    if (!lanes->failed()) {
      // Lanes are draining retired work; the generation starts next frame
      // rather than as a slow serial one now.
      return false;
    }
    reportLaneFailure();
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
  serialCost.add(timings.totalMilliseconds);
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
  if (!completed && lanes) {
    if (lanes->poll(completedGrid,
                    delta,
                    elapsedMilliseconds,
                    advanceSucceeded,
                    timings)) {
      laneRequestPending = false;
      return true;
    }
    reportLaneFailure();
    if (laneRequestPending && lanes->failed()) {
      // A lane failed mid-generation: finish it here. The working grid was
      // already brought to the published state; copy again to be exact.
      laneRequestPending = false;
      SimulationRunnerTimings serialTimings;
      SparseGenerationDelta serialDelta;
      resultAdvanceSucceeded = runGeneration(requestWorkingGrid,
                                             requestPublishedGrid,
                                             requestRuleSet,
                                             SparseGenerationDelta{},
                                             false,
                                             &serialDelta,
                                             &serialTimings);
      resultGrid = requestWorkingGrid;
      resultDelta = std::move(serialDelta);
      resultElapsedMilliseconds = serialTimings.totalMilliseconds;
      resultTimings = serialTimings;
      completed = true;
    }
  }
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
  // Lane replies arrive only between guest calls; nothing here can wait.
  return tryTakeCompleted(
    completedGrid, delta, elapsedMilliseconds, advanceSucceeded, timings);
}

bool
SimulationRunner::isBusy() const
{
  return completed || (lanes && lanes->busy());
}

bool
SimulationRunner::canBlock() const
{
  return !lanes;
}

void
SimulationRunner::retire()
{
  completed = false;
  resultGrid = nullptr;
  resultDelta.clear();
  laneRequestPending = false;
  if (lanes) {
    lanes->retire();
  }
}

void
SimulationRunner::reportLaneFailure()
{
  if (!lanes || !lanes->failed() || laneFailureReported) {
    return;
  }
  laneFailureReported = true;
  Logger::LogWarning(
    "Simulation lanes turned off; generations continue in the game store: " +
    lanes->failure());
}

std::string
SimulationRunner::describeExecution() const
{
  if (!lanes) {
    return "serial in the game store";
  }
  if (lanes->failed()) {
    return "serial in the game store (lanes off: " + lanes->failure() + ")";
  }
  if (lanes->lanes() == 0u) {
    return "serial in the game store (awaiting the lane grant)";
  }
  if (!preferLanes) {
    return "serial in the game store; " + std::to_string(lanes->lanes()) +
           " simulation lanes idle (serial p50 " +
           std::to_string(serialCost.median()) + " ms)";
  }
  return std::to_string(lanes->lanes()) + " simulation lanes; round trip " +
         "p50/p95 " + std::to_string(lanes->roundTripMetric().median()) + "/" +
         std::to_string(lanes->roundTripMetric().p95()) +
         " ms, slowest lane advance p50/p95 " +
         std::to_string(lanes->laneAdvanceMetric().median()) + "/" +
         std::to_string(lanes->laneAdvanceMetric().p95()) +
         " ms, lane patch/collect p50 " +
         std::to_string(lanes->lanePatchMetric().median()) + "/" +
         std::to_string(lanes->laneCollectMetric().median()) +
         " ms, merge p50 " + std::to_string(lanes->mergeMetric().median()) +
         " (build " + std::to_string(lanes->mergeBuildMetric().median()) + ")" +
         " ms, resyncs " + std::to_string(lanes->resynchronizations()) +
         ", retirements " + std::to_string(lanes->retirements());
}

void
SimulationRunner::shutdown()
{
  stopping = true;
  completed = false;
  resultGrid = nullptr;
  resultDelta.clear();
  laneRequestPending = false;
  if (lanes) {
    lanes->retire();
  }
}
