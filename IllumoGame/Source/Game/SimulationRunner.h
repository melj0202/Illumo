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

  bool start(SparseCellGrid* workingGrid,
             const SparseCellGrid* publishedGrid,
             const RuleSet* ruleSet,
             SparseGenerationDelta&& mirrorDelta,
             bool useMirrorDelta);
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

private:
  // Mirrors or copies the published grid, advances one generation and
  // captures its delta. Returns true only when both advance and capture held.
  static bool runGeneration(SparseCellGrid* workingGrid,
                            const SparseCellGrid* publishedGrid,
                            const RuleSet* ruleSet,
                            SparseGenerationDelta&& mirrorDelta,
                            bool useMirrorDelta,
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
  // Lanes pay a round trip and a merge per generation, so they are used
  // only while a serial generation would cost more than that.
  bool preferLanes = false;
  RollingMetric serialCost;
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
