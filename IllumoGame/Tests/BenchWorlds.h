#pragma once

#include "Game/SparseCellGrid.h"

#include <cstdint>
#include <random>

// Deterministic Game of Life benchmark worlds, shared by the native runner
// benchmark and the WASM package benchmark so both measure identical cells.
// The package benchmark seeds natively and hands the guest a v4 save, so the
// standard library's distribution details never differ between the two.
struct BenchWorld
{
  const char* name;
  int chunksX;
  int chunksY;
  int densityPercent;
  std::uint32_t seed;
};

inline constexpr BenchWorld kBenchWorlds[] = {
  { "bench-dense32", 32, 32, 35, 11u },
  { "bench-dense64", 64, 64, 35, 12u },
  { "bench-sparse128", 128, 128, 4, 13u },
};

// Live cells use state 0 (binary rules encode 0 as alive), centred on the
// origin so the default camera sees the middle of the world.
inline void
seedBenchWorld(SparseCellGrid& grid, const BenchWorld& world)
{
  std::mt19937 generator(world.seed);
  std::uniform_int_distribution<int> percent(0, 99);
  const std::int64_t width = static_cast<std::int64_t>(world.chunksX) * 16;
  const std::int64_t height = static_cast<std::int64_t>(world.chunksY) * 16;
  for (std::int64_t y = 0; y < height; ++y) {
    for (std::int64_t x = 0; x < width; ++x) {
      if (percent(generator) < world.densityPercent) {
        grid.setCell(CellAddress{ x - width / 2, y - height / 2 }, 0);
      }
    }
  }
}
