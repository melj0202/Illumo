#include "RuleSet.h"
#include "Game/CellGrid.h"
#include <thread>
#include <tracy/Tracy.hpp>
#include <vector>

RuleSet::RuleSet(CellGrid* targetCanvas)
  : canvas(targetCanvas)
{
}

int RuleSet::workerOverride = 0;

const RuleSet::TransitionTable&
RuleSet::getTransitionTable() const
{
  if (transitionTableReady) {
    return transitionTable;
  }
  ZoneScopedN("Rule.buildTransitionTable");
  for (std::size_t state = 0u; state < kCellStateCount; ++state) {
    for (std::size_t neighbors = 0u; neighbors < kNeighborCountCount;
         ++neighbors) {
      transitionTable[state * kNeighborCountCount + neighbors] =
        nextState(static_cast<unsigned char>(state),
                  static_cast<unsigned char>(neighbors));
    }
  }
  transitionTableReady = true;
  return transitionTable;
}

void
RuleSet::setWorkerOverride(int workers)
{
  if (workers < 0) {
    workers = 0;
  }
  workerOverride = workers;
}

int
RuleSet::getWorkerOverride()
{
  return workerOverride;
}

// Project convention: 0 = "alive" for neighbor counting (see historical
// !getCanvasPixel).
int
RuleSet::countAliveNeighbors(const unsigned char* grid,
                             int w,
                             int h,
                             int x,
                             int y)
{
  const int xm = (x > 0) ? (x - 1) : (w - 1);
  const int xp = (x + 1 < w) ? (x + 1) : 0;
  const int ym = (y > 0) ? (y - 1) : (h - 1);
  const int yp = (y + 1 < h) ? (y + 1) : 0;

  const unsigned char* rowm =
    grid + static_cast<size_t>(ym) * static_cast<size_t>(w);
  const unsigned char* row =
    grid + static_cast<size_t>(y) * static_cast<size_t>(w);
  const unsigned char* rowp =
    grid + static_cast<size_t>(yp) * static_cast<size_t>(w);

  int count = 0;
  count += (rowm[xm] == 0);
  count += (rowm[x] == 0);
  count += (rowm[xp] == 0);
  count += (row[xm] == 0);
  count += (row[xp] == 0);
  count += (rowp[xm] == 0);
  count += (rowp[x] == 0);
  count += (rowp[xp] == 0);
  return count;
}

int
RuleSet::countAliveNeighborsInterior(const unsigned char* grid,
                                     int w,
                                     int x,
                                     int y)
{
  const unsigned char* rowm =
    grid + static_cast<size_t>(y - 1) * static_cast<size_t>(w);
  const unsigned char* row =
    grid + static_cast<size_t>(y) * static_cast<size_t>(w);
  const unsigned char* rowp =
    grid + static_cast<size_t>(y + 1) * static_cast<size_t>(w);
  const int xm = x - 1;
  const int xp = x + 1;

  int count = 0;
  count += (rowm[xm] == 0);
  count += (rowm[x] == 0);
  count += (rowm[xp] == 0);
  count += (row[xm] == 0);
  count += (row[xp] == 0);
  count += (rowp[xm] == 0);
  count += (rowp[x] == 0);
  count += (rowp[xp] == 0);
  return count;
}

int
RuleSet::resolveWorkerCount(int width, int height) const
{
  const int cells = width * height;
  if (workerOverride > 0) {
    return workerOverride;
  }
  if (cells < kParallelCellThreshold) {
    return 1;
  }
  unsigned int hw = std::thread::hardware_concurrency();
  if (hw < 2) {
    return 1;
  }
  int workers = static_cast<int>(hw);
  if (workers > 8) {
    workers = 8;
  }
  if (workers > height) {
    workers = height;
  }
  if (workers < 1) {
    workers = 1;
  }
  return workers;
}

