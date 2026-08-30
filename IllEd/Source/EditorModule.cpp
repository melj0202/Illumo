#include "EditorModule.h"

#include "EditorUiAtlas.h"
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
#include <utility>

static bool
editorContextComplete(const IllumoContext* context)
{
  return context != nullptr && context->envVars != nullptr &&
         context->window != nullptr && context->camera != nullptr &&
         context->renderer != nullptr && context->inputManager != nullptr &&
         context->commandLine != nullptr && context->scene != nullptr;
}

EditorModule::EditorModule(std::string initialScenePath)
  : m_activeTool(EditorCommand::SelectTool)
  , m_initialScenePath(std::move(initialScenePath))
  , m_dragging(false)
  , m_panning(false)
  , m_mouseWasDown(false)
  , m_lastMouseX(0.0)
  , m_lastMouseY(0.0)
  , m_animTime(0.0f)
  , m_pendingAction(EditorPendingAction::None)
{
}

EditorModule::~EditorModule() = default;

bool
EditorModule::Start(IllumoContext* context)
{
  if (!editorContextComplete(context)) {
    Logger::LogError("EditorModule::Start: IllumoContext missing services");
    ic = context;
    return false;
  }
  ic = context;

  m_toolbar = std::make_unique<EditorToolbar>(ic->window, ic->renderer);
  m_sceneGraphView =
    std::make_unique<EditorSceneGraphView>(ic->window, ic->renderer);
  m_sidebar = std::make_unique<EditorSidebar>(ic->window, ic->renderer);
  m_confirm = std::make_unique<EditorConfirmDialog>(ic->window, ic->renderer);

  syncFontSize();

  if (ic->assetManager != nullptr) {
    TextureOptions options;
    options.filter = TextureFilter::Nearest;
    m_atlas = ic->assetManager->acquireTexture(
      EditorUiAtlas::relativePath(), options, AssetLoadMode::Synchronous);
    if (ic->assetManager->getState(m_atlas).state == AssetState::Ready) {
      m_toolbar->setAtlas(m_atlas);
      m_sceneGraphView->setAtlas(m_atlas);
      m_sidebar->setAtlas(m_atlas);
    }
  }
  m_grid = std::make_unique<MeshVisual>();
  m_grid->prepare(ic->renderer);
  rebuildGrid();
  m_selectionOverlay = std::make_unique<MeshVisual>();
  m_selectionOverlay->prepare(ic->renderer);

  if (m_initialScenePath.empty() && ic->envVars != nullptr) {
    m_initialScenePath = ic->envVars->getVar("LaunchScene").value;
  }
  if (!m_initialScenePath.empty()) {
    std::string error;
    if (!m_document.loadFromFile(m_initialScenePath, &error)) {
      ic->commandLine->logError(error);
      m_document.clear();
    }
  }

  ic->camera->SetSmoothingSpeed(18.0f);
  ic->camera->SetPositionPrecise(m_document.camera().x, m_document.camera().y);
  ic->camera->SetZoom(m_document.camera().zoom);

  rebuildGraph();
  updateStatus();
  return true;
}

void
EditorModule::Exit()
{
  if (ic != nullptr && ic->assetManager != nullptr && m_atlas.isValid()) {
    ic->assetManager->releaseTexture(m_atlas);
    m_atlas = TextureHandle{};
  }
  m_handles.clear();
  m_attachments.clear();
  m_selectionOverlay.reset();
  m_grid.reset();
  m_confirm.reset();
  m_sidebar.reset();
  m_sceneGraphView.reset();
  m_toolbar.reset();
  m_graph.clear();
}

glm::mat4
EditorModule::currentViewProjection() const
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
EditorModule::applyWorldCamera()
{
  if (ic == nullptr || ic->camera == nullptr) {
    return;
  }
  if (m_document.worldMode() != IlscWorldMode::World3D) {
    ic->camera->setProjectionType(ProjectionType::Orthographic);
    return;
  }

  const IlscCameraState& state = m_document.camera();
  const glm::dvec2 position = ic->camera->GetPositionPrecise();
  const float zoom = ic->camera->GetZoom();
  const float distance = std::max(2.0f, 12.0f / std::max(0.15f, zoom / 32.0f));
  const glm::vec3 target(
    static_cast<float>(position.x), 0.0f, static_cast<float>(position.y));
  const glm::vec3 eye =
    target + glm::vec3(std::cos(state.pitch) * std::sin(state.yaw) * distance,
                       std::sin(state.pitch) * distance,
                       std::cos(state.pitch) * std::cos(state.yaw) * distance);
  ic->camera->lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
  ic->camera->setPerspective(50.0f, 0.1f, 250.0f);
  ic->camera->setProjectionType(ProjectionType::Perspective);
}

