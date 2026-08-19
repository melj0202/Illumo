#include "Cursor.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>

Cursor::Cursor()
{
  visual.setSpace(PrimitiveSpace::World);
  visual.setLayerHint(RenderLayerId::UI);
}

void
Cursor::init(Renderer* rend, IRenderWindow* win, Camera* cam)
{
  visual.setRenderer(rend);
  visual.setWindow(win);
  visual.setCamera(cam);
  visual.setSpace(PrimitiveSpace::World);
  visual.setLayerHint(RenderLayerId::UI);
  if (rend) {
    visual.prepare(rend);
  }
  initialized = true;
  rebuild();
}

void
Cursor::setCellSize(float size)
{
  if (size > 0.0f && cellSize != size) {
    cellSize = size;
    rebuild();
  }
}

void
Cursor::setColor(ColorRgba c)
{
  color = c;
  rebuild();
}

void
Cursor::setWorldPosition(float x, float y)
{
  worldX = x;
  worldY = y;
  rebuild();
}

void
Cursor::setFromCell(std::int64_t cellX, std::int64_t cellY)
{
  worldX = static_cast<float>(cellX) * cellSize - cellSize * 0.5f;
  worldY = static_cast<float>(cellY) * cellSize - cellSize * 0.5f;
  rebuild();
}

void
Cursor::rebuild()
{
  visual.clearPrimitives();
  if (!initialized) {
    return;
  }

  // Cell outline + crosshair inside the cell.
  visual.addOutlineRect(worldX, worldY, cellSize, cellSize, color, 2.0f);
  const float midX = worldX + cellSize * 0.5f;
  const float midY = worldY + cellSize * 0.5f;
  const float arm = cellSize * 0.35f;
  visual.addLine(midX - arm, midY, midX + arm, midY, color, 1.5f);
  visual.addLine(midX, midY - arm, midX, midY + arm, color, 1.5f);
}

bool
Cursor::AppendCommands(Renderer* renderer)
{
  visual.setVisible(isVisible());
  return visual.AppendCommands(renderer);
}
