#pragma once

#include "EditorAssetBrowser.h"
#include "EditorAutosave.h"
#include "EditorBehaviours.h"
#include "EditorConfirmDialog.h"
#include "EditorDocument.h"
#include "EditorGizmo.h"
#include "EditorInspector.h"
#include "EditorRecoveryDialog.h"
#include "EditorSceneGraphView.h"
#include "EditorSelection.h"
#include "EditorTab.h"
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
  // Reloads the document's saved location.
  RevertDocument,
  // Closes the active tab.
  CloseTab,
  ExitEditor
};

// The editor, as the IllEd program's one scene: panels, viewport input, gizmo
// and file flows around the open documents (EditorTab), one of which is
// active. The Hierarchy, Assets, Tools and Inspector panels live in a
// GuiPanelDock (D-UI7): docked in the left and right columns or popped out
// into their own windows. Implementation is split by concern:
//   EditorScene.cpp          lifetime, per-frame update, dock, drawables
//   EditorScenePanels.cpp    dock layout, panel placement, layout saving
//   EditorSceneCommands.cpp  command dispatch, file flows, node commands
//   EditorSceneViewport.cpp  camera, grid, picking, gizmo and selection input
//   EditorSceneTabs.cpp      tabs: opening, switching, closing, Save All
//   EditorSceneAutosave.cpp  autosave slots, recovery at start, session record
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
  // Every document picks through proxies (the tabs own their instances; the
  // program scene's content stays unused).
  SceneInstanceOptions contentOptions() const override;
  EditorSceneDetail sceneDetail() const;

  // The open documents and the active one; there is always at least one.
  const std::vector<std::unique_ptr<EditorTab>>& tabs() const { return m_tabs; }
  std::size_t activeTabIndex() const { return m_activeTab; }
  EditorTab& activeTab() { return *m_tabs[m_activeTab]; }
  const EditorTab& activeTab() const { return *m_tabs[m_activeTab]; }
  EditorDocument& document() { return activeTab().document; }
  const EditorDocument& document() const { return activeTab().document; }
  EditorSelection& selection() { return activeTab().selection; }
  const EditorSelection& selection() const { return activeTab().selection; }

