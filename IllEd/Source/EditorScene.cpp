#include "EditorScene.h"

#include "EditorAssets.h"
#include "EditorUiAtlas.h"
#include <Illumo/Content/SceneInstance.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Foundation/ParseNumber.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Gui/PanelSurfaces.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/SkyboxVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <utility>

static bool
editorContextComplete(const IllumoContext* context)
{
  return context != nullptr && context->envVars != nullptr &&
         context->window != nullptr && context->camera != nullptr &&
         context->renderer != nullptr && context->inputManager != nullptr &&
         context->commandLine != nullptr && context->scene != nullptr;
}

EditorScene::EditorScene(std::string initialScenePath)
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
  // There is always a tab, from before start to after stop.
  openTab();
}

EditorScene::~EditorScene() = default;

SceneInstanceOptions
EditorScene::contentOptions() const
{
  SceneInstanceOptions options;
  options.pickProxies = true;
  return options;
}

bool
EditorScene::start(IllumoContext& startContext)
{
  IllumoContext* context = &startContext;
  if (!editorContextComplete(context)) {
    Logger::LogError("EditorScene::start: IllumoContext missing services");
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
  m_inspector->setBehaviours(&m_behaviours);
  m_confirm = std::make_unique<EditorConfirmDialog>(ic->window, ic->renderer);
  m_recovery = std::make_unique<EditorRecoveryDialog>(ic->window, ic->renderer);

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
  // Behaviour descriptions from installed apps, packages and the project.
  m_behaviours.discover();
  // The tabs' documents (kept across a stop) bind to this start's services;
  // only the active one draws through the render world. New documents
  // belong to the mounted project, when there is one.
  for (std::size_t index = 0; index < m_tabs.size(); ++index) {
    bindTab(*m_tabs[index], index == m_activeTab);
    m_tabs[index]->document.rebase(documentRoot({}));
  }
  m_grid = std::make_unique<MeshVisual>();
  m_grid->prepare(ic->renderer);
  m_gridBuilt = false;
  m_selectionOverlay = std::make_unique<MeshVisual>();
  m_selectionOverlay->prepare(ic->renderer);
  m_dropPreview = std::make_unique<MeshVisual>();
  m_dropPreview->prepare(ic->renderer);
  m_dropPreviewShown = false;
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
    } else {
      restoreSession();
    }
  }
  // Unsaved work from an earlier run is offered once every slot was read.
  m_recoverable.clear();
  m_autosaveScanning = true;
  m_autosaveScanRead = false;
  m_autosaveScanSlot = 0;
  m_slotTaken.fill(false);
  m_autosaveWarned = false;
  m_closeAfterAutosave = false;
  m_autosaveClock = 0.0;
  for (std::unique_ptr<EditorTab>& tab : m_tabs) {
    tab->autosave = EditorTab::Autosave{};
    tab->autosave.seenUid = tab->document.history().currentUid();
  }
  m_exitDiscarded.clear();
  m_switchProjectId = 0;
  m_projectsListed = false;
  refreshProjects();

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
EditorScene::stop()
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
  // The documents keep their nodes and history; only render bindings are
  // released, so a stopped module can restart with the same tabs.
  for (std::unique_ptr<EditorTab>& tab : m_tabs) {
    tab->document.setRenderWorld(nullptr);
    tab->document.setRenderer(nullptr);
  }
  m_openQueue.clear();
  m_saveAllQueue.clear();
  m_saveAllDone = nullptr;
  m_exitDiscarded.clear();
  m_gridBuilt = false;
  m_selectionOverlay.reset();
  m_dropPreview.reset();
  m_dropPreviewShown = false;
  m_marquee.reset();
  m_grid.reset();
  m_confirm.reset();
  m_recovery.reset();
  m_recoverable.clear();
  m_autosaveScanning = false;
  for (std::unique_ptr<EditorTab>& tab : m_tabs) {
    tab->autosave.inFlight = false;
    tab->autosave.freeWanted = false;
    tab->autosave.freeDone = nullptr;
  }
  m_closeAfterAutosave = false;
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
EditorScene::refreshView()
{
  selection().prune(document().scene());
  applyWorldCamera();
  if (!m_gridBuilt || m_gridMode != document().worldMode()) {
    rebuildGrid();
    m_gridBuilt = true;
    m_gridMode = document().worldMode();
  }
  rebuildSelectionOverlay();
}

