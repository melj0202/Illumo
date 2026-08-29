#pragma once

#include "EditorAttachment.h"
#include "EditorConfirmDialog.h"
#include "EditorDocument.h"
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
  bool m_graveWasDown;
  double m_lastMouseX;
  double m_lastMouseY;
  EditorPendingAction m_pendingAction;

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
  void updateStatus();
  glm::mat4 currentViewProjection() const;
  bool uiBlocksWorld(float screenX, float screenY) const;
  SaveLoadDialogSpec dialogSpec() const;
  bool screenToWorld(float screenX,
                     float screenY,
                     float* worldX,
                     float* worldY) const;
  void applyActiveToolAt(float worldX, float worldY);
  void nudgeSelectedExtent();
  void cycleSelectedColor();
  SceneNodeKind kindFromTool(EditorCommand command) const;
};
