#include "EditorScene.h"

#include "EditorAssets.h"
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

glm::mat4
EditorScene::currentViewProjection() const
{
  float aspect = 1.0f;
  if (ic != nullptr && ic->window != nullptr) {
    const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
    if (dimensions[1] > 0) {
      aspect =
        static_cast<float>(dimensions[0]) / static_cast<float>(dimensions[1]);
    }
  }
  if (ic == nullptr || ic->camera == nullptr) {
    return glm::mat4(1.0f);
  }
  return ic->camera->GetMVPMatrix(aspect);
}

void
EditorScene::applyWorldCamera()
{
  if (ic == nullptr || ic->camera == nullptr) {
    return;
  }
  if (m_document.worldMode() != SceneWorldMode::World3D) {
    ic->camera->setProjectionType(ProjectionType::Orthographic);
    return;
  }
  const SceneEditorState& state = m_document.editorState();
  const glm::dvec2 position = ic->camera->GetPositionPrecise();
  const float zoom = ic->camera->GetZoom();
  const float distance = std::max(2.0f, 12.0f / std::max(0.15f, zoom / 32.0f));
  const glm::vec3 target(static_cast<float>(position.x),
                         m_cameraTargetY,
                         static_cast<float>(position.y));
  const glm::vec3 eye =
    target + glm::vec3(std::cos(state.pitch) * std::sin(state.yaw) * distance,
                       std::sin(state.pitch) * distance,
                       std::cos(state.pitch) * std::cos(state.yaw) * distance);
  ic->camera->lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
  ic->camera->setPerspective(50.0f, 0.1f, 250.0f);
  ic->camera->setProjectionType(ProjectionType::Perspective);
}

// The grid window the current view needs: its centre (snapped to major
// lines) and half extent in world units, covering the viewport with margin.
static void
gridWindowFor(bool is3D,
              float spacing,
              float cameraX,
              float cameraY,
              float zoom,
              float viewWidth,
              float viewHeight,
              float* centerX,
              float* centerY,
              float* half)
{
  if (is3D) {
    const float distance =
      std::max(2.0f, 12.0f / std::max(0.15f, zoom / 32.0f));
    *half = std::max(20.0f * spacing, distance * 3.0f);
  } else {
    const float scale = std::max(0.01f, zoom);
    *half = std::max(viewWidth, viewHeight) * 0.5f / scale * 1.3f;
  }
  const float major = spacing * 5.0f;
  *centerX = std::round(cameraX / major) * major;
  *centerY = std::round(cameraY / major) * major;
}

void
EditorScene::followGrid()
{
  if (ic == nullptr || ic->camera == nullptr || !m_gridBuilt) {
    return;
  }
  const float spacing = std::max(0.001f, m_document.editorState().gridSpacing);
  float width = 0.0f;
  float height = 0.0f;
  viewportPixels(&width, &height, nullptr, nullptr);
  const glm::dvec2 position = ic->camera->GetPositionPrecise();
  float centerX = 0.0f;
  float centerY = 0.0f;
  float half = 0.0f;
  gridWindowFor(m_document.worldMode() == SceneWorldMode::World3D,
                spacing,
                static_cast<float>(position.x),
                static_cast<float>(position.y),
                ic->camera->GetZoom(),
                width,
                height,
                &centerX,
                &centerY,
                &half);
  // Rebuild when the view outgrows the grid, shrinks well inside it, or
  // drifts toward its edge.
  const float drift = std::max(std::fabs(centerX - m_gridCenterX),
                               std::fabs(centerY - m_gridCenterY));
  if (half > m_gridHalf || half < m_gridHalf * 0.4f ||
      drift > m_gridHalf * 0.25f || spacing != m_gridSpacing) {
    rebuildGrid();
  }
}