void
EditorScene::storeCameraState()
{
  if (ic == nullptr || ic->camera == nullptr) {
    return;
  }
  SceneEditorState state = document().editorState();
  const glm::dvec2 position = ic->camera->GetPositionPrecise();
  const float zoom = ic->camera->GetZoom();
  if (std::abs(state.cameraX - position.x) > 0.001 ||
      std::abs(state.cameraY - position.y) > 0.001 ||
      std::abs(state.zoom - zoom) > 0.001f ||
      !document().scene().document().hasEditor) {
    state.cameraX = position.x;
    state.cameraY = position.y;
    state.zoom = std::clamp(zoom, 0.1f, 100.0f);
    // View state is saved with the scene but never marks it dirty.
    document().setEditorState(state);
  }
}

void
EditorScene::restoreCameraState()
{
  if (ic == nullptr || ic->camera == nullptr) {
    return;
  }
  const SceneEditorState& state = document().editorState();
  ic->camera->SetPositionPrecise(state.cameraX, state.cameraY);
  ic->camera->SetZoom(state.zoom);
  activeTab().cameraTargetY = 0.0f;
}

void
EditorScene::updateStatus()
{
  if (!m_toolbar) {
    return;
  }
  std::string status = document().displayName().empty()
                         ? std::string("Untitled")
                         : document().displayName();
  if (document().isDirty()) {
    status += " *";
  }
  const SceneNode* node = document().findNode(selection().primary());
  if (node != nullptr) {
    status += "  |  " + node->name + " (#" + node->id + ")";
    if (selection().size() > 1) {
      status += " +" + std::to_string(selection().size() - 1);
    }
  }
  status += "  |  ";
  status += document().worldMode() == SceneWorldMode::World3D ? "3d" : "2d";
  status += m_gizmoMode == GizmoMode::Translate ? "  |  Move"
            : m_gizmoMode == GizmoMode::Rotate  ? "  |  Rotate"
                                                : "  |  Scale";
  status += m_gizmoSpace == GizmoSpace::World ? " world" : " local";
  if (document().editorState().snapEnabled) {
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
      const bool is3D = document().worldMode() == SceneWorldMode::World3D;
      status += "  |  X: ";
      status += std::to_string(static_cast<int>(std::round(worldX)));
      status += is3D ? ", Z: " : ", Y: ";
      status += std::to_string(static_cast<int>(std::round(worldY)));
    }
    const int zoom = static_cast<int>(std::round(ic->camera->GetZoom()));
    status += "  |  Zoom: " + std::to_string(zoom) + "x";
  }
  const SceneNode* hovered = activeTab().hoverId.empty()
                               ? nullptr
                               : document().findNode(activeTab().hoverId);
  if (hovered != nullptr) {
    status += "  |  Under cursor: " +
              (hovered->name.empty() ? hovered->id : hovered->name);
  }
  if (document().worldMode() == SceneWorldMode::World3D) {
    status += "  |  [Arrows/MMB: Pan  PgUp/PgDn: Up/Down  RMB: Orbit  "
              "Wheel: Zoom  F: Frame]";
  } else {
    status += "  |  [Arrows/MMB: Pan  Wheel: Zoom  F: Frame]";
  }
  if (!m_playApplication.empty()) {
    status = "Playing in " + m_playApplication + "  |  " + status;
  }
  if (!IllEdPlatform::current().hasProject()) {
    status = "No project  |  " + status;
  }
  if (activeTab().isolated) {
    status = "Isolated (Shift+H)  |  " + status;
  }
  m_toolbar->setStatus(status);
  // Every location change passes here, so the session record and the
  // window title follow.
  recordSession();
  applyWindowTitle();
}

