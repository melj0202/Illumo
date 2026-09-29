#pragma once

#include "Game/SparseCellGrid.h"
#include <Illumo/Foundation/RollingMetric.h>
#include <memory>
#include <string>
#ifndef ILLUMO_SERIAL_GUEST
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

class RuleSet;
class SimulationLaneCoordinator;

struct SimulationRunnerTimings
{
  double mirrorMilliseconds = 0.0;
  double advanceMilliseconds = 0.0;
  double captureMilliseconds = 0.0;
  double totalMilliseconds = 0.0;
  bool usedMirrorDelta = false;
  bool usedFullCopy = false;
  bool usedDirectSourceAdvance = false;
  // Generations the published result advanced (a lane block or a serial
  // multi-generation start publishes several at once).
  std::uint32_t generations = 1u;
};

// Native builds advance on one persistent worker thread. The WASM guest
// (ILLUMO_SERIAL_GUEST) has no threads: it fans each generation out to
// isolated simulation lanes (CSimPlatform::simulationLanes()) when granted and
// otherwise runs the same generation body inside start(). Either way results
// are taken by the next try call, so callers keep one publication protocol.
class SimulationRunner
{
public:
  SimulationRunner();
  ~SimulationRunner();

  // False when an outstanding generation can only complete on a later frame
  // (guest lanes): drain it with retire() instead of waitAndTakeCompleted().
  bool canBlock() const;
  // Discards any outstanding or completed-but-untaken generation. The
  // published grid is never touched by outstanding work.
  void retire();
  // One status line describing where generations execute.
  std::string describeExecution() const;

  SimulationRunner(const SimulationRunner&) = delete;
  SimulationRunner& operator=(const SimulationRunner&) = delete;

  // Starts up to `requestedGenerations` generations published as one result;
  // `acceptedGenerations` receives how many (the completion's
  // timings.generations). The native runner and a first serial guest start
  // run one.
  bool start(SparseCellGrid* workingGrid,
             const SparseCellGrid* publishedGrid,
             const RuleSet* ruleSet,
             SparseGenerationDelta&& mirrorDelta,
             bool useMirrorDelta,
             std::uint32_t requestedGenerations = 1u,
             std::uint32_t* acceptedGenerations = nullptr);
  bool tryTakeCompleted(SparseCellGrid** completedGrid,
                        SparseGenerationDelta* delta,
                        double* elapsedMilliseconds,
                        bool* advanceSucceeded,
                        SimulationRunnerTimings* timings = nullptr);
  bool waitAndTakeCompleted(SparseCellGrid** completedGrid,
                            SparseGenerationDelta* delta,
                            double* elapsedMilliseconds,
                            bool* advanceSucceeded,
                            SimulationRunnerTimings* timings = nullptr);
  bool isBusy() const;
  void shutdown();
  // Test access to the generation body the runners share.
  static bool runGenerationsForTesting(SparseCellGrid* workingGrid,
                                       const SparseCellGrid* publishedGrid,
                                       const RuleSet* ruleSet,
                                       SparseGenerationDelta&& mirrorDelta,
                                       bool useMirrorDelta,
                                       std::uint32_t generations,
                                       SparseGenerationDelta* completedDelta,
                                       SimulationRunnerTimings* timings)
  {
    return runGeneration(workingGrid,
                         publishedGrid,
                         ruleSet,
                         std::move(mirrorDelta),
                         useMirrorDelta,
                         generations,
                         completedDelta,
                         timings);
  }

private:
  // Mirrors or copies the published grid, advances `generations` generations
  // and captures their net change as one delta against the published grid.
  // Returns true only when every advance and the capture held.
  static bool runGeneration(SparseCellGrid* workingGrid,
                            const SparseCellGrid* publishedGrid,
                            const RuleSet* ruleSet,
                            SparseGenerationDelta&& mirrorDelta,
                            bool useMirrorDelta,
                            std::uint32_t generations,
                            SparseGenerationDelta* completedDelta,
                            SimulationRunnerTimings* timings);

#ifndef ILLUMO_SERIAL_GUEST
  void workerLoop();

  mutable std::mutex mutex;
  std::condition_variable condition;
  std::thread worker;
#else
  std::unique_ptr<SimulationLaneCoordinator> lanes;
  bool laneRequestPending = false;
  // Lanes pay a round trip and a merge per block, so they are used only while
  // serial generations would deliver less (docs/simulation-lanes-v2-design.md).
  bool preferLanes = false;
  // Per-generation cost of serial starts, which run inside the frame.
  RollingMetric serialCost;
  // Generations each start was asked for: the demand per frame.
  RollingMetric requestedMetric;
  // Diagnostics only: each lane state change is logged once, never per frame.
  void reportLaneFailure();
  bool laneFailureReported = false;
  std::uint32_t reportedLaneCount = 0;
  std::string reportedSerialRule;
#endif
  bool stopping = false;
  bool requestPending = false;
  bool running = false;
  bool completed = false;
  bool requestUsesMirror = false;
  SparseCellGrid* requestWorkingGrid = nullptr;
  const SparseCellGrid* requestPublishedGrid = nullptr;
  const RuleSet* requestRuleSet = nullptr;
  SparseGenerationDelta requestMirrorDelta;
  SparseCellGrid* resultGrid = nullptr;
  SparseGenerationDelta resultDelta;
  double resultElapsedMilliseconds = 0.0;
  SimulationRunnerTimings resultTimings;
  bool resultAdvanceSucceeded = false;
};
