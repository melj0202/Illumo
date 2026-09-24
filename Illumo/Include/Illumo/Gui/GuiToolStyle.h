#pragma once

#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <string>
#include <vector>

class GameVisual;
class GuiTextEdit;

// The plain tool look shared by IllEd and IllMeshViewer (D-UI7): flat, dense
// panels on a neutral palette, like the developer console. Everything is drawn
// with rectangles, lines and text, so it reads the same docked or in a detached
// window. No animation, glow or gradient; the accent marks only selection,
// focus and active state.
struct GuiToolPalette
{
  static constexpr ColorRgba window{ 24, 25, 27, 255 };
  static constexpr ColorRgba panel{ 30, 31, 34, 255 };
  static constexpr ColorRgba bar{ 38, 39, 43, 255 };
  static constexpr ColorRgba barHover{ 48, 50, 55, 255 };
  static constexpr ColorRgba border{ 58, 60, 66, 255 };
  static constexpr ColorRgba rule{ 44, 46, 51, 255 };
  static constexpr ColorRgba field{ 20, 21, 23, 255 };
  static constexpr ColorRgba text{ 214, 216, 220, 255 };
  static constexpr ColorRgba dim{ 140, 144, 152, 255 };
  static constexpr ColorRgba faint{ 96, 100, 108, 255 };
  static constexpr ColorRgba accent{ 86, 156, 214, 255 };
  static constexpr ColorRgba selection{ 44, 72, 108, 255 };
  static constexpr ColorRgba hover{ 44, 46, 52, 255 };
  static constexpr ColorRgba pressed{ 56, 60, 68, 255 };
  static constexpr ColorRgba button{ 46, 48, 53, 255 };
  static constexpr ColorRgba good{ 128, 196, 120, 255 };
  static constexpr ColorRgba warning{ 222, 184, 96, 255 };
  static constexpr ColorRgba error{ 232, 104, 94, 255 };
  static constexpr ColorRgba scrollTrack{ 34, 35, 38, 255 };
  static constexpr ColorRgba scrollThumb{ 84, 88, 96, 255 };
  static constexpr ColorRgba caret{ 235, 235, 235, 255 };
};

struct GuiToolRect
{
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  bool contains(float px, float py) const
  {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

// Buttons a panel title bar can carry, right to left.
enum class GuiToolTitleButton
{
  None,
  PopOut, // detach into a window
  Dock,   // return into the main window
  Hide
};

class GuiToolStyle
{
public:
  static constexpr float kFontSize = 13.0f;
  static constexpr float kSmallFontSize = 11.0f;
  static constexpr float kTitleHeight = 22.0f;
  static constexpr float kRowHeight = 20.0f;
  static constexpr float kPad = 8.0f;
  static constexpr float kSplitter = 4.0f;
  static constexpr float kMenuHeight = 24.0f;
  static constexpr float kStatusHeight = 22.0f;
  static constexpr float kScrollbar = 6.0f;

  static void text(GameVisual& visual,
                   const std::string& value,
                   float x,
                   float y,
                   float size,
                   ColorRgba color);
  // Text clipped with "..." to fit width.
  static void fittedText(GameVisual& visual,
                         const std::string& value,
                         float x,
                         float y,
                         float width,
                         float size,
                         ColorRgba color);
  static float textWidth(const std::string& value, float size);

  static void panel(GameVisual& visual, const GuiToolRect& rect);
  // Title bar with buttons laid out by titleButtonRect.
  static void titleBar(GameVisual& visual,
                       const GuiToolRect& rect,
                       const std::string& title,
                       const std::vector<GuiToolTitleButton>& buttons,
                       int hoveredButton);
  // Button index 0 is the rightmost.
  static GuiToolRect titleButtonRect(const GuiToolRect& bar, int index);
  static void splitter(GameVisual& visual,
                       const GuiToolRect& rect,
                       bool hovered,
                       bool active);
  static void sectionHeader(GameVisual& visual,
                            const GuiToolRect& rect,
                            const std::string& label);
  static void row(GameVisual& visual,
                  const GuiToolRect& rect,
                  bool selected,
                  bool hovered);
  static void button(GameVisual& visual,
                     const GuiToolRect& rect,
                     const std::string& label,
                     bool hovered,
                     bool active);
  // A labelled check box.
  static void toggle(GameVisual& visual,
                     const GuiToolRect& rect,
                     const std::string& label,
                     bool on,
                     bool hovered);
  // A labelled horizontal slider; fraction in [0, 1].
  static void slider(GameVisual& visual,
                     const GuiToolRect& rect,
                     const std::string& label,
                     float fraction,
                     const std::string& value,
                     bool hovered,
                     bool active);
  // The track part of slider(), for hit testing.
  static GuiToolRect sliderTrack(const GuiToolRect& rect);
  static void labelValue(GameVisual& visual,
                         const GuiToolRect& rect,
                         const std::string& label,
                         const std::string& value);
  static void textField(GameVisual& visual,
                        const GuiToolRect& rect,
                        const std::string& value,
                        const GuiTextEdit* edit,
                        bool invalid,
                        bool hovered,
                        bool readOnly,
                        float fontSize = kFontSize);
  // Vertical scrollbar for a window of `visible` rows out of `total`.
  static void scrollbar(GameVisual& visual,
                        const GuiToolRect& track,
                        float first,
                        float visible,
                        float total);
  static void menuBar(GameVisual& visual,
                      const GuiToolRect& rect,
                      const std::vector<std::string>& menus,
                      int open,
                      int hovered);
  // The rectangle of menu `index` in a menuBar laid out at rect.
  static GuiToolRect menuRect(const GuiToolRect& bar,
                              const std::vector<std::string>& menus,
                              int index);
  struct MenuItem
  {
    std::string label;
    std::string hint; // shortcut text, right aligned
    bool enabled = true;
    bool checked = false;
    bool separator = false;
  };
  static float dropdownWidth(const std::vector<MenuItem>& items);
  static void dropdown(GameVisual& visual,
                       float x,
                       float y,
                       const std::vector<MenuItem>& items,
                       int hovered);
  // The item under a point of a dropdown drawn at x, y; -1 when none.
  static int dropdownItemAt(float x,
                            float y,
                            const std::vector<MenuItem>& items,
                            float px,
                            float py);
  static void statusBar(GameVisual& visual,
                        const GuiToolRect& rect,
                        const std::string& left,
                        const std::string& right);
};
