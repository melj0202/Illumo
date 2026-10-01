#pragma once

#include "CanvasEditAction.h"
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <array>
#include <string>

class InputManager;
class IRenderWindow;
class Renderer;

// What the canvas hands its toolbar each frame.
struct CanvasActionBarState
{
  // The bar may take the pointer: EDIT mode with nothing covering the canvas.
  bool interactive = false;
  // The edit chrome's lift spring: 1 rests in place, 0 hides above the
  // window. It overshoots as the chrome enters, so the bar bounces too.
  float lift = 0.0f;
  bool selection = false;
  // The selection's size, e.g. "12 x 8", shown on the selection group.
  std::string selectionLabel;
  // The paint brush's color, shown on the Fill button.
  ColorRgba brushColor{ 255, 255, 255, 255 };
  bool reducedMotion = false;
};

// The canvas's edit toolbar: a glass pill centered along the top edge in EDIT
// mode. It always offers Save, Load, Paste, Reset (the rule's starting
// pattern) and Clear; while cells are selected a second
// group pours out of it with the selection's size and Copy, Cut, Fill, Erase
// and Deselect. It wears the settings button's look (CanvasChromeStyle): a
// teal-to-indigo tile with a breathing violet-to-cyan halo, popping in from
// nothing and squashing like jelly on bouncy springs. Buttons swell when
// hovered and squish when pressed. Presentation and hit testing only: the
// canvas runs the returned action. Buttons are hit where they were laid out
// last frame; the pop and squash deform only the drawing.
class CanvasActionBar
{
public:
  static constexpr float kTop = 12.0f;
  static constexpr float kHeight = 34.0f;
  static constexpr int kButtonCount = 10;

  CanvasActionBar();
  CanvasActionBar(const CanvasActionBar&) = delete;
  CanvasActionBar& operator=(const CanvasActionBar&) = delete;
  CanvasActionBar(CanvasActionBar&&) = delete;
  CanvasActionBar& operator=(CanvasActionBar&&) = delete;

  void prepare(IRenderWindow* window, Renderer* renderer);
  // Samples the pointer, advances the springs and redraws when anything
  // changed. `acceptInput` false keeps the bar drawn but deaf this frame (a
  // press that dismissed the context menu). Returns the pressed action.
  CanvasEditAction update(const CanvasActionBarState& state,
                          InputManager* input,
                          float deltaSeconds,
                          bool acceptInput);
  void hide();

  // The pointer is over the bar as drawn: the canvas must not paint there.
  bool isPointerOver() const { return m_pointerOver; }
  bool isVisible() const { return m_visual.isVisible(); }
  GameVisual& getVisual() { return m_visual; }
  // The drop shadow and breathing halo, drawn beneath getVisual(). It
  // redraws as the halo breathes while the bar keeps its primitives.
  GameVisual& getHaloVisual() { return m_haloVisual; }
  // Center of a button as last drawn, in window pixels; false when it is not
  // shown or cannot be pressed.
  bool buttonCenter(CanvasEditAction action, float* x, float* y) const;

private:
  struct Button
  {
    CanvasEditAction action = CanvasEditAction::None;
    const char* label = "";
    bool selectionOnly = false;
    bool destructive = false;
    // As drawn last frame, in virtual pixels.
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    bool pressable = false;
    GuiSpring hover;
    GuiSpring squish;
    // Rests at 0; kicked as the pointer arrives and on a press, so the icon
    // hops and settles.
    GuiSpring iconPop;
  };

  float uiScale() const;

  GameVisual m_visual;
  GuiDrawKey m_key;
  GameVisual m_haloVisual;
  GuiDrawKey m_haloKey;
  // The bar pops in from nothing each time it appears, swells a little while
  // the pointer is over it, and jiggles when a button is pressed.
  GuiSpring m_pop;
  GuiSpring m_barHover;
  bool m_shown = false;
  // Seconds, for the halo's idle breath.
  double m_clock = 0.0;
  IRenderWindow* m_window = nullptr;
  Renderer* m_renderer = nullptr;
  std::array<Button, kButtonCount> m_buttons;
  // The selection group's reveal (0 folded away, 1 open), and the size
  // chip's pop when the selection changes shape.
  GuiSpring m_group;
  GuiSpring m_chipPop;
  std::string m_chipLabel;
  // Bumped when the chip's label changes, so the bar redraws its text.
  unsigned int m_chipRevision = 0u;
  // The whole bar as drawn last frame, in virtual pixels.
  float m_barX = 0.0f;
  float m_barY = 0.0f;
  float m_barWidth = 0.0f;
  float m_barHeight = 0.0f;
  bool m_barDrawn = false;
  int m_hovered = -1;
  bool m_leftWasDown = false;
  bool m_pointerOver = false;
};