void
EditorScene::rebuildGrid()
{
  if (!m_grid || ic == nullptr || ic->renderer == nullptr) {
    return;
  }
  m_grid->clearPrimitives();
  const float spacing = std::max(0.001f, m_document.editorState().gridSpacing);
  const bool is3D = m_document.worldMode() == SceneWorldMode::World3D;
  float width = 1280.0f;
  float height = 720.0f;
  viewportPixels(&width, &height, nullptr, nullptr);
  const glm::dvec2 position =
    ic->camera != nullptr ? ic->camera->GetPositionPrecise() : glm::dvec2(0.0);
  const float zoom = ic->camera != nullptr ? ic->camera->GetZoom() : 32.0f;
  gridWindowFor(is3D,
                spacing,
                static_cast<float>(position.x),
                static_cast<float>(position.y),
                zoom,
                width,
                height,
                &m_gridCenterX,
                &m_gridCenterY,
                &m_gridHalf);
  m_gridSpacing = spacing;
  // Far out, minor lines would crowd: step in fives so at most about 160
  // lines cross each way, every fifth of them major.
  float step = spacing;
  while (m_gridHalf / step > 80.0f) {
    step *= 5.0f;
  }
  const int count = static_cast<int>(std::ceil(m_gridHalf / step));
  const float firstX = m_gridCenterX - static_cast<float>(count) * step;
  const float firstY = m_gridCenterY - static_cast<float>(count) * step;
  const float extent = static_cast<float>(count) * step;
  const ColorRgba minorColor =
    is3D ? ColorRgba{ 38, 50, 66, 255 } : ColorRgba{ 32, 44, 58, 255 };
  const ColorRgba majorColor =
    is3D ? ColorRgba{ 58, 76, 100, 255 } : ColorRgba{ 48, 66, 88, 255 };
  for (int i = 0; i <= count * 2; ++i) {
    const float x = firstX + static_cast<float>(i) * step;
    const float y = firstY + static_cast<float>(i) * step;
    const bool majorX =
      std::fmod(std::fabs(std::round(x / step)), 5.0f) == 0.0f;
    const bool majorY =
      std::fmod(std::fabs(std::round(y / step)), 5.0f) == 0.0f;
    // The world axes are drawn in colour below.
    if (std::fabs(x) > step * 0.01f) {
      if (is3D) {
        m_grid->addLine(glm::vec3(x, 0.0f, m_gridCenterY - extent),
                        glm::vec3(x, 0.0f, m_gridCenterY + extent),
                        majorX ? majorColor : minorColor);
      } else {
        m_grid->addLine(glm::vec3(x, m_gridCenterY - extent, 0.0f),
                        glm::vec3(x, m_gridCenterY + extent, 0.0f),
                        majorX ? majorColor : minorColor);
      }
    }
    if (std::fabs(y) > step * 0.01f) {
      if (is3D) {
        m_grid->addLine(glm::vec3(m_gridCenterX - extent, 0.0f, y),
                        glm::vec3(m_gridCenterX + extent, 0.0f, y),
                        majorY ? majorColor : minorColor);
      } else {
        m_grid->addLine(glm::vec3(m_gridCenterX - extent, y, 0.0f),
                        glm::vec3(m_gridCenterX + extent, y, 0.0f),
                        majorY ? majorColor : minorColor);
      }
    }
  }
  const float left = m_gridCenterX - extent;
  const float right = m_gridCenterX + extent;
  const float bottom = m_gridCenterY - extent;
  const float top = m_gridCenterY + extent;
  if (is3D) {
    if (bottom <= 0.0f && top >= 0.0f) {
      m_grid->addLine(glm::vec3(left, 0.0f, 0.0f),
                      glm::vec3(right, 0.0f, 0.0f),
                      ColorRgba{ 220, 65, 65, 255 });
    }
    if (left <= 0.0f && right >= 0.0f) {
      m_grid->addLine(glm::vec3(0.0f, 0.0f, bottom),
                      glm::vec3(0.0f, 0.0f, top),
                      ColorRgba{ 65, 120, 230, 255 });
    }
    m_grid->addLine(glm::vec3(0.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 2.5f, 0.0f),
                    ColorRgba{ 65, 210, 95, 255 });
  } else {
    if (bottom <= 0.0f && top >= 0.0f) {
      m_grid->addLine(glm::vec3(left, 0.0f, 0.0f),
                      glm::vec3(right, 0.0f, 0.0f),
                      ColorRgba{ 190, 60, 60, 255 });
    }
    if (left <= 0.0f && right >= 0.0f) {
      m_grid->addLine(glm::vec3(0.0f, bottom, 0.0f),
                      glm::vec3(0.0f, top, 0.0f),
                      ColorRgba{ 60, 180, 85, 255 });
    }
    const float cross = 0.35f;
    m_grid->addLine(glm::vec3(-cross, 0.0f, 0.01f),
                    glm::vec3(cross, 0.0f, 0.01f),
                    ColorRgba{ 255, 220, 100, 255 });
    m_grid->addLine(glm::vec3(0.0f, -cross, 0.01f),
                    glm::vec3(0.0f, cross, 0.01f),
                    ColorRgba{ 255, 220, 100, 255 });
  }
}
void
EditorScene::updateCamera(double dt)
{
  if (ic == nullptr || ic->camera == nullptr || ic->inputManager == nullptr ||
      ic->window == nullptr) {
    return;
  }
  const std::array<double, 2> mouse = ic->window->getMouseCoords();
  const bool middle =
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseMiddle);
  // A right-click that opened the hierarchy menu never orbits or pans.
  const bool right =
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseRight) &&
    !(m_sceneGraphView && m_sceneGraphView->menuOpen());
  const bool keyboardFree =
    !ic->inputManager->isControlPressed() && !ic->inputManager->isAltPressed();

  if (m_document.worldMode() == SceneWorldMode::World3D) {
    const SceneEditorState& camState = m_document.editorState();
    const float sinYaw = std::sin(camState.yaw);
    const float cosYaw = std::cos(camState.yaw);
    // Camera right on XZ: (cosYaw, 0, -sinYaw); forward: (-sinYaw, 0, -cosYaw)
    const glm::dvec2 rightDir(cosYaw, -sinYaw);
    const glm::dvec2 forwardDir(-sinYaw, -cosYaw);

    if (keyboardFree) {
      const double speed = 420.0 * dt;
      glm::dvec2 pan(0.0, 0.0);
      if (ic->inputManager->isKeyPressed(KeyCode::Left)) {
        pan -= rightDir * speed;
      }
      if (ic->inputManager->isKeyPressed(KeyCode::Right)) {
        pan += rightDir * speed;
      }
      if (ic->inputManager->isKeyPressed(KeyCode::Up)) {
        pan += forwardDir * speed;
      }
      if (ic->inputManager->isKeyPressed(KeyCode::Down)) {
        pan -= forwardDir * speed;
      }
      if (ic->inputManager->isKeyPressed(KeyCode::PageUp)) {
        m_cameraTargetY += static_cast<float>(speed);
      }
      if (ic->inputManager->isKeyPressed(KeyCode::PageDown)) {
        m_cameraTargetY -= static_cast<float>(speed);
      }
      if (pan.x != 0.0 || pan.y != 0.0) {
        ic->camera->Pan(pan);
      }
    }

    if (middle) {
      if (m_panning) {
        const double deltaScreenX = mouse[0] - m_lastMouseX;
        const double deltaScreenY = mouse[1] - m_lastMouseY;
        // Dragging right moves the world right; dragging down moves the
        // camera forward.
        const glm::dvec2 panOffset =
          -rightDir * deltaScreenX + forwardDir * deltaScreenY;
        ic->camera->Pan(panOffset);
      }
      m_panning = true;
    } else {
      m_panning = false;
    }

    if (right) {
      SceneEditorState state = m_document.editorState();
      state.yaw += static_cast<float>(mouse[0] - m_lastMouseX) * 0.01f;
      state.yaw = std::remainder(state.yaw, 6.28318530718f);
      state.pitch = std::clamp(
        state.pitch + static_cast<float>(m_lastMouseY - mouse[1]) * 0.01f,
        0.05f,
        1.5f);
      m_document.setEditorState(state);
    }
  } else {
    if (keyboardFree) {
      const float speed = 420.0f * static_cast<float>(dt);
      glm::vec2 pan(0.0f, 0.0f);
      if (ic->inputManager->isKeyPressed(KeyCode::Left)) {
        pan.x -= speed;
      }
      if (ic->inputManager->isKeyPressed(KeyCode::Right)) {
        pan.x += speed;
      }
      if (ic->inputManager->isKeyPressed(KeyCode::Up)) {
        pan.y += speed;
      }
      if (ic->inputManager->isKeyPressed(KeyCode::Down)) {
        pan.y -= speed;
      }
      if (pan.x != 0.0f || pan.y != 0.0f) {
        ic->camera->Pan(pan);
      }
    }

    if (middle || right) {
      if (m_panning) {
        ic->camera->Pan(
          glm::dvec2(m_lastMouseX - mouse[0], mouse[1] - m_lastMouseY));
      }
      m_panning = true;
    } else {
      m_panning = false;
    }
  }

  double* scroll = ic->inputManager->getMouseScrollOffset();
  if (scroll != nullptr && *scroll != 0.0) {
    const float factor = *scroll > 0.0 ? 1.1f : 0.9f;
    if (m_document.worldMode() != SceneWorldMode::World3D) {
      // Anchored in the view ZoomAt works in, so quick notches stay under
      // the cursor while the camera still eases.
      const glm::dvec2 worldMouse =
        ic->camera->ScreenToTargetWorldPrecise(glm::dvec2(mouse[0], mouse[1]));
      ic->camera->ZoomAt(factor, worldMouse);
    } else {
      ic->camera->ZoomAt(factor, ic->camera->GetPositionPrecise());
    }
    *scroll = 0.0;
  }
}

