#include "EditorSceneGraphView.h"
#include "EditorShortcuts.h"
#include "EditorToolbar.h"
#include "EditorUiAtlas.h"

#include <Illumo/Gui/GuiToolStyle.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>
#include <vector>

EditorSceneGraphView::EditorSceneGraphView(IRenderWindow* window,
                                           Renderer* renderer)
  : m_window(window)
  , m_renderer(renderer)
  , m_visual(2048u)
  , m_consumedPress(false)
  , m_isDragging(false)
  , m_dropValid(false)
  , m_dragStartX(0.0f)
  , m_dragStartY(0.0f)
  , m_fontSize(EditorToolbar::kDefaultFontSize)
  , m_width(0.0f)
  , m_rowHeight(kDefaultRowHeight)
  , m_x(0.0f)
  , m_y(0.0f)
  , m_height(0.0f)
  , m_treeStartY(0.0f)
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
  // Until the module places it: the top of a left dock column.
  m_placement.area = {
    0.0f, GuiToolStyle::kMenuHeight + GuiToolStyle::kTitleHeight, 250.0f, 400.0f
  };
  updateLayout();
}

void
EditorSceneGraphView::setFontSize(float sizePt)
{
  const float clamped = std::clamp(sizePt, 8.0f, 48.0f);
  if (std::abs(m_fontSize - clamped) > 0.001f) {
    m_fontSize = clamped;
    updateLayout();
  }
}

void
EditorSceneGraphView::setPlacement(const GuiPanelPlacement& placement)
{
  if (placement.surface != m_placement.surface ||
      (m_placement.visible && !placement.visible)) {
    // A drag or menu never follows the panel into another window.
    m_isDragging = false;
    m_draggedNodeId.clear();
    m_dropTargetNodeId.clear();
    m_dropValid = false;
    m_menuOpen = false;
  }
  m_placement = placement;
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
  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  m_rowHeight = std::max(20.0f, std::round(20.0f * fontScale));
  m_x = m_placement.area.x;
  m_y = m_placement.area.y;
  m_width = std::max(0.0f, m_placement.area.w);
  m_height = std::max(0.0f, m_placement.area.h);
  m_treeStartY = m_y + 4.0f;
}

bool
EditorSceneGraphView::containsScreenPoint(float x, float y) const
{
  if (!m_placement.visible) {
    return false;
  }
  return (x >= m_x && x <= m_x + m_width && y >= m_y && y <= m_y + m_height) ||
         menuContains(x, y);
}

static EditorCommand
iconFor(const SceneNode& node)
{
  if (node.components.empty()) {
    return EditorCommand::CreateEmpty;
  }
  const ScenePrimitive* primitive =
    std::get_if<ScenePrimitive>(&node.components.front().value);
  if (primitive == nullptr) {
    return EditorCommand::CreateEmpty;
  }
  switch (primitive->shape) {
    case ScenePrimitiveShape::Rect:
      return EditorCommand::CreateRect;
    case ScenePrimitiveShape::Ellipse:
      return EditorCommand::CreateEllipse;
    case ScenePrimitiveShape::Triangle:
      return EditorCommand::CreateTriangle;
    case ScenePrimitiveShape::Cube:
    case ScenePrimitiveShape::WireCube:
      return EditorCommand::CreateCube;
    case ScenePrimitiveShape::Pyramid:
      return EditorCommand::CreatePyramid;
    case ScenePrimitiveShape::Sphere:
    case ScenePrimitiveShape::WireSphere:
      return EditorCommand::CreateSphere;
  }
  return EditorCommand::CreateEmpty;
}

