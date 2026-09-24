#pragma once

#include "MeshViewerCamera.h"
#include "MeshViewerPanels.h"
#include "MeshViewerPlatform.h"
#include "MeshViewerUi.h"
#include <Illumo/Content/SceneInstance.h>
#include <Illumo/Engine/IModule.h>
#include <Illumo/Gui/GuiPanelDock.h>
#include <Illumo/Rendering/MeshData.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/Primitives/SkyboxVisual.h>
#include <array>
#include <memory>
#include <string>
#include <vector>

struct IllumoContext;

// The mesh viewer: an orbit camera over one mesh or scene, a plain menu and
// status bar (MeshViewerUi), and a GuiPanelDock (D-UI7) whose right column
// holds the Info and Display panels, each of which can pop out into its own
// window.
class MeshViewerModule : public IModule
{
public:
  explicit MeshViewerModule(std::string initialMeshPath = "");
  ~MeshViewerModule() override;

  MeshViewerModule(const MeshViewerModule&) = delete;
  MeshViewerModule& operator=(const MeshViewerModule&) = delete;
  MeshViewerModule(MeshViewerModule&&) = delete;
  MeshViewerModule& operator=(MeshViewerModule&&) = delete;

  bool Start(IllumoContext* context) override;
  void Update(double dt) override;
  void DispatchDrawables(Scene* scene) override;
  void Exit() override;

#if !defined(ILLUMO_SERIAL_GUEST)
  // Native paths keep tinyobj's material search beside the OBJ.
  bool loadMesh(const std::string& path);
#endif
  bool loadMeshFromMemory(const std::string& content,
                          const std::string& name = "model.obj");
  // Reads a chosen or launch mesh through MeshViewerPlatform, then loads it.
  void loadMeshLocation(const MeshViewerLocation& location);
  // Opens a location as a scene when it names an .ilsc file, else as a mesh.
  void openLocation(const MeshViewerLocation& location);
  // Reads an .ilsc scene, fetches the assets it references, then shows it.
  // Relative references resolve against the scene's package root: the mount
  // holding a "vfs:" location, or /local for a dialog pick (which then
  // only reaches absolute references such as /engine/...).
  void loadSceneLocation(const MeshViewerLocation& location);
  // Instantiates scene text whose assets are already readable. missing is
  // reported in the info card; the scene still shows with placeholders.
  bool loadSceneFromText(const std::string& text,
                         const std::string& name,
                         const std::string& packageRoot,
                         const std::vector<std::string>& missing = {},
                         std::string* error = nullptr);
  // True when a location names an .ilsc scene.
  static bool isSceneLocation(const MeshViewerLocation& location);

  const MeshData& meshData() const { return m_meshData; }
  // The open scene, or nullptr while a mesh (or nothing) is shown.
  SceneInstance* sceneInstance() { return m_scene.get(); }
  const std::string& meshPath() const { return m_meshPath; }
  const MeshViewerCamera& cameraController() const { return m_camera; }
  MeshViewerCamera& cameraController() { return m_camera; }

  bool showGrid() const { return m_showGrid; }
  void setShowGrid(bool show);

  bool showWireframe() const { return m_showWireframe; }
  void setShowWireframe(bool show);

  bool showAxes() const { return m_showAxes; }
  void setShowAxes(bool show);

  bool showSkybox() const { return m_showSkybox; }
  void setShowSkybox(bool show);

  void resetCamera();
  // Requests the open dialog; the mesh loads when the choice completes.
  bool openMeshDialog();

  MeshViewerUi* ui() { return m_ui.get(); }
  MeshViewerInfoPanel* infoPanel() { return m_info.get(); }
  MeshViewerDisplayPanel* displayPanel() { return m_display.get(); }
  GuiPanelDock& dock() { return m_dock; }
  MeshVisual* meshVisual() { return m_meshVisual.get(); }
  MeshVisual* gridVisual() { return m_gridVisual.get(); }
  MeshVisual* wireframeVisual() { return m_wireframeVisual.get(); }
  SkyboxVisual* skyboxVisual() { return m_skyboxVisual.get(); }

private:
  void rebuildGrid();
  void rebuildWireframe();
  void rebuildMeshVisual();
  void applyLightingFromEnv();
  void applyShadowsFromEnv();
  void applyMotionBlurFromEnv();
  // Mouse orbit, pan and zoom only when the pointer belongs to the
  // viewport (dragFree: the drag began there; wheelFree: it is over it).
  void updateCameraInput(double dt, bool dragFree, bool wheelFree);
  void handleAction(MeshViewerAction action);
  void syncUiMetadata();
  SaveLoadDialogSpec dialogSpec() const;
  void clearMesh();
  void clearScene();
  void frameScene();
  void reportLoadFailure(const std::string& error);
  void registerCommands();
  void unregisterCommands();
  // Dock: layout, chrome input and panel placement (as IllEd does).
  void setupDock();
  void layoutDock();
  void updateDock(bool modal);
  GuiPanelPlacement placementFor(const std::string& id) const;
  std::vector<MeshViewerPanelMenuEntry> panelMenu() const;
  bool handlePanelAction(MeshViewerAction action);
  void syncLayout();
  MeshViewerDisplaySettings displaySettings() const;
  void applyDisplayEdits(const std::vector<MeshViewerDisplayEdit>& edits);
  // Stores a display toggle in its setting so it survives a restart.
  void storeToggle(const char* key, bool value);

  IllumoContext* ic{ nullptr };
  std::string m_initialMeshPath;
  std::string m_meshPath;
  MeshData m_meshData;
  MeshHandle m_meshAsset{};
  std::unique_ptr<SceneInstance> m_scene;
  std::size_t m_sceneMissing = 0;
  // World bounds of the open scene, for framing and the wireframe outline.
  AxisAlignedBounds3 m_sceneBounds;

  MeshViewerCamera m_camera;
  std::unique_ptr<MeshVisual> m_meshVisual;
  std::unique_ptr<MeshVisual> m_gridVisual;
  std::unique_ptr<MeshVisual> m_wireframeVisual;
  std::unique_ptr<SkyboxVisual> m_skyboxVisual;
  std::unique_ptr<MeshViewerUi> m_ui;
  std::unique_ptr<MeshViewerInfoPanel> m_info;
  std::unique_ptr<MeshViewerDisplayPanel> m_display;
  GuiPanelDock m_dock;
  std::unique_ptr<GameVisual> m_dockVisual;
  std::array<std::unique_ptr<GameVisual>, 2> m_detachedChrome;
  bool m_dockLeftWasDown = false;
  bool m_dockPressHeld = false;
  bool m_mainPanelsBlocked = false;
  bool m_layoutLoaded = false;
  std::string m_savedLayout;
  // A camera drag that began over the chrome or a panel never orbits.
  bool m_cameraButtonsWereDown = false;
  bool m_dragOnUi = false;

  bool m_showGrid;
  bool m_showWireframe;
  bool m_showAxes;
  bool m_showSkybox;

  bool m_isOrbiting;
  bool m_isPanning;
  bool m_mouseWasDown;
  double m_lastMouseX;
  double m_lastMouseY;
  // Expires on Exit so late platform completions never touch a stopped
  // module.
  std::shared_ptr<bool> m_lifetime;
};
