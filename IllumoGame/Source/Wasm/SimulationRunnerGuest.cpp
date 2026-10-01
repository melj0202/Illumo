#include "Game/CSimPlatform.h"
#include "Game/SimulationRunner.h"
#include "Rulesets/RuleSet.h"
#include "SimulationLanes.h"
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <string>
#include <utility>

// WASM guest runner. No thread exists in this store: with granted simulation
// lanes each block of generations is fanned out to isolated worker stores and
// taken by a later try call; otherwise (no grant, pending grant, a failed lane
// or an unpartitionable rule) start() completes its generations before
// returning, as many as fit kSerialBudgetMilliseconds of the frame.
// The choice follows throughput (docs/simulation-lanes-v2-design.md, 4.5):
// lanes once serial generations cannot deliver the requested generations per
// frame within that budget, serial again once the lanes' summed work per
// generation would fit half of it. Summed lane work includes halo and
// per-lane overhead, so it overstates a serial generation; the factor of two
// between the thresholds keeps the choice from flapping.
static constexpr double kSerialBudgetMilliseconds = 4.0;
static constexpr double kLeaveLanesFraction = 0.5;
// A serial start's generation count grows from one as costs are measured.
static constexpr std::uint32_t kMaximumSerialGenerations = 64u;

// Generations of `requested` a serial start runs within the frame budget.
static std::uint32_t
serialGenerationsFor(std::uint32_t requested, const RollingMetric& cost)
{
  if (requested <= 1u || cost.size() < 4u || !(cost.median() > 0.0)) {
    return 1u;
  }
  const double fit = kSerialBudgetMilliseconds / cost.median();
  const std::uint32_t affordable =
    fit >= static_cast<double>(kMaximumSerialGenerations)
      ? kMaximumSerialGenerations
      : std::max(1u, static_cast<std::uint32_t>(fit));
  return std::min(requested, affordable);
}

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
                        bool useMirrorDelta,
                        std::uint32_t requestedGenerations,
                        std::uint32_t* acceptedGenerations)
{
  ILLUMO_PROFILE_ZONE("SimulationRunner.start");
  requestedGenerations = std::max(1u, requestedGenerations);
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
    ILLUMO_PROFILE_ZONE("SimulationRunner.chooseLanesOrSerial");
    // Generations per frame the canvas asks for (whole steps due).
    requestedMetric.add(static_cast<double>(requestedGenerations));
    const double demand = std::max(1.0, requestedMetric.median());
    if (!preferLanes && serialCost.size() >= 8u &&
        serialCost.median() * demand > kSerialBudgetMilliseconds) {
      preferLanes = true;
      lanes->resetWorkMetrics();
      Logger::LogTrace("Serial generations cannot keep up within the frame "
                       "budget; preferring simulation lanes");
    } else if (preferLanes && lanes->laneWorkMetric().size() >= 32u &&
               lanes->laneWorkMetric().median() * demand <
                 kSerialBudgetMilliseconds * kLeaveLanesFraction &&
               !lanes->busy()) {
      preferLanes = false;
      serialCost = RollingMetric{};
      lanes->retire(); // any speculative block is dropped
      Logger::LogTrace("Serial generations fit the frame budget again; "
                       "generations return to the game store");
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
                    std::to_string(reportedLaneCount) + " simulation lanes");
  }
  ILLUMO_PROFILE_PLOT("Sim.lanesActive",
                      availability ==
                          SimulationLaneCoordinator::Availability::Available
                        ? lanes->lanes()
                        : 0u);
  if (lanes && preferLanes &&
      availability == SimulationLaneCoordinator::Availability::Available) {
    if (lanes->start(workingGrid,
                     publishedGrid,
                     ruleSet,
                     std::move(mirrorDelta),
                     useMirrorDelta,
                     requestedGenerations,
                     acceptedGenerations)) {
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
  ILLUMO_PROFILE_ZONE("SimulationRunner.serialGeneration");
  SimulationRunnerTimings timings;
  SparseGenerationDelta completedDelta;
  const std::uint32_t generations =
    serialGenerationsFor(requestedGenerations, serialCost);
  if (acceptedGenerations != nullptr) {
    *acceptedGenerations = generations;
  }
  resultAdvanceSucceeded = runGeneration(workingGrid,
                                         publishedGrid,
                                         ruleSet,
                                         std::move(mirrorDelta),
                                         useMirrorDelta,
                                         generations,
                                         &completedDelta,
                                         &timings);
  resultGrid = workingGrid;
  resultDelta = std::move(completedDelta);
  resultElapsedMilliseconds = timings.totalMilliseconds;
  resultTimings = timings;
  serialCost.add(timings.totalMilliseconds /
                 static_cast<double>(timings.generations));
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
  ILLUMO_PROFILE_ZONE("SimulationRunner.tryTakeCompleted");
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
      ILLUMO_PROFILE_ZONE("SimulationRunner.laneFailureFallback");
      laneRequestPending = false;
      SimulationRunnerTimings serialTimings;
      SparseGenerationDelta serialDelta;
      resultAdvanceSucceeded = runGeneration(requestWorkingGrid,
                                             requestPublishedGrid,
                                             requestRuleSet,
                                             SparseGenerationDelta{},
                                             false,
                                             1u,
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
  ILLUMO_PROFILE_ZONE("SimulationRunner.retire");
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
