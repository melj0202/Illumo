#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct CellPatternCell
{
  std::int32_t dx = 0;
  std::int32_t dy = 0;
  unsigned char state = 0;
};

class CellPattern
{
public:
  static constexpr int kMaxWidth = 256;
  static constexpr int kMaxHeight = 256;
  static constexpr std::size_t kMaxStoredCells = 8192;

  CellPattern() = default;

  int getWidth() const { return width; }
  int getHeight() const { return height; }
  const std::vector<CellPatternCell>& getCells() const { return cells; }
  bool empty() const { return cells.empty(); }

  void clear();
  bool setExtent(int newWidth, int newHeight);
  bool addCell(std::int32_t dx, std::int32_t dy, unsigned char state);
  bool rotateCw();
  bool flipX();
  bool flipY();

private:
  int width = 0;
  int height = 0;
  std::vector<CellPatternCell> cells;
};
