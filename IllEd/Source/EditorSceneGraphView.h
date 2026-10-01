#pragma once

#include "EditorCommand.h"
#include "EditorDocument.h"
#include "EditorSelection.h"
#include <Illumo/Gui/GuiPanelPointer.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <string>
#include <unordered_set>
#include <vector>

class InputManager;
class IRenderWindow;
class Renderer;

// The Hierarchy dock panel's content: the scene tree drawn into the
// rectangle and surface it is given (docked or detached), with folding,
// visibility eyes, drag reordering inside its own surface and a context menu.
class EditorSceneGraphView : public DrawableBase
{
public:
  static constexpr float kDefaultRowHeight = 22.0f;
  static constexpr float kRowHeight = kDefaultRowHeight;

  EditorSceneGraphView(IRenderWindow* window, Renderer* renderer);
  ~EditorSceneGraphView() override = default;

  EditorSceneGraphView(const EditorSceneGraphView&) = delete;
  EditorSceneGraphView& operator=(const EditorSceneGraphView&) = delete;

  void setFontSize(float sizePt);
  float fontSize() const { return m_fontSize; }
  // The content rectangle and surface for this frame.
  void setPlacement(const GuiPanelPlacement& placement);
  const GuiPanelPlacement& placement() const { return m_placement; }
  float panelX() const { return m_x; }
  float panelWidth() const { return m_width; }
  float panelBottom() const { return m_y + m_height; }
  float rowHeight() const { return m_rowHeight; }

  // Returns true when the document changed (a drop, reorder or visibility
  // toggle). Rows outside the panel's row window are neither hit nor drawn;
  // the wheel scrolls it and a drag near its edges auto-scrolls.
  bool update(InputManager* inputManager,
              EditorDocument* document,
              EditorSelection* selection,
              float dt = 0.016f);

  bool consumedPress() const { return m_consumedPress; }
  // A context-menu choice, once: the command and the row it was chosen on.
  EditorCommand takeCommand(std::string* targetId);
  void setAtlas(TextureHandle atlas);
  TextureHandle atlas() const { return m_atlas; }
  // A point of the panel's own surface lies on it (or its open menu).
  bool containsScreenPoint(float x, float y) const;
  GameVisual& getVisual() { return m_visual; }

  // Testing hooks
  void clickAtForTesting(float x,
                         float y,
                         EditorDocument* document,
                         EditorSelection* selection,
                         bool toggle = false);
  void dragAndDropForTesting(const std::string& sourceId,
                             const std::string& targetParentId,
                             EditorDocument* document);
  size_t visibleRowCountForTesting() const { return m_rows.size(); }
  bool isFolded(const std::string& id) const
  {
    return m_folded.find(id) != m_folded.end();
  }
  // Screen centers of a row's fold arrow and visibility eye.
  bool foldCenterForTesting(size_t index, float* x, float* y) const;
  bool eyeCenterForTesting(size_t index, float* x, float* y) const;
  void openContextMenuForTesting(float x,
                                 float y,
                                 EditorDocument* document,
                                 EditorSelection* selection);
  bool menuItemCenterForTesting(EditorCommand command,
                                float* x,
                                float* y) const;
  bool menuOpen() const { return m_menuOpen; }
  // Drops a dragged row before, after or into a target row.
  bool dropForTesting(const std::string& sourceId,
                      const std::string& targetId,
                      int zone,
                      EditorDocument* document);
  void scrollForTesting(float pixels) { m_scroll += pixels; }
  float scrollOffset() const { return m_scroll; }
  float rowScreenY(size_t index) const
  {
    return index < m_rows.size() ? m_rows[index].y : 0.0f;
  }
  const std::string& draggedNodeIdForTesting() const { return m_draggedNodeId; }
  const std::string& dropTargetNodeIdForTesting() const
  {
    return m_dropTargetNodeId;
  }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  struct TreeRow
  {
    std::string id;
    std::string name;
    // Atlas icon: the Create command of the node's first component.
    EditorCommand icon = EditorCommand::CreateEmpty;
    int depth = 0;
    float y = 0.0f;
    bool isLastChild = false;
    bool hasChildren = false;
    bool folded = false;
    bool visible = true;
  };

  IRenderWindow* m_window;
  Renderer* m_renderer;
  GameVisual m_visual;
  TextureHandle m_atlas{};
  std::vector<TreeRow> m_rows;
  GuiPanelPlacement m_placement;

  GuiPanelPointer m_pointer;
  bool m_consumedPress;
  bool m_isDragging;
  std::string m_draggedNodeId;
  std::string m_dropTargetNodeId;
  bool m_dropValid;
  float m_dragStartX;
  float m_dragStartY;

  float m_fontSize;
  float m_width;
  float m_rowHeight;
  float m_x;
  float m_y;
  float m_height;
  float m_treeStartY;
  float m_animTime;
  int m_hoverRow;
  bool m_hoverRootZone;
  float m_mouseX;
  float m_mouseY;
  float m_scroll = 0.0f;
  std::unordered_set<std::string> m_folded;
  // Where a drag lands on the hovered row: -1 before, 0 into, 1 after.
  int m_dropZone = 0;
  bool m_menuOpen = false;
  float m_menuX = 0.0f;
  float m_menuY = 0.0f;
  int m_menuHover = -1;
  std::string m_menuTarget;
  EditorCommand m_pendingCommand = EditorCommand::None;
  std::string m_pendingTarget;

  struct RowGeometry
  {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float contentX = 0.0f;
    float foldX = 0.0f;
    float eyeX = 0.0f;
    float iconSize = 0.0f;
  };

  float m_lastClickTime = -1.0f;
  std::string m_lastClickId;
  std::string m_revealedPrimary;

  void updateLayout();
  RowGeometry rowGeometry(const TreeRow& row) const;
  // Keeps the scroll offset inside the content and shifts rows to match.
  void clampScroll();
  // Unfolds the primary selection's ancestors and scrolls it into view.
  void revealPrimary(const EditorDocument* document,
                     const EditorSelection* selection);
  // A left press at (x, y): menu, fold arrow, eye, row or empty space.
  // Returns true when the document changed.
  bool handlePress(float x,
                   float y,
                   EditorDocument* document,
                   EditorSelection* selection,
                   bool toggle,
                   bool armDrag);
  void openMenu(float x,
                float y,
                EditorDocument* document,
                EditorSelection* selection);
  float menuWidth() const;
  float menuHeight() const;
  bool menuContains(float x, float y) const;
  int menuItemAt(float x, float y) const;
  void rebuildTreeRows(const EditorDocument* document);
  void rebuildVisual(const EditorDocument* document,
                     const EditorSelection* selection);
  int hitTestRow(float x, float y) const;
  float treeBottom() const;
  bool applyDrop(const std::string& sourceId,
                 const std::string& targetId,
                 int zone,
                 EditorDocument* document);
  bool hitTestRootZone(float x, float y) const;
};
