#pragma once

#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <array>
#include <string>

class IRenderWindow;
class Renderer;

// The glyph a canvas corner button wears: the settings menu's three bars, a
// glider on a 3x3 neighborhood for the Ruleset Workshop, or the mode the
// mode button switches to (a pencil for EDIT, a play triangle for NORMAL).
enum class CanvasChromeIcon
{
  Settings,
  Rules,
  Edit,
  Run
};

// One of the canvas's top-right icon buttons, in the CanvasChromeStyle look:
// an opaque tile that pops in like a bead whenever it reappears, swells on
// hover with a jelly overshoot, lights its glyph in a cascade (one spring per
// bar or glider row), and springs out a glass hint naming its action and key.
// A slow clock breathes its halo. The owner decides placement, hit testing
// and what a click does; the button draws and animates.
class CanvasChromeButton
{
public:
  CanvasChromeButton(CanvasChromeIcon icon, std::string label, std::string key);

  CanvasChromeButton(const CanvasChromeButton&) = delete;
  CanvasChromeButton& operator=(const CanvasChromeButton&) = delete;

  void prepare(IRenderWindow* window, Renderer* renderer);
  // Places the button at (x, y) in UI-scaled pixels and runs one frame of its
  // motion. `pointerX`/`pointerY` are in the same space; `mouseDown` is the
  // held left button. The first frame after hide() pops the button back in.
  void update(double dt,
              float x,
              float y,
              float size,
              float pointerX,
              float pointerY,
              bool mouseDown,
              bool reducedMotion);
  // Hides the button and arms its pop-in for when it next updates.
  void hide();
  // Swaps the glyph and the hint's action. A new glyph springs in from
  // nothing while the tile dips and boings back.
  void setIcon(CanvasChromeIcon icon, std::string label);
  // A click's feedback: a liquid splash leaps from the tile and the tile dips
  // and boings back.
  void pulse();
  CanvasChromeIcon icon() const { return m_icon; }

  bool isVisible() const { return m_tile.isVisible(); }
  bool isHovered() const { return m_hovered; }
  bool containsPoint(float x, float y) const;
  float x() const { return m_x; }
  float y() const { return m_y; }
  float size() const { return m_size; }
  // The idle clock, in seconds, shared by chrome that breathes with it.
  double clock() const { return m_clock; }
  // Back to front: the shadow and breathing halo, then the tile, glyph and
  // hint. The halo animates every frame, so it redraws alone; the tile layer
  // redraws only when its inputs change.
  GameVisual& getHaloVisual() { return m_halo; }
  GameVisual& getVisual() { return m_tile; }

private:
  void drawGlyph(float centerX,
                 float centerY,
                 float grow,
                 float squash,
                 float hover,
                 bool pressed,
                 const std::array<float, 3>& rowGrowth);
  void drawHint(float tipSpring, float tipShown, float tipSquash);

  CanvasChromeIcon m_icon;
  std::string m_label;
  std::string m_key;
  GameVisual m_halo;
  GameVisual m_tile;
  GuiDrawKey m_haloKey;
  GuiDrawKey m_tileKey;
  float m_x = 0.0f;
  float m_y = 0.0f;
  float m_size = 32.0f;
  bool m_hovered = false;
  bool m_shown = false;
  GuiSpring m_pop;
  GuiSpring m_hover;
  GuiSpring m_tip;
  // The glyph's scale, which a new icon springs up from zero.
  GuiSpring m_glyphPop;
  std::array<GuiSpring, 3> m_rows;
  static constexpr float kSplashSeconds = 0.5f;
  float m_splashElapsed = kSplashSeconds;
  double m_hoverClock = 0.0;
  double m_clock = 0.0;
};
