#include "SparseWorkerPool.h"
#include <Illumo/Foundation/Profile.h>

SparseWorkerPool::SparseWorkerPool() = default;

SparseWorkerPool::~SparseWorkerPool()
{
  {
    std::lock_guard<std::mutex> lock(mutex);
    stopping = true;
    workGeneration += 1;
  }
  workReady.notify_all();
  for (std::thread& worker : workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

void
SparseWorkerPool::evaluate(const SparseCellGrid* grid,
                           const unsigned char* transitions,
                           const std::vector<ChunkAddress>* targets,
                           std::vector<SparseCellGrid::TargetResult>* results,
                           unsigned int workerCount)
{
  ILLUMO_PROFILE_ZONE("SparseWorkerPool.evaluate");
  if (grid == nullptr || transitions == nullptr || targets == nullptr ||
      results == nullptr || workerCount <= 1u || targets->empty()) {
    return;
  }

  const unsigned int workerThreads = workerCount - 1u;
  ensureWorkerCount(workerThreads);

  {
    std::lock_guard<std::mutex> lock(mutex);
    activeGrid = grid;
    activeTransitions = transitions;
    activeTargets = targets;
    activeResults = results;
    activeCandidatePreparationScratch = nullptr;
    activeCandidatePreparationRanges = nullptr;
    activeCandidateScratch = nullptr;
    activeCandidateRanges = nullptr;
    activeJob = nullptr;
    nextWorkItem.store(0u);
    availableWorkerSlots.store(workerThreads);
    completedWorkers = 0u;
    requiredWorkers = workerThreads;
    workGeneration += 1;
  }
  workReady.notify_all();

  executeAvailableWork(0u);

  ILLUMO_PROFILE_ZONE("SparseWorkerPool.waitForWorkers");
  std::unique_lock<std::mutex> lock(mutex);
  workComplete.wait(lock,
                    [this]() { return completedWorkers >= requiredWorkers; });
}

void
SparseWorkerPool::evaluateCandidates(
  const SparseCellGrid* grid,
  const unsigned char* transitions,
  const std::vector<SparseCellGrid::CandidateScratchChunk>* scratch,
  std::vector<SparseCellGrid::CandidateWorkRange>* ranges,
  std::vector<SparseCellGrid::TargetResult>* results,
  unsigned int workerCount)
{
  ILLUMO_PROFILE_ZONE("SparseWorkerPool.evaluateCandidates");
  if (grid == nullptr || transitions == nullptr || scratch == nullptr ||
      ranges == nullptr || results == nullptr || workerCount <= 1u ||
      ranges->empty()) {
    return;
  }

  const unsigned int workerThreads = workerCount - 1u;
  ensureWorkerCount(workerThreads);

  {
    std::lock_guard<std::mutex> lock(mutex);
    activeGrid = grid;
    activeTransitions = transitions;
    activeTargets = nullptr;
    activeCandidatePreparationScratch = nullptr;
    activeCandidatePreparationRanges = nullptr;
    activeCandidateScratch = scratch;
    activeCandidateRanges = ranges;
    activeResults = results;
    activeJob = nullptr;
    nextWorkItem.store(0u);
    availableWorkerSlots.store(workerThreads);
    completedWorkers = 0u;
    requiredWorkers = workerThreads;
    workGeneration += 1;
  }
  workReady.notify_all();

  executeAvailableWork(0u);

  ILLUMO_PROFILE_ZONE("SparseWorkerPool.waitForWorkers");
  std::unique_lock<std::mutex> lock(mutex);
  workComplete.wait(lock,
                    [this]() { return completedWorkers >= requiredWorkers; });
}

void
SparseWorkerPool::prepareCandidates(
  const SparseCellGrid* grid,
  std::vector<SparseCellGrid::CandidateScratchChunk>* scratch,
  const std::vector<SparseCellGrid::CandidateWorkRange>* ranges,
  unsigned int workerCount)
{
  ILLUMO_PROFILE_ZONE("SparseWorkerPool.prepareCandidates");
  if (grid == nullptr || scratch == nullptr || workerCount <= 1u ||
      ranges == nullptr || ranges->empty()) {
    return;
  }

  const unsigned int workerThreads = workerCount - 1u;
  ensureWorkerCount(workerThreads);

  {
    std::lock_guard<std::mutex> lock(mutex);
    activeGrid = grid;
    activeTransitions = nullptr;
    activeTargets = nullptr;
    activeCandidatePreparationScratch = scratch;
    activeCandidatePreparationRanges = ranges;
    activeCandidateScratch = nullptr;
    activeCandidateRanges = nullptr;
    activeResults = nullptr;
    activeJob = nullptr;
    nextWorkItem.store(0u);
    availableWorkerSlots.store(workerThreads);
    completedWorkers = 0u;
    requiredWorkers = workerThreads;
    workGeneration += 1;
  }
  workReady.notify_all();

  executeAvailableWork(0u);

  ILLUMO_PROFILE_ZONE("SparseWorkerPool.waitForWorkers");
  std::unique_lock<std::mutex> lock(mutex);
  workComplete.wait(lock,
                    [this]() { return completedWorkers >= requiredWorkers; });
}

void
SparseWorkerPool::run(std::size_t itemCount,
                      unsigned int workerCount,
                      const Job& job)
{
  ILLUMO_PROFILE_ZONE("SparseWorkerPool.run");
  if (itemCount == 0u) {
    return;
  }
  if (workerCount <= 1u) {
    for (std::size_t index = 0u; index < itemCount; ++index) {
      job(index, 0u);
    }
    return;
  }

  const unsigned int workerThreads = workerCount - 1u;
  ensureWorkerCount(workerThreads);

  {
    std::lock_guard<std::mutex> lock(mutex);
    activeGrid = nullptr;
    activeTransitions = nullptr;
    activeTargets = nullptr;
    activeCandidatePreparationScratch = nullptr;
    activeCandidatePreparationRanges = nullptr;
    activeCandidateScratch = nullptr;
    activeCandidateRanges = nullptr;
    activeResults = nullptr;
    activeJob = &job;
    activeJobCount = itemCount;
    nextWorkItem.store(0u);
    availableWorkerSlots.store(workerThreads);
    completedWorkers = 0u;
    requiredWorkers = workerThreads;
    workGeneration += 1;
  }
  workReady.notify_all();

  executeAvailableWork(0u);

  ILLUMO_PROFILE_ZONE("SparseWorkerPool.waitForWorkers");
  std::unique_lock<std::mutex> lock(mutex);
  workComplete.wait(lock,
                    [this]() { return completedWorkers >= requiredWorkers; });
  activeJob = nullptr;
}

void
SparseWorkerPool::ensureWorkerCount(unsigned int requiredCount)
{
  while (workers.size() < static_cast<std::size_t>(requiredCount)) {
    const unsigned int shardIndex =
      static_cast<unsigned int>(workers.size()) + 1u;
    workers.emplace_back(&SparseWorkerPool::workerLoop, this, shardIndex);
  }
}

bool
SparseWorkerPool::claimWorkerSlot()
{
  unsigned int remaining = availableWorkerSlots.load();
  while (remaining > 0u) {
    if (availableWorkerSlots.compare_exchange_weak(remaining, remaining - 1u)) {
      return true;
    }
  }
  return false;
}

void
SparseWorkerPool::executeAvailableWork(unsigned int memoShardIndex)
{
  for (;;) {
    const std::size_t index = nextWorkItem.fetch_add(1u);
    if (activeJob != nullptr) {
      if (index >= activeJobCount) {
        return;
      }
      (*activeJob)(index, memoShardIndex);
      continue;
    }
    if (activeGrid == nullptr) {
      return;
    }
    if (activeCandidatePreparationScratch != nullptr) {
      if (activeCandidatePreparationRanges == nullptr ||
          index >= activeCandidatePreparationRanges->size()) {
        return;
      }
      const SparseCellGrid::CandidateWorkRange& range =
        (*activeCandidatePreparationRanges)[index];
      ILLUMO_PROFILE_ZONE("SparseWorkerPool.prepareRange");
      for (std::size_t scratchIndex = range.begin; scratchIndex < range.end;
           ++scratchIndex) {
        activeGrid->prepareCandidateScratchChunk(
          &(*activeCandidatePreparationScratch)[scratchIndex]);
      }
      continue;
    }
    if (activeResults == nullptr || activeTransitions == nullptr) {
      return;
    }
    if (activeCandidateScratch != nullptr && activeCandidateRanges != nullptr) {
      if (index >= activeCandidateRanges->size()) {
        return;
      }
      SparseCellGrid::CandidateWorkRange& range =
        (*activeCandidateRanges)[index];
      ILLUMO_PROFILE_ZONE("SparseWorkerPool.evaluateRange");
      range.outputChunkCount = 0u;
      for (std::size_t scratchIndex = range.begin; scratchIndex < range.end;
           ++scratchIndex) {
        activeGrid->evaluateCandidateChunk(
          (*activeCandidateScratch)[scratchIndex],
          activeTransitions,
          &(*activeResults)[scratchIndex],
          memoShardIndex);
        if ((*activeResults)[scratchIndex].hasNonBackground) {
          range.outputChunkCount += 1u;
        }
      }
      continue;
    }
    if (activeTargets == nullptr || index >= activeTargets->size()) {
      return;
    }
    activeGrid->evaluateTargetChunk((*activeTargets)[index],
                                    activeTransitions,
                                    &(*activeResults)[index],
                                    memoShardIndex);
  }
}

void
SparseWorkerPool::workerLoop(unsigned int memoShardIndex)
{
  ILLUMO_PROFILE_THREAD("SparseWorkerPool");
  std::size_t observedGeneration = 0u;
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(mutex);
      workReady.wait(lock, [this, &observedGeneration]() {
        return stopping || workGeneration != observedGeneration;
      });
      if (stopping) {
        return;
      }
      observedGeneration = workGeneration;
    }

    if (!claimWorkerSlot()) {
      continue;
    }
    ILLUMO_PROFILE_ZONE("SparseWorkerPool.workerShare");
    executeAvailableWork(memoShardIndex);

    {
      std::lock_guard<std::mutex> lock(mutex);
      completedWorkers += 1u;
      if (completedWorkers >= requiredWorkers) {
        workComplete.notify_one();
      }
    }
  }
}