bool
EditorModule::rebuildGraph()
{
  m_graph.clear();
  m_attachments.clear();
  m_handles.clear();
  const size_t count = m_document.nodeCount();
  std::vector<char> created(count, 0);
  size_t remaining = count;
  while (remaining > 0) {
    size_t progressed = 0;
    for (size_t i = 0; i < count; ++i) {
      if (created[i] != 0) {
        continue;
      }
      const IlscNode* node = m_document.nodeAt(i);
      if (node == nullptr) {
        continue;
      }
      SceneNodeHandle parentHandle;
      if (!node->parentId.empty()) {
        const std::unordered_map<std::string, SceneNodeHandle>::const_iterator
          parentFound = m_handles.find(node->parentId);
        if (parentFound == m_handles.end()) {
          continue;
        }
        parentHandle = parentFound->second;
      }
      const SceneNodeHandle handle = m_graph.createNode(parentHandle);
      if (!handle.isValid()) {
        return false;
      }
      m_graph.setLocalTransform(handle, node->transform);
      m_graph.setEnabled(handle, node->enabled);
      m_graph.setVisible(handle, node->visible);
      if (IlscCodec::kindHasGeometry(node->kind) && ic != nullptr &&
          ic->renderer != nullptr) {
        std::unique_ptr<EditorAttachment> attachment =
          std::make_unique<EditorAttachment>();
        attachment->configure(
          ic->renderer, ic->camera, *node, m_document.worldMode());
        m_graph.setRenderAttachment(handle, attachment.get());
        m_attachments.push_back(std::move(attachment));
      }
      m_handles[node->id] = handle;
      created[i] = 1;
      ++progressed;
      --remaining;
    }
    if (progressed == 0) {
      return false;
    }
  }
  applyWorldCamera();
  rebuildGrid();
  rebuildSelectionOverlay();
  return true;
}

void
EditorModule::rebuildGrid()
{
  if (!m_grid || ic == nullptr || ic->renderer == nullptr) {
    return;
  }
  m_grid->clearPrimitives();
  if (m_document.worldMode() == IlscWorldMode::World3D) {
    const int halfCount = 20;
    const float spacing = 1.0f;
    const float extent = static_cast<float>(halfCount) * spacing;
    const ColorRgba minorColor{ 38, 50, 66, 255 };
    const ColorRgba majorColor{ 58, 76, 100, 255 };
    for (int i = -halfCount; i <= halfCount; ++i) {
      if (i == 0) {
        continue;
      }
      const float pos = static_cast<float>(i) * spacing;
      const ColorRgba color = (i % 5 == 0) ? majorColor : minorColor;
      m_grid->addLine(
        glm::vec3(-extent, 0.0f, pos), glm::vec3(extent, 0.0f, pos), color);
      m_grid->addLine(
        glm::vec3(pos, 0.0f, -extent), glm::vec3(pos, 0.0f, extent), color);
    }
    m_grid->addLine(glm::vec3(-extent, 0.0f, 0.0f),
                    glm::vec3(extent, 0.0f, 0.0f),
                    ColorRgba{ 220, 65, 65, 255 });
    m_grid->addLine(glm::vec3(0.0f, 0.0f, -extent),
                    glm::vec3(0.0f, 0.0f, extent),
                    ColorRgba{ 65, 120, 230, 255 });
    m_grid->addLine(glm::vec3(0.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 2.5f, 0.0f),
                    ColorRgba{ 65, 210, 95, 255 });
  } else {
    const int halfCount = 30;
    const float spacing = 1.0f;
    const float extent = static_cast<float>(halfCount) * spacing;
    const ColorRgba minorColor{ 32, 44, 58, 255 };
    const ColorRgba majorColor{ 48, 66, 88, 255 };
    for (int i = -halfCount; i <= halfCount; ++i) {
      if (i == 0) {
        continue;
      }
      const float pos = static_cast<float>(i) * spacing;
      const ColorRgba color = (i % 5 == 0) ? majorColor : minorColor;
      m_grid->addLine(
        glm::vec3(-extent, pos, 0.0f), glm::vec3(extent, pos, 0.0f), color);
      m_grid->addLine(
        glm::vec3(pos, -extent, 0.0f), glm::vec3(pos, extent, 0.0f), color);
    }
    m_grid->addLine(glm::vec3(-extent, 0.0f, 0.0f),
                    glm::vec3(extent, 0.0f, 0.0f),
                    ColorRgba{ 190, 60, 60, 255 });
    m_grid->addLine(glm::vec3(0.0f, -extent, 0.0f),
                    glm::vec3(0.0f, extent, 0.0f),
                    ColorRgba{ 60, 180, 85, 255 });
    const float cross = 0.35f;
    m_grid->addLine(glm::vec3(-cross, 0.0f, 0.01f),
                    glm::vec3(cross, 0.0f, 0.01f),
                    ColorRgba{ 255, 220, 100, 255 });
    m_grid->addLine(glm::vec3(0.0f, -cross, 0.01f),
                    glm::vec3(0.0f, cross, 0.01f),
                    ColorRgba{ 255, 220, 100, 255 });
  }
}

