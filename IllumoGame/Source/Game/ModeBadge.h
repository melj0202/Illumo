#pragma once

#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <string>

class IRenderWindow;
class Renderer;

// The simulator's corner mode badge (EDIT / NORMAL): a glass pill that
// mirrors the settings button in the opposite corner. When the mode changes
// it drops in from above as a round bead, stretches into a pill on a spring
// while its label fades in and a sheen sweeps across, holds while its status
// dot glows (breathing while the simulation runs), then melts back into the
// bead and floats away. A mode change while it shows keeps the motion going
// and makes the pill wobble instead of restarting it. Presentation only.
class ModeBadge
{
public:
  static constexpr float kMargin = 12.0f;
  static constexpr float kHeight = 32.0f;
  static constexpr float kHoldSeconds = 1.6f;
  static constexpr float kLabelSize = 13.0f;

  ModeBadge();

  void prepare(IRenderWindow* window, Renderer* renderer);
  // `live` makes the status dot breathe (the simulation is running).
  void show(const std::string& label, ColorRgba accent, bool live);
  void hide();
  // Advances the springs and rebuilds the visual.
  void tick(float deltaSeconds, bool reducedMotion);

  GameVisual& getVisual() { return m_visual; }
  bool isVisible() const { return m_visual.isVisible(); }
  const std::string& label() const { return m_label; }
  // Pill width in virtual pixels as last drawn (the bead is kHeight wide).
  float width() const { return m_drawnWidth; }
  float top() const { return m_drawnTop; }

private:
  void rebuild();
  float fullWidth() const;

  GameVisual m_visual;
  std::string m_label;
  ColorRgba m_accent{ 255, 255, 255, 255 };
  ColorRgba m_previousAccent{ 255, 255, 255, 255 };
  float m_colorBlend = 1.0f;
  bool m_live = false;
  bool m_reducedMotion = false;
  // Stretch from bead (0) to pill (1), and drop from above (0) into place (1).
  GuiSpring m_grow;
  GuiSpring m_drop;
  float m_elapsed = 0.0f;
  float m_ambient = 0.0f;
  bool m_showing = false;
  float m_drawnWidth = 0.0f;
  float m_drawnTop = 0.0f;
};
