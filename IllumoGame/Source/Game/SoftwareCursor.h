#pragma once

#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <array>

class IRenderWindow;
class Renderer;

// CSim's software mouse pointer, drawn in place of the hidden system cursor.
// A menu-glass arrow: a deep glass body, a rim lit cyan to violet, a soft
// edge glow and a thin dark halo (readable on any ruleset colour). Its tip sits
// exactly on the pointer, so clicks stay precise, while its body sways: it
// leans against sideways motion on a spring and swings past upright when the
// pointer stops, a faint holographic afterimage follows movement, and a press
// squishes it while a splash leaps from the tip; releasing boings it back.
// Presentation only.
class SoftwareCursor
{
public:
  // The afterimage: kGhostCount ghosts, each kGhostSpacing seconds further
  // back along the pointer's path, which is kept for kHistoryLength frames.
  static constexpr int kGhostCount = 6;
  static constexpr float kGhostSpacing = 0.035f;
  static constexpr int kHistoryLength = 96;
  static constexpr float kSplashSeconds = 0.45f;

  SoftwareCursor();

  void prepare(IRenderWindow* window, Renderer* renderer);
  // pointerX/Y are in the visual's (UI-scale) pixels. A hidden cursor
  // forgets its motion so it reappears still, at the pointer.
  void update(float deltaSeconds,
              float pointerX,
              float pointerY,
              bool visible,
              bool pressed,
              bool reducedMotion);

  GameVisual& getVisual() { return m_visual; }
  bool isVisible() const { return m_visual.isVisible(); }
  // The body's lean in radians (positive swings the tail left) and its press
  // scale, for tests.
  float lean() const { return m_lean.value(); }
  float pressScale() const { return m_press.value(); }
  float tipX() const { return m_x; }
  float tipY() const { return m_y; }

private:
  void rebuild();
  // Where the tip was `delay` seconds ago, interpolated from the history.
  bool pathAt(float delay, float* x, float* y) const;

  GameVisual m_visual;
  float m_x = 0.0f;
  float m_y = 0.0f;
  float m_velocityX = 0.0f;
  float m_velocityY = 0.0f;
  bool m_placed = false;
  bool m_wasPressed = false;
  bool m_reducedMotion = false;
  float m_splashElapsed = kSplashSeconds;
  float m_ambient = 0.0f;
  GuiSpring m_lean;
  GuiSpring m_press;
  // Where the tip was and when, newest first, for the afterimage.
  std::array<float, kHistoryLength> m_historyX{};
  std::array<float, kHistoryLength> m_historyY{};
  std::array<double, kHistoryLength> m_historyTime{};
  int m_historyCount = 0;
  double m_clock = 0.0;
};
