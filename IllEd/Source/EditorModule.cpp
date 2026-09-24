#include "EditorModule.h"

#include "EditorAssets.h"
#include "EditorUiAtlas.h"
#include <Illumo/Content/SceneInstance.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Gui/PanelSurfaces.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/SkyboxVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <array>
#include <cmath>
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

  m_exitApproved = false;
  m_toolbar = std::make_unique<EditorToolbar>(ic->window, ic->renderer);
  m_sceneGraphView =
    std::make_unique<EditorSceneGraphView>(ic->window, ic->renderer);
  m_assetBrowser =
    std::make_unique<EditorAssetBrowser>(ic->window, ic->renderer);
  m_tools = std::make_unique<EditorToolsPanel>(ic->window, ic->renderer);
  m_inspector = std::make_unique<EditorInspector>(ic->window, ic->renderer);
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
      m_tools->setAtlas(m_atlas);
    } else {
      Logger::LogWarning(std::string("Editor UI atlas ") +
                         EditorUiAtlas::relativePath() +
                         " is unavailable; panels draw without icons");
    }
  }
  m_document.setAssetManager(ic->assetManager);
  m_document.setRenderer(ic->renderer);
  // New documents belong to the mounted project, when there is one.
  m_document.rebase(documentRoot({}));
  m_grid = std::make_unique<MeshVisual>();
  m_grid->prepare(ic->renderer);
  m_gridBuilt = false;
  m_selectionOverlay = std::make_unique<MeshVisual>();
  m_selectionOverlay->prepare(ic->renderer);
  m_marquee = std::make_unique<GameVisual>(16u);
  m_marquee->setSpace(PrimitiveSpace::Pixels);
  m_marquee->setLayerHint(RenderLayerId::UI);
  m_marquee->setWindow(ic->window);
  m_marquee->setRenderer(ic->renderer);
  m_marquee->prepare(ic->renderer);
  m_boxSelecting = false;

  m_lifetime = std::make_shared<bool>(true);
  m_busy = false;
  m_closeAfterBusy = false;
  if (m_initialScenePath.empty() && ic->envVars != nullptr) {
    m_initialScenePath = ic->envVars->getVar("LaunchScene").value;
  }
  if (!m_initialScenePath.empty()) {
    loadDocument({ m_initialScenePath, m_initialScenePath }, true);
  } else {
    const IllEdLocation launch = IllEdPlatform::current().launchDocument();
    if (!launch.empty()) {
      loadDocument(launch, true);
    }
  }

  setupDock();
  updateDock(false);
  registerCommands();
  ic->camera->SetSmoothingSpeed(18.0f);
  restoreCameraState();
  refreshView();
  updateStatus();
  Logger::LogInfo(IllEdPlatform::current().hasProject()
                    ? "Scene editor ready; new scenes belong to /project"
                    : "Scene editor ready");
  return true;
}

void
EditorModule::Exit()
{
  unregisterCommands();
  syncLayout();
  closePanelWindows();
  m_lifetime.reset();
  m_busy = false;
  m_closeAfterBusy = false;
  if (ic != nullptr && ic->assetManager != nullptr && m_atlas.isValid()) {
    ic->assetManager->releaseTexture(m_atlas);
    m_atlas = TextureHandle{};
  }
  // The document keeps its nodes and history; only render bindings are
  // released, so a stopped module can restart with the same document.
  m_document.setRenderer(nullptr);
  m_gridBuilt = false;
  m_selectionOverlay.reset();
  m_marquee.reset();
  m_grid.reset();
  m_confirm.reset();
  m_inspector.reset();
  m_tools.reset();
  m_dockVisual.reset();
  for (std::unique_ptr<GameVisual>& chrome : m_detachedChrome) {
    chrome.reset();
  }
  m_assetBrowser.reset();
  m_sceneGraphView.reset();
  m_toolbar.reset();
}

void
EditorModule::refreshView()
{
  m_selection.prune(m_document.scene());
  applyWorldCamera();
  if (!m_gridBuilt || m_gridMode != m_document.worldMode()) {
    rebuildGrid();
    m_gridBuilt = true;
    m_gridMode = m_document.worldMode();
  }
  rebuildSelectionOverlay();
}

