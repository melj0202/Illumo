#include "EditorSceneGraphView.h"
#include "EditorToolbar.h"
#include "EditorUiAtlas.h"

#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

EditorSceneGraphView::EditorSceneGraphView(IRenderWindow* window,
                                           Renderer* renderer)
  : m_window(window)
  , m_renderer(renderer)
  , m_visual(2048u)
  , m_mouseWasDown(false)
  , m_consumedPress(false)
  , m_isDragging(false)
  , m_dropValid(false)
  , m_dragStartX(0.0f)
  , m_dragStartY(0.0f)
  , m_x(0.0f)
  , m_y(EditorToolbar::kBarHeight)
  , m_height(670.0f)
  , m_headerY(EditorToolbar::kBarHeight)
  , m_treeStartY(EditorToolbar::kBarHeight + 32.0f)
  , m_animTime(0.0f)
  , m_hoverRow(-1)
  , m_hoverRootZone(false)
  , m_mouseX(0.0f)
  , m_mouseY(0.0f)
{
  m_visual.setSpace(PrimitiveSpace::Pixels);
  m_visual.setLayerHint(RenderLayerId::UI);
  m_visual.setWindow(window);
  m_visual.setRenderer(renderer);
  m_visual.prepare(renderer);
  updateLayout();
}

void
EditorSceneGraphView::setAtlas(TextureHandle atlas)
{
  m_atlas = atlas;
}

void
EditorSceneGraphView::updateLayout()
{
  int width = 1280;
  int height = 720;
  if (m_window != nullptr) {
    const std::array<int, 2> dimensions = m_window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }
  const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
  const float virtualHeight =
    static_cast<float>(height) / (scale > 0.0f ? scale : 1.0f);

  m_x = 0.0f;
  m_y = EditorToolbar::kBarHeight;
  m_height = std::max(80.0f,
                      virtualHeight - EditorToolbar::kBarHeight -
                        EditorToolbar::kStatusHeight);
  m_headerY = m_y;
  m_treeStartY = m_y + 30.0f;
}

bool
EditorSceneGraphView::containsScreenPoint(float x, float y) const
{
  return x >= m_x && x <= m_x + kWidth && y >= m_y && y <= m_y + m_height;
}

void
EditorSceneGraphView::rebuildTreeRows(const EditorDocument* document)
{
  m_rows.clear();
  if (document == nullptr || document->nodeCount() == 0) {
    return;
  }

  const size_t count = document->nodeCount();

  // Build adjacency map: parentId -> vector of node indices
  std::unordered_map<std::string, std::vector<size_t>> childrenMap;
  std::vector<size_t> rootIndices;

  for (size_t i = 0; i < count; ++i) {
    const IlscNode* node = document->nodeAt(i);
    if (node == nullptr) {
      continue;
    }
    if (node->parentId.empty() ||
        document->findNode(node->parentId) == nullptr) {
      rootIndices.push_back(i);
    } else {
      childrenMap[node->parentId].push_back(i);
    }
  }

  // Iterative DFS traversal to produce flat pre-order tree rows with depth
  struct StackEntry
  {
    size_t nodeIndex = 0;
    int depth = 0;
    bool isLastChild = false;
  };

  std::vector<StackEntry> stack;
  // Push roots in reverse order so first root is popped first
  for (size_t i = rootIndices.size(); i > 0; --i) {
    stack.push_back({ rootIndices[i - 1], 0, (i == rootIndices.size()) });
  }

  std::unordered_set<std::string> visited;
  float currentY = m_treeStartY;

  while (!stack.empty()) {
    const StackEntry entry = stack.back();
    stack.pop_back();

    const IlscNode* node = document->nodeAt(entry.nodeIndex);
    if (node == nullptr || visited.find(node->id) != visited.end()) {
      continue;
    }
    visited.insert(node->id);

    TreeRow row;
    row.id = node->id;
    row.name = node->name.empty() ? ("Node #" + node->id) : node->name;
    row.kind = node->kind;
    row.depth = entry.depth;
    row.y = currentY;
    row.isLastChild = entry.isLastChild;
    m_rows.push_back(row);
    currentY += kRowHeight;

    const auto it = childrenMap.find(node->id);
    if (it != childrenMap.end()) {
      const std::vector<size_t>& childList = it->second;
      for (size_t c = childList.size(); c > 0; --c) {
        stack.push_back(
          { childList[c - 1], entry.depth + 1, (c == childList.size()) });
      }
    }
  }
}