private:
  // The services this scene started with.
  IllumoContext* ic{ nullptr };
  // Before the documents, which borrow its schema.
  EditorBehaviours m_behaviours;
  std::vector<std::unique_ptr<EditorTab>> m_tabs;
  std::size_t m_activeTab = 0;
  uint64_t m_nextTabSerial = 1;
  std::unique_ptr<EditorToolbar> m_toolbar;
  std::unique_ptr<EditorSceneGraphView> m_sceneGraphView;
  std::unique_ptr<EditorAssetBrowser> m_assetBrowser;
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
  // The window the grid was built for (it follows the view).
  float m_gridCenterX = 0.0f;
  float m_gridCenterY = 0.0f;
  float m_gridHalf = 0.0f;
  float m_gridSpacing = 0.0f;
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
  // A dialog, read or write is in flight (always briefly: guest services
  // complete on a later update). Editing input is held until it finishes,
  // and the active tab cannot change, so the operation's completion still
  // finds it active.
  bool m_busy = false;
  bool m_closeAfterBusy = false;
  // A right press in the viewport, to tell a click (the context menu) from
  // an orbit or pan drag.
  bool m_rightDown = false;
  bool m_rightInWorld = false;
  float m_rightStartX = 0.0f;
  float m_rightStartY = 0.0f;
  float m_rightTravel = 0.0f;
  // The edit-plane point the context menu was opened at.
  bool m_contextPointValid = false;
  float m_contextWorldX = 0.0f;
  float m_contextWorldY = 0.0f;
  // Consecutive nudges share one history command.
  uint64_t m_nudgeSerial = 0;
  // The game Play launched (empty when none runs), and a launch in flight.
  std::string m_playApplication;
  bool m_playStarting = false;
  // Expires on Exit so late platform completions never touch a stopped
  // module.
  std::shared_ptr<bool> m_lifetime;

  // Autosave and recovery (EditorSceneAutosave.cpp). Every dirty tab is
  // written to its own storage slot on a cadence and the slot is emptied
  // once the document is clean; at start every slot is read and the live
  // ones are offered back, each into its own tab, through the recovery
  // dialog.
  std::unique_ptr<EditorRecoveryDialog> m_recovery;
  struct RecoveryEntry
  {
    int slot = 0;
    SceneDocument document;
    EditorAutosaveRecord record;
  };
  // Offered newest first; what is left when the dialog closes stays on
  // disk for the next start.
  std::vector<RecoveryEntry> m_recoverable;
  bool m_autosaveScanning = false;
  bool m_autosaveScanRead = false;
  int m_autosaveScanSlot = 0;
  // Slots holding content: a recoverable document, or a tab's.
  std::array<bool, EditorAutosaveRecord::kSlots> m_slotTaken{};
  double m_autosaveClock = 0.0;
  bool m_autosaveWarned = false;
  // A clean close waits for the slots' free writes to land.
  bool m_closeAfterAutosave = false;
  // scene_status lines carry a serial so the console never merges them.
  unsigned m_statusSerial = 0;
  // The OS window title last applied ("<name>* - IllEd").
  std::string m_appliedTitle;

  // Tabs (EditorSceneTabs.cpp). Documents to open one after another once
  // the editor is not busy (the last session's scenes, recovered work); the
  // tab to activate when the queue drains.
  struct PendingOpen
  {
    IllEdLocation location;
    std::shared_ptr<RecoveryEntry> recovery;
  };
  std::vector<PendingOpen> m_openQueue;
  // The tab (by position) to activate once the queue drains; npos for the
  // last one opened.
  std::size_t m_openQueueActivateIndex = static_cast<std::size_t>(-1);
  // Save All in progress: the tabs still to save, and what to do after.
  std::vector<uint64_t> m_saveAllQueue;
  std::function<void(bool saved)> m_saveAllDone;
  // Tabs whose changes were discarded in the current exit negotiation.
  std::vector<uint64_t> m_exitDiscarded;
  // Projects the host keeps (File > Open Project, the recent list), and the
  // one an exit negotiation switches to (0 for a plain exit): the editor
  // restarts on it once every dirty tab was settled.
  std::vector<IllEdPlatform::ProjectEntry> m_recentProjects;
  bool m_projectsListed = false;
  std::uint32_t m_switchProjectId = 0;

  // EditorSceneTabs.cpp
  // Appends an empty document bound to this scene's services (not active).
  EditorTab& openTab();
  // Makes a tab the one drawn and edited: camera, hierarchy state and the
  // render world move with it. Refused while a file operation is busy.
  bool activateTab(std::size_t index);
  // Releases the tab's assets and autosave slot; the last tab is replaced
  // by an empty one.
  void closeTab(std::size_t index);
  EditorTab* tabBySerial(uint64_t serial);
  std::size_t tabIndex(uint64_t serial) const;
  // Clean, unnamed and empty: reused rather than opened beside.
  static bool pristine(const EditorTab& tab);
  // The active tab when it is pristine, else a new tab made active.
  EditorTab& tabForOpening();
  // Ctrl+W: closes the active tab after the unsaved-changes confirm.
  void requestCloseTab();
  void cycleTab(int direction);
  // The index of the first dirty tab not yet discarded in this exit
  // negotiation, or npos.
  std::size_t firstDirtyTab() const;
  void syncTabStrip();
  void queueOpen(const IllEdLocation& location);
  // Opens the next queued document once nothing is in flight.
  void drainOpenQueue();
  // Saves every dirty tab in turn (activating each); done receives whether
  // all were saved.
  void saveAll(std::function<void(bool saved)> done);
  void continueSaveAll(bool saved);
  // Binds or unbinds a tab's document from the renderer and render world.
  void bindTab(EditorTab& tab, bool active);
  // Projects (EditorSceneTabs.cpp): the host's recent list into the File
  // menu; the dialogs; the switch, which settles every dirty tab like an
  // exit and then restarts the editor on the chosen project.
  void refreshProjects();
  void openProject(bool create);
  void switchProject(std::uint32_t id);
  void finishProjectSwitch();

  // EditorSceneAutosave.cpp
  // Issues the next slot reads (all of them at once on the synchronous
  // native oracle); offers recovery once every slot was read.
  void updateRecoveryScan();
  void finishRecoveryScan();
  void offerRecovery();
  // Recovers every offered document into its own tab, discards them all,
  // or keeps them for the next start.
  void recoverAll();
  void discardAll();
  void deferRecovery();
  // Loads a recovered document into the active tab, which adopts its slot.
  void recoverInto(const std::shared_ptr<RecoveryEntry>& entry);
  void updateAutosave(double dt);
  // Writes a dirty tab to its slot now (nothing for a clean one).
  void autosaveNow(EditorTab& tab);
  // Empties a tab's slot, then `done`; immediate when it is already free.
  void freeAutosaveSlot(EditorTab& tab, std::function<void()> done);
  // Empties every live slot, then `done`.
  void freeAllAutosaveSlots(std::function<void()> done);
  int takeFreeSlot();
  // Remembers the open tabs' tree locations and the active one for the
  // next start (sessionTabs, sessionActive).
  void recordSession();
  // Queues the last session's tree documents; false when there are none or
  // restoreSession is off.
  bool restoreSession();

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
  // A project scene's previous version is kept as scenes/.backup/<name>
  // (one generation) before it is overwritten.
  void writeDocument(const IllEdLocation& location,
                     std::function<void(bool saved)> done);
  void writeDocumentNow(const IllEdLocation& location,
                        std::function<void(bool saved)> done);
  // Revert to Saved: reloads the saved location after a confirm.
  void revertDocument();
  // "<name>* - IllEd" on the OS window, through the panel surfaces when the
  // host granted them, else the window itself.
  void applyWindowTitle();
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
  // Hierarchy commands: group and ungroup the selection, select parents or
  // children, lock or unlock against viewport picking, isolate the view.
  void groupSelection();
  void ungroupSelection();
  void selectRelatives(bool parents);
  void toggleLockSelection();
  void toggleIsolate();
  // Lowers each top-level selected subtree onto the surface below it (or the
  // ground plane), as one command.
  void dropSelectionToFloor();
  // Moves the selection one move-snap step along a world direction; repeated
  // nudges merge into one command (Alt+arrows, Alt+PageUp/PageDown).
  void nudgeSelection(const Vector3& direction);
  // Viewport interaction (EditorSceneViewport.cpp): a right click that did
  // not drag opens the context menu for the node under it; its Create items
  // place at the clicked point.
  void updateContextClick();
  void openViewportMenu(float pixelX, float pixelY);
  bool createAtContextPoint(EditorCommand command);
  // Alt held as a move starts: duplicate the selection and move the copies.
  void duplicateForDrag();
  // Takes Alt+arrow and Alt+PageUp/PageDown presses as nudges along the
  // world axes nearest the view (EditorSceneViewport.cpp).
  void handleNudgeKeys();
  // Play (Ctrl+P): launches the scene's game on a copy of the document, or
  // stops it.
  void togglePlay();
  // Notices a game window that closed since Play, and updates the menu.
  void updatePlay();
  // The game Play launches: the scene's choice, else the game that describes
  // one of its behaviours, else the only installed game; empty when none.
  std::string playApplication() const;
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
  // Rebuilds the grid when the view has moved or zoomed out of it.
  void followGrid();
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