SaveLoadDialogSpec
EditorModule::dialogSpec() const
{
  SaveLoadDialogSpec specification;
  specification.fileDescription = "Illumo Scene";
  specification.defaultFilename = "Scene.ilsc";
  specification.extensionPattern = "*.ilsc";
  return specification;
}

void
EditorModule::updateStatus()
{
  if (!m_toolbar) {
    return;
  }
  std::string status =
    m_document.path().empty() ? "Untitled" : m_document.path();
  if (m_document.isDirty()) {
    status += " *";
  }
  if (!m_selectedId.empty()) {
    const IlscNode* node = m_document.findNode(m_selectedId);
    status += "  |  ";
    if (node != nullptr) {
      status += node->name;
      status += " (#";
      status += node->id;
      status += ")";
    } else {
      status += m_selectedId;
    }
  }
  status += "  |  ";
  status += IlscCodec::worldModeName(m_document.worldMode());
  if (ic != nullptr && ic->window != nullptr && ic->camera != nullptr) {
    const std::array<double, 2> mouse = ic->window->getMouseCoords();
    const glm::dvec2 world =
      ic->camera->ScreenToWorldPrecise(glm::dvec2(mouse[0], mouse[1]));
    status += "  |  X: ";
    status += std::to_string(static_cast<int>(std::round(world.x)));
    status += ", Y: ";
    status += std::to_string(static_cast<int>(std::round(world.y)));
    const int zoom = static_cast<int>(std::round(ic->camera->GetZoom()));
    status += "  |  Zoom: " + std::to_string(zoom) + "x";
  }
#ifndef NDEBUG
  status += "  |  [WASD/MMB: Pan  Wheel: Zoom  ~: Console]";
#else
  status += "  |  [WASD/MMB: Pan  Wheel: Zoom]";
#endif
  m_toolbar->setStatus(status);
}

bool
EditorModule::uiBlocksWorld(float screenX, float screenY) const
{
  if (m_confirm && m_confirm->isOpen()) {
    return true;
  }
  if (m_toolbar && m_toolbar->containsScreenPoint(screenX, screenY)) {
    return true;
  }
  if (m_sceneGraphView &&
      m_sceneGraphView->containsScreenPoint(screenX, screenY)) {
    return true;
  }
  if (m_sidebar && m_sidebar->containsScreenPoint(screenX, screenY)) {
    return true;
  }
  if (ic != nullptr && ic->window != nullptr) {
    const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
    const float scale =
      ic->renderer != nullptr ? ic->renderer->getUiScale() : 1.0f;
    const float virtualHeight =
      static_cast<float>(dimensions[1]) / (scale > 0.0f ? scale : 1.0f);
    const float statusHeight = m_toolbar ? m_toolbar->statusHeight()
                                         : EditorToolbar::kDefaultStatusHeight;
    if (screenY >= virtualHeight - statusHeight) {
      return true;
    }
  }
  return false;
}

bool
EditorModule::saveDocument(bool saveAs)
{
  std::string path = m_document.path();
  if (saveAs || path.empty()) {
    path = SaveLoad::GetSaveLocation(dialogSpec());
    if (path.empty()) {
      return false;
    }
  }
  std::string error;
  if (!m_document.saveToFile(path, &error)) {
    if (ic != nullptr && ic->commandLine != nullptr) {
      ic->commandLine->logError(error);
    }
    if (m_toolbar) {
      m_toolbar->showToast("Failed to save: " + error,
                           ColorRgba{ 245, 100, 110, 255 });
    }
    return false;
  }
  if (m_toolbar) {
    m_toolbar->showToast("Saved scene: " + (path.empty() ? "Scene.ilsc" : path),
                         ColorRgba{ 60, 220, 120, 255 });
  }
  updateStatus();
  return true;
}

bool
EditorModule::openDocument()
{
  const std::string path = SaveLoad::GetLoadLocation(dialogSpec());
  if (path.empty()) {
    return false;
  }
  std::string error;
  if (!m_document.loadFromFile(path, &error)) {
    if (ic != nullptr && ic->commandLine != nullptr) {
      ic->commandLine->logError(error);
    }
    if (m_toolbar) {
      m_toolbar->showToast("Failed to load: " + error,
                           ColorRgba{ 245, 100, 110, 255 });
    }
    return false;
  }
  m_selectedId.clear();
  if (ic != nullptr && ic->camera != nullptr) {
    ic->camera->SetPositionPrecise(m_document.camera().x,
                                   m_document.camera().y);
    ic->camera->SetZoom(m_document.camera().zoom);
  }
  if (m_toolbar) {
    m_toolbar->showToast("Opened scene: " + path,
                         ColorRgba{ 66, 214, 210, 255 });
  }
  rebuildGraph();
  updateStatus();
  return true;
}

