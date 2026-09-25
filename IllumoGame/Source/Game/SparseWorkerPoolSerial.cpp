#include "SparseWorkerPool.h"

// An isolated worker instance owns its grid. These kernels run inside that
// guest; no host thread receives pointers into it or evaluates cells for it.
SparseWorkerPool::SparseWorkerPool() = default;
SparseWorkerPool::~SparseWorkerPool() = default;

void
SparseWorkerPool::evaluate(const SparseCellGrid* grid,
                           const unsigned char* transitions,
                           const std::vector<ChunkAddress>* targets,
                           std::vector<SparseCellGrid::TargetResult>* results,
                           unsigned int)
{
  for (std::size_t index = 0; index < targets->size(); ++index) {
    grid->evaluateTargetChunk(
      (*targets)[index], transitions, &(*results)[index], 0u);
  }
}

void
SparseWorkerPool::evaluateCandidates(
  const SparseCellGrid* grid,
  const unsigned char* transitions,
  const std::vector<SparseCellGrid::CandidateScratchChunk>* scratch,
  std::vector<SparseCellGrid::CandidateWorkRange>* ranges,
  std::vector<SparseCellGrid::TargetResult>* results,
  unsigned int)
{
  for (SparseCellGrid::CandidateWorkRange& range : *ranges) {
    range.outputChunkCount = 0u;
    for (std::size_t index = range.begin; index < range.end; ++index) {
      grid->evaluateCandidateChunk(
        (*scratch)[index], transitions, &(*results)[index], 0u);
      if ((*results)[index].hasNonBackground) {
        ++range.outputChunkCount;
      }
    }
  }
}

void
SparseWorkerPool::prepareCandidates(
  const SparseCellGrid* grid,
  std::vector<SparseCellGrid::CandidateScratchChunk>* scratch,
  const std::vector<SparseCellGrid::CandidateWorkRange>* ranges,
  unsigned int)
{
  for (const SparseCellGrid::CandidateWorkRange& range : *ranges) {
    for (std::size_t index = range.begin; index < range.end; ++index) {
      grid->prepareCandidateScratchChunk(&(*scratch)[index]);
    }
  }
}

void
SparseWorkerPool::run(std::size_t itemCount, unsigned int, const Job& job)
{
  for (std::size_t index = 0u; index < itemCount; ++index) {
    job(index, 0u);
  }
}
