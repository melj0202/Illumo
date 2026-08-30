#pragma once

#include "MeshViewerCamera.h"
#include "MeshViewerUi.h"
#include <Illumo/Engine/IModule.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Rendering/MeshData.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <memory>
#include <string>

struct IllumoContext;

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

  bool loadMesh(const std::string& path);
  bool loadMeshFromMemory(const std::string& content,
                          const std::string& name = "model.obj");

  const MeshData& meshData() const { return m_meshData; }
  const std::string& meshPath() const { return m_meshPath; }
  const MeshViewerCamera& cameraController() const { return m_camera; }
  MeshViewerCamera& cameraController() { return m_camera; }

  bool showGrid() const { return m_showGrid; }
  void setShowGrid(bool show);

  bool showWireframe() const { return m_showWireframe; }
  void setShowWireframe(bool show);

  bool showAxes() const { return m_showAxes; }
  void setShowAxes(bool show);

  void resetCamera();
  bool openMeshDialog();

  MeshViewerUi* ui() { return m_ui.get(); }
  MeshVisual* meshVisual() { return m_meshVisual.get(); }
  MeshVisual* gridVisual() { return m_gridVisual.get(); }
  MeshVisual* wireframeVisual() { return m_wireframeVisual.get(); }

private:
  void rebuildGrid();
  void rebuildWireframe();
  void rebuildMeshVisual();
  void updateCameraInput(double dt);
  void handleAction(MeshViewerAction action);
  void syncUiMetadata();
  SaveLoadDialogSpec dialogSpec() const;

  IllumoContext* ic{ nullptr };
  std::string m_initialMeshPath;
  std::string m_meshPath;
  MeshData m_meshData;

  MeshViewerCamera m_camera;
  std::unique_ptr<MeshVisual> m_meshVisual;
  std::unique_ptr<MeshVisual> m_gridVisual;
  std::unique_ptr<MeshVisual> m_wireframeVisual;
  std::unique_ptr<MeshViewerUi> m_ui;

  bool m_showGrid;
  bool m_showWireframe;
  bool m_showAxes;

  bool m_isOrbiting;
  bool m_isPanning;
  bool m_mouseWasDown;
  double m_lastMouseX;
  double m_lastMouseY;
};
