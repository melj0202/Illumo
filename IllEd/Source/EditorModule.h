#pragma once

#include "EditorAttachment.h"
#include "EditorConfirmDialog.h"
#include "EditorDocument.h"
#include "EditorSceneGraphView.h"
#include "EditorSidebar.h"
#include "EditorToolbar.h"
#include "IllEdPlatform.h"
#include <Illumo/Engine/IModule.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Scene/SceneGraphDrawable.h>
#include <Illumo/Scene/SceneNodeHandle.h>
#include <functional>
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
  bool OnCloseRequested() override;
  EditorSceneDetail sceneDetail() const;

private:
  EditorDocument m_document;
  SceneGraph& m_graph = m_document.graph();
  SceneGraphDrawable m_graphDrawable{ m_graph };
  std::unique_ptr<EditorToolbar> m_toolbar;
  std::unique_ptr<EditorSceneGraphView> m_sceneGraphView;
  std::unique_ptr<EditorSidebar> m_sidebar;
  std::unique_ptr<EditorConfirmDialog> m_confirm;
  TextureHandle m_atlas{};
  std::unique_ptr<MeshVisual> m_grid;
  std::unique_ptr<MeshVisual> m_selectionOverlay;
  std::vector<std::unique_ptr<EditorAttachment>> m_attachments;
  std::vector<SceneNodeHandle> m_attachmentHandles;
  std::vector<SceneChange> m_graphChanges;
  std::vector<SceneNodeHandle> m_changedBindings;
  uint64_t m_graphCursor = 0;
  bool m_gridBuilt = false;
  IlscWorldMode m_gridMode = IlscWorldMode::World2D;
  EditorCommand m_activeTool;
  std::string m_selectedId;
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
  glm::vec3 m_dragGizmoOrigin{ 0.0f };
  glm::vec3 m_dragGizmoHitOffset{ 0.0f };
  float m_cameraTargetY = 0.0f;
  // A dialog, read or write is in flight (always briefly: guest services
  // complete on a later update). Editing input is held until it finishes.
  bool m_busy = false;
  bool m_closeAfterBusy = false;
  // Expires on Exit so late platform completions never touch a stopped
  // module.
  std::shared_ptr<bool> m_lifetime;

  void syncFontSize();
  void applyFontSize(float size);
  bool syncGraph();
  bool syncAttachment(SceneNodeHandle node);
  void applyWorldCamera();
  void handleCommand(EditorCommand command);
  void requestAction(EditorPendingAction action);
  void performPendingAction();
  // `done` receives whether the document was saved (false on cancellation).
  void saveDocument(bool saveAs, std::function<void(bool saved)> done = {});
  void writeDocument(const IllEdLocation& location,
                     std::function<void(bool saved)> done);
  void openDocument();
  // `initial` loads the launch document: failures are logged, not toasted.
  void loadDocument(const IllEdLocation& location, bool initial);
  void finishBusy();
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