void
EditorModule::storeCameraState()
{
  if (ic == nullptr || ic->camera == nullptr) {
    return;
  }
  SceneEditorState state = m_document.editorState();
  const glm::dvec2 position = ic->camera->GetPositionPrecise();
  const float zoom = ic->camera->GetZoom();
  if (std::abs(state.cameraX - position.x) > 0.001 ||
      std::abs(state.cameraY - position.y) > 0.001 ||
      std::abs(state.zoom - zoom) > 0.001f ||
      !m_document.scene().document().hasEditor) {
    state.cameraX = position.x;
    state.cameraY = position.y;
    state.zoom = std::clamp(zoom, 0.1f, 100.0f);
    // View state is saved with the scene but never marks it dirty.
    m_document.setEditorState(state);
  }
}

void
EditorModule::restoreCameraState()
{
  if (ic == nullptr || ic->camera == nullptr) {
    return;
  }
  const SceneEditorState& state = m_document.editorState();
  ic->camera->SetPositionPrecise(state.cameraX, state.cameraY);
  ic->camera->SetZoom(state.zoom);
  m_cameraTargetY = 0.0f;
}

void
EditorModule::updateStatus()
{
  if (!m_toolbar) {
    return;
  }
  std::string status = m_document.displayName().empty()
                         ? std::string("Untitled")
                         : m_document.displayName();
  if (m_document.isDirty()) {
    status += " *";
  }
  const SceneNode* node = m_document.findNode(m_selection.primary());
  if (node != nullptr) {
    status += "  |  " + node->name + " (#" + node->id + ")";
    if (m_selection.size() > 1) {
      status += " +" + std::to_string(m_selection.size() - 1);
    }
  }
  status += "  |  ";
  status += m_document.worldMode() == SceneWorldMode::World3D ? "3d" : "2d";
  status += m_gizmoMode == GizmoMode::Translate ? "  |  Move"
            : m_gizmoMode == GizmoMode::Rotate  ? "  |  Rotate"
                                                : "  |  Scale";
  status += m_gizmoSpace == GizmoSpace::World ? " world" : " local";
  if (m_document.editorState().snapEnabled) {
    status += " snap";
  }
  if (ic != nullptr && ic->window != nullptr && ic->camera != nullptr) {
    const std::array<double, 2> mouse = ic->window->getMouseCoords();
    float worldX = 0.0f;
    float worldY = 0.0f;
    if (screenToWorld(static_cast<float>(mouse[0]),
                      static_cast<float>(mouse[1]),
                      &worldX,
                      &worldY)) {
      const bool is3D = m_document.worldMode() == SceneWorldMode::World3D;
      status += "  |  X: ";
      status += std::to_string(static_cast<int>(std::round(worldX)));
      status += is3D ? ", Z: " : ", Y: ";
      status += std::to_string(static_cast<int>(std::round(worldY)));
    }
    const int zoom = static_cast<int>(std::round(ic->camera->GetZoom()));
    status += "  |  Zoom: " + std::to_string(zoom) + "x";
  }
  if (m_document.worldMode() == SceneWorldMode::World3D) {
    status += "  |  [Arrows/MMB: Pan  PgUp/PgDn: Up/Down  RMB: Orbit  "
              "Wheel: Zoom  F: Frame]";
  } else {
    status += "  |  [Arrows/MMB: Pan  Wheel: Zoom  F: Frame]";
  }
  m_toolbar->setStatus(status);
}

