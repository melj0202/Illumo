#pragma once

#include <Illumo/Gui/GuiPanelPointer.h>
#include <Illumo/Gui/GuiToolStyle.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <glm/vec3.hpp>
#include <string>
#include <vector>

class InputManager;
class IRenderWindow;
class Renderer;

enum class MeshViewerAction
{
  None,
  OpenMesh,
  ResetView,
  ToggleGrid,
  ToggleWireframe,
  ToggleAxes,
  ToggleSkybox,
  // View > panels.
  ToggleInfoPanel,
  ToggleDisplayPanel,
  PopOutInfoPanel,
  PopOutDisplayPanel,
  ResetLayout,
};

struct MeshMetadata
{
  std::string filename;
  size_t vertexCount = 0;
  size_t triangleCount = 0;
  size_t submeshCount = 0;
  size_t materialCount = 0;
  glm::vec3 dimensions = glm::vec3(0.0f);
  bool hasMesh = false;
  // An .ilsc scene: the card shows nodes and assets instead of geometry.
  bool isScene = false;
  size_t nodeCount = 0;
  size_t assetCount = 0;
  // Scene assets that could not be read (drawn as placeholders).
  size_t missingCount = 0;
};

// One dock panel as the View menu lists it.
struct MeshViewerPanelMenuEntry
{
  std::string title;
  MeshViewerAction toggle = MeshViewerAction::None;
  MeshViewerAction popOut = MeshViewerAction::None;
  bool visible = true;
  bool detached = false;
};

// The viewer's main-window chrome in the plain tool look (D-UI7): a File and
// View menu bar, the empty-state card and toasts inside the viewport, and a
// status bar with key hints and camera info. Mesh details live in the Info
// panel and display settings in the Display panel.
class MeshViewerUi : public DrawableBase
{
public:
  static constexpr float kHeaderHeight = GuiToolStyle::kMenuHeight;
  static constexpr float kStatusHeight = GuiToolStyle::kStatusHeight;
  static constexpr float kDefaultFontSize = 13.0f;

  MeshViewerUi(IRenderWindow* window, Renderer* renderer);
  ~MeshViewerUi() override = default;

  MeshViewerUi(const MeshViewerUi&) = delete;
  MeshViewerUi& operator=(const MeshViewerUi&) = delete;

  void setFontSize(float sizePt) { m_fontSize = sizePt; }
  float fontSize() const { return m_fontSize; }

  void setMeshMetadata(const MeshMetadata& metadata);
  const MeshMetadata& meshMetadata() const { return m_metadata; }

  void setDisplayOptions(bool showGrid,
                         bool showWireframe,
                         bool showAxes,
                         bool showSkybox = true);
  void setCameraInfo(float yawDegrees, float pitchDegrees, float distance);
  // The viewport between the dock and the bars.
  void setViewport(const GuiToolRect& viewport) { m_viewport = viewport; }
  void setPanels(const std::vector<MeshViewerPanelMenuEntry>& panels,
                 bool canDetach);

  void showToast(const std::string& message,
                 ColorRgba color = GuiToolPalette::good);
  const std::string& toastForTesting() const { return m_toastMessage; }

  MeshViewerAction update(InputManager* inputManager, float dt = 0.016f);
  bool containsScreenPoint(float x, float y) const;
  bool consumedPress() const { return m_consumedPress; }
  bool isMenuOpen() const { return m_openMenu >= 0; }
  void closeMenus() { m_openMenu = -1; }

  GameVisual& getVisual() { return m_visual; }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

  // Testing hooks
  MeshViewerAction clickAtForTesting(float x, float y);
  // Opens the menu holding an action and returns its item's center.
  bool menuItemCenterForTesting(MeshViewerAction action, float* x, float* y);
  size_t menuCountForTesting() const { return m_menus.size(); }

private:
  struct MenuItem
  {
    std::string label;
    std::string hint;
    MeshViewerAction action = MeshViewerAction::None;
    bool enabled = true;
    bool checked = false;
    bool separator = false;
  };
  struct Menu
  {
    std::string title;
    std::vector<MenuItem> items;
  };

  void rebuildMenus();
  std::vector<std::string> titles() const;
  std::vector<GuiToolStyle::MenuItem> styleItems(const Menu& menu) const;
  GuiToolRect barRect() const;
  GuiToolRect dropdownRect() const;
  GuiToolRect emptyCardRect() const;
  int menuAt(float x, float y) const;
  int itemAt(float x, float y) const;
  MeshViewerAction clickAt(float x, float y);
  void updateLayout();
  void rebuildVisual();

  IRenderWindow* m_window;
  Renderer* m_renderer;
  GameVisual m_visual;

  GuiPanelPointer m_pointer;
  GuiPanelPlacement m_placement;
  float m_fontSize;
  bool m_consumedPress;
  float m_width = 1280.0f;
  float m_height = 720.0f;
  GuiToolRect m_viewport;

  MeshMetadata m_metadata;
  bool m_showGrid;
  bool m_showWireframe;
  bool m_showAxes;
  bool m_showSkybox;
  std::vector<MeshViewerPanelMenuEntry> m_panels;
  bool m_canDetach = false;

  float m_yawDeg;
  float m_pitchDeg;
  float m_distance;

  std::string m_toastMessage;
  ColorRgba m_toastColor;
  float m_toastTimer;

  std::vector<Menu> m_menus;
  int m_openMenu = -1;
  int m_hoverMenu = -1;
  int m_hoverItem = -1;
};
