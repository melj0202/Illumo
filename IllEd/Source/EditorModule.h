#pragma once

#include "EditorAssetBrowser.h"
#include "EditorConfirmDialog.h"
#include "EditorDocument.h"
#include "EditorGizmo.h"
#include "EditorInspector.h"
#include "EditorSceneGraphView.h"
#include "EditorSelection.h"
#include "EditorToolbar.h"
#include "EditorToolsPanel.h"
#include "IllEdPlatform.h"
#include <Illumo/Engine/IModule.h>
#include <Illumo/Gui/GuiPanelDock.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

enum class EditorPendingAction
{
  None,
  NewDocument,
  OpenDocument,
  // Opens m_pendingLocation (a scene picked in the asset browser).
  OpenLocation,
  ExitEditor
};

// The editor module: panels, viewport input, gizmo and file flows around one
// EditorDocument. The Hierarchy, Assets, Tools and Inspector panels live in a
// GuiPanelDock (D-UI7): docked in the left and right columns or popped out
// into their own windows. Implementation is split by concern:
//   EditorModule.cpp          lifetime, per-frame update, dock, drawables
//   EditorModulePanels.cpp    dock layout, panel placement, layout saving
//   EditorModuleCommands.cpp  command dispatch, file flows, node commands
//   EditorModuleViewport.cpp  camera, grid, picking, gizmo and selection input
class EditorModule : public IModule
{
  friend class EditorModuleTestAccess;

public:
  explicit EditorModule(std::string initialScenePath = {});
  ~EditorModule() override;

  EditorModule(const EditorModule&) = delete;
  EditorModule& operator=(const EditorModule&) = delete;

  bool Start(IllumoContext* context) override;
  void Update(double dt) override;
  void DispatchDrawables(Scene* scene) override;
  void Exit() override;
  bool OnCloseRequested() override;
  EditorSceneDetail sceneDetail() const;

private:
  EditorDocument m_document;
  EditorSelection m_selection;
  std::unique_ptr<EditorToolbar> m_toolbar;
  std::unique_ptr<EditorSceneGraphView> m_sceneGraphView;
  std::unique_ptr<EditorAssetBrowser> m_assetBrowser;
  IllEdLocation m_pendingLocation;
  std::unique_ptr<EditorToolsPanel> m_tools;
  std::unique_ptr<EditorInspector> m_inspector;
  GuiPanelDock m_dock;
  // Dock chrome: docked title bars and splitters in the main window, and each
  // detached panel's title bar in its own window (indexed like the panels).
  std::unique_ptr<GameVisual> m_dockVisual;
  std::array<std::unique_ptr<GameVisual>, 4> m_detachedChrome;
  // A main-window press taken by the menu bar or a dialog stays away from
  // the dock and the docked panels until the button lifts.
  bool m_dockLeftWasDown = false;
  bool m_dockPressHeld = false;
  bool m_mainPanelsBlocked = false;
  // The saved layout (panelLayout) was applied, and what was last saved.
  bool m_layoutLoaded = false;
  std::string m_savedLayout;
  std::unique_ptr<EditorConfirmDialog> m_confirm;
  TextureHandle m_atlas{};
  std::unique_ptr<MeshVisual> m_grid;
  std::unique_ptr<MeshVisual> m_selectionOverlay;
  // Box select: a drag from empty viewport space, in window pixels.
  std::unique_ptr<GameVisual> m_marquee;
  bool m_boxSelecting = false;
  bool m_boxAdditive = false;
  float m_boxStartX = 0.0f;
  float m_boxStartY = 0.0f;
  float m_boxEndX = 0.0f;
  float m_boxEndY = 0.0f;
  bool m_gridBuilt = false;
  SceneWorldMode m_gridMode = SceneWorldMode::World2D;
  EditorCommand m_activeTool;
  std::string m_initialScenePath;
  bool m_dragging;
  bool m_panning;
  bool m_mouseWasDown;
  double m_lastMouseX;
  double m_lastMouseY;
  float m_animTime;
  EditorPendingAction m_pendingAction;
  bool m_exitApproved = false;
  std::string m_appliedFontSizeVar;
  GizmoPart m_hoveredGizmoPart = GizmoPart::None;
  GizmoPart m_activeGizmoPart = GizmoPart::None;
  GizmoMode m_gizmoMode = GizmoMode::Translate;
  GizmoSpace m_gizmoSpace = GizmoSpace::World;
  EditorGizmo m_gizmo;
  // The drag's top-level nodes and their transforms at the press.
  std::vector<std::string> m_dragIds;
  std::vector<Matrix4> m_dragStartWorld;
  std::vector<Transform3D> m_dragStartLocal;
  // Each drag gets its own history merge key, so one drag is one undo step.
  uint64_t m_dragSerial = 0;
  float m_cameraTargetY = 0.0f;
  // A dialog, read or write is in flight (always briefly: guest services
  // complete on a later update). Editing input is held until it finishes.
  bool m_busy = false;
  bool m_closeAfterBusy = false;
  // Expires on Exit so late platform completions never touch a stopped
  // module.
  std::shared_ptr<bool> m_lifetime;

