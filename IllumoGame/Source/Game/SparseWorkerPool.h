#pragma once

#include "SparseCellGrid.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>
#include <vector>

class SparseWorkerPool
{
public:
  SparseWorkerPool();
  ~SparseWorkerPool();

  SparseWorkerPool(const SparseWorkerPool&) = delete;
  SparseWorkerPool& operator=(const SparseWorkerPool&) = delete;

  void evaluate(const SparseCellGrid* grid,
                const unsigned char* transitions,
                const std::vector<ChunkAddress>* targets,
                std::vector<SparseCellGrid::TargetResult>* results,
                unsigned int workerCount);

  void evaluateCandidates(
    const SparseCellGrid* grid,
    const unsigned char* transitions,
    const std::vector<SparseCellGrid::CandidateScratchChunk>* scratch,
    std::vector<SparseCellGrid::CandidateWorkRange>* ranges,
    std::vector<SparseCellGrid::TargetResult>* results,
    unsigned int workerCount);

  void prepareCandidates(
    const SparseCellGrid* grid,
    std::vector<SparseCellGrid::CandidateScratchChunk>* scratch,
    const std::vector<SparseCellGrid::CandidateWorkRange>* ranges,
    unsigned int workerCount);

private:
  void ensureWorkerCount(unsigned int requiredCount);
  bool claimWorkerSlot();
  void executeAvailableWork(unsigned int memoShardIndex);
  void workerLoop(unsigned int memoShardIndex);

  std::mutex mutex;
  std::condition_variable workReady;
  std::condition_variable workComplete;
  std::vector<std::thread> workers;
  std::atomic<std::size_t> nextWorkItem{ 0u };
  std::atomic<unsigned int> availableWorkerSlots{ 0u };
  const SparseCellGrid* activeGrid = nullptr;
  const unsigned char* activeTransitions = nullptr;
  const std::vector<ChunkAddress>* activeTargets = nullptr;
  std::vector<SparseCellGrid::CandidateScratchChunk>*
    activeCandidatePreparationScratch = nullptr;
  const std::vector<SparseCellGrid::CandidateWorkRange>*
    activeCandidatePreparationRanges = nullptr;
  const std::vector<SparseCellGrid::CandidateScratchChunk>*
    activeCandidateScratch = nullptr;
  std::vector<SparseCellGrid::CandidateWorkRange>* activeCandidateRanges =
    nullptr;
  std::vector<SparseCellGrid::TargetResult>* activeResults = nullptr;
  std::size_t workGeneration = 0u;
  unsigned int requiredWorkers = 0u;
  unsigned int completedWorkers = 0u;
  bool stopping = false;
};
