#pragma once

#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <cstdint>

// Game-owned editor cursor that composes its rendering from engine primitives.
class Cursor : public DrawableBase
{
public:
  Cursor();
  ~Cursor() override = default;

  Cursor(const Cursor&) = delete;
  Cursor& operator=(const Cursor&) = delete;
  Cursor(Cursor&&) = delete;
  Cursor& operator=(Cursor&&) = delete;

  void init(Renderer* renderer, IRenderWindow* window, Camera* camera);
  void setCellSize(float size);
  void setColor(ColorRgba color);
  void setWorldPosition(float worldX, float worldY);
  void setFromCell(std::int64_t cellX, std::int64_t cellY);
  void rebuild();

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

  GameVisual& getVisual() { return visual; }
  const GameVisual& getVisual() const { return visual; }

private:
  GameVisual visual;
  float cellSize = 16.0f;
  float worldX = 0.0f;
  float worldY = 0.0f;
  ColorRgba color{ 80, 220, 255, 220 };
  bool initialized = false;
};
