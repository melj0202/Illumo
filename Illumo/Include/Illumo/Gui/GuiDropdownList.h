#pragma once

#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Services/KeyCode.h>
#include <string>
#include <vector>

class InputManager;

// One choice in a GuiDropdownList.
struct GuiDropdownItem
{
  std::string label;
  // Optional muted text at the row's right edge (a count, a tag), or empty.
  std::string detail;
};

enum class GuiDropdownResult
{
  None,
  // An item was chosen; read it with chosenIndex(). The list has closed.
  Chosen,
  // Closed without a choice (Escape, or a press outside the list).
  Dismissed
};

// A drop-down list for picking one of many items, for fields whose choices
// are too many to step through. It opens under the field it belongs to (or
// above it when there is more room there), shows a window of rows that the
// wheel, the arrow keys, Page Up/Down, Home/End and type-ahead scroll, and
// marks the current value. The highlight is the menus' liquid selection
// drop, and the list pops open on a bouncy spring.
//
// Like GuiDialog it keeps its own state, springs and hit testing and draws
// through GuiKit into caller-owned layers, all in the caller's virtual space.
// While it is open the owner routes that frame's input to update() instead of
// its own handling, and draws the list above everything else.
class GuiDropdownList final
{
public:
  static constexpr int kMaximumVisibleRows = 8;
  static constexpr float kRowHeight = 28.0f;
  static constexpr float kPadding = 5.0f;
  // Space between the field and the list.
  static constexpr float kGap = 4.0f;
  // Letters typed within this long extend the type-ahead prefix.
  static constexpr float kTypeAheadSeconds = 0.9f;

  GuiDropdownList();

  // Opens below the field at (fieldX, fieldY, fieldWidth, fieldHeight), or
  // above it when more rows fit there, within [spaceTop, spaceBottom].
  // `currentIndex` is marked and first highlighted (-1 for none).
  void open(std::vector<GuiDropdownItem> items,
            int currentIndex,
            float fieldX,
            float fieldY,
            float fieldWidth,
            float fieldHeight,
            float spaceTop,
            float spaceBottom,
            bool reducedMotion);
  void close() { m_open = false; }
  // Moves the open list with its field (a panel tilting toward the pointer
  // carries the field along), keeping its size and side.
  void follow(float fieldX, float fieldY);
  bool isOpen() const { return m_open; }

  void tick(float deltaSeconds);
  // Takes this frame's keys (leaving Grave for the console), typed
  // characters, wheel and the sampled pointer. A press on the field it
  // belongs to closes the list.
  GuiDropdownResult update(InputManager* input,
                           const GuiPointerTracker& pointer);
  // The same steps one at a time, for owners and tests that route input.
  GuiDropdownResult handleKey(KeyCode key);
  void handleCharacter(unsigned int codepoint);
  GuiDropdownResult handlePointer(float x, float y, bool moved, bool clicked);
  void scrollRows(int rows);

  // Draws the list: its card, shadow, scroll bar and edge hints into `card`,
  // the liquid highlight into `highlight` (it animates, so it may redraw
  // alone) and the labels into `text`. Nothing is drawn while closed.
  void draw(GameVisual& card,
            GameVisual& highlight,
            GameVisual& text,
            unsigned char opacity) const;

  int itemCount() const { return static_cast<int>(m_items.size()); }
  int highlightedIndex() const { return m_highlighted; }
  int currentIndex() const { return m_current; }
  int chosenIndex() const { return m_chosen; }
  int firstVisibleRow() const { return m_firstVisible; }
  int visibleRowCount() const { return m_visibleRows; }
  bool opensUpward() const { return m_upward; }
  // The list's full rectangle once open (before its reveal animation), in
  // the caller's virtual space.
  float listX() const { return m_x; }
  float listY() const { return m_y; }
  float listWidth() const { return m_width; }
  float listHeight() const { return m_height; }
  // Where row `index` is drawn, or false when it is scrolled out of view.
  bool rowBounds(int index,
                 float* x,
                 float* y,
                 float* width,
                 float* height) const;

private:
  void highlight(int index);
  int rowAt(float x, float y) const;
  float revealedHeight() const;

  std::vector<GuiDropdownItem> m_items;
  bool m_open = false;
  bool m_reducedMotion = false;
  int m_current = -1;
  int m_highlighted = 0;
  int m_chosen = -1;
  int m_firstVisible = 0;
  int m_visibleRows = 1;
  bool m_upward = false;
  float m_fieldX = 0.0f;
  float m_fieldY = 0.0f;
  float m_fieldWidth = 0.0f;
  float m_fieldHeight = 0.0f;
  float m_x = 0.0f;
  float m_y = 0.0f;
  float m_width = 0.0f;
  float m_height = 0.0f;
  // The card pops open on a bouncy spring from the field's edge.
  GuiSpring m_reveal;
  // Highlight travel (with its arrival sheen), in item units.
  GuiMenuAnimator m_motion;
  // Label emphasis by item (the first GuiSpringArray::kCapacity items).
  GuiSpringArray m_focus;
  std::string m_typeAhead;
  float m_typeAheadAge = 0.0f;
};
