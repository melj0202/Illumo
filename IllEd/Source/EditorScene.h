#pragma once

#include "EditorAssetBrowser.h"
#include "EditorBehaviours.h"
#include "EditorConfirmDialog.h"
#include "EditorDocument.h"
#include "EditorGizmo.h"
#include "EditorInspector.h"
#include "EditorSceneGraphView.h"
#include "EditorSelection.h"
#include "EditorToolbar.h"
#include "EditorToolsPanel.h"
#include "IllEdPlatform.h"
#include <Illumo/Content/ProgramScene.h>
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

// The editor, as the IllEd program's one scene: panels, viewport input, gizmo and file flows around one
// EditorDocument. The Hierarchy, Assets, Tools and Inspector panels live in a
// GuiPanelDock (D-UI7): docked in the left and right columns or popped out
// into their own windows. Implementation is split by concern:
//   EditorScene.cpp          lifetime, per-frame update, dock, drawables
//   EditorScenePanels.cpp    dock layout, panel placement, layout saving
//   EditorSceneCommands.cpp  command dispatch, file flows, node commands
//   EditorSceneViewport.cpp  camera, grid, picking, gizmo and selection input
class EditorScene : public ProgramScene
{
  friend class EditorSceneTestAccess;

public:
  explicit EditorScene(std::string initialScenePath = {});
  ~EditorScene() override;

  EditorScene(const EditorScene&) = delete;
  EditorScene& operator=(const EditorScene&) = delete;

  bool start(IllumoContext& context) override;
  void update(double dt) override;
  void dispatch(DrawList& frame) override;
  void stop() override;
  bool closeRequested() override;
  // The edited document is this scene's content, which picks through proxies.
  SceneInstanceOptions contentOptions() const override;
  EditorSceneDetail sceneDetail() const;

private:
  // The services this scene started with.
  IllumoContext* ic{ nullptr };
  // Before the document, which borrows its schema.
  EditorBehaviours m_behaviours;
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
  // A ghost on the edit plane where a dragged mesh or texture would land.
  std::unique_ptr<MeshVisual> m_dropPreview;
  bool m_dropPreviewShown = false;
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
  // Rotate and scale about the selection's bounds centre (else the primary
  // node's origin). Session state, like the gizmo space.
  bool m_pivotCenter = false;
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

  // EditorScene.cpp
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
  // Keys for the hierarchy's focused filter field, and its clipboard.
  void updateHierarchyFilter(float dt);
  void restoreCameraState();

  // EditorSceneCommands.cpp
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
  // Grid and snap settings: toggles and ladder steps of the editor view
  // state (never an edit); false when the command is not one.
  bool handleSettingCommand(EditorCommand command);
  // Align and distribute commands; false when the command is not one.
  bool handleArrangeCommand(EditorCommand command);
  void nudgeSelectedExtent();
  void cycleSelectedColor();
  SaveLoadDialogSpec dialogSpec() const;
  void toast(const std::string& message, ColorRgba color);

  // EditorScenePanels.cpp
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
  void dispatchPanels(DrawList* scene);
  void closePanelWindows();

  // EditorSceneViewport.cpp
  void applyWorldCamera();
  void updateCamera(double dt);
  void updateSelection(double dt);
  void rebuildSelectionOverlay();
  void rebuildGrid();
  void frameSelection();
  // The viewport between the dock columns and bars, in window pixels: its
  // size, and how far its centre sits from the window's centre.
  void viewportPixels(float* width,
                      float* height,
                      float* offsetX,
                      float* offsetY) const;
  // Zooms and aims the camera so a world point lands on the viewport's
  // centre.
  void placeCamera(const Vector3& point, float zoom);
  // Window pixels of a world point; false when it is behind the camera.
  bool worldToScreen(const Vector3& world,
                     float* screenX,
                     float* screenY) const;
  // Selects every visible node whose bounds center projects inside the box
  // (window pixels); additive adds to the selection instead of replacing it.
  void boxSelect(float x0, float y0, float x1, float y1, bool additive);
  void rebuildMarquee();
  // Shows the drop ghost while the Assets panel drags a placeable file over
  // the viewport (docked or from a detached window), and hides it otherwise.
  void updateDropPreview();
  // Shows the ghost for a file at a main-window pixel; false (hidden) when
  // the file cannot be placed or the point is not over the viewport.
  bool showDropPreview(const std::string& path, float pixelX, float pixelY);
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
  // The centre of the selection's top-level world bounds (a node without
  // bounds counts as its origin); false when nothing is selected.
  bool selectionCenter(Vector3* center) const;
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