void
EditorScene::frameSelection()
{
  if (ic == nullptr || ic->camera == nullptr || ic->window == nullptr) {
    return;
  }
  const SceneGraph& graph = m_document.graph();
  bool any = false;
  AxisAlignedBounds3 bounds;
  const std::vector<std::string>& ids = m_selection.ids();
  std::vector<std::string> all;
  if (ids.empty()) {
    for (SceneNodeHandle node = graph.firstNode(); !node.isNull();
         node = graph.nextNode(node)) {
      all.emplace_back(graph.getName(node));
    }
  }
  for (const std::string& id : ids.empty() ? all : ids) {
    AxisAlignedBounds3 world;
    const SceneNodeHandle handle = m_document.nodeHandle(id);
    if (!graph.getWorldBounds(handle, &world)) {
      Matrix4 matrix(1.0f);
      graph.getWorldTransform(handle, &matrix);
      const Vector3 position(matrix[3]);
      world = AxisAlignedBounds3{ position - Vector3(0.5f),
                                  position + Vector3(0.5f) };
    }
    if (!any) {
      bounds = world;
      any = true;
    } else {
      bounds.include(world);
    }
  }
  if (!any) {
    return;
  }
  const Vector3 center = (bounds.minimum + bounds.maximum) * 0.5f;
  const Vector3 size = glm::max(bounds.maximum - bounds.minimum, Vector3(0.5f));
  // Fit the visible viewport between the dock columns, not the window.
  float width = 0.0f;
  float height = 0.0f;
  viewportPixels(&width, &height, nullptr, nullptr);
  if (m_document.worldMode() == SceneWorldMode::World3D) {
    const float radius = glm::length(size) * 0.5f;
    const float distance = std::max(2.0f, radius * 2.6f);
    placeCamera(center, std::clamp(32.0f * 12.0f / distance, 0.5f, 100.0f));
  } else {
    const float zoom = std::min(width / size.x, height / size.y) * 0.6f;
    placeCamera(center, std::clamp(zoom, 0.5f, 100.0f));
  }
}

void
EditorScene::viewportPixels(float* width,
                            float* height,
                            float* offsetX,
                            float* offsetY) const
{
  const std::array<int, 2> dimensions = ic != nullptr && ic->window != nullptr
                                          ? ic->window->getWindowDimensions()
                                          : std::array<int, 2>{ 1, 1 };
  const float windowWidth = static_cast<float>(std::max(1, dimensions[0]));
  const float windowHeight = static_cast<float>(std::max(1, dimensions[1]));
  float w = windowWidth;
  float h = windowHeight;
  float dx = 0.0f;
  float dy = 0.0f;
  const GuiToolRect center = m_dock.center();
  if (ic != nullptr && center.w > 1.0f && center.h > 1.0f) {
    const float scale =
      GuiPanelLayout::viewport(ic->window, ic->renderer).layoutScale;
    w = center.w * scale;
    h = center.h * scale;
    dx = (center.x + center.w * 0.5f) * scale - windowWidth * 0.5f;
    dy = (center.y + center.h * 0.5f) * scale - windowHeight * 0.5f;
  }
  if (width != nullptr) {
    *width = w;
  }
  if (height != nullptr) {
    *height = h;
  }
  if (offsetX != nullptr) {
    *offsetX = dx;
  }
  if (offsetY != nullptr) {
    *offsetY = dy;
  }
}

void
EditorScene::placeCamera(const Vector3& point, float zoom)
{
  if (ic == nullptr || ic->camera == nullptr || ic->window == nullptr) {
    return;
  }
  // The camera looks at the window's centre; shift its target so `point`
  // lands on the viewport's centre instead.
  float offsetX = 0.0f;
  float offsetY = 0.0f;
  viewportPixels(nullptr, nullptr, &offsetX, &offsetY);
  ic->camera->SetZoom(zoom);
  if (m_document.worldMode() == SceneWorldMode::World3D) {
    // World units per pixel at the target, from the editor camera's fixed
    // 50 degree field of view and its zoom-to-distance mapping.
    const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
    const float distance =
      std::max(2.0f, 12.0f / std::max(0.15f, zoom / 32.0f));
    const float perPixel = 2.0f * distance * std::tan(glm::radians(25.0f)) /
                           static_cast<float>(std::max(1, dimensions[1]));
    const SceneEditorState& state = m_document.editorState();
    const Vector3 right(std::cos(state.yaw), 0.0f, -std::sin(state.yaw));
    const Vector3 up(-std::sin(state.pitch) * std::sin(state.yaw),
                     std::cos(state.pitch),
                     -std::sin(state.pitch) * std::cos(state.yaw));
    const Vector3 target =
      point - right * (offsetX * perPixel) + up * (offsetY * perPixel);
    ic->camera->SetPositionPrecise(target.x, target.z);
    m_cameraTargetY = target.y;
  } else {
    ic->camera->SetPositionPrecise(point.x - offsetX / zoom,
                                   point.y + offsetY / zoom);
  }
  applyWorldCamera();
}

