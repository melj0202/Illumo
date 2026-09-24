#pragma once

#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Gui/GuiToolStyle.h>
#include <cstdint>

class InputManager;
class IPanelSurfaces;
class IRenderWindow;
class Renderer;

// Where a dock panel draws this frame and whose pointer it reads (D-UI7):
// its content rectangle in the layout units of its surface, and that surface.
// Docked panels read the main window; detached ones their own window.
struct GuiPanelPlacement
{
  GuiToolRect area;
  bool visible = true;
  IPanelSurfaces* surfaces = nullptr;
  std::uint32_t surface = 0; // 0: the main window
  // Another layer (the menu bar, an open menu, dock chrome) took this
  // frame's press in this surface: the panel sees no click from it.
  bool inputBlocked = false;
};

// One panel's pointer, sampled once per update from its placement's surface:
// position in layout units, left press/click/release edges, a right-click
// edge and the wheel. Moving between surfaces adopts the button state, so
// no edge from one window replays in another.
class GuiPanelPointer
{
public:
  void sample(const GuiPanelPlacement& placement,
              IRenderWindow* window,
              Renderer* renderer,
              InputManager* input);

  float x() const { return m_tracker.x(); }
  float y() const { return m_tracker.y(); }
  bool pressed() const { return m_tracker.pressed(); }
  bool clicked() const { return m_tracker.clicked() && !m_blocked; }
  bool released() const { return m_tracker.released(); }
  bool rightClicked() const { return m_rightClicked && !m_blocked; }
  // This frame's wheel steps (positive away from the user), consumed: the
  // viewport never sees a wheel a panel took.
  float takeWheel();
  // Window pixels of the sampled position in its own surface.
  float pixelX() const { return m_pixelX; }
  float pixelY() const { return m_pixelY; }

private:
  GuiPointerTracker m_tracker;
  InputManager* m_input = nullptr;
  std::uint32_t m_surface = 0;
  bool m_sampled = false;
  bool m_blocked = false;
  bool m_rightDown = false;
  bool m_rightClicked = false;
  double m_wheel = 0.0;
  bool m_mainWheel = false;
  float m_pixelX = 0.0f;
  float m_pixelY = 0.0f;
};
