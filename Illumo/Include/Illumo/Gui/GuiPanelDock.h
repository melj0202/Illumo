#pragma once

#include <Illumo/Gui/GuiToolStyle.h>
#include <cstdint>
#include <string>
#include <vector>

class GameVisual;
class IPanelSurfaces;

enum class GuiDockSide
{
  Left,
  Right
};

enum class GuiDockMode
{
  Docked,
  Opening, // a window was requested; still drawn docked
  Detached,
  Hidden
};

struct GuiDockPanelSpec
{
  std::string id;    // stable, used in saved layouts; no spaces
  std::string title; // title bar and window title
  GuiDockSide side = GuiDockSide::Left;
  // Nonzero surface id used while detached; unique per panel.
  std::uint32_t surface = 0;
  float weight = 1.0f; // share of its column's height
  float minHeight = 80.0f;
  // Window client size when first detached (layout units).
  float detachedWidth = 300.0f;
  float detachedHeight = 420.0f;
};

// Where one panel draws this frame, in the layout units of its surface.
struct GuiDockView
{
  bool visible = false;
  std::uint32_t surface = 0; // 0: the main window
  GuiToolRect frame;         // title bar and content
  GuiToolRect title;
  GuiToolRect content;
};

// One window's pointer in its layout units.
struct GuiDockPointer
{
  float x = -1.0f;
  float y = -1.0f;
  bool down = false;
};

// Layout and interaction state for an app window's tool panels (D-UI7):
// left and right dock columns of stacked panels with draggable splitters,
// each panel with a title bar that can hide it, pop it out into its own
// window (IPanelSurfaces), or tear it off by dragging the title past the main
// window's edge, as the console does. A detached panel fills its window and
// docks back from its title bar, from its window's close box, or on reset.
// Immediate-mode: products draw each panel's content into the view's
// content rectangle of the view's surface; the dock never draws content and
// keeps no widget tree. Layout units are window pixels divided by the
// product's UI scale, the same for every window.
class GuiPanelDock
{
public:
  static constexpr float kTearOffMargin = 12.0f;
  static constexpr float kMinimumColumn = 160.0f;
  static constexpr float kDefaultColumn = 250.0f;

  void addPanel(const GuiDockPanelSpec& spec);
  void setSurfaces(IPanelSurfaces* surfaces) { m_surfaces = surfaces; }
  bool canDetach() const;
  void setColumnWidth(GuiDockSide side, float width);
  float columnWidth(GuiDockSide side) const;

  // Main window of width x height layout units; top and bottom are reserved
  // bands (menu and status bars). scale converts window pixels to layout
  // units for detached windows.
  void layout(float width, float height, float top, float bottom, float scale);
  // Once per frame after layout: window events, then pointer interaction.
  void update(const GuiDockPointer& main);

  const GuiDockView& view(const std::string& id) const;
  const std::vector<GuiDockPanelSpec>& panels() const { return m_specs; }
  GuiToolRect center() const { return m_center; }
  // Chrome of docked panels: bodies, title bars and splitters.
  void drawDocked(GameVisual& visual) const;
  // Chrome of one detached panel, drawn into its window's visual.
  void drawDetached(const std::string& id, GameVisual& visual) const;

  // The main window point lies on docked panels or splitters.
  bool overPanels(float x, float y) const;
  // This frame's press landed on dock chrome (title bars, buttons, splitters)
  // in the given surface; products skip their own handling of it.
  bool consumedPress(std::uint32_t surface) const;
  // A splitter or title drag is in progress in the main window.
  bool dragging() const { return m_drag != Drag::None; }

  GuiDockMode mode(const std::string& id) const;
  bool detach(const std::string& id);
  void dock(const std::string& id);
  void setHidden(const std::string& id, bool hidden);
  // Docks and shows every panel at default sizes.
  void reset();

  // A compact text form of columns, modes and window rectangles.
  std::string serialize() const;
  // Applies a saved layout; false (layout unchanged) when invalid. Panels it
  // names detached reopen once surfaces are available, else stay docked.
  bool restore(const std::string& text);

private:
  struct Panel
  {
    GuiDockMode mode = GuiDockMode::Docked;
    bool wantsWindow = false; // restored detached; open when possible
    float weight = 1.0f;
    GuiDockView view;
    // Last known window rectangle (client, main-relative, layout units).
    float windowX = 40.0f;
    float windowY = 40.0f;
    float windowWidth = 0.0f;
    float windowHeight = 0.0f;
    bool previousDown = false;
    int hoveredButton = -1;
  };
  enum class Drag
  {
    None,
    Column,
    Row,
    Title
  };
  int find(const std::string& id) const;
  int findSurface(std::uint32_t surface) const;
  std::vector<GuiToolTitleButton> buttons(int panel) const;
  bool openWindow(int panel, float x, float y);
  void handleEvents();
  void handleMain(const GuiDockPointer& pointer);
  void handleDetached();
  std::vector<int> docked(GuiDockSide side) const;

  std::vector<GuiDockPanelSpec> m_specs;
  std::vector<Panel> m_panels;
  IPanelSurfaces* m_surfaces = nullptr;
  float m_width = 0.0f;
  float m_height = 0.0f;
  float m_top = 0.0f;
  float m_bottom = 0.0f;
  float m_scale = 1.0f;
  float m_leftWidth = kDefaultColumn;
  float m_rightWidth = kDefaultColumn;
  GuiToolRect m_center;
  GuiToolRect m_leftSplitter;
  GuiToolRect m_rightSplitter;
  // Row splitters: the panel above each one.
  std::vector<std::pair<int, GuiToolRect>> m_rowSplitters;
  bool m_previousDown = false;
  Drag m_drag = Drag::None;
  GuiDockSide m_dragSide = GuiDockSide::Left;
  int m_dragPanel = -1;
  float m_grabX = 0.0f;
  float m_grabY = 0.0f;
  float m_dragStart = 0.0f;
  float m_dragValue = 0.0f;
  float m_dragValueBelow = 0.0f;
  std::vector<std::uint32_t> m_consumed;
  GuiDockPointer m_mainPointer;
  static const GuiDockView kHiddenView;
};