// Twelve edges of a local box under a world matrix.
static void
addOrientedBox(MeshVisual& visual,
               const Matrix4& world,
               const AxisAlignedBounds3& local,
               float inflate,
               ColorRgba color)
{
  const Vector3 center = (local.minimum + local.maximum) * 0.5f;
  const Vector3 half = (local.maximum - local.minimum) * 0.5f * inflate;
  Vector3 corners[8];
  for (int index = 0; index < 8; ++index) {
    const Vector3 offset((index & 1) ? half.x : -half.x,
                         (index & 2) ? half.y : -half.y,
                         (index & 4) ? half.z : -half.z);
    corners[index] = Vector3(world * glm::vec4(center + offset, 1.0f));
  }
  const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },
                             { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
                             { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
  for (const int* edge : edges) {
    visual.addLine(corners[edge[0]], corners[edge[1]], color);
  }
}

void
EditorScene::updateDropPreview()
{
  float pixelX = 0.0f;
  float pixelY = 0.0f;
  const EditorAssetBrowser::Drop drag =
    m_assetBrowser ? m_assetBrowser->dragPoint() : EditorAssetBrowser::Drop{};
  if (drag.path.empty() || !dropToMain(drag, &pixelX, &pixelY)) {
    showDropPreview({}, 0.0f, 0.0f);
    return;
  }
  showDropPreview(drag.path, pixelX, pixelY);
}

bool
EditorScene::showDropPreview(const std::string& path,
                             float pixelX,
                             float pixelY)
{
  const bool wasShown = m_dropPreviewShown;
  m_dropPreviewShown = false;
  const EditorAssetKind kind = EditorAssets::kindFor(path);
  float worldX = 0.0f;
  float worldY = 0.0f;
  if (m_dropPreview && ic != nullptr && !path.empty() &&
      (kind == EditorAssetKind::Mesh || kind == EditorAssetKind::Texture)) {
    const float scale =
      GuiPanelLayout::viewport(ic->window, ic->renderer).layoutScale;
    if (!uiBlocksWorld(pixelX / scale, pixelY / scale) &&
        screenToWorld(pixelX, pixelY, &worldX, &worldY)) {
      // Where placeAsset would put the node: a unit box for a mesh, the
      // default sprite square for a texture.
      const Matrix4 world =
        m_document.makeEditPlaneTransform(worldX, worldY).toMatrix();
      const float depth = kind == EditorAssetKind::Mesh ? 0.5f : 0.0f;
      m_dropPreview->clearPrimitives();
      addOrientedBox(*m_dropPreview,
                     world,
                     AxisAlignedBounds3{ Vector3(-0.5f, -0.5f, -depth),
                                         Vector3(0.5f, 0.5f, depth) },
                     1.0f,
                     ColorRgba{ 66, 214, 210, 220 });
      m_dropPreviewShown = true;
    }
  }
  if (wasShown && !m_dropPreviewShown && m_dropPreview) {
    m_dropPreview->clearPrimitives();
  }
  return m_dropPreviewShown;
}

void
EditorScene::rebuildSelectionOverlay()
{
  if (!m_selectionOverlay) {
    return;
  }
  m_selectionOverlay->clearPrimitives();
  // The node under the cursor, thin and pale, unless it is selected.
  if (!m_hoverId.empty() && !m_selection.contains(m_hoverId) &&
      m_document.findNode(m_hoverId) != nullptr) {
    AxisAlignedBounds3 local;
    if (!m_document.scene().localBounds(m_hoverId, &local)) {
      local = AxisAlignedBounds3{ Vector3(-0.25f), Vector3(0.25f) };
    }
    addOrientedBox(*m_selectionOverlay,
                   m_document.worldMatrix(m_hoverId),
                   local,
                   1.04f,
                   ColorRgba{ 196, 208, 226, 90 });
  }
  const std::string& primary = m_selection.primary();
  if (primary.empty() || m_document.findNode(primary) == nullptr) {
    return;
  }
  const float pulse = 0.82f + 0.18f * std::sin(m_animTime * 4.0f);
  const ColorRgba gold{ 255,
                        static_cast<unsigned char>(205.0f * pulse),
                        static_cast<unsigned char>(60.0f * pulse),
                        255 };
  const ColorRgba secondary{ 120, 190, 255, 220 };
  for (const std::string& id : m_selection.ids()) {
    AxisAlignedBounds3 local;
    if (!m_document.scene().localBounds(id, &local)) {
      local = AxisAlignedBounds3{ Vector3(-0.25f), Vector3(0.25f) };
    }
    const Matrix4 world = m_document.worldMatrix(id);
    addOrientedBox(*m_selectionOverlay,
                   world,
                   local,
                   1.08f,
                   id == primary ? gold : secondary);
    if (id == primary) {
      const float outerPulse = 0.5f + 0.5f * std::sin(m_animTime * 6.0f);
      const unsigned char cyanAlpha =
        static_cast<unsigned char>(180.0f * outerPulse);
      addOrientedBox(*m_selectionOverlay,
                     world,
                     local,
                     1.14f,
                     ColorRgba{ 66, 214, 210, cyanAlpha });
    }
  }
  const GizmoFrame frame =
    m_gizmo.active() ? m_gizmo.frame() : gizmoFrame(primary);
  const GizmoPart highlighted =
    m_gizmo.active() ? m_gizmo.part() : m_hoveredGizmoPart;
  EditorGizmo::draw(*m_selectionOverlay, frame, m_gizmoMode, highlighted);
}

std::string
EditorScene::dragMergeKey() const
{
  return "drag:" + std::to_string(m_dragSerial);
}

GizmoFrame
EditorScene::gizmoFrame(const std::string& id) const
{
  GizmoFrame frame;
  const Matrix4 world = m_document.worldMatrix(id);
  frame.origin = Vector3(world[3]);
  Vector3 center(0.0f);
  if (m_pivotCenter && selectionCenter(&center)) {
    frame.origin = center;
  }
  frame.is3D = m_document.worldMode() == SceneWorldMode::World3D;
  frame.scale = gizmoScale(frame.origin);
  if (m_gizmoSpace == GizmoSpace::Local) {
    for (int axis = 0; axis < 3; ++axis) {
      const Vector3 column(world[axis]);
      const float length = glm::length(column);
      if (length > 1.0e-6f) {
        frame.axes[axis] = column / length;
      }
    }
  }
  return frame;
}

bool
EditorScene::selectionCenter(Vector3* center) const
{
  bool any = false;
  AxisAlignedBounds3 all;
  for (const std::string& id : m_selection.topLevel(m_document.scene())) {
    AxisAlignedBounds3 bounds;
    if (!m_document.subtreeWorldBounds(id, &bounds)) {
      continue;
    }
    if (!any) {
      all = bounds;
      any = true;
    } else {
      all.include(bounds);
    }
  }
  if (any) {
    *center = (all.minimum + all.maximum) * 0.5f;
  }
  return any;
}

GizmoSnap
EditorScene::snapSettings() const
{
  const SceneEditorState& state = m_document.editorState();
  GizmoSnap snap;
  // Ctrl inverts the snap setting for the duration of a drag.
  const bool invert = ic != nullptr && ic->inputManager != nullptr &&
                      ic->inputManager->isControlPressed();
  snap.enabled = state.snapEnabled != invert;
  snap.translate = state.snapTranslate;
  snap.angleDegrees = state.snapRotateDegrees;
  snap.scale = state.snapScale;
  return snap;
}

void
EditorScene::beginDrag(GizmoPart part,
                        const Vector3& rayOrigin,
                        const Vector3& rayDirection,
                        GizmoMode mode)
{
  const std::string primary = m_selection.primary();
  if (!m_gizmo.begin(
        gizmoFrame(primary), mode, part, rayOrigin, rayDirection)) {
    m_dragging = false;
    return;
  }
  ++m_dragSerial;
  m_dragging = true;
  m_dragIds = m_selection.topLevel(m_document.scene());
  m_dragStartWorld.clear();
  m_dragStartLocal.clear();
  for (const std::string& id : m_dragIds) {
    m_dragStartWorld.push_back(m_document.worldMatrix(id));
    m_dragStartLocal.push_back(m_document.findNode(id)->transform);
  }
}

void
EditorScene::applyDrag(const Vector3& rayOrigin, const Vector3& rayDirection)
{
  const GizmoDelta delta =
    m_gizmo.drag(rayOrigin, rayDirection, snapSettings());
  if (!delta.valid) {
    return;
  }
  const GizmoFrame& frame = m_gizmo.frame();
  const Matrix4 pivot = glm::translate(Matrix4(1.0f), frame.origin);
  const Matrix4 unpivot = glm::translate(Matrix4(1.0f), -frame.origin);
  Matrix3 axes(1.0f);
  for (int axis = 0; axis < 3; ++axis) {
    axes[axis] = frame.axes[axis];
  }
  const Matrix3 stretch = axes *
                          Matrix3(glm::scale(Matrix4(1.0f), delta.scale)) *
                          glm::transpose(axes);
  std::vector<Transform3D> transforms;
  transforms.reserve(m_dragIds.size());
  for (size_t index = 0; index < m_dragIds.size(); ++index) {
    const Matrix4& start = m_dragStartWorld[index];
    Matrix4 target = start;
    if (m_gizmo.mode() == GizmoMode::Translate) {
      target = glm::translate(Matrix4(1.0f), delta.translation) * start;
    } else if (m_gizmo.mode() == GizmoMode::Rotate) {
      target = pivot * glm::mat4_cast(delta.rotation) * unpivot * start;
    }
    const SceneNodeHandle parent =
      m_document.graph().getParent(m_document.nodeHandle(m_dragIds[index]));
    Matrix4 parentWorld(1.0f);
    m_document.graph().getWorldTransform(parent, &parentWorld);
    Transform3D local = m_dragStartLocal[index];
    if (m_gizmo.mode() == GizmoMode::Scale) {
      // Scale stays in each node's own axes; positions spread from the pivot.
      const Vector3 position(start[3]);
      const Vector3 moved = frame.origin + stretch * (position - frame.origin);
      const Vector3 localPosition(glm::inverse(parentWorld) *
                                  glm::vec4(moved, 1.0f));
      local.position = localPosition;
      local.scale = m_dragStartLocal[index].scale * delta.scale;
    } else {
      const Transform3D decomposed =
        Transform3D::fromMatrix(glm::inverse(parentWorld) * target);
      local.position = decomposed.position;
      local.rotation = glm::normalize(decomposed.rotation);
    }
    transforms.push_back(local);
  }
  m_document.setTransforms(m_dragIds, transforms, dragMergeKey());
}

bool
EditorScene::worldToScreen(const Vector3& world,
                            float* screenX,
                            float* screenY) const
{
  if (ic == nullptr || ic->camera == nullptr || ic->window == nullptr) {
    return false;
  }
  if (m_document.worldMode() != SceneWorldMode::World3D) {
    // The 2D camera maps screen to world affinely without rotation.
    const glm::dvec2 origin = ic->camera->ScreenToWorldPrecise(glm::dvec2(0.0));
    const glm::dvec2 right =
      ic->camera->ScreenToWorldPrecise(glm::dvec2(100.0, 0.0));
    const glm::dvec2 down =
      ic->camera->ScreenToWorldPrecise(glm::dvec2(0.0, 100.0));
    const double unitX = right.x - origin.x;
    const double unitY = down.y - origin.y;
    if (unitX == 0.0 || unitY == 0.0) {
      return false;
    }
    *screenX = static_cast<float>((world.x - origin.x) * 100.0 / unitX);
    *screenY = static_cast<float>((world.y - origin.y) * 100.0 / unitY);
    return true;
  }
  const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
  const glm::vec4 clip = currentViewProjection() * glm::vec4(world, 1.0f);
  if (clip.w <= 0.000001f) {
    return false;
  }
  const float ndcX = clip.x / clip.w;
  const float ndcY = clip.y / clip.w;
  *screenX = (ndcX * 0.5f + 0.5f) * static_cast<float>(dimensions[0]);
  *screenY = (0.5f - ndcY * 0.5f) * static_cast<float>(dimensions[1]);
  return true;
}

void
EditorScene::boxSelect(float x0, float y0, float x1, float y1, bool additive)
{
  const float left = std::min(x0, x1);
  const float right = std::max(x0, x1);
  const float top = std::min(y0, y1);
  const float bottom = std::max(y0, y1);
  const SceneGraph& graph = m_document.graph();
  std::vector<std::string> hits;
  for (SceneNodeHandle node = graph.firstNode(); !node.isNull();
       node = graph.nextNode(node)) {
    // Hidden nodes are not pickable, so they are not box-selectable either.
    if (!graph.isEffectivelyVisible(node) ||
        m_document.isLocked(std::string(graph.getName(node)))) {
      continue;
    }
    Vector3 center(0.0f);
    AxisAlignedBounds3 bounds;
    if (graph.getWorldBounds(node, &bounds)) {
      center = (bounds.minimum + bounds.maximum) * 0.5f;
    } else {
      Matrix4 matrix(1.0f);
      graph.getWorldTransform(node, &matrix);
      center = Vector3(matrix[3]);
    }
    float screenX = 0.0f;
    float screenY = 0.0f;
    if (worldToScreen(center, &screenX, &screenY) && screenX >= left &&
        screenX <= right && screenY >= top && screenY <= bottom) {
      hits.emplace_back(graph.getName(node));
    }
  }
  if (!additive) {
    m_selection.set(hits);
  } else {
    for (const std::string& id : hits) {
      m_selection.add(id);
    }
  }
  rebuildSelectionOverlay();
}

void
EditorScene::rebuildMarquee()
{
  if (!m_marquee || ic == nullptr) {
    return;
  }
  m_marquee->clearPrimitives();
  const float scale =
    GuiPanelLayout::viewport(ic->window, ic->renderer).layoutScale;
  const float x = std::min(m_boxStartX, m_boxEndX) / scale;
  const float y = std::min(m_boxStartY, m_boxEndY) / scale;
  const float w = std::fabs(m_boxEndX - m_boxStartX) / scale;
  const float h = std::fabs(m_boxEndY - m_boxStartY) / scale;
  m_marquee->addFilledRect(x, y, w, h, ColorRgba{ 66, 214, 210, 40 });
  m_marquee->addOutlineRect(x, y, w, h, ColorRgba{ 66, 214, 210, 220 }, 1.0f);
}

void
EditorScene::updateSelection(double dt)
{
  (void)dt;
  if (ic == nullptr || ic->inputManager == nullptr || ic->camera == nullptr ||
      ic->window == nullptr) {
    return;
  }
  if ((m_confirm && m_confirm->isOpen()) || m_busy) {
    return;
  }
  if (ic->commandLine != nullptr && ic->commandLine->isOpen) {
    return;
  }

  const std::array<double, 2> mouse = ic->window->getMouseCoords();
  const float uiScale =
    GuiPanelLayout::viewport(ic->window, ic->renderer).layoutScale;
  const float uiX = static_cast<float>(mouse[0]) / uiScale;
  const float uiY = static_cast<float>(mouse[1]) / uiScale;
  const float mouseScreenX = static_cast<float>(mouse[0]);
  const float mouseScreenY = static_cast<float>(mouse[1]);
  const bool left = ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
  const std::string primary = m_selection.primary();
  glm::vec3 rayOrigin{ 0.0f };
  glm::vec3 rayDir{ 0.0f, 0.0f, -1.0f };
  const bool haveRay =
    screenToWorldRay(mouseScreenX, mouseScreenY, &rayOrigin, &rayDir);

  // A held drag keeps tracking anywhere on screen, even over panels.
  if (left && m_dragging && m_gizmo.active()) {
    if (haveRay) {
      applyDrag(rayOrigin, rayDir);
    }
    rebuildSelectionOverlay();
    m_mouseWasDown = left;
    return;
  }
  if (!left && m_dragging) {
    m_dragging = false;
    m_gizmo.end();
    m_dragIds.clear();
  }
  m_activeGizmoPart = m_gizmo.active() ? m_gizmo.part() : GizmoPart::None;

  // A marquee keeps tracking anywhere on screen until the button lifts.
  if (m_boxSelecting) {
    m_boxEndX = mouseScreenX;
    m_boxEndY = mouseScreenY;
    if (!left) {
      m_boxSelecting = false;
      if (std::fabs(m_boxEndX - m_boxStartX) > 4.0f ||
          std::fabs(m_boxEndY - m_boxStartY) > 4.0f) {
        boxSelect(
          m_boxStartX, m_boxStartY, m_boxEndX, m_boxEndY, m_boxAdditive);
      }
    } else {
      rebuildMarquee();
    }
    m_mouseWasDown = left;
    return;
  }

  if (uiBlocksWorld(uiX, uiY)) {
    if (!m_hoverId.empty()) {
      m_hoverId.clear();
      rebuildSelectionOverlay();
    }
    m_mouseWasDown = left;
    return;
  }
  std::string underCursor;
  if (haveRay) {
    m_document.pickRay(rayOrigin, rayDir, &underCursor);
  }
  if (underCursor != m_hoverId) {
    m_hoverId = underCursor;
    rebuildSelectionOverlay();
  }

  const bool hasSelection =
    !primary.empty() && m_document.findNode(primary) != nullptr;
  if (!m_dragging && hasSelection && haveRay) {
    const GizmoPart hovered =
      EditorGizmo::hitTest(gizmoFrame(primary), m_gizmoMode, rayOrigin, rayDir);
    if (hovered != m_hoveredGizmoPart) {
      m_hoveredGizmoPart = hovered;
      rebuildSelectionOverlay();
    }
  } else if (!hasSelection) {
    m_hoveredGizmoPart = GizmoPart::None;
  }

  float worldX = 0.0f;
  float worldY = 0.0f;
  const bool groundHit =
    screenToWorld(mouseScreenX, mouseScreenY, &worldX, &worldY);

  if (left && !m_mouseWasDown && haveRay) {
    const bool toggle = ic->inputManager->isControlPressed() ||
                        ic->inputManager->isShiftPressed();
    if (m_activeTool != EditorCommand::SelectTool &&
        m_activeTool != EditorCommand::None) {
      if (groundHit) {
        applyActiveToolAt(worldX, worldY);
      }
    } else {
      if (hasSelection && !toggle) {
        const GizmoPart hitPart = EditorGizmo::hitTest(
          gizmoFrame(primary), m_gizmoMode, rayOrigin, rayDir);
        if (hitPart != GizmoPart::None) {
          if (ic->inputManager->isAltPressed() &&
              m_gizmoMode == GizmoMode::Translate) {
            duplicateForDrag();
          }
          beginDrag(hitPart, rayOrigin, rayDir, m_gizmoMode);
          m_activeGizmoPart = m_gizmo.active() ? hitPart : GizmoPart::None;
          rebuildSelectionOverlay();
          m_mouseWasDown = left;
          return;
        }
      }
      std::string hit;
      const bool picked = m_document.pickRay(rayOrigin, rayDir, &hit);
      if (picked && toggle) {
        m_selection.toggle(hit);
      } else if (picked) {
        if (!m_selection.contains(hit)) {
          m_selection.set(hit);
        } else {
          m_selection.add(hit);
        }
        // Grabbing a body moves the selection on the edit plane, keeping the
        // grabbed point under the cursor; with Alt, it moves copies.
        if (ic->inputManager->isAltPressed()) {
          duplicateForDrag();
        }
        beginDrag(GizmoPart::Center, rayOrigin, rayDir, GizmoMode::Translate);
        m_activeGizmoPart =
          m_gizmo.active() ? GizmoPart::Center : GizmoPart::None;
      } else {
        // Empty space: a click clears (unless adding); a drag box-selects.
        if (!toggle) {
          m_selection.clear();
        }
        m_dragging = false;
        m_gizmo.end();
        m_boxSelecting = true;
        m_boxAdditive = toggle;
        m_boxStartX = m_boxEndX = mouseScreenX;
        m_boxStartY = m_boxEndY = mouseScreenY;
        rebuildMarquee();
      }
    }
    rebuildSelectionOverlay();
  }
  m_mouseWasDown = left;
}
// The world axis (+-X or +-Z) nearest a horizontal direction.
static Vector3
nearestGroundAxis(float x, float z)
{
  if (std::fabs(x) >= std::fabs(z)) {
    return Vector3(x >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
  }
  return Vector3(0.0f, 0.0f, z >= 0.0f ? 1.0f : -1.0f);
}

void
EditorScene::handleNudgeKeys()
{
  if (ic == nullptr || ic->inputManager == nullptr || m_selection.empty()) {
    return;
  }
  const bool is3D = m_document.worldMode() == SceneWorldMode::World3D;
  Vector3 right(1.0f, 0.0f, 0.0f);
  Vector3 away(0.0f, 1.0f, 0.0f);
  if (is3D) {
    // Arrows follow the view: right and away from the camera on the ground.
    const SceneEditorState& state = m_document.editorState();
    right = nearestGroundAxis(std::cos(state.yaw), -std::sin(state.yaw));
    away = nearestGroundAxis(-std::sin(state.yaw), -std::cos(state.yaw));
  }
  std::queue<InputManager::KeyPressEvent>& keys =
    ic->inputManager->getKeyQueue();
  std::queue<InputManager::KeyPressEvent> kept;
  while (!keys.empty()) {
    const InputManager::KeyPressEvent event = keys.front();
    keys.pop();
    const bool alt =
      ic->inputManager->isAltPressed() || (event.modifiers & 0x4) != 0;
    const bool control =
      ic->inputManager->isControlPressed() || (event.modifiers & 0x2) != 0;
    Vector3 direction(0.0f);
    if (event.action == InputAction::Press && alt && !control) {
      switch (event.key) {
        case KeyCode::Left:
          direction = -right;
          break;
        case KeyCode::Right:
          direction = right;
          break;
        case KeyCode::Up:
          direction = away;
          break;
        case KeyCode::Down:
          direction = -away;
          break;
        case KeyCode::PageUp:
          direction = is3D ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(0.0f);
          break;
        case KeyCode::PageDown:
          direction = is3D ? Vector3(0.0f, -1.0f, 0.0f) : Vector3(0.0f);
          break;
        default:
          break;
      }
    }
    if (direction != Vector3(0.0f)) {
      nudgeSelection(direction);
      continue;
    }
    kept.push(event);
  }
  keys.swap(kept);
}

static bool
createShapeFor(EditorCommand command, bool* empty, ScenePrimitiveShape* shape)
{
  *empty = false;
  switch (command) {
    case EditorCommand::CreateEmpty:
      *empty = true;
      return true;
    case EditorCommand::CreateRect:
      *shape = ScenePrimitiveShape::Rect;
      return true;
    case EditorCommand::CreateEllipse:
      *shape = ScenePrimitiveShape::Ellipse;
      return true;
    case EditorCommand::CreateTriangle:
      *shape = ScenePrimitiveShape::Triangle;
      return true;
    case EditorCommand::CreateCube:
      *shape = ScenePrimitiveShape::Cube;
      return true;
    case EditorCommand::CreatePyramid:
      *shape = ScenePrimitiveShape::Pyramid;
      return true;
    case EditorCommand::CreateSphere:
      *shape = ScenePrimitiveShape::Sphere;
      return true;
    case EditorCommand::CreateWireCube:
      *shape = ScenePrimitiveShape::WireCube;
      return true;
    case EditorCommand::CreateWireSphere:
      *shape = ScenePrimitiveShape::WireSphere;
      return true;
    default:
      return false;
  }
}

bool
EditorScene::createAtContextPoint(EditorCommand command)
{
  bool empty = false;
  ScenePrimitiveShape shape = ScenePrimitiveShape::Cube;
  if (!m_contextPointValid || !createShapeFor(command, &empty, &shape)) {
    return false;
  }
  m_activeTool = command;
  applyActiveToolAt(m_contextWorldX, m_contextWorldY);
  m_contextPointValid = false;
  return true;
}

void
EditorScene::duplicateForDrag()
{
  const std::vector<std::string> copies =
    m_document.duplicate(m_selection.topLevel(m_document.scene()));
  if (!copies.empty()) {
    m_selection.set(copies);
    m_hoverId.clear();
  }
}

void
EditorScene::updateContextClick()
{
  if (ic == nullptr || ic->inputManager == nullptr || ic->window == nullptr) {
    return;
  }
  const bool right =
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseRight);
  const std::array<double, 2> mouse = ic->window->getMouseCoords();
  const float x = static_cast<float>(mouse[0]);
  const float y = static_cast<float>(mouse[1]);
  if (right && !m_rightDown) {
    const float scale =
      GuiPanelLayout::viewport(ic->window, ic->renderer).layoutScale;
    m_rightDown = true;
    m_rightInWorld = !uiBlocksWorld(x / scale, y / scale) &&
                     !(m_sceneGraphView && m_sceneGraphView->menuOpen());
    if (m_rightInWorld && m_toolbar) {
      m_toolbar->closeMenus();
    }
    m_rightStartX = x;
    m_rightStartY = y;
    m_rightTravel = 0.0f;
  } else if (right) {
    m_rightTravel =
      std::max(m_rightTravel, std::hypot(x - m_rightStartX, y - m_rightStartY));
  } else if (m_rightDown) {
    m_rightDown = false;
    // A right click (not an orbit or pan) in the world opens the menu.
    if (m_rightInWorld && m_rightTravel < 4.0f) {
      openViewportMenu(x, y);
    }
  }
}

void
EditorScene::openViewportMenu(float pixelX, float pixelY)
{
  if (!m_toolbar) {
    return;
  }
  glm::vec3 origin{ 0.0f };
  glm::vec3 direction{ 0.0f };
  std::string hit;
  if (screenToWorldRay(pixelX, pixelY, &origin, &direction)) {
    m_document.pickRay(origin, direction, &hit);
  }
  m_contextPointValid =
    screenToWorld(pixelX, pixelY, &m_contextWorldX, &m_contextWorldY);
  using Entry = std::pair<std::string, EditorCommand>;
  std::vector<Entry> entries;
  if (!hit.empty()) {
    // The menu acts on what was clicked: it joins a selection holding it,
    // else replaces it.
    if (!m_selection.contains(hit)) {
      m_selection.set(hit);
    }
    const SceneInstance& scene = m_document.scene();
    bool children = false;
    bool parents = false;
    bool allLocked = true;
    for (const std::string& id : m_selection.ids()) {
      children = children || !scene.childIds(id).empty();
      parents = parents || !scene.parentOf(id).empty();
      allLocked = allLocked && m_document.isLockedSelf(id);
    }
    const SceneNode* node = m_document.findNode(hit);
    entries.push_back(
      { node != nullptr && !node->name.empty() ? node->name : hit,
        EditorCommand::None });
    entries.push_back({ "-", EditorCommand::None });
    entries.push_back({ "Frame", EditorCommand::FrameSelection });
    entries.push_back({ "Rename", EditorCommand::Rename });
    entries.push_back({ "Duplicate", EditorCommand::Duplicate });
    entries.push_back({ "Copy", EditorCommand::Copy });
    entries.push_back({ "Cut", EditorCommand::Cut });
    entries.push_back({ "Paste", EditorCommand::Paste });
    entries.push_back({ "Delete", EditorCommand::DeleteNode });
    entries.push_back({ "-", EditorCommand::None });
    entries.push_back({ "Group", EditorCommand::GroupSelection });
    entries.push_back(
      { "Ungroup",
        children ? EditorCommand::UngroupSelection : EditorCommand::None });
    entries.push_back(
      { "Select Parent",
        parents ? EditorCommand::SelectParent : EditorCommand::None });
    entries.push_back(
      { "Select Children",
        children ? EditorCommand::SelectChildren : EditorCommand::None });
    entries.push_back({ "-", EditorCommand::None });
    entries.push_back(
      { allLocked ? "Unlock" : "Lock", EditorCommand::ToggleLock });
    entries.push_back({ "Hide", EditorCommand::ToggleVisible });
    entries.push_back({ m_isolated ? "Show Everything" : "Isolate",
                        EditorCommand::ToggleIsolate });
    entries.push_back({ "Drop to Floor", EditorCommand::DropToFloor });
  } else {
    const bool is3D = m_document.worldMode() == SceneWorldMode::World3D;
    entries.push_back({ "Paste", EditorCommand::Paste });
    entries.push_back({ "-", EditorCommand::None });
    entries.push_back({ "Create Empty", EditorCommand::CreateEmpty });
    if (is3D) {
      entries.push_back({ "Create Cube", EditorCommand::CreateCube });
      entries.push_back({ "Create Sphere", EditorCommand::CreateSphere });
      entries.push_back({ "Create Pyramid", EditorCommand::CreatePyramid });
    } else {
      entries.push_back({ "Create Rect", EditorCommand::CreateRect });
      entries.push_back({ "Create Ellipse", EditorCommand::CreateEllipse });
      entries.push_back({ "Create Triangle", EditorCommand::CreateTriangle });
    }
    entries.push_back({ "Create Light", EditorCommand::CreateLight });
    entries.push_back({ "Create Camera", EditorCommand::CreateCamera });
    entries.push_back({ "-", EditorCommand::None });
    entries.push_back({ m_selection.empty() ? "Frame All" : "Frame Selection",
                        EditorCommand::FrameSelection });
    if (m_isolated) {
      entries.push_back({ "Show Everything", EditorCommand::ToggleIsolate });
    }
  }
  const float scale =
    GuiPanelLayout::viewport(ic->window, ic->renderer).layoutScale;
  m_toolbar->openPopup(pixelX / scale, pixelY / scale, entries);
  rebuildSelectionOverlay();
}

void
EditorScene::applyActiveToolAt(float worldX, float worldY)
{
  bool empty = false;
  ScenePrimitiveShape shape = ScenePrimitiveShape::Cube;
  if (!createShapeFor(m_activeTool, &empty, &shape)) {
    return;
  }
  // New nodes go to the root; reparent them in the hierarchy.
  const Transform3D transform =
    m_document.makeEditPlaneTransform(worldX, worldY);
  const std::string id =
    m_document.createPrimitive(empty, shape, std::string(), transform);
  if (id.empty()) {
    return;
  }
  m_selection.set(id);
  const SceneNode* created = m_document.findNode(id);
  toast("Created " + (created ? created->name : std::string("node")),
        ColorRgba{ 60, 220, 120, 255 });
  m_activeTool = EditorCommand::SelectTool;
}

bool
EditorScene::screenToWorld(float screenX,
                            float screenY,
                            float* worldX,
                            float* worldY) const
{
  if (worldX == nullptr || worldY == nullptr || ic == nullptr ||
      ic->camera == nullptr || ic->window == nullptr) {
    return false;
  }
  if (m_document.worldMode() != SceneWorldMode::World3D) {
    const glm::dvec2 world = ic->camera->ScreenToWorldPrecise(
      glm::dvec2(static_cast<double>(screenX), static_cast<double>(screenY)));
    *worldX = static_cast<float>(world.x);
    *worldY = static_cast<float>(world.y);
    return true;
  }
  glm::vec3 origin{ 0.0f };
  glm::vec3 direction{ 0.0f };
  if (!screenToWorldRay(screenX, screenY, &origin, &direction) ||
      std::fabs(direction.y) < 0.000001f) {
    return false;
  }
  const float t = -origin.y / direction.y;
  *worldX = origin.x + direction.x * t;
  *worldY = origin.z + direction.z * t;
  return true;
}

bool
EditorScene::screenToWorldRay(float screenX,
                               float screenY,
                               glm::vec3* rayOrigin,
                               glm::vec3* rayDir) const
{
  if (rayOrigin == nullptr || rayDir == nullptr || ic == nullptr ||
      ic->camera == nullptr || ic->window == nullptr) {
    return false;
  }
  const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
  if (dimensions[0] <= 0 || dimensions[1] <= 0) {
    return false;
  }
  if (m_document.worldMode() != SceneWorldMode::World3D) {
    const glm::dvec2 world = ic->camera->ScreenToWorldPrecise(
      glm::dvec2(static_cast<double>(screenX), static_cast<double>(screenY)));
    *rayOrigin = glm::vec3(
      static_cast<float>(world.x), static_cast<float>(world.y), 1000.0f);
    *rayDir = glm::vec3(0.0f, 0.0f, -1.0f);
    return true;
  }
  const float ndcX =
    (2.0f * screenX / static_cast<float>(dimensions[0])) - 1.0f;
  const float ndcY =
    1.0f - (2.0f * screenY / static_cast<float>(dimensions[1]));
  const glm::mat4 inverse = glm::inverse(currentViewProjection());
  glm::vec4 nearPoint = inverse * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 farPoint = inverse * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
  if (std::fabs(nearPoint.w) < 0.000001f || std::fabs(farPoint.w) < 0.000001f) {
    return false;
  }
  nearPoint /= nearPoint.w;
  farPoint /= farPoint.w;
  *rayOrigin = glm::vec3(nearPoint);
  const glm::vec3 dir = glm::vec3(farPoint) - glm::vec3(nearPoint);
  const float len = glm::length(dir);
  if (len < 0.000001f) {
    return false;
  }
  *rayDir = dir / len;
  return true;
}

float
EditorScene::gizmoScale(const glm::vec3& worldPos) const
{
  if (ic == nullptr || ic->camera == nullptr) {
    return 1.0f;
  }
  if (m_document.worldMode() != SceneWorldMode::World3D) {
    const float zoom = ic->camera->GetZoom();
    return std::clamp(28.0f / std::max(1.0f, zoom), 0.35f, 3.0f);
  }
  const glm::vec3 eye = ic->camera->getEye();
  const float dist = glm::length(eye - worldPos);
  // Constant screen size: at distance 12 the scale is about 1.
  return std::clamp(dist * 0.085f, 0.4f, 8.0f);
}

GizmoPart
EditorScene::hitTestGizmo(float screenX,
                           float screenY,
                           const glm::vec3& gizmoOrigin,
                           float scale) const
{
  glm::vec3 rayOrigin{ 0.0f };
  glm::vec3 rayDir{ 0.0f, 0.0f, -1.0f };
  if (!screenToWorldRay(screenX, screenY, &rayOrigin, &rayDir)) {
    return GizmoPart::None;
  }
  GizmoFrame frame;
  frame.origin = gizmoOrigin;
  frame.scale = scale;
  frame.is3D = m_document.worldMode() == SceneWorldMode::World3D;
  return EditorGizmo::hitTest(frame, m_gizmoMode, rayOrigin, rayDir);
}