void
EditorSceneGraphView::rebuildTreeRows(const EditorDocument* document)
{
  m_rows.clear();
  if (document == nullptr) {
    return;
  }
  const SceneGraph& graph = document->graph();
  float currentY = m_treeStartY - m_scroll;
  std::vector<SceneNodeHandle> ancestors;
  // Rows deeper than a folded row are skipped until the walk leaves it.
  int foldedDepth = -1;
  for (SceneNodeHandle handle = graph.firstNode(); !handle.isNull();
       handle = graph.nextNode(handle)) {
    const SceneNodeHandle parent = graph.getParent(handle);
    while (!ancestors.empty() && ancestors.back() != parent) {
      ancestors.pop_back();
    }
    const int depth = static_cast<int>(ancestors.size());
    ancestors.push_back(handle);
    if (foldedDepth >= 0) {
      if (depth > foldedDepth) {
        continue;
      }
      foldedDepth = -1;
    }
    const SceneNode* node =
      document->findNode(std::string(graph.getName(handle)));
    if (node == nullptr) {
      continue;
    }
    TreeRow row;
    row.id = node->id;
    row.name = node->name.empty() ? ("Node #" + node->id) : node->name;
    row.icon = iconFor(*node);
    row.depth = depth;
    row.y = currentY;
    row.isLastChild = graph.getNextSibling(handle).isNull();
    row.hasChildren = graph.getChildCount(handle) > 0;
    row.folded = row.hasChildren && isFolded(row.id);
    row.visible = node->visible;
    if (row.folded) {
      foldedDepth = depth;
    }
    m_rows.push_back(row);
    currentY += m_rowHeight;
  }
}

float
EditorSceneGraphView::treeBottom() const
{
  return m_y + m_height - 4.0f;
}

void
EditorSceneGraphView::clampScroll()
{
  // Two spare rows keep a root drop zone below the last row.
  const float content = static_cast<float>(m_rows.size() + 2) * m_rowHeight;
  const float window = std::max(0.0f, treeBottom() - m_treeStartY);
  const float clamped =
    std::clamp(m_scroll, 0.0f, std::max(0.0f, content - window));
  const float shift = m_scroll - clamped;
  if (shift != 0.0f) {
    for (TreeRow& row : m_rows) {
      row.y += shift;
    }
    m_scroll = clamped;
  }
}

void
EditorSceneGraphView::revealPrimary(const EditorDocument* document,
                                    const EditorSelection* selection)
{
  const std::string primary =
    selection != nullptr ? selection->primary() : std::string();
  if (primary == m_revealedPrimary) {
    return;
  }
  m_revealedPrimary = primary;
  if (primary.empty() || document == nullptr) {
    return;
  }
  bool unfolded = false;
  std::string ancestor = document->scene().parentOf(primary);
  while (!ancestor.empty()) {
    unfolded = m_folded.erase(ancestor) > 0 || unfolded;
    ancestor = document->scene().parentOf(ancestor);
  }
  if (unfolded) {
    rebuildTreeRows(document);
  }
  for (const TreeRow& row : m_rows) {
    if (row.id != primary) {
      continue;
    }
    if (row.y < m_treeStartY) {
      m_scroll -= m_treeStartY - row.y;
    } else if (row.y + m_rowHeight > treeBottom()) {
      m_scroll += row.y + m_rowHeight - treeBottom();
    }
    rebuildTreeRows(document);
    break;
  }
}

EditorSceneGraphView::RowGeometry
EditorSceneGraphView::rowGeometry(const TreeRow& row) const
{
  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  const float indent = std::max(14.0f, std::round(14.0f * fontScale));
  RowGeometry geometry;
  geometry.x = m_x + 2.0f;
  geometry.w = m_width - 4.0f - GuiToolStyle::kScrollbar;
  geometry.y = row.y;
  geometry.h = m_rowHeight - 1.0f;
  geometry.iconSize = std::max(14.0f, std::round(14.0f * fontScale));
  geometry.foldX =
    geometry.x + 4.0f * fontScale + static_cast<float>(row.depth) * indent;
  geometry.contentX = geometry.foldX + 12.0f * fontScale;
  geometry.eyeX =
    geometry.x + geometry.w - geometry.iconSize - 4.0f * fontScale;
  return geometry;
}