void
EditorModule::newDocument()
{
  m_document.clear();
  m_selectedId.clear();
  if (ic != nullptr && ic->camera != nullptr) {
    ic->camera->Reset();
    ic->camera->SetZoom(32.0f);
  }
  if (m_toolbar) {
    m_toolbar->showToast("Created new scene", ColorRgba{ 66, 214, 210, 255 });
  }
  rebuildGraph();
  updateStatus();
}

void
EditorModule::createNode(SceneNodeKind kind)
{
  const std::string parentId = m_selectedId;
  const std::string id = m_document.createNode(kind, parentId);
  if (id.empty()) {
    return;
  }
  if (ic != nullptr && ic->window != nullptr) {
    const std::array<double, 2> mouse = ic->window->getMouseCoords();
    float worldX = 0.0f;
    float worldY = 0.0f;
    if (screenToWorld(static_cast<float>(mouse[0]),
                      static_cast<float>(mouse[1]),
                      &worldX,
                      &worldY)) {
      m_document.setTransform(
        id, m_document.makeEditPlaneTransform(worldX, worldY));
    }
  }
  m_selectedId = id;
  if (m_toolbar) {
    const IlscNode* node = m_document.findNode(id);
    m_toolbar->showToast("Created " + (node ? node->name : "node"),
                         ColorRgba{ 60, 220, 120, 255 });
  }
  rebuildGraph();
  updateStatus();
}

void
EditorModule::deleteSelection()
{
  if (m_selectedId.empty()) {
    return;
  }
  m_document.destroySubtree(m_selectedId);
  m_selectedId.clear();
  if (m_toolbar) {
    m_toolbar->showToast("Deleted selected node",
                         ColorRgba{ 245, 100, 110, 255 });
  }
  rebuildGraph();
  updateStatus();
}

void
EditorModule::unparentSelection()
{
  if (m_selectedId.empty()) {
    return;
  }
  if (m_document.setParent(m_selectedId, {})) {
    if (m_toolbar) {
      m_toolbar->showToast("Unparented node to root",
                           ColorRgba{ 66, 214, 210, 255 });
    }
    rebuildGraph();
    updateStatus();
  }
}

void
EditorModule::requestAction(EditorPendingAction action)
{
  if (m_document.isDirty()) {
    m_pendingAction = action;
    if (m_confirm) {
      m_confirm->open("Save changes before continuing?");
    }
    return;
  }
  m_pendingAction = action;
  performPendingAction();
}

void
EditorModule::performPendingAction()
{
  const EditorPendingAction action = m_pendingAction;
  m_pendingAction = EditorPendingAction::None;
  if (action == EditorPendingAction::NewDocument) {
    newDocument();
  } else if (action == EditorPendingAction::OpenDocument) {
    openDocument();
  } else if (action == EditorPendingAction::ExitEditor) {
    if (ic != nullptr && ic->window != nullptr) {
      ic->window->requestClose();
    }
  }
}

void
EditorModule::handleCommand(EditorCommand command)
{
  if (command == EditorCommand::None) {
    return;
  }
  if (command == EditorCommand::NewDocument) {
    requestAction(EditorPendingAction::NewDocument);
  } else if (command == EditorCommand::OpenDocument) {
    requestAction(EditorPendingAction::OpenDocument);
  } else if (command == EditorCommand::SaveDocument) {
    saveDocument(false);
  } else if (command == EditorCommand::SaveDocumentAs) {
    saveDocument(true);
  } else if (command == EditorCommand::ExitEditor) {
    requestAction(EditorPendingAction::ExitEditor);
  } else if (command == EditorCommand::DeleteNode) {
    deleteSelection();
  } else if (command == EditorCommand::UnparentNode) {
    unparentSelection();
  } else if (command == EditorCommand::CreateEmpty ||
             command == EditorCommand::CreateRect ||
             command == EditorCommand::CreateEllipse ||
             command == EditorCommand::CreateTriangle ||
             command == EditorCommand::CreateCube ||
             command == EditorCommand::CreatePyramid ||
             command == EditorCommand::CreateSphere) {
    m_activeTool = command;
    if (m_sidebar) {
      m_sidebar->setActiveTool(command);
    }
  } else if (command == EditorCommand::SelectTool) {
    m_activeTool = EditorCommand::SelectTool;
    if (m_sidebar) {
      m_sidebar->setActiveTool(m_activeTool);
    }
  } else if (command == EditorCommand::SetMode2D) {
    m_document.setWorldMode(IlscWorldMode::World2D);
    if (m_toolbar) {
      m_toolbar->showToast("Mode: 2D Orthographic (XY)",
                           ColorRgba{ 60, 220, 120, 255 });
    }
    rebuildGraph();
  } else if (command == EditorCommand::SetMode3D) {
    m_document.setWorldMode(IlscWorldMode::World3D);
    if (m_toolbar) {
      m_toolbar->showToast("Mode: 3D Perspective (XZ)",
                           ColorRgba{ 70, 160, 255, 255 });
    }
    rebuildGraph();
  } else if (command == EditorCommand::NudgeExtent) {
    nudgeSelectedExtent();
  } else if (command == EditorCommand::CycleColor) {
    cycleSelectedColor();
  } else if (command == EditorCommand::ResetCamera && ic != nullptr &&
             ic->camera != nullptr) {
    ic->camera->Reset();
    ic->camera->SetZoom(32.0f);
    if (m_toolbar) {
      m_toolbar->showToast("Camera reset to origin",
                           ColorRgba{ 66, 214, 210, 255 });
    }
  }
}