bool
EditorModule::uiBlocksWorld(float screenX, float screenY) const
{
  if ((m_confirm && m_confirm->isOpen()) || m_busy) {
    return true;
  }
  if (m_toolbar && m_toolbar->containsScreenPoint(screenX, screenY)) {
    return true;
  }
  if (m_dock.dragging() || m_dock.overPanels(screenX, screenY)) {
    return true;
  }
  // Only the viewport between the dock columns and the bars is the world.
  return !m_dock.center().contains(screenX, screenY);
}
void
EditorModule::Update(double dt)
{
  // Resuming a frame means close negotiation was canceled, possibly before
  // this module was consulted. Approval cannot survive further editing.
  m_exitApproved = false;
  if (ic == nullptr) {
    return;
  }
  syncFontSize();
  const float dtF = static_cast<float>(dt);
  m_animTime += dtF;

  if (m_toolbar) {
    m_toolbar->setWorldMode(m_document.worldMode() == SceneWorldMode::World3D);
    m_toolbar->setHistoryLabels(m_document.history().undoLabel(),
                                m_document.history().redoLabel());
  }

  const bool consoleOpen =
    ic->commandLine != nullptr && ic->commandLine->isOpen;
  const bool modal =
    (m_confirm && m_confirm->isOpen()) || m_busy || consoleOpen;
  syncLayout();
  updateDock(modal);
  const uint64_t revisionBefore = m_document.revision();
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
      const std::weak_ptr<bool> alive = m_lifetime;
      saveDocument(false, [this, alive](bool saved) {
        if (alive.expired()) {
          return;
        }
        if (saved) {
          performPendingAction();
        } else {
          m_pendingAction = EditorPendingAction::None;
        }
      });
    }
  } else if (m_busy) {
    // A dialog or file transfer is in flight; editing resumes after it.
  } else if (m_toolbar) {
    if (!consoleOpen) {
      // A focused inspector field consumes the keys first, so typing never
      // triggers editor shortcuts.
      updateInspector(dtF);
      handleCommand(m_toolbar->update(ic->inputManager, dtF));
      if (m_sceneGraphView) {
        m_sceneGraphView->update(
          ic->inputManager, &m_document, &m_selection, dtF);
        // Context-menu choices and double-click renames act on the selection,
        // which the panel already moved to the chosen row.
        handleCommand(m_sceneGraphView->takeCommand(nullptr));
      }
      if (m_assetBrowser) {
        m_assetBrowser->update(ic->inputManager, dtF);
        const std::string activated = m_assetBrowser->takeActivated();
        if (!activated.empty() &&
            EditorAssets::kindFor(activated) == EditorAssetKind::Scene) {
          const std::size_t slash = activated.rfind('/');
          m_pendingLocation = { IllEdPlatform::kTreePrefix + activated,
                                activated.substr(slash + 1) };
          requestAction(EditorPendingAction::OpenLocation);
        }
        // A drop may come from a detached Assets window; it places where
        // it lands in the main window.
        const EditorAssetBrowser::Drop dropped = m_assetBrowser->takeDrop();
        float dropX = 0.0f;
        float dropY = 0.0f;
        if (!dropped.path.empty() && dropToMain(dropped, &dropX, &dropY)) {
          placeDroppedAsset(dropped.path, dropX, dropY);
        }
      }
      if (m_tools) {
        handleCommand(m_tools->update(ic->inputManager, dtF));
      }
    } else {
      m_toolbar->closeMenus();
    }
  }

  const bool uiConsumedClick =
    (m_toolbar && m_toolbar->consumedPress()) ||
    (m_sceneGraphView && m_sceneGraphView->consumedPress()) ||
    (m_assetBrowser && m_assetBrowser->consumedPress()) ||
    (m_tools && m_tools->consumedPress()) ||
    (m_inspector && m_inspector->consumedPress()) ||
    m_dock.consumedPress(IPanelSurfaces::kMainSurface) || m_dock.dragging();
  if (!(m_confirm && m_confirm->isOpen()) && !m_busy &&
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

  if (m_document.revision() != revisionBefore) {
    refreshView();
  }
  m_document.scene().update();
  if (!m_selection.empty()) {
    rebuildSelectionOverlay();
  }
  storeCameraState();

  if (m_tools) {
    EditorToolsState state;
    state.is3D = m_document.worldMode() == SceneWorldMode::World3D;
    state.activeTool = m_activeTool;
    state.gizmoMode = m_gizmoMode;
    state.gizmoSpace = m_gizmoSpace;
    state.snap = m_document.editorState().snapEnabled;
    m_tools->setState(state);
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
  if (m_assetBrowser) {
    m_assetBrowser->getVisual().prepare(ic->renderer);
  }
  if (m_marquee && m_boxSelecting) {
    m_marquee->prepare(ic->renderer);
  }
  if (m_tools) {
    m_tools->getVisual().prepare(ic->renderer);
  }
  if (m_inspector) {
    m_inspector->getVisual().prepare(ic->renderer);
  }
  if (m_dockVisual) {
    m_dockVisual->prepare(ic->renderer);
  }
  for (std::unique_ptr<GameVisual>& chrome : m_detachedChrome) {
    if (chrome) {
      chrome->prepare(ic->renderer);
    }
  }
}

void
EditorModule::registerCommands()
{
  if (ic == nullptr || ic->commandRegistry == nullptr) {
    return;
  }
  ic->commandRegistry->RegisterCommand(
    "scene_select",
    [this](const std::vector<std::string>& args) {
      m_selection.clear();
      for (const std::string& id : args) {
        if (m_document.findNode(id) != nullptr) {
          m_selection.add(id);
        }
      }
      refreshView();
    },
    "scene_select [id...]",
    "Select scene nodes by id (none clears the selection)");
  ic->commandRegistry->RegisterCommand(
    "scene_place",
    [this](const std::vector<std::string>& args) {
      if (args.empty()) {
        return;
      }
      float x = 0.0f;
      float y = 0.0f;
      try {
        x = args.size() > 1 ? std::stof(args[1]) : 0.0f;
        y = args.size() > 2 ? std::stof(args[2]) : 0.0f;
      } catch (...) {
        Logger::LogWarning("scene_place ignored: the coordinates are not "
                           "numbers");
        return;
      }
      placeAssetAt(args[0], m_document.makeEditPlaneTransform(x, y));
    },
    "scene_place <virtual path> [x] [y]",
    "Place a mesh or texture from the file tree on the edit plane");
  ic->commandRegistry->RegisterCommand(
    "scene_save_project",
    [this](const std::vector<std::string>&) { saveToProject(); },
    "scene_save_project",
    "Save the scene into /project/scenes (needs --project)");
  ic->commandRegistry->RegisterCommand(
    "scene_undo",
    [this](const std::vector<std::string>&) {
      handleCommand(EditorCommand::Undo);
    },
    "scene_undo",
    "Undo the last scene edit");
  ic->commandRegistry->RegisterCommand(
    "scene_redo",
    [this](const std::vector<std::string>&) {
      handleCommand(EditorCommand::Redo);
    },
    "scene_redo",
    "Redo the next scene edit");
  ic->commandRegistry->RegisterCommand(
    "scene_frame",
    [this](const std::vector<std::string>&) {
      handleCommand(EditorCommand::FrameSelection);
    },
    "scene_frame",
    "Frame the selection (or the whole scene)");
}

void
EditorModule::unregisterCommands()
{
  if (ic == nullptr || ic->commandRegistry == nullptr) {
    return;
  }
  for (const char* name : { "scene_select",
                            "scene_place",
                            "scene_save_project",
                            "scene_undo",
                            "scene_redo",
                            "scene_frame" }) {
    ic->commandRegistry->UnregisterCommand(name);
  }
}

void
EditorModule::updateInspector(float dt)
{
  if (!m_inspector || ic == nullptr) {
    return;
  }
  m_inspector->update(ic->inputManager, &m_document, &m_selection, dt);
  std::string copied;
  if (m_inspector->takeCopyRequest(&copied)) {
    IllEdPlatform::current().setClipboardText(copied);
  }
  if (m_inspector->takePasteRequest()) {
    const std::weak_ptr<bool> alive = m_lifetime;
    IllEdPlatform::current().requestClipboardText(
      [this, alive](const std::string& text) {
        if (!alive.expired() && m_inspector) {
          m_inspector->providePaste(text);
        }
      });
  }
}

void
EditorModule::DispatchDrawables(Scene* scene)
{
  if (scene == nullptr) {
    return;
  }
  if (m_document.worldMode() == SceneWorldMode::World3D &&
      m_document.scene().skybox() != nullptr) {
    scene->AddDrawable(m_document.scene().skybox(), RenderLayerId::World);
  }
  if (m_grid && m_document.editorState().gridVisible) {
    scene->AddDrawable(m_grid.get(), RenderLayerId::World);
  }
  scene->AddDrawable(&m_document.scene().drawable(), RenderLayerId::World);
  if (m_selectionOverlay) {
    scene->AddDrawable(m_selectionOverlay.get(), RenderLayerId::World);
  }
  if (m_marquee && m_boxSelecting) {
    scene->AddDrawable(m_marquee.get(), RenderLayerId::UI);
  }
  // Dock chrome and docked panels here; detached ones in their windows.
  dispatchPanels(scene);
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
  return m_document.sceneDetail(m_selection);
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
    Logger::LogWarning("Ignored the fontSize setting '" + fontVar +
                       "': not a number");
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
  if (m_assetBrowser) {
    m_assetBrowser->setFontSize(size);
  }
  if (m_tools) {
    m_tools->setFontSize(size);
  }
  if (m_inspector) {
    m_inspector->setFontSize(size);
  }
  if (m_confirm) {
    m_confirm->setFontSize(size);
  }
}
