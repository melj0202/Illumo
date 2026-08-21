#include "CellPattern.h"

void
CellPattern::clear()
{
  width = 0;
  height = 0;
  cells.clear();
}

bool
CellPattern::setExtent(int newWidth, int newHeight)
{
  if (newWidth < 0 || newHeight < 0 || newWidth > kMaxWidth ||
      newHeight > kMaxHeight) {
    return false;
  }
  width = newWidth;
  height = newHeight;
  return true;
}

bool
CellPattern::addCell(std::int32_t dx, std::int32_t dy, unsigned char state)
{
  if (dx < 0 || dy < 0 || dx >= kMaxWidth || dy >= kMaxHeight) {
    return false;
  }
  if (cells.size() >= kMaxStoredCells) {
    return false;
  }
  if (dx >= width) {
    width = dx + 1;
  }
  if (dy >= height) {
    height = dy + 1;
  }
  if (width > kMaxWidth || height > kMaxHeight) {
    return false;
  }
  CellPatternCell cell;
  cell.dx = dx;
  cell.dy = dy;
  cell.state = state;
  cells.push_back(cell);
  return true;
}

bool
CellPattern::rotateCw()
{
  if (empty()) {
    return true;
  }
  const int oldWidth = width;
  const int oldHeight = height;
  for (std::size_t i = 0; i < cells.size(); ++i) {
    const std::int32_t dx = cells[i].dx;
    const std::int32_t dy = cells[i].dy;
    cells[i].dx = oldHeight - 1 - dy;
    cells[i].dy = dx;
  }
  width = oldHeight;
  height = oldWidth;
  return true;
}

bool
CellPattern::flipX()
{
  if (empty() || width <= 0) {
    return true;
  }
  for (std::size_t i = 0; i < cells.size(); ++i) {
    cells[i].dx = width - 1 - cells[i].dx;
  }
  return true;
}

bool
CellPattern::flipY()
{
  if (empty() || height <= 0) {
    return true;
  }
  for (std::size_t i = 0; i < cells.size(); ++i) {
    cells[i].dy = height - 1 - cells[i].dy;
  }
  return true;
}
