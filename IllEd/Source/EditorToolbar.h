#pragma once

#include "EditorCommand.h"
#include <Illumo/Gui/GuiPanelPointer.h>
#include <Illumo/Gui/GuiToolStyle.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <string>
#include <utility>
#include <vector>

class InputManager;
class IRenderWindow;
class Renderer;

// One open document as the tab strip shows it.
struct EditorTabEntry
{
  std::string label;
  bool dirty = false;
  bool active = false;
};

// One dock panel as the View menu lists it.
struct EditorPanelMenuEntry
{
  std::string title;
  EditorCommand toggle = EditorCommand::None; // show or hide
  EditorCommand popOut = EditorCommand::None; // pop out or dock
  bool visible = true;
  bool detached = false;
};

// The main window's chrome in the plain tool look (D-UI7): the menu bar with
// its dropdowns, the tab strip of open documents under it, the viewport's
// mode label, toasts and the status bar. Menu items come from the
// EditorShortcuts table, so every hint shown works.
class EditorToolbar : public DrawableBase
{
public:
  static constexpr float kDefaultFontSize = 13.0f;
  static constexpr float kDefaultBarHeight = GuiToolStyle::kMenuHeight;
  static constexpr float kDefaultStatusHeight = GuiToolStyle::kStatusHeight;
  static constexpr float kBarHeight = kDefaultBarHeight;
  static constexpr float kStatusHeight = kDefaultStatusHeight;
  static constexpr float kTabHeight = 24.0f;

  EditorToolbar(IRenderWindow* window, Renderer* renderer);
  ~EditorToolbar() override = default;

  EditorToolbar(const EditorToolbar&) = delete;
  EditorToolbar& operator=(const EditorToolbar&) = delete;

  // Panel content follows the font size; the chrome keeps the tool metrics
  // and scales with the renderer's UI scale only.
  void setFontSize(float sizePt);
  float fontSize() const { return m_fontSize; }
  float barHeight() const { return kDefaultBarHeight; }
  float statusHeight() const { return kDefaultStatusHeight; }
  float tabHeight() const { return kTabHeight; }
  // The menu bar and the tab strip: where the dock and viewport begin.
  float chromeTop() const { return barHeight() + tabHeight(); }
  // The open documents, in order; the active one is drawn raised.
  void setTabs(const std::vector<EditorTabEntry>& tabs);
  // A click on a tab (or on its close box) since the last call.
  bool takeTabClick(int* index, bool* close);
  // The strip's rectangle and a tab's (for tests); false past the end.
  GuiToolRect tabStripRect() const;
  bool tabRectForTesting(int index, GuiToolRect* rect) const;
  bool tabCloseRectForTesting(int index, GuiToolRect* rect) const;

