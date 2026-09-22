#pragma once

#include "Game/SparseCellGrid.h"
#ifndef ILLUMO_SERIAL_GUEST
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

class RuleSet;

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

// Native builds advance on one persistent worker thread. A WASM guest
// (ILLUMO_SERIAL_GUEST) has no threads: start() runs the same generation body
// immediately and the result is taken by the next try/wait call, so callers
// keep one publication and drain protocol.
class SimulationRunner
{
public:
  SimulationRunner();
  ~SimulationRunner();

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