bool
EditorScene::uiBlocksWorld(float screenX, float screenY) const
{
  if ((m_confirm && m_confirm->isOpen()) ||
      (m_recovery && m_recovery->isOpen()) || m_busy) {
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
EditorScene::update(double dt)
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
    m_toolbar->setWorldMode(document().worldMode() == SceneWorldMode::World3D);
    m_toolbar->setViewToggles(document().editorState().gridVisible,
                              m_pivotCenter);
    updatePlay();
    // A new document's scene starts unisolated.
    if (activeTab().isolated &&
        activeTab().isolatedGeneration != document().sceneGeneration()) {
      activeTab().isolated = false;
    }
    m_toolbar->setIsolated(activeTab().isolated);
    m_toolbar->setHistoryLabels(document().history().undoLabel(),
                                document().history().redoLabel());
  }

  updateRecoveryScan();
  const bool consoleOpen =
    ic->commandLine != nullptr && ic->commandLine->isOpen;
  const bool modal = (m_confirm && m_confirm->isOpen()) ||
                     (m_recovery && m_recovery->isOpen()) || m_busy ||
                     consoleOpen;
  syncLayout();
  updateDock(modal);
  const uint64_t revisionBefore = document().revision();
  if (m_recovery && m_recovery->isOpen()) {
    const EditorRecoveryAction action =
      m_recovery->update(ic->inputManager, dtF);
    if (action == EditorRecoveryAction::Recover) {
      m_recovery->close();
      recoverAll();
    } else if (action == EditorRecoveryAction::Discard) {
      m_recovery->close();
      discardAll();
    } else if (action == EditorRecoveryAction::Later) {
      m_recovery->close();
      deferRecovery();
    }
  } else if (m_confirm && m_confirm->isOpen()) {
    const EditorConfirmAction action = m_confirm->update(ic->inputManager, dtF);
    if (action == EditorConfirmAction::Cancel) {
      m_confirm->close();
      m_pendingAction = EditorPendingAction::None;
      m_exitDiscarded.clear();
      m_switchProjectId = 0;
    } else if (action == EditorConfirmAction::Discard) {
      m_confirm->close();
      // An exit walks every dirty tab; a discarded one is not asked again.
      if (m_pendingAction == EditorPendingAction::ExitEditor) {
        m_exitDiscarded.push_back(activeTab().serial);
      }
      performPendingAction();
    } else if (action == EditorConfirmAction::Save ||
               action == EditorConfirmAction::SaveAll) {
      m_confirm->close();
      const std::weak_ptr<bool> alive = m_lifetime;
      const std::function<void(bool)> then = [this, alive](bool saved) {
        if (alive.expired()) {
          return;
        }
        if (saved) {
          performPendingAction();
        } else {
          m_pendingAction = EditorPendingAction::None;
          m_exitDiscarded.clear();
        }
      };
      if (action == EditorConfirmAction::SaveAll) {
        saveAll(then);
      } else {
        saveDocument(false, then);
      }
    }
  } else if (m_busy) {
    // A dialog or file transfer is in flight; editing resumes after it.
  } else if (!m_openQueue.empty()) {
    // The last session's scenes or recovered work, one document per update.
    drainOpenQueue();
  } else if (m_toolbar) {
    // No exit negotiation is running: discards of an earlier one are void.
    if (m_pendingAction != EditorPendingAction::ExitEditor) {
      m_exitDiscarded.clear();
    }
    syncTabStrip();
    if (!consoleOpen) {
      int clickedTab = -1;
      bool closeTab = false;
      if (m_toolbar->takeTabClick(&clickedTab, &closeTab)) {
        if (closeTab) {
          if (activateTab(static_cast<std::size_t>(clickedTab))) {
            requestCloseTab();
          }
        } else {
          activateTab(static_cast<std::size_t>(clickedTab));
        }
      }
      // A focused inspector or hierarchy filter field consumes the keys
      // first, so typing never triggers editor shortcuts.
      updateInspector(dtF);
      updateHierarchyFilter(dtF);
      // The viewport menu's Create items place where it was opened.
      const EditorCommand chosen = m_toolbar->update(ic->inputManager, dtF);
      if (!(m_toolbar->commandFromPopup() && createAtContextPoint(chosen))) {
        handleCommand(chosen);
      }
      handleNudgeKeys();
      if (m_sceneGraphView) {
        m_sceneGraphView->update(
          ic->inputManager, &document(), &selection(), dtF);
        // Context-menu choices and double-click renames act on the selection,
        // which the panel already moved to the chosen row.
        handleCommand(m_sceneGraphView->takeCommand(nullptr));
      }
      if (m_assetBrowser) {
        m_assetBrowser->update(ic->inputManager, dtF);
        const std::string activated = m_assetBrowser->takeActivated();
        if (!activated.empty() &&
            EditorAssets::kindFor(activated) == EditorAssetKind::Scene) {
          // Into its own tab (or the active one while it is still empty).
          const std::size_t slash = activated.rfind('/');
          tabForOpening();
          loadDocument({ IllEdPlatform::kTreePrefix + activated,
                         activated.substr(slash + 1) },
                       false);
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
  if (!(m_confirm && m_confirm->isOpen()) &&
      !(m_recovery && m_recovery->isOpen()) && !m_busy &&
      !(ic->commandLine != nullptr && ic->commandLine->isOpen)) {
    updateCamera(dt);
    applyWorldCamera();
    updateContextClick();
    if (uiConsumedClick) {
      if (ic->inputManager != nullptr) {
        m_mouseWasDown =
          ic->inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
      }
    } else {
      updateSelection(dt);
    }
  }

  updateDropPreview();
  if (document().revision() != revisionBefore) {
    refreshView();
  }
  document().scene().update();
  if (!selection().empty()) {
    rebuildSelectionOverlay();
  }
  storeCameraState();
  followGrid();
  updateAutosave(dt);

  if (m_tools) {
    EditorToolsState state;
    state.is3D = document().worldMode() == SceneWorldMode::World3D;
    state.activeTool = m_activeTool;
    state.gizmoMode = m_gizmoMode;
    state.gizmoSpace = m_gizmoSpace;
    state.pivotCenter = m_pivotCenter;
    const SceneEditorState& view = document().editorState();
    state.snap = view.snapEnabled;
    state.gridVisible = view.gridVisible;
    state.gridSpacing = view.gridSpacing;
    state.snapTranslate = view.snapTranslate;
    state.snapRotateDegrees = view.snapRotateDegrees;
    state.snapScale = view.snapScale;
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
EditorScene::registerCommands()
{
  if (ic == nullptr || ic->commandRegistry == nullptr) {
    return;
  }
  ic->commandRegistry->RegisterCommand(
    "scene_select",
    [this](const std::vector<std::string>& args) {
      selection().clear();
      for (const std::string& id : args) {
        if (document().findNode(id) != nullptr) {
          selection().add(id);
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
      if ((args.size() > 1 && !parseFloating(args[1], &x)) ||
          (args.size() > 2 && !parseFloating(args[2], &y))) {
        Logger::LogWarning("scene_place ignored: the coordinates are not "
                           "numbers");
        return;
      }
      placeAssetAt(args[0], document().makeEditPlaneTransform(x, y));
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
  ic->commandRegistry->RegisterCommand(
    "scene_field",
    [this](const std::vector<std::string>& args) {
      if (!args.empty() && m_inspector) {
        m_inspector->activateField(args.front(), &document(), &selection());
      }
    },
    "scene_field <inspector key>",
    "Activate an inspector field as a click would (a swatch opens its "
    "picker)");
  ic->commandRegistry->RegisterCommand(
    "scene_play",
    [this](const std::vector<std::string>&) {
      handleCommand(EditorCommand::PlayScene);
    },
    "scene_play",
    "Play the scene in its game's window, or stop it");
  ic->commandRegistry->RegisterCommand(
    "scene_create",
    [this](const std::vector<std::string>& args) {
      struct Shape
      {
        const char* name;
        EditorCommand command;
      };
      static const Shape shapes[] = {
        { "empty", EditorCommand::CreateEmpty },
        { "rect", EditorCommand::CreateRect },
        { "ellipse", EditorCommand::CreateEllipse },
        { "triangle", EditorCommand::CreateTriangle },
        { "cube", EditorCommand::CreateCube },
        { "pyramid", EditorCommand::CreatePyramid },
        { "sphere", EditorCommand::CreateSphere },
        { "wire_cube", EditorCommand::CreateWireCube },
        { "wire_sphere", EditorCommand::CreateWireSphere },
      };
      const std::string name = args.empty() ? std::string("cube") : args[0];
      for (const Shape& shape : shapes) {
        if (name == shape.name) {
          const EditorCommand tool = m_activeTool;
          m_activeTool = shape.command;
          applyActiveToolAt(0.0f, 0.0f);
          m_activeTool = tool;
          refreshView();
          updateStatus();
          return;
        }
      }
      if (name == "light" || name == "camera") {
        createLightOrCamera(name == "light");
        refreshView();
        updateStatus();
        return;
      }
      ic->commandLine->logError("scene_create: unknown shape " + name);
    },
    "scene_create [shape]",
    "Create a node at the origin: empty, rect, ellipse, triangle, cube, "
    "pyramid, sphere, wire_cube, wire_sphere, light or camera");
  ic->commandRegistry->RegisterCommand(
    "scene_autosave",
    [this](const std::vector<std::string>&) { autosaveNow(activeTab()); },
    "scene_autosave",
    "Write the dirty document to its autosave slot now");
  ic->commandRegistry->RegisterCommand(
    "scene_recover",
    [this](const std::vector<std::string>& args) {
      if (!m_recovery || !m_recovery->isOpen()) {
        ic->commandLine->logError("scene_recover: nothing is offered");
        return;
      }
      const std::string answer =
        args.empty() ? std::string("recover") : args[0];
      m_recovery->close();
      if (answer == "recover") {
        recoverAll();
      } else if (answer == "discard") {
        discardAll();
      } else {
        deferRecovery();
      }
    },
    "scene_recover [recover|discard|later]",
    "Answer the recovery dialog as a click would");
  ic->commandRegistry->RegisterCommand(
    "scene_status",
    [this](const std::vector<std::string>&) {
      std::string line = "scene_status #" + std::to_string(++m_statusSerial) +
                         " " + std::to_string(document().nodeCount()) +
                         " nodes " + (document().isDirty() ? "dirty" : "clean");
      if (m_recovery && m_recovery->isOpen()) {
        line += " recovery";
      }
      if (activeTab().autosave.live) {
        line += " autosaved";
      }
      line += " tab " + std::to_string(m_activeTab + 1) + "/" +
              std::to_string(m_tabs.size());
      line +=
        " " + (document().displayName().empty() ? std::string("Untitled")
                                                : document().displayName());
      ic->commandLine->logNormal(line);
    },
    "scene_status",
    "Print the node count, dirty state, tab and name (scripts and tests)");
  ic->commandRegistry->RegisterCommand(
    "scene_tab",
    [this](const std::vector<std::string>& args) {
      const std::string action = args.empty() ? std::string("list") : args[0];
      if (action == "open") {
        tabForOpening();
        newDocument();
      } else if (action == "next") {
        cycleTab(1);
      } else if (action == "prev") {
        cycleTab(-1);
      } else if (action == "close") {
        requestCloseTab();
      } else if (action == "go" && args.size() > 1) {
        long number = 0;
        if (parseWholeInteger(args[1], &number) && number >= 1) {
          activateTab(static_cast<std::size_t>(number) - 1);
        } else {
          ic->commandLine->logError("scene_tab go: expected a tab number");
        }
      } else {
        for (std::size_t index = 0; index < m_tabs.size(); ++index) {
          const EditorDocument& entry = m_tabs[index]->document;
          ic->commandLine->logNormal("scene_tab " + std::to_string(index + 1) +
                                     (index == m_activeTab ? "*" : "") + " " +
                                     (entry.displayName().empty()
                                        ? std::string("Untitled")
                                        : entry.displayName()) +
                                     (entry.isDirty() ? " dirty" : " clean"));
        }
      }
    },
    "scene_tab [open|next|prev|close|go <n>|list]",
    "Open, switch, close or list document tabs (scripts and tests)");
  ic->commandRegistry->RegisterCommand(
    "scene_project",
    [this](const std::vector<std::string>& args) {
      const std::string action = args.empty() ? std::string("list") : args[0];
      if (action == "open" && args.size() > 1) {
        long number = 0;
        if (parseWholeInteger(args[1], &number) && number >= 1 &&
            static_cast<std::size_t>(number) <= m_recentProjects.size()) {
          switchProject(
            m_recentProjects[static_cast<std::size_t>(number) - 1].id);
        } else {
          ic->commandLine->logError("scene_project open: expected a listed "
                                    "project number");
        }
      } else if (action == "choose") {
        openProject(false);
      } else if (action == "new") {
        openProject(true);
      } else if (action == "clear") {
        handleCommand(EditorCommand::ClearRecentProjects);
      } else if (action == "refresh") {
        refreshProjects();
      } else {
        ic->commandLine->logNormal(
          "scene_project #" + std::to_string(++m_statusSerial) + " " +
          (IllEdPlatform::current().canSwitchProject() ? "available "
                                                       : "unavailable ") +
          std::to_string(m_recentProjects.size()) + " recent");
        for (std::size_t index = 0; index < m_recentProjects.size(); ++index) {
          ic->commandLine->logNormal("scene_project " +
                                     std::to_string(index + 1) + " " +
                                     m_recentProjects[index].label);
        }
      }
    },
    "scene_project [list|open <n>|choose|new|clear|refresh]",
    "List the host's recent projects or switch to one (scripts and tests)");
}

void
EditorScene::unregisterCommands()
{
  if (ic == nullptr || ic->commandRegistry == nullptr) {
    return;
  }
  for (const char* name : { "scene_select",
                            "scene_place",
                            "scene_save_project",
                            "scene_undo",
                            "scene_redo",
                            "scene_frame",
                            "scene_field",
                            "scene_play",
                            "scene_create",
                            "scene_autosave",
                            "scene_recover",
                            "scene_status",
                            "scene_tab",
                            "scene_project" }) {
    ic->commandRegistry->UnregisterCommand(name);
  }
}

void
EditorScene::updateInspector(float dt)
{
  if (!m_inspector || ic == nullptr) {
    return;
  }
  m_inspector->update(ic->inputManager, &document(), &selection(), dt);
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
EditorScene::updateHierarchyFilter(float dt)
{
  if (!m_sceneGraphView || ic == nullptr) {
    return;
  }
  m_sceneGraphView->updateFilterInput(ic->inputManager, dt);
  std::string copied;
  if (m_sceneGraphView->takeFilterCopy(&copied)) {
    IllEdPlatform::current().setClipboardText(copied);
  }
  if (m_sceneGraphView->takeFilterPaste()) {
    const std::weak_ptr<bool> alive = m_lifetime;
    IllEdPlatform::current().requestClipboardText(
      [this, alive](const std::string& text) {
        if (!alive.expired() && m_sceneGraphView) {
          m_sceneGraphView->provideFilterPaste(text);
        }
      });
  }
}

void
EditorScene::dispatch(DrawList& frame)
{
  DrawList* scene = &frame;
  if (document().worldMode() == SceneWorldMode::World3D &&
      document().scene().skybox() != nullptr) {
    scene->AddDrawable(document().scene().skybox(), RenderLayerId::World);
  }
  if (m_grid && document().editorState().gridVisible) {
    scene->AddDrawable(m_grid.get(), RenderLayerId::World);
  }
  scene->AddDrawable(&document().scene().drawable(), RenderLayerId::World);
  if (m_selectionOverlay) {
    scene->AddDrawable(m_selectionOverlay.get(), RenderLayerId::World);
  }
  if (m_dropPreview && m_dropPreviewShown) {
    scene->AddDrawable(m_dropPreview.get(), RenderLayerId::World);
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
  if (m_recovery && m_recovery->isOpen()) {
    scene->AddDrawable(m_recovery.get(), RenderLayerId::UI);
  }
}

EditorSceneDetail
EditorScene::sceneDetail() const
{
  return document().sceneDetail(selection());
}

void
EditorScene::syncFontSize()
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
  float size = 0.0f;
  if (!parseFloating(fontVar, &size)) {
    Logger::LogWarning("Ignored the fontSize setting '" + fontVar +
                       "': not a number");
    return;
  }
  if (size > 0.0f && size <= 4.0f) {
    size *= EditorToolbar::kDefaultFontSize;
  }
  size = std::clamp(size, 8.0f, 48.0f);
  applyFontSize(size);
}

void
EditorScene::applyFontSize(float size)
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
  if (m_recovery) {
    m_recovery->setFontSize(size);
  }
}