  EditorCommand update(InputManager* inputManager, float dt = 0.016f);
  void setAtlas(TextureHandle atlas) { m_atlas = atlas; }
  TextureHandle atlas() const { return m_atlas; }
  void setStatus(const std::string& text);
  void setWorldMode(bool is3D);
  // Check marks for View > Show Grid and Tools > Pivot at Selection Center.
  void setViewToggles(bool gridVisible, bool pivotCenter);
  // View > Play reads Stop Playing while the scene's game runs.
  void setPlaying(bool playing);
  // Edit > Isolate Selection shows a check mark while the view is isolated.
  void setIsolated(bool isolated);
  // The viewport rectangle between the dock columns: the mode label sits at
  // its top left and toasts at its bottom right.
  void setViewport(const GuiToolRect& viewport) { m_viewport = viewport; }
  // The View menu's panel items.
  void setPanels(const std::vector<EditorPanelMenuEntry>& panels,
                 bool canDetach);
  // File > Open Project, New Project and the recent list (newest first, at
  // most ten shown); `available` false hides them all (a host without
  // projects).
  void setRecentProjects(const std::vector<std::string>& labels,
                         bool available);
  // Edit menu shows "Undo <label>" / "Redo <label>"; empty disables the item.
  void setHistoryLabels(const std::string& undoLabel,
                        const std::string& redoLabel);
  void showToast(const std::string& message,
                 ColorRgba color = GuiToolPalette::accent,
                 float duration = 2.5f);
  void closeMenus();
  bool isMenuOpen() const { return m_openMenu >= 0; }
  // A context menu at a point of the main window (UI units), kept inside
  // it. Each entry is a label and its command; a "-" label is a separator
  // and None disables an entry. A choice comes back from update() like a
  // menu item's; Escape or a press elsewhere closes it.
  void openPopup(
    float x,
    float y,
    const std::vector<std::pair<std::string, EditorCommand>>& entries);
  bool popupOpen() const { return m_popupOpen; }
  // Whether the command update() last returned came from the popup.
  bool commandFromPopup() const { return m_fromPopup; }
  // The popup's item center for a command (tests); false when absent.
  bool popupItemCenterForTesting(EditorCommand command, float* x, float* y);
  bool consumedPress() const { return m_consumedPress; }
  bool containsScreenPoint(float x, float y) const;
  EditorCommand clickAtForTesting(float x, float y);
  int openMenuForTesting() const { return m_openMenu; }
  const std::string& statusForTesting() const { return m_status; }
  const std::string& toastForTesting() const { return m_toastMessage; }
  // The shortcut hint a menu item shows for a command (empty when absent).
  std::string menuHintForTesting(EditorCommand command) const;
  // Opens the menu holding a command and returns its item's center.
  bool menuItemCenterForTesting(EditorCommand command, float* x, float* y);
  GameVisual& getVisual() { return m_visual; }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  struct MenuItem
  {
    std::string label;
    std::string shortcut;
    EditorCommand command = EditorCommand::None;
    bool enabled = true;
    bool checked = false;
    bool separator = false;
  };

  struct Menu
  {
    std::string title;
    std::vector<MenuItem> items;
  };

  IRenderWindow* m_window;
  Renderer* m_renderer;
  GameVisual m_visual;
  TextureHandle m_atlas{};
  std::vector<Menu> m_menus;
  GuiPanelPointer m_pointer;
  GuiPanelPlacement m_placement;
  int m_openMenu = -1;
  int m_hoverMenu = -1;
  int m_hoverItem = -1;
  Menu m_popup;
  bool m_popupOpen = false;
  float m_popupX = 0.0f;
  float m_popupY = 0.0f;
  int m_popupHover = -1;
  bool m_fromPopup = false;
  bool m_consumedPress = false;
  std::string m_status;
  float m_fontSize = kDefaultFontSize;
  float m_width = 1280.0f;
  float m_height = 720.0f;
  GuiToolRect m_viewport;
  float m_toastElapsed = 10.0f;
  float m_toastDuration = 0.0f;
  std::string m_toastMessage;
  ColorRgba m_toastColor = GuiToolPalette::accent;
  bool m_is3D = false;
  bool m_gridVisible = true;
  bool m_pivotCenter = false;
  bool m_playing = false;
  bool m_isolated = false;
  std::string m_undoLabel;
  std::string m_redoLabel;
  std::vector<EditorPanelMenuEntry> m_panels;
  bool m_canDetach = false;
  std::vector<std::string> m_recentProjects;
  bool m_projectsAvailable = false;
  std::vector<EditorTabEntry> m_tabs;
  // Horizontal offset keeping the active tab in view when they overflow.
  float m_tabScroll = 0.0f;
  int m_tabClick = -1;
  bool m_tabClickClose = false;

  static MenuItem item(const char* label, EditorCommand command);
  // Lays the tabs out left to right at the current scroll.
  std::vector<GuiToolRect> tabRects() const;
  static GuiToolRect tabCloseRect(const GuiToolRect& tab);
  float tabWidth(const EditorTabEntry& tab) const;
  int tabAt(float x, float y) const;
  void drawTabs();
  static MenuItem separator();
  void rebuildMenus();
  void updateLayout();
  std::vector<std::string> titles() const;
  GuiToolRect barRect() const;
  std::vector<GuiToolStyle::MenuItem> styleItems(const Menu& menu) const;
  // The open dropdown's rectangle; empty when no menu is open.
  GuiToolRect dropdownRect() const;
  GuiToolRect popupRect() const;
  int popupItemAt(float x, float y) const;
  int menuAt(float x, float y) const;
  int itemAt(float x, float y) const;
  void rebuildVisual();
  EditorCommand clickAt(float x, float y);
};
