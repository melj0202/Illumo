#pragma once

#include <Illumo/Gui/GuiFileTree.h>
#include <Illumo/Gui/GuiPanelPointer.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <cstdint>
#include <memory>
#include <string>

class InputManager;
class IRenderWindow;
class Renderer;

// The Assets dock panel's content: the virtual file tree (/app, /engine,
// /packages/<id>, /project) listed through IllEdPlatform, drawn into the
// rectangle and surface it is given. Directories expand in place; a file row
// dragged out of the panel and released is handed to the module as a drop
// with its release point, in whichever window the panel is (meshes and
// textures become nodes); a double-clicked scene opens. Rows outside the
// panel's window are neither drawn nor hit.
class EditorAssetBrowser : public DrawableBase
{
public:
  EditorAssetBrowser(IRenderWindow* window, Renderer* renderer);
  ~EditorAssetBrowser() override;

  EditorAssetBrowser(const EditorAssetBrowser&) = delete;
  EditorAssetBrowser& operator=(const EditorAssetBrowser&) = delete;

  void setFontSize(float sizePt);
  // The content rectangle and surface; an invisible panel ignores input.
  void setPlacement(const GuiPanelPlacement& placement);
  const GuiPanelPlacement& placement() const { return m_placement; }
  bool visible() const { return m_visible; }
  bool containsScreenPoint(float x, float y) const;

  // Requests missing listings and handles pointer input.
  void update(InputManager* input, float dt);
  bool consumedPress() const { return m_consumedPress; }
  // Forgets every listing so the tree is listed again (after an import).
  void refresh();

  // A file dragged out of the panel and released this frame, once. The
  // release point is in window pixels of `surface` (the panel's window).
  struct Drop
  {
    std::string path;
    std::uint32_t surface = 0;
    float pixelX = 0.0f;
    float pixelY = 0.0f;
  };
  Drop takeDrop();
  // A file double-clicked this frame, once.
  std::string takeActivated();
  // The file row being dragged, empty when none.
  const std::string& dragging() const { return m_dragPath; }
  const std::string& selected() const { return m_selected; }

  // Testing hooks.
  std::vector<GuiFileTreeRow> rowsForTesting() const { return m_tree.rows(); }
  void clickRowForTesting(std::size_t index, bool doubleClick = false);
  // A drop released at a point of the panel's surface (window pixels).
  void dragRowOutForTesting(std::size_t index,
                            float pixelX = 640.0f,
                            float pixelY = 360.0f);
  float rowHeight() const;
  // Layout-unit center of a visible row, in the panel's surface.
  bool rowCenterForTesting(std::size_t index, float* x, float* y) const;

  GameVisual& getVisual() { return m_visual; }
  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  IRenderWindow* m_window;
  Renderer* m_renderer;
  GameVisual m_visual;
  GuiFileTree m_tree;
  GuiPanelPointer m_pointer;
  GuiPanelPlacement m_placement;
  // Expires with the browser so late listings never touch it.
  std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
  float m_fontSize;
  bool m_visible = false;
  float m_scroll = 0.0f;
  float m_animTime = 0.0f;
  int m_hover = -1;
  bool m_consumedPress = false;
  std::string m_selected;
  std::string m_armedPath;
  std::string m_dragPath;
  float m_pressX = 0.0f;
  float m_pressY = 0.0f;
  Drop m_drop;
  std::string m_activated;
  std::string m_lastClick;
  float m_lastClickTime = -1.0f;

  float top() const;
  int rowAt(float x, float y, const std::vector<GuiFileTreeRow>& rows) const;
  void requestListings();
  void press(const GuiFileTreeRow& row, bool doubleClick);
  void rebuild(const std::vector<GuiFileTreeRow>& rows);
};
