#pragma once

#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <cstdint>

class GameVisual;

// A tiny Conway's Life world on a wrapped (toroidal) grid of at most 8x8
// cells, used as a living decoration in the menus. It steps on its own clock
// and crossfades between generations, so a glider visibly walks across it.
// Presentation only: it is not connected to the simulation domain.
class CellMotif
{
public:
  static constexpr int kMaximumSide = 8;
  static constexpr float kStepSeconds = 0.45f;

  CellMotif();

  // Replace the world. Bit (row * columns + column) marks a live cell.
  void reset(std::uint64_t cells, int columns, int rows);
  // A single glider placed at the given cell offset.
  void resetGlider(int columns, int rows, int column, int row);

  // Advance the clock; reduced motion freezes the current generation.
  void tick(float deltaSeconds, bool reducedMotion);

  // Draw at (x, y); cells are cellSize wide with `gap` between them. `glow`
  // (0..1) adds a soft halo behind live cells.
  void draw(GameVisual& visual,
            float x,
            float y,
            float cellSize,
            float gap,
            ColorRgba lit,
            ColorRgba dim,
            float glow,
            unsigned char opacity) const;

  float width(float cellSize, float gap) const;
  std::uint64_t cells() const { return m_current; }
  int columns() const { return m_columns; }
  int rows() const { return m_rows; }
  // Crossfade progress from the previous generation, 0..1.
  float blend() const;

  // One Life (B3/S23) generation on the torus, exposed for tests.
  static std::uint64_t step(std::uint64_t cells, int columns, int rows);

private:
  std::uint64_t m_current = 0;
  std::uint64_t m_previous = 0;
  int m_columns = 7;
  int m_rows = 7;
  float m_elapsed = 0.0f;
  bool m_reducedMotion = false;
};
