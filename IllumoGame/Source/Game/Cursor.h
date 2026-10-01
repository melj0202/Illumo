#pragma once

#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <cstdint>

// Game-owned editor cursor that composes its rendering from engine primitives.
// It hugs the hovered cell's LED key: a soft glow, a thin rounded rim, four
// viewfinder corner brackets that breathe, and a small centre cross. Moving
// to another cell, it glides there on a spring, stretching a little along
// its travel and landing with a small bounce while its brackets pop out and
// settle. The first placement after it was hidden snaps into place.
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
  // Targets for the glide, in world units (the cell's lower-left corner).
  void setWorldPosition(float worldX, float worldY);
  void setFromCell(std::int64_t cellX, std::int64_t cellY);
  // Advances the glide and the bracket breathing, then rebuilds.
  void tick(float deltaSeconds, bool reducedMotion);
  void rebuild();

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

  // The rim and centre cross; the breathing glow and brackets are drawn
  // behind them from breathVisual.
  GameVisual& getVisual() { return visual; }
  const GameVisual& getVisual() const { return visual; }
  // Where the cursor is drawn now, and where it is gliding to.
  float displayX() const { return glideX.value(); }
  float displayY() const { return glideY.value(); }
  float targetX() const { return glideX.target(); }
  float targetY() const { return glideY.target(); }

private:
  void retarget(float x, float y);

  // The glow and brackets breathe every frame, so they sit in their own
  // visual behind the rim and cross, which redraw only when their key does.
  GameVisual breathVisual;
  GameVisual visual;
  GuiDrawKey breathKey;
  GuiDrawKey rimKey;
  float cellSize = 16.0f;
  ColorRgba color{ 80, 220, 255, 220 };
  bool initialized = false;
  // The glide toward the hovered cell and the brackets' landing pop.
  GuiSpring glideX;
  GuiSpring glideY;
  GuiSpring landingPop;
  // A hidden cursor reappears where it is placed rather than gliding in.
  bool snapNext = true;
  bool reducedMotion = false;
  float breathPhase = 0.0f;
};
