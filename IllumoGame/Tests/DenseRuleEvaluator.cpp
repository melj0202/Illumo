#include "DenseRuleEvaluator.h"
#include <algorithm>
#include <thread>
#include <tracy/Tracy.hpp>
#include <vector>

// Project convention: 0 = "alive" for neighbor counting (see historical
// !getCanvasPixel).
int
DenseRuleEvaluator::countAliveNeighbors(const unsigned char* grid,
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
DenseRuleEvaluator::countAliveNeighborsInterior(const unsigned char* grid,
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
DenseRuleEvaluator::resolveWorkerCount(int width, int height) const
{
  const int cells = width * height;
  if (workerOverride > 0) {
    return std::min(workerOverride, height);
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
DenseRuleEvaluator::evalRows(const unsigned char* src,
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
        const unsigned char next =
          transitions[RuleSet::transitionIndex(src[i], n)];
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
        const unsigned char next =
          transitions[RuleSet::transitionIndex(src[i], n)];
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
        const unsigned char next =
          transitions[RuleSet::transitionIndex(src[i], n)];
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
        const unsigned char next =
          transitions[RuleSet::transitionIndex(src[i], n)];
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
DenseRuleEvaluator::calcGeneration(const int& x_start,
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
  if (rules.getNeighborhoodKind() == RuleSet::NeighborhoodKind::Elementary1D) {
    int sourceY = -1;
    int minX = width;
    int maxX = -1;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        if (src[static_cast<size_t>(y) * width + x] == 0) {
          if (sourceY != y) {
            sourceY = y;
            minX = x;
            maxX = x;
          } else {
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
          }
        }
      }
    }
    if (sourceY < 0) {
      return;
    }
    std::copy_n(src, static_cast<size_t>(width) * height, dst);
    const int destY = (sourceY + 1) % height;
    bool changed = false;
    for (int x = minX - 1; x <= maxX + 1; ++x) {
      const int center = (x + width) % width;
      const size_t row = static_cast<size_t>(sourceY) * width;
      const unsigned char next =
        rules.nextElementary(src[row + (center + width - 1) % width],
                             src[row + center],
                             src[row + (center + 1) % width]);
      const size_t index = static_cast<size_t>(destY) * width + center;
      changed = changed || next != src[index];
      dst[index] = next;
    }
    if (changed) {
      canvas->swapLifeBuffers();
      canvas->markCellsDirtyRegion(0, destY, width - 1, destY);
    }
    return;
  }
  if (rules.getNeighborhoodKind() == RuleSet::NeighborhoodKind::ExtendedRange) {
    const int radius = static_cast<int>(rules.getNeighborhoodRadius());
    const bool includeCenter = rules.includesCenterInNeighborCount();
    const RuleSet::ExtendedNeighborhoodShape shape =
      rules.getExtendedNeighborhoodShape();
    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;
    bool anyChange = false;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const std::size_t index =
          static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x);
        const unsigned char countedState =
          rules.getExtendedCountedState(src[index]);
        unsigned int aliveCount = 0u;
        for (int offsetY = -radius; offsetY <= radius; ++offsetY) {
          for (int offsetX = -radius; offsetX <= radius; ++offsetX) {
            if ((!includeCenter && offsetX == 0 && offsetY == 0) ||
                !RuleSet::extendedNeighborhoodContains(
                  shape, radius, offsetX, offsetY)) {
              continue;
            }
            const int neighborX = ((x + offsetX) % width + width) % width;
            const int neighborY = ((y + offsetY) % height + height) % height;
            const std::size_t neighborIndex =
              static_cast<std::size_t>(neighborY) *
                static_cast<std::size_t>(width) +
              static_cast<std::size_t>(neighborX);
            if (src[neighborIndex] == countedState) {
              aliveCount += 1u;
            }
          }
        }
        const unsigned char next =
          rules.nextStateFromExtendedCount(src[index], aliveCount);
        dst[index] = next;
        if (next != src[index]) {
          anyChange = true;
          minX = std::min(minX, x);
          minY = std::min(minY, y);
          maxX = std::max(maxX, x);
          maxY = std::max(maxY, y);
        }
      }
    }
    if (anyChange) {
      canvas->swapLifeBuffers();
      canvas->markCellsDirtyRegion(minX, minY, maxX, maxY);
    }
    return;
  }
  if (rules.getNeighborhoodKind() ==
      RuleSet::NeighborhoodKind::VonNeumannDirectional) {
    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;
    bool anyChange = false;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const int northY = (y - 1 + height) % height;
        const int eastX = (x + 1) % width;
        const int southY = (y + 1) % height;
        const int westX = (x - 1 + width) % width;
        const RuleSet::DirectionalNeighbors neighbors = {
          src[static_cast<std::size_t>(northY) *
                static_cast<std::size_t>(width) +
              static_cast<std::size_t>(x)],
          src[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
              static_cast<std::size_t>(eastX)],
          src[static_cast<std::size_t>(southY) *
                static_cast<std::size_t>(width) +
              static_cast<std::size_t>(x)],
          src[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
              static_cast<std::size_t>(westX)]
        };
        const std::size_t index =
          static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x);
        const unsigned char next =
          rules.nextStateFromDirectionalNeighborhood(src[index], neighbors);
        dst[index] = next;
        if (next != src[index]) {
          anyChange = true;
          minX = std::min(minX, x);
          minY = std::min(minY, y);
          maxX = std::max(maxX, x);
          maxY = std::max(maxY, y);
        }
      }
    }
    if (anyChange) {
      canvas->swapLifeBuffers();
      canvas->markCellsDirtyRegion(minX, minY, maxX, maxY);
    }
    return;
  }
  if (rules.getNeighborhoodKind() ==
      RuleSet::NeighborhoodKind::MooreStateCounts) {
    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;
    bool anyChange = false;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        RuleSet::NeighborStateCounts counts{};
        for (int offsetY = -1; offsetY <= 1; ++offsetY) {
          for (int offsetX = -1; offsetX <= 1; ++offsetX) {
            if (offsetX == 0 && offsetY == 0) {
              continue;
            }
            const int neighborX = (x + offsetX + width) % width;
            const int neighborY = (y + offsetY + height) % height;
            const std::size_t neighborIndex =
              static_cast<std::size_t>(neighborY) *
                static_cast<std::size_t>(width) +
              static_cast<std::size_t>(neighborX);
            counts[src[neighborIndex]] += 1u;
          }
        }
        const std::size_t index =
          static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(x);
        const unsigned char next =
          rules.nextStateFromNeighborhood(src[index], counts);
        dst[index] = next;
        if (next != src[index]) {
          anyChange = true;
          minX = std::min(minX, x);
          minY = std::min(minY, y);
          maxX = std::max(maxX, x);
          maxY = std::max(maxY, y);
        }
      }
    }
    if (anyChange) {
      canvas->swapLifeBuffers();
      canvas->markCellsDirtyRegion(minX, minY, maxX, maxY);
    }
    return;
  }
  const unsigned char* transitions = rules.getTransitionTable().data();

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
      std::vector<std::jthread> threads;
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

      for (std::jthread& t : threads) {
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
