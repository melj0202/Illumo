#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

class CanvasCoordinatePolicy
{
public:
  static constexpr double kCellSize = 16.0;
  // Leave room for int-sized viewport/cache spans, chunk alignment and halos.
  static constexpr std::int64_t kMaximumCell =
    std::numeric_limits<std::int64_t>::max() - (std::int64_t{ 1 } << 32) + 1;
  static constexpr double kMaximumWorld =
    static_cast<double>(kMaximumCell) * kCellSize;

  static bool validPosition(double x, double y)
  {
    return std::isfinite(x) && std::isfinite(y) &&
           std::abs(x) <= kMaximumWorld && std::abs(y) <= kMaximumWorld;
  }

  static bool tryWorldToCell(double world, std::int64_t* cell)
  {
    if (cell == nullptr || !validPosition(world, 0.0)) {
      return false;
    }
    *cell = static_cast<std::int64_t>(std::floor(world / kCellSize + 0.5));
    return true;
  }
};