void
EditorModule::updateCamera(double dt)
{
  if (ic == nullptr || ic->camera == nullptr || ic->inputManager == nullptr) {
    return;
  }
  if (!ic->inputManager->isControlPressed()) {
    const float speed = 420.0f * static_cast<float>(dt);
    glm::vec2 pan(0.0f, 0.0f);
    if (ic->inputManager->isKeyPressed(KeyCode::A) ||
        ic->inputManager->isKeyPressed(KeyCode::Left)) {
      pan.x -= speed;
    }
    if (ic->inputManager->isKeyPressed(KeyCode::D) ||
        ic->inputManager->isKeyPressed(KeyCode::Right)) {
      pan.x += speed;
    }
    if (ic->inputManager->isKeyPressed(KeyCode::W) ||
        ic->inputManager->isKeyPressed(KeyCode::Up)) {
      pan.y += speed;
    }
    if (ic->inputManager->isKeyPressed(KeyCode::S) ||
        ic->inputManager->isKeyPressed(KeyCode::Down)) {
      pan.y -= speed;
    }
    if (pan.x != 0.0f || pan.y != 0.0f) {
      ic->camera->Pan(pan);
    }
  }

  const std::array<double, 2> mouse = ic->window->getMouseCoords();
  const bool middle =
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseMiddle);
  const bool right =
    ic->inputManager->isMouseButtonPressed(KeyCode::MouseRight);
  if (middle || (right && m_document.worldMode() != IlscWorldMode::World3D)) {
    if (m_panning) {
      ic->camera->Pan(
        glm::dvec2(m_lastMouseX - mouse[0], mouse[1] - m_lastMouseY));
    }
    m_panning = true;
  } else if (!(m_document.worldMode() == IlscWorldMode::World3D && right)) {
    m_panning = false;
  }

  if (m_document.worldMode() == IlscWorldMode::World3D && right) {
    IlscCameraState camera = m_document.camera();
    camera.yaw += static_cast<float>(mouse[0] - m_lastMouseX) * 0.01f;
    camera.pitch = std::clamp(
      camera.pitch + static_cast<float>(m_lastMouseY - mouse[1]) * 0.01f,
      0.05f,
      1.5f);
    m_document.setCamera(camera);
  }

  double* scroll = ic->inputManager->getMouseScrollOffset();
  if (scroll != nullptr && *scroll != 0.0) {
    const float factor = *scroll > 0.0 ? 1.1f : 0.9f;
    if (m_document.worldMode() != IlscWorldMode::World3D) {
      const glm::dvec2 worldMouse =
        ic->camera->ScreenToWorldPrecise(glm::dvec2(mouse[0], mouse[1]));
      ic->camera->ZoomAt(factor, worldMouse);
    } else {
      ic->camera->ZoomAt(factor, ic->camera->GetPositionPrecise());
    }
    *scroll = 0.0;
  }
}