int
EditorSceneGraphView::hitTestRow(float x, float y) const
{
  if (x < m_x || x > m_x + kWidth) {
    return -1;
  }
  for (size_t i = 0; i < m_rows.size(); ++i) {
    if (y >= m_rows[i].y && y < m_rows[i].y + kRowHeight) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool
EditorSceneGraphView::hitTestRootZone(float x, float y) const
{
  if (x < m_x || x > m_x + kWidth) {
    return false;
  }
  const float treeEndY =
    m_treeStartY + static_cast<float>(m_rows.size()) * kRowHeight;
  return y >= treeEndY && y <= m_y + m_height;
}

void
EditorSceneGraphView::clickAtForTesting(float x,
                                        float y,
                                        EditorDocument* document,
                                        std::string* selectedId)
{
  updateLayout();
  rebuildTreeRows(document);
  const int rowIdx = hitTestRow(x, y);
  if (rowIdx >= 0 && static_cast<size_t>(rowIdx) < m_rows.size() &&
      selectedId) {
    *selectedId = m_rows[rowIdx].id;
  }
}

void
EditorSceneGraphView::dragAndDropForTesting(const std::string& sourceId,
                                            const std::string& targetParentId,
                                            EditorDocument* document)
{
  if (document != nullptr && document->canSetParent(sourceId, targetParentId)) {
    document->setParent(sourceId, targetParentId);
  }
}

bool
EditorSceneGraphView::update(InputManager* inputManager,
                             EditorDocument* document,
                             std::string* selectedId,
                             float dt)
{
  m_animTime += std::max(0.0f, dt);
  updateLayout();
  rebuildTreeRows(document);

  m_consumedPress = false;
  bool hierarchyChanged = false;

  // Track mouse coordinates
  if (m_window != nullptr) {
    const std::array<double, 2> mouseCoords = m_window->getMouseCoords();
    const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
    m_mouseX =
      static_cast<float>(mouseCoords[0]) / (scale > 0.0f ? scale : 1.0f);
    m_mouseY =
      static_cast<float>(mouseCoords[1]) / (scale > 0.0f ? scale : 1.0f);
  }

  const bool inPanel = containsScreenPoint(m_mouseX, m_mouseY);
  m_hoverRow = inPanel ? hitTestRow(m_mouseX, m_mouseY) : -1;
  m_hoverRootZone = inPanel ? hitTestRootZone(m_mouseX, m_mouseY) : false;

  if (inputManager != nullptr) {
    const bool mouseDown =
      inputManager->isMouseButtonPressed(KeyCode::MouseLeft);

    // Mouse Press event
    if (mouseDown && !m_mouseWasDown) {
      if (inPanel) {
        m_consumedPress = true;
        if (m_hoverRow >= 0 &&
            static_cast<size_t>(m_hoverRow) < m_rows.size()) {
          const std::string clickedId = m_rows[m_hoverRow].id;
          if (selectedId != nullptr) {
            *selectedId = clickedId;
          }
          // Arm potential drag
          m_draggedNodeId = clickedId;
          m_dragStartX = m_mouseX;
          m_dragStartY = m_mouseY;
          m_isDragging = false;
        } else if (m_hoverRootZone) {
          // Clicking empty space clears selection
          if (selectedId != nullptr) {
            selectedId->clear();
          }
          m_draggedNodeId.clear();
          m_isDragging = false;
        }
      }
    }

    // Drag in progress
    if (mouseDown && !m_draggedNodeId.empty()) {
      const float dx = m_mouseX - m_dragStartX;
      const float dy = m_mouseY - m_dragStartY;
      const float distSq = dx * dx + dy * dy;
      if (distSq > 16.0f) { // 4px threshold
        m_isDragging = true;
      }

      if (m_isDragging) {
        m_consumedPress = true;
        if (inPanel) {
          if (m_hoverRow >= 0 &&
              static_cast<size_t>(m_hoverRow) < m_rows.size()) {
            m_dropTargetNodeId = m_rows[m_hoverRow].id;
            m_dropValid =
              (document != nullptr) &&
              document->canSetParent(m_draggedNodeId, m_dropTargetNodeId);
          } else if (m_hoverRootZone) {
            m_dropTargetNodeId = ""; // reparent to root
            m_dropValid = (document != nullptr) &&
                          document->canSetParent(m_draggedNodeId, "");
          } else {
            m_dropTargetNodeId.clear();
            m_dropValid = false;
          }
        } else {
          m_dropTargetNodeId.clear();
          m_dropValid = false;
        }
      }
    }

    // Mouse Release event
    if (!mouseDown && m_mouseWasDown) {
      if (m_isDragging && !m_draggedNodeId.empty()) {
        if (inPanel && m_dropValid && document != nullptr) {
          if (document->setParent(m_draggedNodeId, m_dropTargetNodeId)) {
            hierarchyChanged = true;
            if (selectedId != nullptr) {
              *selectedId = m_draggedNodeId;
            }
          }
        }
      }
      m_isDragging = false;
      m_draggedNodeId.clear();
      m_dropTargetNodeId.clear();
      m_dropValid = false;
    }

    m_mouseWasDown = mouseDown;
  }

  const std::string activeSel = selectedId ? *selectedId : std::string{};
  rebuildVisual(document, activeSel);
  return hierarchyChanged;
}

void
EditorSceneGraphView::rebuildVisual(const EditorDocument* document,
                                    const std::string& selectedId)
{
  m_visual.clearPrimitives();
  updateLayout();

  const ColorRgba panelBg{ 13, 19, 29, 255 };
  const ColorRgba panelBorder{ 40, 56, 78, 255 };
  const ColorRgba text = UiTheme::textPrimary();
  const ColorRgba muted = UiTheme::textMuted();
  const ColorRgba cyanAccent{ 66, 214, 210, 255 };
  const ColorRgba treeLineColor{ 45, 65, 90, 220 };

  // Background and right border
  m_visual.addFilledRect(m_x, m_y, kWidth, m_height, panelBg);
  m_visual.addLine(
    m_x + kWidth, m_y, m_x + kWidth, m_y + m_height, panelBorder, 1.0f);

  // Section Header: SCENE GRAPH
  const float headerGlow = 0.70f + 0.30f * std::sin(m_animTime * 3.0f);
  m_visual.addFilledRect(
    m_x + 6.0f,
    m_headerY + 8.0f,
    2.0f,
    8.0f,
    ColorRgba{ 66, 214, 210, static_cast<unsigned char>(255.0f * headerGlow) });
  m_visual.addText("SCENE GRAPH", m_x + 12.0f, m_headerY + 6.0f, 11.0f, muted);

  // Node count pill badge
  const size_t totalNodes = document ? document->nodeCount() : 0;
  const std::string countStr = std::to_string(totalNodes);
  m_visual.addFilledRect(m_x + kWidth - 36.0f,
                         m_headerY + 6.0f,
                         28.0f,
                         14.0f,
                         ColorRgba{ 20, 32, 48, 220 });
  m_visual.addOutlineRect(m_x + kWidth - 36.0f,
                          m_headerY + 6.0f,
                          28.0f,
                          14.0f,
                          ColorRgba{ 44, 62, 86, 255 },
                          1.0f);
  m_visual.addText(
    countStr, m_x + kWidth - 28.0f, m_headerY + 7.0f, 10.0f, cyanAccent);

  // Divider under header
  m_visual.addLine(m_x + 6.0f,
                   m_headerY + 24.0f,
                   m_x + kWidth - 6.0f,
                   m_headerY + 24.0f,
                   ColorRgba{ 35, 48, 66, 255 },
                   1.0f);

  // Empty state note if no nodes exist
  if (m_rows.empty()) {
    m_visual.addText(
      "No nodes in scene", m_x + 16.0f, m_treeStartY + 16.0f, 11.0f, muted);
    m_visual.addText("Use Tools or Create menu",
                     m_x + 16.0f,
                     m_treeStartY + 34.0f,
                     11.0f,
                     ColorRgba{ 90, 112, 135, 255 });
    return;
  }

  // Render tree rows
  for (size_t i = 0; i < m_rows.size(); ++i) {
    const TreeRow& row = m_rows[i];
    const bool isSelected = (row.id == selectedId);
    const bool isHovered = (m_hoverRow == static_cast<int>(i));
    const bool isDragged = (m_isDragging && row.id == m_draggedNodeId);
    const bool isDropTarget = (m_isDragging && row.id == m_dropTargetNodeId);

    const float rowX = m_x + 4.0f;
    const float rowW = kWidth - 8.0f;
    const float rowY = row.y;

    // Row background and borders
    if (isDropTarget) {
      if (m_dropValid) {
        // Valid drop target: glowing cyan border and pill
        m_visual.addFilledRect(
          rowX, rowY, rowW, kRowHeight - 2.0f, ColorRgba{ 25, 80, 100, 240 });
        m_visual.addOutlineRect(rowX,
                                rowY,
                                rowW,
                                kRowHeight - 2.0f,
                                ColorRgba{ 66, 214, 210, 255 },
                                1.5f);
        m_visual.addFilledRect(rowX, rowY, 3.0f, kRowHeight - 2.0f, cyanAccent);
      } else {
        // Invalid drop target (cycle / self): red warning pill
        m_visual.addFilledRect(
          rowX, rowY, rowW, kRowHeight - 2.0f, ColorRgba{ 90, 25, 30, 240 });
        m_visual.addOutlineRect(rowX,
                                rowY,
                                rowW,
                                kRowHeight - 2.0f,
                                ColorRgba{ 240, 70, 80, 255 },
                                1.5f);
        m_visual.addFilledRect(
          rowX, rowY, 3.0f, kRowHeight - 2.0f, ColorRgba{ 240, 70, 80, 255 });
      }
    } else if (isSelected) {
      // Selected row: vibrant blue/cyan highlight with glowing left edge
      const float pulse = 0.85f + 0.15f * std::sin(m_animTime * 3.5f);
      const ColorRgba activeBg{ 25, 75, 110, 240 };
      const ColorRgba activeBorder{
        66, 180, 230, static_cast<unsigned char>(255.0f * pulse)
      };
      m_visual.addFilledRect(rowX, rowY, rowW, kRowHeight - 2.0f, activeBg);
      m_visual.addOutlineRect(
        rowX, rowY, rowW, kRowHeight - 2.0f, activeBorder, 1.0f);
      m_visual.addFilledRect(rowX, rowY, 3.0f, kRowHeight - 2.0f, cyanAccent);
    } else if (isHovered && !m_isDragging) {
      m_visual.addFilledRect(
        rowX, rowY, rowW, kRowHeight - 2.0f, ColorRgba{ 24, 38, 56, 220 });
      m_visual.addOutlineRect(rowX,
                              rowY,
                              rowW,
                              kRowHeight - 2.0f,
                              ColorRgba{ 52, 75, 105, 200 },
                              1.0f);
    } else if (isDragged) {
      // Ghost dimming for the node being dragged
      m_visual.addFilledRect(
        rowX, rowY, rowW, kRowHeight - 2.0f, ColorRgba{ 15, 22, 33, 160 });
      m_visual.addOutlineRect(rowX,
                              rowY,
                              rowW,
                              kRowHeight - 2.0f,
                              ColorRgba{ 40, 55, 75, 160 },
                              1.0f);
    }

    // Depth indentation & tree guide branch lines
    const float indent = 14.0f;
    const float contentX = rowX + 8.0f + static_cast<float>(row.depth) * indent;

    if (row.depth > 0) {
      // Draw horizontal hook into the node
      const float hookX0 = contentX - 10.0f;
      const float hookX1 = contentX - 2.0f;
      const float hookY = rowY + (kRowHeight - 2.0f) * 0.5f;
      m_visual.addLine(hookX0, hookY, hookX1, hookY, treeLineColor, 1.0f);

      // Draw vertical stem
      const float stemY0 = rowY;
      const float stemY1 = row.isLastChild ? hookY : (rowY + kRowHeight - 2.0f);
      m_visual.addLine(hookX0, stemY0, hookX0, stemY1, treeLineColor, 1.0f);
    }

    // Node icon (from atlas if available or styled bullet)
    float labelX = contentX;
    if (m_atlas.isValid()) {
      EditorCommand iconCmd = EditorCommand::CreateEmpty;
      switch (row.kind) {
        case SceneNodeKind::FilledRect:
          iconCmd = EditorCommand::CreateRect;
          break;
        case SceneNodeKind::FilledEllipse:
          iconCmd = EditorCommand::CreateEllipse;
          break;
        case SceneNodeKind::FilledTriangle:
          iconCmd = EditorCommand::CreateTriangle;
          break;
        case SceneNodeKind::SolidCube:
          iconCmd = EditorCommand::CreateCube;
          break;
        case SceneNodeKind::SolidPyramid:
          iconCmd = EditorCommand::CreatePyramid;
          break;
        case SceneNodeKind::WireSphere:
          iconCmd = EditorCommand::CreateSphere;
          break;
        default:
          iconCmd = EditorCommand::CreateEmpty;
          break;
      }
      m_visual.addCenteredSprite(m_atlas,
                                 contentX + 7.0f,
                                 rowY + (kRowHeight - 2.0f) * 0.5f,
                                 14.0f,
                                 14.0f,
                                 EditorUiAtlas::regionFor(iconCmd));
      labelX = contentX + 18.0f;
    } else {
      // Small bullet dot
      m_visual.addFilledRect(contentX + 2.0f,
                             rowY + 8.0f,
                             4.0f,
                             4.0f,
                             isSelected ? cyanAccent : muted);
      labelX = contentX + 10.0f;
    }

    // Node label and id tag
    const std::string labelText = row.name + " (#" + row.id + ")";
    const ColorRgba labelColor =
      isDragged ? ColorRgba{ 120, 140, 160, 180 }
                : (isSelected ? ColorRgba{ 255, 255, 255, 255 }
                              : (isHovered ? ColorRgba{ 220, 235, 250, 255 }
                                           : ColorRgba{ 180, 198, 218, 255 }));

    m_visual.addText(labelText, labelX, rowY + 4.0f, 12.0f, labelColor);
  }

  // Root drop zone indicator if dragging
  if (m_isDragging) {
    const float treeEndY =
      m_treeStartY + static_cast<float>(m_rows.size()) * kRowHeight + 6.0f;
    const float zoneH =
      std::min(40.0f, std::max(24.0f, m_y + m_height - treeEndY - 8.0f));

    if (zoneH > 20.0f) {
      const bool isTargetRoot = (m_dropTargetNodeId.empty() && m_hoverRootZone);
      const ColorRgba rootZoneBg = isTargetRoot ? ColorRgba{ 25, 75, 95, 220 }
                                                : ColorRgba{ 15, 22, 33, 180 };
      const ColorRgba rootZoneBorder = isTargetRoot
                                         ? ColorRgba{ 66, 214, 210, 255 }
                                         : ColorRgba{ 35, 50, 70, 180 };

      m_visual.addFilledRect(
        m_x + 8.0f, treeEndY, kWidth - 16.0f, zoneH, rootZoneBg);
      m_visual.addOutlineRect(
        m_x + 8.0f, treeEndY, kWidth - 16.0f, zoneH, rootZoneBorder, 1.0f);
      m_visual.addText("Drop here -> Root",
                       m_x + 16.0f,
                       treeEndY + zoneH * 0.5f - 5.0f,
                       11.0f,
                       isTargetRoot ? ColorRgba{ 255, 255, 255, 255 } : muted);
    }

    // Dragged floating ghost pill following cursor
    const float ghostW = 130.0f;
    const float ghostH = 22.0f;
    const float ghostX = m_mouseX + 12.0f;
    const float ghostY = m_mouseY - 11.0f;

    const ColorRgba ghostBg =
      m_dropValid ? ColorRgba{ 18, 55, 80, 240 } : ColorRgba{ 70, 25, 30, 240 };
    const ColorRgba ghostBorder = m_dropValid ? ColorRgba{ 66, 214, 210, 255 }
                                              : ColorRgba{ 230, 60, 70, 255 };

    m_visual.addFilledRect(ghostX, ghostY, ghostW, ghostH, ghostBg);
    m_visual.addOutlineRect(ghostX, ghostY, ghostW, ghostH, ghostBorder, 1.5f);

    const IlscNode* draggedNode =
      document ? document->findNode(m_draggedNodeId) : nullptr;
    const std::string draggedTitle =
      draggedNode ? (draggedNode->name + " (#" + m_draggedNodeId + ")")
                  : ("#" + m_draggedNodeId);
    m_visual.addText(draggedTitle,
                     ghostX + 8.0f,
                     ghostY + 4.0f,
                     11.0f,
                     ColorRgba{ 255, 255, 255, 255 });
  }
}

bool
EditorSceneGraphView::AppendCommands(Renderer* renderer)
{
  return m_visual.AppendCommands(renderer);
}