  // EditorModule.cpp
  void syncFontSize();
  void applyFontSize(float size);
  // Refreshes view state that depends on the document: camera projection,
  // grid and selection overlay.
  void refreshView();
  void updateStatus();
  bool uiBlocksWorld(float screenX, float screenY) const;
  void storeCameraState();
  // scene_select, scene_undo, scene_redo and scene_frame console commands,
  // for scripted captures and debugging.
  void registerCommands();
  void unregisterCommands();
  // Positions and runs the inspector, then services its clipboard requests.
  void updateInspector(float dt);
  void restoreCameraState();

  // EditorModuleCommands.cpp
  // Dispatches and then refreshes view state if the document changed.
  void handleCommand(EditorCommand command);
  void dispatchCommand(EditorCommand command);
  void requestAction(EditorPendingAction action);
  void performPendingAction();
  // `done` receives whether the document was saved (false on cancellation).
  void saveDocument(bool saveAs, std::function<void(bool saved)> done = {});
  void writeDocument(const IllEdLocation& location,
                     std::function<void(bool saved)> done);
  void openDocument();
  // `initial` loads the launch document: failures are logged, not toasted.
  // Asset references are fetched before the scene instantiates.
  void loadDocument(const IllEdLocation& location, bool initial);
  void finishLoad(const IllEdLocation& location,
                  bool initial,
                  const std::string& text,
                  const std::string& packageRoot,
                  const std::vector<std::string>& missing);
  // The package root a document's relative references resolve against.
  std::string documentRoot(const std::string& location) const;
  void saveToProject();
  void importAsset();
  void packProject();
  void createLightOrCamera(bool light);
  // Places a mesh or texture dropped from the asset browser at a main-window
  // pixel position on the edit plane, after fetching its bytes.
  void placeDroppedAsset(const std::string& path, float screenX, float screenY);
  // Fetches and places a mesh or texture at a world transform.
  void placeAssetAt(const std::string& path, const Transform3D& transform);
  void finishBusy();
  void newDocument();
  void undo();
  void redo();
  void duplicateSelection();
  void deleteSelection();
  void unparentSelection();
  // Copies the selection's subtrees to the system clipboard; cut also
  // deletes them.
  void copySelection(bool cut);
  // Pastes clipboard nodes after the primary selection (or at the root end).
  void pasteClipboard();
  // Pastes fragment text now; used by pasteClipboard's completion and tests.
  bool pasteText(const std::string& text);
  // Hides (or shows, when all are hidden) the selection; likewise enabled.
  void toggleSelectionFlag(bool visibility);
  void createChildOfPrimary();
  void selectAll();
  void nudgeSelectedExtent();
  void cycleSelectedColor();
  SaveLoadDialogSpec dialogSpec() const;
  void toast(const std::string& message, ColorRgba color);

  // EditorModulePanels.cpp
  void setupDock();
  // Lays out the dock, runs its chrome input (splitters, title bars, window
  // events) and places each panel in its rectangle and surface.
  void updateDock(bool modal);
  void layoutDock();
  GuiPanelPlacement placementFor(const std::string& id) const;
  // Applies the saved layout once settings are loaded; saves changes.
  void syncLayout();
  // View menu panel commands; false when the command is not one.
  bool handlePanelCommand(EditorCommand command);
  std::vector<EditorPanelMenuEntry> panelMenu() const;
  // A drop released in a panel's window, in main-window pixels.
  bool dropToMain(const EditorAssetBrowser::Drop& drop,
                  float* pixelX,
                  float* pixelY) const;
  void dispatchPanels(Scene* scene);
  void closePanelWindows();

  // EditorModuleViewport.cpp
  void applyWorldCamera();
  void updateCamera(double dt);
  void updateSelection(double dt);
  void rebuildSelectionOverlay();
  void rebuildGrid();
  void frameSelection();
  // Window pixels of a world point; false when it is behind the camera.
  bool worldToScreen(const Vector3& world,
                     float* screenX,
                     float* screenY) const;
  // Selects every visible node whose bounds center projects inside the box
  // (window pixels); additive adds to the selection instead of replacing it.
  void boxSelect(float x0, float y0, float x1, float y1, bool additive);
  void rebuildMarquee();
  glm::mat4 currentViewProjection() const;
  bool screenToWorld(float screenX,
                     float screenY,
                     float* worldX,
                     float* worldY) const;
  bool screenToWorldRay(float screenX,
                        float screenY,
                        glm::vec3* rayOrigin,
                        glm::vec3* rayDir) const;
  float gizmoScale(const glm::vec3& worldPos) const;
  GizmoPart hitTestGizmo(float screenX,
                         float screenY,
                         const glm::vec3& gizmoOrigin,
                         float gizmoScale) const;
  GizmoFrame gizmoFrame(const std::string& id) const;
  GizmoSnap snapSettings() const;
  void beginDrag(GizmoPart part,
                 const Vector3& rayOrigin,
                 const Vector3& rayDirection,
                 GizmoMode mode);
  // Applies the gizmo's cumulative delta to the transforms captured at the
  // press, as one merged history command per drag.
  void applyDrag(const Vector3& rayOrigin, const Vector3& rayDirection);
  // Places the armed Create tool's node on the edit plane, at the root.
  void applyActiveToolAt(float worldX, float worldY);
  std::string dragMergeKey() const;
};