void
EditorModule::rebuildSelectionOverlay()
{
  if (!m_selectionOverlay) {
    return;
  }
  m_selectionOverlay->clearPrimitives();
  if (m_selectedId.empty()) {
    return;
  }
  const IlscNode* node = m_document.findNode(m_selectedId);
  if (node == nullptr) {
    return;
  }
  glm::vec3 half(0.25f, 0.25f, 0.25f);
  if (IlscCodec::kindHasGeometry(node->kind)) {
    half = node->primitive.extent * 1.08f;
  }
  const float pulse = 0.82f + 0.18f * std::sin(m_animTime * 4.0f);
  const ColorRgba gold{ 255,
                        static_cast<unsigned char>(205.0f * pulse),
                        static_cast<unsigned char>(60.0f * pulse),
                        255 };

  const Matrix4 worldMat = m_document.worldMatrix(m_selectedId);
  const Transform3D worldTransform = Transform3D::fromMatrix(worldMat);
  const Vector3 worldPos = worldTransform.position;
  const Vector3 worldScale = worldTransform.scale;
  half *= worldScale;

  m_selectionOverlay->addWireCube(worldPos, half, gold);

  // Outer corner bracket / halo flare
  const float outerPulse = 0.5f + 0.5f * std::sin(m_animTime * 6.0f);
  const unsigned char cyanAlpha =
    static_cast<unsigned char>(180.0f * outerPulse);
  const ColorRgba haloCyan{ 66, 214, 210, cyanAlpha };
  m_selectionOverlay->addWireCube(worldPos, half * 1.05f, haloCyan);
}

void
EditorModule::updateSelection(double dt)
{
  (void)dt;
  if (ic == nullptr || ic->inputManager == nullptr || ic->camera == nullptr ||
      ic->window == nullptr) {
    return;
  }
  if (m_confirm && m_confirm->isOpen()) {
    return;
  }
  if (ic->commandLine != nullptr && ic->commandLine->isOpen) {
    return;
  }

  const std::array<double, 2> mouse = ic->window->getMouseCoords();
  const float scale =
    ic->renderer != nullptr ? ic->renderer->getUiScale() : 1.0f;
  const float uiX =
    static_cast<float>(mouse[0]) / (scale > 0.0f ? scale : 1.0f);
  const float uiY =
    static_cast<float>(mouse[1]) / (scale > 0.0f ? scale : 1.0f);
  const bool left = ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
  if (uiBlocksWorld(uiX, uiY)) {
    m_dragging = false;
    m_mouseWasDown = left;
    return;
  }

  float worldX = 0.0f;
  float worldY = 0.0f;
  if (!screenToWorld(static_cast<float>(mouse[0]),
                     static_cast<float>(mouse[1]),
                     &worldX,
                     &worldY)) {
    m_mouseWasDown = left;
    return;
  }
  if (left && !m_mouseWasDown) {
    if (m_activeTool != EditorCommand::SelectTool &&
        m_activeTool != EditorCommand::None) {
      applyActiveToolAt(worldX, worldY);
    } else {
      std::string hit;
      if (m_document.pick(worldX, worldY, &hit)) {
        m_selectedId = hit;
        m_dragging = true;
      } else {
        m_selectedId.clear();
        m_dragging = false;
      }
    }
    rebuildSelectionOverlay();
  } else if (left && m_dragging && !m_selectedId.empty()) {
    float previousX = worldX;
    float previousY = worldY;
    screenToWorld(static_cast<float>(m_lastMouseX),
                  static_cast<float>(m_lastMouseY),
                  &previousX,
                  &previousY);
    m_document.translate(m_selectedId, worldX - previousX, worldY - previousY);
    const IlscNode* node = m_document.findNode(m_selectedId);
    if (node != nullptr) {
      const std::unordered_map<std::string, SceneNodeHandle>::const_iterator
        found = m_handles.find(m_selectedId);
      if (found != m_handles.end()) {
        m_graph.setLocalTransform(found->second, node->transform);
      }
    }
    rebuildSelectionOverlay();
  } else if (!left) {
    m_dragging = false;
  }
  m_mouseWasDown = left;
}

