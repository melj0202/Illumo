#pragma once

#include "CanvasEditAction.h"
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <string>
#include <vector>

class InputManager;
class IRenderWindow;
class Renderer;

// The selection's right-click menu: a small glass card that springs open at
// the pointer (flipping to stay on screen) with Cut, Copy, Paste here, Fill
// with the brush, Erase and Deselect, each with its shortcut. A liquid drop
// follows the hovered row. It works like a desktop context menu: click a row,
// or keep the right button held, move onto a row and release; Up/Down and
// Enter choose from the keyboard; Escape or a press anywhere else closes it.
// Closing is immediate. Presentation and input only: the canvas runs the
// returned action.
class CanvasContextMenu
{
public:
  CanvasContextMenu();
  CanvasContextMenu(const CanvasContextMenu&) = delete;
  CanvasContextMenu& operator=(const CanvasContextMenu&) = delete;
  CanvasContextMenu(CanvasContextMenu&&) = delete;
  CanvasContextMenu& operator=(CanvasContextMenu&&) = delete;

  void prepare(IRenderWindow* window, Renderer* renderer);
  // Opens at the pointer. `fillLabel` names the paint brush the Fill row
  // uses and `brushColor` is its swatch.
  void open(const std::string& fillLabel,
            ColorRgba brushColor,
            InputManager* input,
            bool reducedMotion);
  void close();
  bool isOpen() const { return m_open; }
  // One frame of hover, presses and keys; returns the chosen action (which
  // also closes the menu).
  CanvasEditAction update(InputManager* input,
                          float deltaSeconds,
                          bool reducedMotion);
  // The menu closed this frame because a mouse button was pressed outside
  // it; the canvas swallows that press. `byRightPress` tells whether it was
  // the right button, which may reopen the menu where it was pressed.
  bool dismissedByPress() const { return m_dismissedByPress; }
  bool dismissedByRightPress() const { return m_dismissedByRightPress; }
  bool containsPointer() const;

  // Back to front: glass card, hover drop, then labels and shortcuts.
  GameVisual& getPanelVisual() { return m_panelVisual; }
  GameVisual& getDropVisual() { return m_dropVisual; }
  GameVisual& getLabelVisual() { return m_labelVisual; }
  // Center of a row, in window pixels; false while the menu is closed or
  // the row is absent.
  bool rowCenter(CanvasEditAction action, float* x, float* y) const;
  int rowCount() const { return static_cast<int>(m_rows.size()); }

private:
  struct Row
  {
    CanvasEditAction action = CanvasEditAction::None;
    std::string label;
    std::string shortcut;
    bool destructive = false;
    bool separatorBefore = false;
    // Top of the row, in virtual pixels.
    float y = 0.0f;
  };

  float uiScale() const;
  void pointer(float* x, float* y) const;
  int rowAt(float x, float y) const;
  bool contains(float x, float y) const;
  CanvasEditAction choose(int row);
  void layout(float pointerX, float pointerY);
  void draw();

  GameVisual m_panelVisual;
  GameVisual m_dropVisual;
  GameVisual m_labelVisual;
  GuiDrawKey m_panelKey;
  IRenderWindow* m_window = nullptr;
  Renderer* m_renderer = nullptr;
  std::vector<Row> m_rows;
  ColorRgba m_brushColor{ 255, 255, 255, 255 };
  bool m_open = false;
  bool m_reducedMotion = false;
  // The card, in virtual pixels, and the corner it grows from (the pointer).
  float m_x = 0.0f;
  float m_y = 0.0f;
  float m_width = 0.0f;
  float m_height = 0.0f;
  float m_originX = 0.0f;
  float m_originY = 0.0f;
  float m_keyWidth = 0.0f;
  // Opening pop (0 closed, 1 open; it overshoots), the drop's head and tail
  // (row tops, in virtual pixels) and its opacity, and per-row emphasis.
  GuiSpring m_pop;
  GuiSpring m_dropHead;
  GuiSpring m_dropTail;
  GuiSpring m_dropShown;
  GuiSpringArray m_emphasis;
  bool m_dropPlaced = false;
  float m_clock = 0.0f;
  int m_selected = -1;
  // Pointer bookkeeping: where it opened, whether it has moved since, and
  // the buttons' previous states (the opening right button starts held).
  float m_openX = 0.0f;
  float m_openY = 0.0f;
  float m_lastX = 0.0f;
  float m_lastY = 0.0f;
  bool m_movedSinceOpen = false;
  bool m_leftWasDown = false;
  bool m_rightWasDown = false;
  bool m_dismissedByPress = false;
  bool m_dismissedByRightPress = false;
};
