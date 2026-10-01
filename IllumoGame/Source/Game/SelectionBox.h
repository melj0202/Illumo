#pragma once

#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <cstdint>

// The editor's selection box, dressed like the hover cursor (Cursor): a soft
// breathing glow, a thin rounded rim over a faint gradient fill, and four
// viewfinder brackets at the corners that breathe and pop out. The box glides
// to each new rectangle on springs, so dragging a selection out trails the
// pointer smoothly, and a fresh selection pops in with its brackets flaring.
// The first placement after it was hidden snaps into place.
class SelectionBox : public DrawableBase
{
public:
  SelectionBox();
  ~SelectionBox() override = default;

  SelectionBox(const SelectionBox&) = delete;
  SelectionBox& operator=(const SelectionBox&) = delete;
  SelectionBox(SelectionBox&&) = delete;
  SelectionBox& operator=(SelectionBox&&) = delete;

  void init(Renderer* renderer, IRenderWindow* window, Camera* camera);
  void setCamera(Camera* camera);
  void setCellSize(float size);
  // Targets the box at the inclusive cell rectangle x0..x1, y0..y1.
  void setCells(std::int64_t x0,
                std::int64_t y0,
                std::int64_t x1,
                std::int64_t y1);
  // Hides the box; the next placement snaps instead of gliding.
  void hide();
  // Advances the glide, the pop and the breathing, then rebuilds.
  void tick(float deltaSeconds, bool reducedMotion);

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

  // Where the box is drawn now and where it is gliding to, in world units
  // (lower-left corner and size).
  float displayX() const { return glideX.value(); }
  float displayY() const { return glideY.value(); }
  float displayWidth() const { return glideWidth.value(); }
  float displayHeight() const { return glideHeight.value(); }
  float targetX() const { return glideX.target(); }
  float targetY() const { return glideY.target(); }
  float targetWidth() const { return glideWidth.target(); }
  float targetHeight() const { return glideHeight.target(); }

private:
  void rebuild();

  // The breathing glow and brackets sit behind the fill and rim, and redraw
  // only when their inputs change.
  GameVisual glowVisual;
  GameVisual rimVisual;
  GuiDrawKey glowKey;
  GuiDrawKey rimKey;
  float cellSize = 16.0f;
  bool initialized = false;
  GuiSpring glideX;
  GuiSpring glideY;
  GuiSpring glideWidth;
  GuiSpring glideHeight;
  // The brackets' pop when the selection appears, and the fade in.
  GuiSpring appearPop;
  GuiSpring reveal;
  bool snapNext = true;
  bool reducedMotion = false;
  float breathPhase = 0.0f;
};