int
EditorSceneGraphView::hitTestRow(float x, float y) const
{
  if (!m_placement.visible || x < m_x || x > m_x + m_width ||
      y < m_treeStartY || y >= treeBottom()) {
    return -1;
  }
  for (size_t i = 0; i < m_rows.size(); ++i) {
    if (y >= m_rows[i].y && y < m_rows[i].y + m_rowHeight) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool
EditorSceneGraphView::hitTestRootZone(float x, float y) const
{
  if (!m_placement.visible || x < m_x || x > m_x + m_width) {
    return false;
  }
  const float treeEndY =
    m_rows.empty() ? m_treeStartY : m_rows.back().y + m_rowHeight;
  return y >= std::max(treeEndY, m_treeStartY) && y <= m_y + m_height;
}

bool
EditorSceneGraphView::foldCenterForTesting(size_t index,
                                           float* x,
                                           float* y) const
{
  if (index >= m_rows.size() || !m_rows[index].hasChildren) {
    return false;
  }
  const RowGeometry geometry = rowGeometry(m_rows[index]);
  *x = geometry.foldX + 5.0f;
  *y = geometry.y + geometry.h * 0.5f;
  return true;
}

bool
EditorSceneGraphView::eyeCenterForTesting(size_t index,
                                          float* x,
                                          float* y) const
{
  if (index >= m_rows.size()) {
    return false;
  }
  const RowGeometry geometry = rowGeometry(m_rows[index]);
  *x = geometry.eyeX + geometry.iconSize * 0.5f;
  *y = geometry.y + geometry.h * 0.5f;
  return true;
}

struct SceneGraphMenuEntry
{
  const char* label;
  EditorCommand command;
};

static const std::array<SceneGraphMenuEntry, 10> kMenuEntries = { {
  { "Rename", EditorCommand::Rename },
  { "Duplicate", EditorCommand::Duplicate },
  { "Copy", EditorCommand::Copy },
  { "Cut", EditorCommand::Cut },
  { "Paste", EditorCommand::Paste },
  { "Add Child", EditorCommand::CreateChild },
  { "Show/Hide", EditorCommand::ToggleVisible },
  { "Enable/Disable", EditorCommand::ToggleEnabled },
  { "Unparent", EditorCommand::UnparentNode },
  { "Delete", EditorCommand::DeleteNode },
} };

float
EditorSceneGraphView::menuWidth() const
{
  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  return std::min(std::max(160.0f, std::round(170.0f * fontScale)),
                  std::max(40.0f, m_width - 8.0f));
}

float
EditorSceneGraphView::menuHeight() const
{
  return static_cast<float>(kMenuEntries.size()) * m_rowHeight + 6.0f;
}

bool
EditorSceneGraphView::menuContains(float x, float y) const
{
  return m_menuOpen && x >= m_menuX && x <= m_menuX + menuWidth() &&
         y >= m_menuY && y <= m_menuY + menuHeight();
}

int
EditorSceneGraphView::menuItemAt(float x, float y) const
{
  if (!menuContains(x, y)) {
    return -1;
  }
  const int index =
    static_cast<int>(std::floor((y - m_menuY - 3.0f) / m_rowHeight));
  return index >= 0 && index < static_cast<int>(kMenuEntries.size()) ? index
                                                                     : -1;
}

bool
EditorSceneGraphView::menuItemCenterForTesting(EditorCommand command,
                                               float* x,
                                               float* y) const
{
  for (size_t index = 0; index < kMenuEntries.size(); ++index) {
    if (m_menuOpen && kMenuEntries[index].command == command) {
      *x = m_menuX + menuWidth() * 0.5f;
      *y = m_menuY + 3.0f + (static_cast<float>(index) + 0.5f) * m_rowHeight;
      return true;
    }
  }
  return false;
}

void
EditorSceneGraphView::openMenu(float x,
                               float y,
                               EditorDocument* document,
                               EditorSelection* selection)
{
  const int rowIndex = hitTestRow(x, y);
  m_menuTarget.clear();
  if (rowIndex >= 0) {
    m_menuTarget = m_rows[static_cast<size_t>(rowIndex)].id;
    if (selection != nullptr && !selection->contains(m_menuTarget)) {
      selection->set(m_menuTarget);
    }
  } else if (selection != nullptr && hitTestRootZone(x, y)) {
    selection->clear();
  }
  (void)document;
  m_menuOpen = true;
  m_menuHover = -1;
  // The menu stays inside the panel, which may be its own window.
  m_menuX = std::clamp(x, m_x + 2.0f, m_x + m_width - menuWidth() - 2.0f);
  m_menuY = std::max(m_y, std::min(y, m_y + m_height - menuHeight()));
}

void
EditorSceneGraphView::openContextMenuForTesting(float x,
                                                float y,
                                                EditorDocument* document,
                                                EditorSelection* selection)
{
  updateLayout();
  rebuildTreeRows(document);
  openMenu(x, y, document, selection);
}

EditorCommand
EditorSceneGraphView::takeCommand(std::string* targetId)
{
  const EditorCommand command = m_pendingCommand;
  if (targetId != nullptr) {
    *targetId = m_pendingTarget;
  }
  m_pendingCommand = EditorCommand::None;
  m_pendingTarget.clear();
  return command;
}

bool
EditorSceneGraphView::handlePress(float x,
                                  float y,
                                  EditorDocument* document,
                                  EditorSelection* selection,
                                  bool toggle,
                                  bool armDrag)
{
  if (m_menuOpen) {
    const int item = menuItemAt(x, y);
    if (item >= 0) {
      m_pendingCommand = kMenuEntries[static_cast<size_t>(item)].command;
      m_pendingTarget = m_menuTarget;
    }
    m_menuOpen = false;
    return false;
  }
  const int rowIndex = hitTestRow(x, y);
  if (rowIndex >= 0) {
    const TreeRow& row = m_rows[static_cast<size_t>(rowIndex)];
    const RowGeometry geometry = rowGeometry(row);
    if (row.hasChildren && x >= geometry.foldX - 2.0f &&
        x < geometry.contentX - 1.0f) {
      if (!m_folded.erase(row.id)) {
        m_folded.insert(row.id);
      }
      rebuildTreeRows(document);
      return false;
    }
    if (x >= geometry.eyeX - 2.0f &&
        x <= geometry.eyeX + geometry.iconSize + 2.0f) {
      return document != nullptr && document->setVisible(row.id, !row.visible);
    }
    const std::string clickedId = row.id;
    if (selection != nullptr) {
      if (toggle) {
        selection->toggle(clickedId);
      } else if (selection->contains(clickedId)) {
        selection->add(clickedId);
      } else {
        selection->set(clickedId);
      }
    }
    // A second click on the same row soon after renames it.
    if (!toggle && clickedId == m_lastClickId &&
        m_animTime - m_lastClickTime < 0.35f) {
      m_pendingCommand = EditorCommand::Rename;
      m_pendingTarget = clickedId;
      m_lastClickId.clear();
    } else {
      m_lastClickId = clickedId;
      m_lastClickTime = m_animTime;
    }
    if (armDrag) {
      m_draggedNodeId = clickedId;
      m_dragStartX = x;
      m_dragStartY = y;
      m_isDragging = false;
    }
    return false;
  }
  if (hitTestRootZone(x, y)) {
    if (selection != nullptr) {
      selection->clear();
    }
    m_draggedNodeId.clear();
    m_isDragging = false;
  }
  return false;
}

void
EditorSceneGraphView::clickAtForTesting(float x,
                                        float y,
                                        EditorDocument* document,
                                        EditorSelection* selection,
                                        bool toggle)
{
  updateLayout();
  rebuildTreeRows(document);
  clampScroll();
  handlePress(x, y, document, selection, toggle, false);
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
EditorSceneGraphView::dropForTesting(const std::string& sourceId,
                                     const std::string& targetId,
                                     int zone,
                                     EditorDocument* document)
{
  return applyDrop(sourceId, targetId, zone, document);
}

// The parent and insert-before sibling a drop lands on: into the target, or
// beside it (zone -1 before, 1 after).
static bool
dropPlacement(const EditorDocument& document,
              const std::string& sourceId,
              const std::string& targetId,
              int zone,
              std::string* parentId,
              std::string* insertBefore)
{
  if (zone == 0) {
    *parentId = targetId;
    insertBefore->clear();
    return document.canSetParent(sourceId, targetId);
  }
  const SceneNode* target = document.findNode(targetId);
  if (target == nullptr || targetId == sourceId) {
    return false;
  }
  *parentId = document.scene().parentOf(targetId);
  *insertBefore =
    zone < 0 ? targetId : document.scene().nextSiblingId(targetId);
  if (*insertBefore == sourceId) {
    *insertBefore = document.scene().nextSiblingId(sourceId);
  }
  return document.canSetParent(sourceId, *parentId);
}

bool
EditorSceneGraphView::applyDrop(const std::string& sourceId,
                                const std::string& targetId,
                                int zone,
                                EditorDocument* document)
{
  std::string parentId;
  std::string insertBefore;
  if (document == nullptr ||
      !dropPlacement(
        *document, sourceId, targetId, zone, &parentId, &insertBefore)) {
    return false;
  }
  return document->setParent(sourceId, parentId, insertBefore);
}

bool
EditorSceneGraphView::update(InputManager* inputManager,
                             EditorDocument* document,
                             EditorSelection* selection,
                             float dt)
{
  m_animTime += std::max(0.0f, dt);
  updateLayout();

  m_consumedPress = false;
  bool hierarchyChanged = false;

  m_pointer.sample(m_placement, m_window, m_renderer, inputManager);
  m_mouseX = m_pointer.x();
  m_mouseY = m_pointer.y();
  const bool inPanel = containsScreenPoint(m_mouseX, m_mouseY);

  if (inPanel) {
    const float wheel = m_pointer.takeWheel();
    if (wheel != 0.0f) {
      m_scroll -= wheel * m_rowHeight * 3.0f;
    }
  }
  if (m_isDragging) {
    // Auto-scroll while a dragged row nears the window's edges.
    if (m_mouseY < m_treeStartY + m_rowHeight) {
      m_scroll -= 480.0f * std::max(0.0f, dt);
    } else if (m_mouseY > treeBottom() - m_rowHeight) {
      m_scroll += 480.0f * std::max(0.0f, dt);
    }
  }
  if (m_placement.visible) {
    revealPrimary(document, selection);
    rebuildTreeRows(document);
    clampScroll();
  } else {
    m_rows.clear();
  }

  m_menuHover = menuItemAt(m_mouseX, m_mouseY);
  const bool overPanel = inPanel && !menuContains(m_mouseX, m_mouseY);
  m_hoverRow = overPanel ? hitTestRow(m_mouseX, m_mouseY) : -1;
  m_hoverRootZone = overPanel ? hitTestRootZone(m_mouseX, m_mouseY) : false;

  if (inputManager != nullptr) {
    const bool mouseDown = m_pointer.pressed();
    if (m_pointer.rightClicked() && inPanel && !m_isDragging) {
      m_consumedPress = true;
      openMenu(m_mouseX, m_mouseY, document, selection);
    }

    if (m_pointer.clicked()) {
      if (m_menuOpen) {
        // Any click closes the menu; it never reaches the viewport.
        m_consumedPress = true;
        handlePress(m_mouseX, m_mouseY, document, selection, false, false);
      } else if (inPanel) {
        m_consumedPress = true;
        hierarchyChanged = handlePress(m_mouseX,
                                       m_mouseY,
                                       document,
                                       selection,
                                       inputManager->isControlPressed() ||
                                         inputManager->isShiftPressed(),
                                       true) ||
                           hierarchyChanged;
      }
    }

    if (mouseDown && !m_draggedNodeId.empty()) {
      const float dx = m_mouseX - m_dragStartX;
      const float dy = m_mouseY - m_dragStartY;
      if (dx * dx + dy * dy > 16.0f) { // 4px threshold
        m_isDragging = true;
      }
      if (m_isDragging) {
        m_consumedPress = true;
        m_dropTargetNodeId.clear();
        m_dropValid = false;
        m_dropZone = 0;
        if (inPanel && m_hoverRow >= 0) {
          const TreeRow& row = m_rows[static_cast<size_t>(m_hoverRow)];
          const float within = (m_mouseY - row.y) / m_rowHeight;
          m_dropZone = within < 0.3f ? -1 : (within > 0.7f ? 1 : 0);
          m_dropTargetNodeId = row.id;
          std::string parentId;
          std::string insertBefore;
          m_dropValid = document != nullptr && dropPlacement(*document,
                                                             m_draggedNodeId,
                                                             row.id,
                                                             m_dropZone,
                                                             &parentId,
                                                             &insertBefore);
        } else if (inPanel && m_hoverRootZone) {
          m_dropValid =
            document != nullptr && document->canSetParent(m_draggedNodeId, "");
        }
      }
    }

    if (m_pointer.released()) {
      if (m_isDragging && !m_draggedNodeId.empty() && inPanel && m_dropValid &&
          document != nullptr) {
        const bool dropped =
          m_dropTargetNodeId.empty()
            ? document->setParent(m_draggedNodeId, "")
            : applyDrop(
                m_draggedNodeId, m_dropTargetNodeId, m_dropZone, document);
        if (dropped) {
          hierarchyChanged = true;
          if (selection != nullptr && !selection->contains(m_draggedNodeId)) {
            selection->set(m_draggedNodeId);
          }
        }
      }
      m_isDragging = false;
      m_draggedNodeId.clear();
      m_dropTargetNodeId.clear();
      m_dropValid = false;
      m_dropZone = 0;
    }
  }

  if (hierarchyChanged && m_placement.visible) {
    rebuildTreeRows(document);
    clampScroll();
  }
  rebuildVisual(document, selection);
  return hierarchyChanged;
}

// A small eye drawn with lines; struck through when the node is hidden.
static void
drawEye(GameVisual& visual,
        float x,
        float centreY,
        float size,
        bool open,
        ColorRgba color)
{
  const float height = size * 0.55f;
  const float top = centreY - height * 0.5f;
  visual.addLine(x, centreY, x + size * 0.5f, top, color, 1.0f);
  visual.addLine(x + size * 0.5f, top, x + size, centreY, color, 1.0f);
  visual.addLine(x + size, centreY, x + size * 0.5f, top + height, color, 1.0f);
  visual.addLine(x + size * 0.5f, top + height, x, centreY, color, 1.0f);
  if (open) {
    visual.addFilledRect(
      x + size * 0.5f - 2.0f, centreY - 2.0f, 4.0f, 4.0f, color);
  } else {
    visual.addLine(
      x + 1.0f, top + height + 1.0f, x + size - 1.0f, top - 1.0f, color, 1.0f);
  }
}

void
EditorSceneGraphView::rebuildVisual(const EditorDocument* document,
                                    const EditorSelection* selection)
{
  m_visual.clearPrimitives();
  updateLayout();
  if (!m_placement.visible || m_width <= 0.0f || m_height <= 0.0f) {
    return;
  }
  m_visual.setPixelClipRect(Rect2{ m_x, m_y, m_width, m_height });

  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  const float smallFont = std::max(9.0f, std::round(11.0f * fontScale));
  const float rowFont = std::max(10.0f, std::round(13.0f * fontScale));
  const float iconSize = std::max(14.0f, std::round(14.0f * fontScale));
  const float indent = std::max(14.0f, std::round(14.0f * fontScale));

  if (m_rows.empty()) {
    GuiToolStyle::fittedText(m_visual,
                             "No nodes yet",
                             m_x + GuiToolStyle::kPad,
                             m_treeStartY + 6.0f,
                             m_width - GuiToolStyle::kPad * 2.0f,
                             smallFont,
                             GuiToolPalette::dim);
    GuiToolStyle::fittedText(m_visual,
                             "Use Create or the Tools panel",
                             m_x + GuiToolStyle::kPad,
                             m_treeStartY + 8.0f + smallFont * 1.5f,
                             m_width - GuiToolStyle::kPad * 2.0f,
                             smallFont,
                             GuiToolPalette::faint);
  }

  // Rows inside the row window only.
  const float windowTop = m_treeStartY;
  const float windowBottom = treeBottom();
  for (size_t i = 0; i < m_rows.size(); ++i) {
    const TreeRow& row = m_rows[i];
    if (row.y + m_rowHeight <= windowTop) {
      continue;
    }
    if (row.y >= windowBottom) {
      break;
    }
    const bool isSelected = selection != nullptr && selection->contains(row.id);
    const bool isHovered = m_hoverRow == static_cast<int>(i);
    const bool isDragged = m_isDragging && row.id == m_draggedNodeId;
    const bool isDropTarget = m_isDragging && row.id == m_dropTargetNodeId;
    const bool dropInto = isDropTarget && m_dropZone == 0;

    const RowGeometry geometry = rowGeometry(row);
    const GuiToolRect rect{ geometry.x, geometry.y, geometry.w, geometry.h };
    GuiToolStyle::row(
      m_visual, rect, isSelected, isHovered && !m_isDragging && !isSelected);
    const ColorRgba dropColor =
      m_dropValid ? GuiToolPalette::accent : GuiToolPalette::error;
    if (dropInto) {
      m_visual.addOutlineRect(rect.x, rect.y, rect.w, rect.h, dropColor, 1.0f);
    } else if (isDropTarget) {
      // Insertion line above or below the target, at its depth.
      const float lineY =
        m_dropZone < 0 ? rect.y - 1.0f : rect.y + m_rowHeight - 1.0f;
      m_visual.addFilledRect(geometry.foldX,
                             lineY - 1.0f,
                             rect.x + rect.w - geometry.foldX,
                             2.0f,
                             dropColor);
    }

    // Tree guide: a hook from the parent's fold column into this row.
    const float hookY = rect.y + rect.h * 0.5f;
    if (row.depth > 0) {
      const float stemX = geometry.foldX - indent + 5.0f * fontScale;
      m_visual.addLine(
        stemX, hookY, geometry.foldX - 1.0f, hookY, GuiToolPalette::rule, 1.0f);
      const float stemBottom = row.isLastChild ? hookY : (rect.y + m_rowHeight);
      m_visual.addLine(
        stemX, rect.y, stemX, stemBottom, GuiToolPalette::rule, 1.0f);
    }

    // Fold arrow: right-pointing (folded) or down-pointing.
    if (row.hasChildren) {
      const float ax = geometry.foldX + 1.0f;
      const float size = std::max(6.0f, std::round(7.0f * fontScale));
      const ColorRgba arrow =
        isHovered ? GuiToolPalette::text : GuiToolPalette::dim;
      if (row.folded) {
        m_visual.addLine(
          ax, hookY - size * 0.5f, ax + size * 0.6f, hookY, arrow, 1.0f);
        m_visual.addLine(
          ax + size * 0.6f, hookY, ax, hookY + size * 0.5f, arrow, 1.0f);
      } else {
        m_visual.addLine(ax,
                         hookY - size * 0.3f,
                         ax + size * 0.5f,
                         hookY + size * 0.3f,
                         arrow,
                         1.0f);
        m_visual.addLine(ax + size * 0.5f,
                         hookY + size * 0.3f,
                         ax + size,
                         hookY - size * 0.3f,
                         arrow,
                         1.0f);
      }
    }

    float labelX = geometry.contentX;
    if (m_atlas.isValid()) {
      m_visual.addCenteredSprite(m_atlas,
                                 geometry.contentX + iconSize * 0.5f,
                                 hookY,
                                 iconSize,
                                 iconSize,
                                 EditorUiAtlas::regionFor(row.icon),
                                 row.visible ? ColorRgba{ 255, 255, 255, 255 }
                                             : ColorRgba{ 255, 255, 255, 110 });
      labelX = geometry.contentX + iconSize + 4.0f * fontScale;
    }

    ColorRgba labelColor = isDragged     ? GuiToolPalette::faint
                           : row.visible ? GuiToolPalette::text
                                         : GuiToolPalette::dim;
    const float textY =
      rect.y + std::max(0.0f, std::round((rect.h - rowFont) * 0.5f)) - 1.0f;
    GuiToolStyle::fittedText(m_visual,
                             row.name,
                             labelX,
                             textY,
                             geometry.eyeX - 4.0f * fontScale - labelX,
                             rowFont,
                             labelColor);

    if (isHovered || isSelected || !row.visible) {
      drawEye(m_visual,
              geometry.eyeX,
              hookY,
              iconSize,
              row.visible,
              row.visible ? GuiToolPalette::dim : GuiToolPalette::faint);
    }
  }

  // Scrollbar when rows overflow the window.
  const float content = static_cast<float>(m_rows.size() + 2) * m_rowHeight;
  GuiToolStyle::scrollbar(m_visual,
                          GuiToolRect{ m_x + m_width - GuiToolStyle::kScrollbar,
                                       windowTop,
                                       GuiToolStyle::kScrollbar - 1.0f,
                                       windowBottom - windowTop },
                          m_scroll,
                          windowBottom - windowTop,
                          content);

  if (m_isDragging) {
    // The root drop zone below the rows, and a label beside the cursor.
    const float treeEndY =
      std::max(m_treeStartY,
               m_rows.empty() ? m_treeStartY : m_rows.back().y + m_rowHeight) +
      4.0f;
    const float zoneH = std::min(
      32.0f * fontScale, std::max(0.0f, m_y + m_height - treeEndY - 6.0f));
    if (zoneH > 16.0f) {
      const bool isTargetRoot = m_dropTargetNodeId.empty() && m_hoverRootZone;
      m_visual.addOutlineRect(m_x + GuiToolStyle::kPad,
                              treeEndY,
                              m_width - GuiToolStyle::kPad * 2.0f,
                              zoneH,
                              isTargetRoot ? GuiToolPalette::accent
                                           : GuiToolPalette::border,
                              1.0f);
      GuiToolStyle::text(
        m_visual,
        "Drop here to move to the root",
        m_x + GuiToolStyle::kPad * 2.0f,
        treeEndY + std::max(0.0f, std::round((zoneH - smallFont) * 0.5f)),
        smallFont,
        isTargetRoot ? GuiToolPalette::text : GuiToolPalette::dim);
    }
    const SceneNode* draggedNode =
      document ? document->findNode(m_draggedNodeId) : nullptr;
    const std::string draggedTitle =
      draggedNode ? draggedNode->name : ("#" + m_draggedNodeId);
    const float ghostW = std::max(120.0f, std::round(130.0f * fontScale));
    const float ghostX = m_mouseX + 12.0f;
    const float ghostY = m_mouseY - m_rowHeight * 0.5f;
    m_visual.addFilledRect(
      ghostX, ghostY, ghostW, m_rowHeight, GuiToolPalette::bar);
    m_visual.addOutlineRect(ghostX,
                            ghostY,
                            ghostW,
                            m_rowHeight,
                            m_dropValid ? GuiToolPalette::accent
                                        : GuiToolPalette::error,
                            1.0f);
    GuiToolStyle::fittedText(
      m_visual,
      draggedTitle,
      ghostX + 6.0f,
      ghostY + std::max(0.0f, std::round((m_rowHeight - smallFont) * 0.5f)),
      ghostW - 12.0f,
      smallFont,
      GuiToolPalette::text);
  }

  // Context menu, drawn last so it sits above the rows.
  if (m_menuOpen) {
    const float menuW = menuWidth();
    const float menuH = menuHeight();
    m_visual.addFilledRect(m_menuX, m_menuY, menuW, menuH, GuiToolPalette::bar);
    m_visual.addOutlineRect(
      m_menuX, m_menuY, menuW, menuH, GuiToolPalette::border, 1.0f);
    for (size_t index = 0; index < kMenuEntries.size(); ++index) {
      const float itemY =
        m_menuY + 3.0f + static_cast<float>(index) * m_rowHeight;
      if (m_menuHover == static_cast<int>(index)) {
        m_visual.addFilledRect(m_menuX + 2.0f,
                               itemY,
                               menuW - 4.0f,
                               m_rowHeight,
                               GuiToolPalette::selection);
      }
      const float textY =
        itemY + std::max(0.0f, std::round((m_rowHeight - rowFont) * 0.5f)) -
        1.0f;
      GuiToolStyle::text(m_visual,
                         kMenuEntries[index].label,
                         m_menuX + GuiToolStyle::kPad,
                         textY,
                         rowFont,
                         GuiToolPalette::text);
      const std::string hint =
        EditorShortcuts::labelFor(kMenuEntries[index].command);
      if (!hint.empty()) {
        GuiToolStyle::text(m_visual,
                           hint,
                           m_menuX + menuW - GuiToolStyle::kPad -
                             GuiToolStyle::textWidth(hint, smallFont),
                           textY + 1.0f,
                           smallFont,
                           GuiToolPalette::faint);
      }
    }
  }
}

bool
EditorSceneGraphView::AppendCommands(Renderer* renderer)
{
  if (!m_placement.visible) {
    return true;
  }
  return m_visual.AppendCommands(renderer);
}
