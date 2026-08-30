#include <Illumo/Gui/GridAtlas.h>
#include <algorithm>

GridAtlas::GridAtlas(unsigned int columns,
                     unsigned int rows,
                     float textureWidth,
                     float textureHeight)
  : m_columns(std::max(1u, columns))
  , m_rows(std::max(1u, rows))
  , m_textureWidth(textureWidth)
  , m_textureHeight(textureHeight)
{
}

TextureRegion
GridAtlas::regionForCell(unsigned int column,
                         unsigned int row,
                         bool flipV) const
{
  return computeCellRegion(column, row, m_columns, m_rows, flipV);
}

TextureRegion
GridAtlas::computeCellRegion(unsigned int column,
                             unsigned int row,
                             unsigned int totalColumns,
                             unsigned int totalRows,
                             bool flipV)
{
  const unsigned int cols = std::max(1u, totalColumns);
  const unsigned int rws = std::max(1u, totalRows);
  const unsigned int clampedCol = std::min(column, cols - 1);
  const unsigned int clampedRow = std::min(row, rws - 1);

  const float uStep = 1.0f / static_cast<float>(cols);
  const float vStep = 1.0f / static_cast<float>(rws);

  const float u0 = static_cast<float>(clampedCol) * uStep;
  const float u1 = u0 + uStep;
  const float v0 = static_cast<float>(clampedRow) * vStep;
  const float v1 = v0 + vStep;

  TextureRegion region;
  region.u0 = u0;
  region.u1 = u1;
  if (flipV) {
    region.v0 = v1;
    region.v1 = v0;
  } else {
    region.v0 = v0;
    region.v1 = v1;
  }
  return region;
}
