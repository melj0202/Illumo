#include "MenuMotifs.h"

#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <algorithm>
#include <cmath>

static bool
cellAlive(std::uint64_t cells, int columns, int column, int row)
{
  const int bit = row * columns + column;
  return ((cells >> static_cast<unsigned int>(bit)) & 1u) != 0u;
}

CellMotif::CellMotif()
{
  resetGlider(7, 7, 1, 1);
}

void
CellMotif::reset(std::uint64_t cells, int columns, int rows)
{
  m_columns = std::clamp(columns, 1, kMaximumSide);
  m_rows = std::clamp(rows, 1, kMaximumSide);
  const int bits = m_columns * m_rows;
  const std::uint64_t mask =
    bits >= 64 ? ~std::uint64_t{ 0 }
               : (std::uint64_t{ 1 } << static_cast<unsigned int>(bits)) - 1u;
  m_current = cells & mask;
  m_previous = m_current;
  m_elapsed = 0.0f;
}

void
CellMotif::resetGlider(int columns, int rows, int column, int row)
{
  const int width = std::clamp(columns, 3, kMaximumSide);
  const int height = std::clamp(rows, 3, kMaximumSide);
  // The classic south-east glider: .o. / ..o / ooo
  const int offsets[5][2] = {
    { 1, 0 }, { 2, 1 }, { 0, 2 }, { 1, 2 }, { 2, 2 }
  };
  std::uint64_t cells = 0;
  for (const int* offset : offsets) {
    const int cellColumn = (column + offset[0]) % width;
    const int cellRow = (row + offset[1]) % height;
    cells |= std::uint64_t{ 1 }
             << static_cast<unsigned int>(cellRow * width + cellColumn);
  }
  reset(cells, width, height);
}

std::uint64_t
CellMotif::step(std::uint64_t cells, int columns, int rows)
{
  std::uint64_t next = 0;
  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < columns; ++column) {
      int neighbors = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          const int wrappedColumn = (column + dx + columns) % columns;
          const int wrappedRow = (row + dy + rows) % rows;
          if (cellAlive(cells, columns, wrappedColumn, wrappedRow)) {
            ++neighbors;
          }
        }
      }
      const bool alive = cellAlive(cells, columns, column, row);
      if (neighbors == 3 || (alive && neighbors == 2)) {
        next |= std::uint64_t{ 1 }
                << static_cast<unsigned int>(row * columns + column);
      }
    }
  }
  return next;
}

void
CellMotif::tick(float deltaSeconds, bool reducedMotion)
{
  m_reducedMotion = reducedMotion;
  if (reducedMotion) {
    // A frozen motif shows one settled generation.
    m_previous = m_current;
    m_elapsed = 0.0f;
    return;
  }
  if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
    return;
  }
  m_elapsed += std::min(deltaSeconds, 0.25f);
  while (m_elapsed >= kStepSeconds) {
    m_elapsed -= kStepSeconds;
    m_previous = m_current;
    m_current = step(m_current, m_columns, m_rows);
    if (m_current == 0u) {
      // A dead world restarts as a fresh glider.
      resetGlider(m_columns, m_rows, 1, 1);
      return;
    }
  }
}

float
CellMotif::blend() const
{
  if (m_reducedMotion) {
    return 1.0f;
  }
  // Settle quickly into each generation, then hold until the next step.
  return GuiEasing::outCubic(m_elapsed / (kStepSeconds * 0.55f));
}

float
CellMotif::width(float cellSize, float gap) const
{
  return static_cast<float>(m_columns) * cellSize +
         static_cast<float>(m_columns - 1) * gap;
}

void
CellMotif::draw(GameVisual& visual,
                float x,
                float y,
                float cellSize,
                float gap,
                ColorRgba lit,
                ColorRgba dim,
                float glow,
                unsigned char opacity) const
{
  const float mix = blend();
  const float halo = std::clamp(glow, 0.0f, 1.0f);
  // Three passes (dormant cells, halos, live cells) keep every halo above
  // the dormant grid and below every live cell.
  for (int pass = 0; pass < 3; ++pass) {
    if (pass == 1 && halo <= 0.0f) {
      continue;
    }
    for (int row = 0; row < m_rows; ++row) {
      for (int column = 0; column < m_columns; ++column) {
        const float px = x + static_cast<float>(column) * (cellSize + gap);
        const float py = y + static_cast<float>(row) * (cellSize + gap);
        if (pass == 0) {
          visual.addFilledRect(
            px, py, cellSize, cellSize, UiTheme::applyOpacity(dim, opacity));
          continue;
        }
        const float was =
          cellAlive(m_previous, m_columns, column, row) ? 1.0f : 0.0f;
        const float now =
          cellAlive(m_current, m_columns, column, row) ? 1.0f : 0.0f;
        const float intensity = was + (now - was) * mix;
        if (intensity <= 0.01f) {
          continue;
        }
        if (pass == 1) {
          const float spread = cellSize * 0.5f;
          visual.addFilledRect(
            px - spread,
            py - spread,
            cellSize + spread * 2.0f,
            cellSize + spread * 2.0f,
            UiTheme::applyOpacity(UiTheme::fade(lit, 0.14f * halo * intensity),
                                  opacity));
          continue;
        }
        // Newborn cells pop in from a smaller square; dying cells shrink.
        const float size = cellSize * (0.55f + 0.45f * intensity);
        const float inset = (cellSize - size) * 0.5f;
        visual.addFilledRect(
          px + inset,
          py + inset,
          size,
          size,
          UiTheme::applyOpacity(UiTheme::fade(lit, intensity), opacity));
      }
    }
  }
}
