#pragma once

#include "EditorAttachment.h"
#include "EditorConfirmDialog.h"
#include "EditorDocument.h"
#include "EditorSceneGraphView.h"
#include "EditorSidebar.h"
#include "EditorToolbar.h"
#include <Illumo/Engine/IModule.h>
#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Scene/SceneNodeHandle.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum class EditorPendingAction
{
  None,
  NewDocument,
  OpenDocument,
  ExitEditor
};

enum class GizmoPart
{
  None,
  Center,
  AxisX,
  AxisY,
  AxisZ,
  PlaneXY,
  PlaneXZ,
  PlaneYZ
};

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
  EditorSceneDetail sceneDetail() const;

private:
  EditorDocument m_document;
  SceneGraph m_graph;
  std::unique_ptr<EditorToolbar> m_toolbar;
  std::unique_ptr<EditorSceneGraphView> m_sceneGraphView;
  std::unique_ptr<EditorSidebar> m_sidebar;
  std::unique_ptr<EditorConfirmDialog> m_confirm;
  TextureHandle m_atlas{};
  std::unique_ptr<MeshVisual> m_grid;
  std::unique_ptr<MeshVisual> m_selectionOverlay;
  std::vector<std::unique_ptr<EditorAttachment>> m_attachments;
  EditorCommand m_activeTool;
  std::unordered_map<std::string, SceneNodeHandle> m_handles;
  std::string m_selectedId;
  std::string m_initialScenePath;
  bool m_dragging;
  bool m_panning;
  bool m_mouseWasDown;
  double m_lastMouseX;
  double m_lastMouseY;
  float m_animTime;
  EditorPendingAction m_pendingAction;
  std::string m_appliedFontSizeVar;
  GizmoPart m_hoveredGizmoPart = GizmoPart::None;
  GizmoPart m_activeGizmoPart = GizmoPart::None;
  glm::vec3 m_dragGizmoOrigin{ 0.0f };
  glm::vec3 m_dragGizmoHitOffset{ 0.0f };
  float m_cameraTargetY = 0.0f;

  void syncFontSize();
  void applyFontSize(float size);
  bool rebuildGraph();
  void applyWorldCamera();
  void handleCommand(EditorCommand command);
  void requestAction(EditorPendingAction action);
  void performPendingAction();
  bool saveDocument(bool saveAs);
  bool openDocument();
  void newDocument();
  void createNode(SceneNodeKind kind);
  void deleteSelection();
  void unparentSelection();
  void updateCamera(double dt);
  void updateSelection(double dt);
  void rebuildSelectionOverlay();
  void rebuildGrid();
  void updateStatus();
  glm::mat4 currentViewProjection() const;
  bool uiBlocksWorld(float screenX, float screenY) const;
  SaveLoadDialogSpec dialogSpec() const;
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
  bool intersectGizmoConstraint(GizmoPart part,
                                const glm::vec3& gizmoOrigin,
                                const glm::vec3& rayOrigin,
                                const glm::vec3& rayDir,
                                glm::vec3* outIntersection) const;
  void applyActiveToolAt(float worldX, float worldY);
  void nudgeSelectedExtent();
  void cycleSelectedColor();
  SceneNodeKind kindFromTool(EditorCommand command) const;
};