void
EditorModule::Update(double dt)
{
  if (ic == nullptr) {
    return;
  }
  syncFontSize();
  const float dtF = static_cast<float>(dt);
  m_animTime += dtF;

  if (m_toolbar) {
    m_toolbar->setWorldMode(m_document.worldMode() == IlscWorldMode::World3D);
  }

  if (m_confirm && m_confirm->isOpen()) {
    const EditorConfirmAction action = m_confirm->update(ic->inputManager, dtF);
    if (action == EditorConfirmAction::Cancel) {
      m_confirm->close();
      m_pendingAction = EditorPendingAction::None;
    } else if (action == EditorConfirmAction::Discard) {
      m_confirm->close();
      performPendingAction();
    } else if (action == EditorConfirmAction::Save) {
      m_confirm->close();
      if (saveDocument(false)) {
        performPendingAction();
      } else {
        m_pendingAction = EditorPendingAction::None;
      }
    }
  } else if (m_toolbar) {
    const bool consoleOpen =
      ic->commandLine != nullptr && ic->commandLine->isOpen;
    if (!consoleOpen) {
      handleCommand(m_toolbar->update(ic->inputManager, dtF));
      if (m_sceneGraphView) {
        if (m_sceneGraphView->update(
              ic->inputManager, &m_document, &m_selectedId, dtF)) {
          rebuildGraph();
        }
      }
      if (m_sidebar) {
        handleCommand(m_sidebar->update(ic->inputManager, dtF));
      }
    } else {
      m_toolbar->closeMenus();
    }
  }

  const bool uiConsumedClick =
    (m_toolbar && m_toolbar->consumedPress()) ||
    (m_sceneGraphView && m_sceneGraphView->consumedPress()) ||
    (m_sidebar && m_sidebar->consumedPress());
  if (!(m_confirm && m_confirm->isOpen()) &&
      !(ic->commandLine != nullptr && ic->commandLine->isOpen)) {
    updateCamera(dt);
    applyWorldCamera();
    if (uiConsumedClick) {
      if (ic->inputManager != nullptr) {
        m_mouseWasDown =
          ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
      }
    } else {
      updateSelection(dt);
    }
  }

  if (!m_selectedId.empty()) {
    rebuildSelectionOverlay();
  }

  if (ic->camera != nullptr) {
    IlscCameraState camera = m_document.camera();
    const glm::dvec2 position = ic->camera->GetPositionPrecise();
    camera.x = position.x;
    camera.y = position.y;
    camera.zoom = ic->camera->GetZoom();
    if (std::abs(camera.x - m_document.camera().x) > 0.001 ||
        std::abs(camera.y - m_document.camera().y) > 0.001 ||
        std::abs(camera.zoom - m_document.camera().zoom) > 0.001f) {
      m_document.setCamera(camera);
    }
  }

  if (m_sidebar) {
    m_sidebar->setDetail(sceneDetail());
    m_sidebar->setActiveTool(m_activeTool);
  }
  applyWorldCamera();
  updateStatus();
  if (ic->window != nullptr) {
    const std::array<double, 2> mouse = ic->window->getMouseCoords();
    m_lastMouseX = mouse[0];
    m_lastMouseY = mouse[1];
  }
  if (m_toolbar) {
    m_toolbar->getVisual().prepare(ic->renderer);
  }
  if (m_sceneGraphView) {
    m_sceneGraphView->getVisual().prepare(ic->renderer);
  }
  if (m_sidebar) {
    m_sidebar->getVisual().prepare(ic->renderer);
  }
}

void
EditorModule::DispatchDrawables(Scene* scene)
{
  if (scene == nullptr) {
    return;
  }
  if (m_grid) {
    scene->AddDrawable(m_grid.get(), RenderLayerId::World);
  }
  scene->AddDrawable(&m_graph, RenderLayerId::World);
  if (m_selectionOverlay) {
    scene->AddDrawable(m_selectionOverlay.get(), RenderLayerId::World);
  }
  if (m_sceneGraphView) {
    scene->AddDrawable(m_sceneGraphView.get(), RenderLayerId::UI);
  }
  if (m_sidebar) {
    scene->AddDrawable(m_sidebar.get(), RenderLayerId::UI);
  }
  if (m_toolbar) {
    scene->AddDrawable(m_toolbar.get(), RenderLayerId::UI);
  }
  if (m_confirm && m_confirm->isOpen()) {
    scene->AddDrawable(m_confirm.get(), RenderLayerId::UI);
  }
}

EditorSceneDetail
EditorModule::sceneDetail() const
{
  return m_document.sceneDetail(m_selectedId);
}

SceneNodeKind
EditorModule::kindFromTool(EditorCommand command) const
{
  if (command == EditorCommand::CreateRect) {
    return SceneNodeKind::FilledRect;
  }
  if (command == EditorCommand::CreateEllipse) {
    return SceneNodeKind::FilledEllipse;
  }
  if (command == EditorCommand::CreateTriangle) {
    return SceneNodeKind::FilledTriangle;
  }
  if (command == EditorCommand::CreateCube) {
    return SceneNodeKind::SolidCube;
  }
  if (command == EditorCommand::CreatePyramid) {
    return SceneNodeKind::SolidPyramid;
  }
  if (command == EditorCommand::CreateSphere) {
    return SceneNodeKind::WireSphere;
  }
  return SceneNodeKind::Empty;
}