void
RuleSet::evalRows(const unsigned char* src,
                  unsigned char* dst,
                  const unsigned char* transitions,
                  int width,
                  int height,
                  int yBegin,
                  int yEnd,
                  int* outMinX,
                  int* outMinY,
                  int* outMaxX,
                  int* outMaxY,
                  bool* outAnyChange) const
{
  if (transitions == nullptr) {
    return;
  }
  int minX = width;
  int minY = height;
  int maxX = -1;
  int maxY = -1;
  bool anyChange = false;

  // Grids smaller than 3×3 have no true interior; use toroidal path for all.
  const bool hasInterior = (width >= 3 && height >= 3);

  for (int y = yBegin; y < yEnd; ++y) {
    const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(width);
    const bool edgeRow = (!hasInterior) || (y == 0) || (y == height - 1);

    if (edgeRow) {
      for (int x = 0; x < width; ++x) {
        const size_t i = rowBase + static_cast<size_t>(x);
        const unsigned char n = static_cast<unsigned char>(
          countAliveNeighbors(src, width, height, x, y));
        const unsigned char next = transitions[transitionIndex(src[i], n)];
        dst[i] = next;
        if (next != src[i]) {
          anyChange = true;
          if (x < minX) {
            minX = x;
          }
          if (y < minY) {
            minY = y;
          }
          if (x > maxX) {
            maxX = x;
          }
          if (y > maxY) {
            maxY = y;
          }
        }
      }
    } else {
      // Left edge (toroidal).
      {
        const int x = 0;
        const size_t i = rowBase;
        const unsigned char n = static_cast<unsigned char>(
          countAliveNeighbors(src, width, height, x, y));
        const unsigned char next = transitions[transitionIndex(src[i], n)];
        dst[i] = next;
        if (next != src[i]) {
          anyChange = true;
          if (x < minX) {
            minX = x;
          }
          if (y < minY) {
            minY = y;
          }
          if (x > maxX) {
            maxX = x;
          }
          if (y > maxY) {
            maxY = y;
          }
        }
      }
      // Interior (no wrap).
      for (int x = 1; x < width - 1; ++x) {
        const size_t i = rowBase + static_cast<size_t>(x);
        const unsigned char n = static_cast<unsigned char>(
          countAliveNeighborsInterior(src, width, x, y));
        const unsigned char next = transitions[transitionIndex(src[i], n)];
        dst[i] = next;
        if (next != src[i]) {
          anyChange = true;
          if (x < minX) {
            minX = x;
          }
          if (y < minY) {
            minY = y;
          }
          if (x > maxX) {
            maxX = x;
          }
          if (y > maxY) {
            maxY = y;
          }
        }
      }
      // Right edge (toroidal).
      {
        const int x = width - 1;
        const size_t i = rowBase + static_cast<size_t>(x);
        const unsigned char n = static_cast<unsigned char>(
          countAliveNeighbors(src, width, height, x, y));
        const unsigned char next = transitions[transitionIndex(src[i], n)];
        dst[i] = next;
        if (next != src[i]) {
          anyChange = true;
          if (x < minX) {
            minX = x;
          }
          if (y < minY) {
            minY = y;
          }
          if (x > maxX) {
            maxX = x;
          }
          if (y > maxY) {
            maxY = y;
          }
        }
      }
    }
  }

  *outMinX = minX;
  *outMinY = minY;
  *outMaxX = maxX;
  *outMaxY = maxY;
  *outAnyChange = anyChange;
}

void
RuleSet::calcGeneration(const int& x_start,
                        const int& y_start,
                        const int& x_end,
                        const int& y_end) const
{
  ZoneScopedN("Rule.calcGeneration");
  if (!canvas || !canvas->lifeCanvas || !canvas->getLifeBackBuffer()) {
    return;
  }

  // Generation is always computed for the full lifeCanvas (toroidal wrap).
  (void)x_start;
  (void)y_start;
  (void)x_end;
  (void)y_end;

  const int width = canvas->canvasWidth;
  const int height = canvas->canvasHeight;
  if (width <= 0 || height <= 0) {
    return;
  }

  const unsigned char* src = canvas->lifeCanvas;
  unsigned char* dst = canvas->getLifeBackBuffer();
  const unsigned char* transitions = getTransitionTable().data();

  int minX = width;
  int minY = height;
  int maxX = -1;
  int maxY = -1;
  bool anyChange = false;

  const int workers = resolveWorkerCount(width, height);

  {
    ZoneScopedN("Rule.evalPass");
    if (workers <= 1) {
      evalRows(src,
               dst,
               transitions,
               width,
               height,
               0,
               height,
               &minX,
               &minY,
               &maxX,
               &maxY,
               &anyChange);
    } else {
      // Row-parallel eval: each worker writes a disjoint y-range into dst.
      // Threads are created per generation (simple, correct). Auto path only
      // engages above kParallelCellThreshold so spawn cost is amortized (D-P7).
      struct WorkerResult
      {
        int minX;
        int minY;
        int maxX;
        int maxY;
        bool anyChange;
      };
      std::vector<WorkerResult> results(static_cast<size_t>(workers));
      std::vector<std::thread> threads;
      threads.reserve(static_cast<size_t>(workers - 1));

      const int baseRows = height / workers;
      const int extra = height % workers;
      int yCursor = 0;

      for (int w = 0; w < workers; ++w) {
        const int rows = baseRows + (w < extra ? 1 : 0);
        const int yBegin = yCursor;
        const int yEnd = yBegin + rows;
        yCursor = yEnd;

        WorkerResult* result = &results[static_cast<size_t>(w)];
        if (w == workers - 1) {
          evalRows(src,
                   dst,
                   transitions,
                   width,
                   height,
                   yBegin,
                   yEnd,
                   &result->minX,
                   &result->minY,
                   &result->maxX,
                   &result->maxY,
                   &result->anyChange);
        } else {
          threads.emplace_back([this,
                                src,
                                dst,
                                transitions,
                                width,
                                height,
                                yBegin,
                                yEnd,
                                result]() {
            evalRows(src,
                     dst,
                     transitions,
                     width,
                     height,
                     yBegin,
                     yEnd,
                     &result->minX,
                     &result->minY,
                     &result->maxX,
                     &result->maxY,
                     &result->anyChange);
          });
        }
      }

      for (std::thread& t : threads) {
        t.join();
      }

      for (int w = 0; w < workers; ++w) {
        const WorkerResult& r = results[static_cast<size_t>(w)];
        if (!r.anyChange) {
          continue;
        }
        anyChange = true;
        if (r.minX < minX) {
          minX = r.minX;
        }
        if (r.minY < minY) {
          minY = r.minY;
        }
        if (r.maxX > maxX) {
          maxX = r.maxX;
        }
        if (r.maxY > maxY) {
          maxY = r.maxY;
        }
      }
    }
  }

  {
    ZoneScopedN("Rule.writeBack");
    if (anyChange) {
      // Promote back buffer to front (O(1)); no full-grid memcpy (D-P5).
      canvas->swapLifeBuffers();
      canvas->markCellsDirtyRegion(minX, minY, maxX, maxY);
    }
  }
}
