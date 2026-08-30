#pragma once

#include "EditorDocument.h"
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <string>
#include <vector>

class InputManager;
class IRenderWindow;
class Renderer;

class EditorSceneGraphView : public DrawableBase
{
public:
  static constexpr float kDefaultWidth = 220.0f;
  static constexpr float kDefaultRowHeight = 22.0f;
  static constexpr float kWidth = kDefaultWidth;
  static constexpr float kRowHeight = kDefaultRowHeight;

  EditorSceneGraphView(IRenderWindow* window, Renderer* renderer);
  ~EditorSceneGraphView() override = default;

  EditorSceneGraphView(const EditorSceneGraphView&) = delete;
  EditorSceneGraphView& operator=(const EditorSceneGraphView&) = delete;

  void setFontSize(float sizePt);
  float fontSize() const { return m_fontSize; }
  void setToolbarDimensions(float barHeight, float statusHeight);
  float panelWidth() const { return m_width; }
  float rowHeight() const { return m_rowHeight; }

  bool update(InputManager* inputManager,
              EditorDocument* document,
              std::string* selectedId,
              float dt = 0.016f);

  bool consumedPress() const { return m_consumedPress; }
  void setAtlas(TextureHandle atlas);
  TextureHandle atlas() const { return m_atlas; }
  bool containsScreenPoint(float x, float y) const;
  float panelX() const { return m_x; }
  GameVisual& getVisual() { return m_visual; }

  // Testing hooks
  void clickAtForTesting(float x,
                         float y,
                         EditorDocument* document,
                         std::string* selectedId);
  void dragAndDropForTesting(const std::string& sourceId,
                             const std::string& targetParentId,
                             EditorDocument* document);
  size_t visibleRowCountForTesting() const { return m_rows.size(); }
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
    SceneNodeKind kind = SceneNodeKind::Empty;
    int depth = 0;
    float y = 0.0f;
    bool isLastChild = false;
  };

  IRenderWindow* m_window;
  Renderer* m_renderer;
  GameVisual m_visual;
  TextureHandle m_atlas{};
  std::vector<TreeRow> m_rows;

  bool m_mouseWasDown;
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
  float m_barHeight;
  float m_statusHeight;
  float m_x;
  float m_y;
  float m_height;
  float m_headerY;
  float m_treeStartY;
  float m_animTime;
  int m_hoverRow;
  bool m_hoverRootZone;
  float m_mouseX;
  float m_mouseY;

  void updateLayout();
  void rebuildTreeRows(const EditorDocument* document);
  void rebuildVisual(const EditorDocument* document,
                     const std::string& selectedId);
  int hitTestRow(float x, float y) const;
  bool hitTestRootZone(float x, float y) const;
};