void
EditorModule::applyActiveToolAt(float worldX, float worldY)
{
  const SceneNodeKind kind = kindFromTool(m_activeTool);
  const std::string parentId = m_selectedId;
  const std::string id = m_document.createNode(kind, parentId);
  if (id.empty()) {
    return;
  }
  if (!parentId.empty()) {
    const Matrix4 parentWorld = m_document.worldMatrix(parentId);
    const Matrix4 invParent = glm::inverse(parentWorld);
    const Transform3D worldTarget =
      m_document.makeEditPlaneTransform(worldX, worldY);
    const Matrix4 localMat = invParent * worldTarget.toMatrix();
    const Vector3 localPos(localMat[3][0], localMat[3][1], localMat[3][2]);
    m_document.setTransform(id, Transform3D::fromPosition(localPos));
  } else {
    m_document.setTransform(id,
                            m_document.makeEditPlaneTransform(worldX, worldY));
  }
  m_selectedId = id;
  if (m_toolbar) {
    const IlscNode* created = m_document.findNode(id);
    m_toolbar->showToast("Created " + (created ? created->name : "node"),
                         ColorRgba{ 60, 220, 120, 255 });
  }
  rebuildGraph();
  m_activeTool = EditorCommand::SelectTool;
  if (m_sidebar) {
    m_sidebar->setActiveTool(m_activeTool);
  }
}

void
EditorModule::nudgeSelectedExtent()
{
  if (m_selectedId.empty()) {
    return;
  }
  const IlscNode* node = m_document.findNode(m_selectedId);
  if (node == nullptr) {
    return;
  }
  Vector3 extent = node->primitive.extent * 1.15f;
  if (m_document.setExtent(m_selectedId, extent)) {
    if (m_toolbar) {
      m_toolbar->showToast("Nudged node size", ColorRgba{ 66, 214, 210, 255 });
    }
    rebuildGraph();
  }
}

void
EditorModule::cycleSelectedColor()
{
  if (m_selectedId.empty()) {
    return;
  }
  const IlscNode* node = m_document.findNode(m_selectedId);
  if (node == nullptr) {
    return;
  }
  ColorRgba next = node->primitive.color;
  if (next.r >= 180 && next.g < 180) {
    next = ColorRgba{ 80, 180, 90, 255 };
  } else if (next.g >= 180 && next.b < 180) {
    next = ColorRgba{ 70, 140, 220, 255 };
  } else {
    next = ColorRgba{ 210, 90, 70, 255 };
  }
  if (m_document.setColor(m_selectedId, next)) {
    if (m_toolbar) {
      m_toolbar->showToast("Updated node color", next);
    }
    rebuildGraph();
  }
}

bool
EditorModule::screenToWorld(float screenX,
                            float screenY,
                            float* worldX,
                            float* worldY) const
{
  if (worldX == nullptr || worldY == nullptr || ic == nullptr ||
      ic->camera == nullptr || ic->window == nullptr) {
    return false;
  }
  if (m_document.worldMode() != IlscWorldMode::World3D) {
    const glm::dvec2 world = ic->camera->ScreenToWorldPrecise(
      glm::dvec2(static_cast<double>(screenX), static_cast<double>(screenY)));
    *worldX = static_cast<float>(world.x);
    *worldY = static_cast<float>(world.y);
    return true;
  }
  const std::array<int, 2> dimensions = ic->window->getWindowDimensions();
  if (dimensions[0] <= 0 || dimensions[1] <= 0) {
    return false;
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
  const glm::vec3 direction = glm::vec3(farPoint) - glm::vec3(nearPoint);
  if (std::fabs(direction.y) < 0.000001f) {
    return false;
  }
  const float t = -nearPoint.y / direction.y;
  *worldX = nearPoint.x + direction.x * t;
  *worldY = nearPoint.z + direction.z * t;
  return true;
}

void
EditorModule::syncFontSize()
{
  if (ic == nullptr) {
    return;
  }
  std::string fontVar =
    (ic->envVars != nullptr) ? ic->envVars->getVar("fontSize").value : "";
  if (fontVar.empty()) {
    fontVar = "13";
  }
  if (fontVar == m_appliedFontSizeVar) {
    return;
  }
  m_appliedFontSizeVar = fontVar;
  try {
    float size = std::stof(fontVar);
    if (size > 0.0f && size <= 4.0f) {
      size *= EditorToolbar::kDefaultFontSize;
    }
    size = std::clamp(size, 8.0f, 48.0f);
    applyFontSize(size);
  } catch (...) {
  }
}

void
EditorModule::applyFontSize(float size)
{
  if (m_toolbar) {
    m_toolbar->setFontSize(size);
  }
  if (m_sceneGraphView) {
    m_sceneGraphView->setFontSize(size);
  }
  if (m_sidebar) {
    m_sidebar->setFontSize(size);
  }
  if (m_confirm) {
    m_confirm->setFontSize(size);
  }
  if (m_toolbar && m_sidebar) {
    m_sidebar->setToolbarDimensions(m_toolbar->barHeight(),
                                    m_toolbar->statusHeight());
  }
  if (m_toolbar && m_sceneGraphView) {
    m_sceneGraphView->setToolbarDimensions(m_toolbar->barHeight(),
                                           m_toolbar->statusHeight());
  }
}
