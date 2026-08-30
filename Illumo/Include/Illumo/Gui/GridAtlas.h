#pragma once

#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>

// Grid-based texture atlas coordinate calculator.
// Maps discrete column/row indices to normalized UV TextureRegion coordinates.
class GridAtlas
{
public:
  GridAtlas(unsigned int columns = 1,
            unsigned int rows = 1,
            float textureWidth = 0.0f,
            float textureHeight = 0.0f);

  unsigned int columns() const { return m_columns; }
  unsigned int rows() const { return m_rows; }
  float textureWidth() const { return m_textureWidth; }
  float textureHeight() const { return m_textureHeight; }

  TextureRegion regionForCell(unsigned int column,
                              unsigned int row,
                              bool flipV = false) const;

  static TextureRegion computeCellRegion(unsigned int column,
                                         unsigned int row,
                                         unsigned int totalColumns,
                                         unsigned int totalRows,
                                         bool flipV = false);

private:
  unsigned int m_columns;
  unsigned int m_rows;
  float m_textureWidth;
  float m_textureHeight;
};
