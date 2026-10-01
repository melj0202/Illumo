#pragma once

#include "EditorScene.h"

class EditorSceneTestAccess
{
public:
  static void setCameraTargetHeight(EditorScene& module, float height)
  {
    module.m_cameraTargetY = height;
  }
  static bool confirmationOpen(const EditorScene& module)
  {
    return module.m_confirm && module.m_confirm->isOpen();
  }
  static EditorDocument& document(EditorScene& module)
  {
    return module.m_document;
  }

  static SceneGraph& graph(EditorScene& module)
  {
    return module.m_document.graph();
  }

  static EditorSelection& selection(EditorScene& module)
  {
    return module.m_selection;
  }

  static const std::string& selectedId(const EditorScene& module)
  {
    return module.m_selection.primary();
  }

  static void setSelectedId(EditorScene& module, const std::string& id)
  {
    module.m_selection.set(id);
  }

  static void refreshView(EditorScene& module) { module.refreshView(); }

  // Creates a node through the armed-tool path at the world origin.
  static std::string createNode(EditorScene& module, EditorCommand tool)
  {
    module.m_activeTool = tool;
    module.applyActiveToolAt(0.0f, 0.0f);
    return module.m_selection.primary();
  }

  static void deleteSelection(EditorScene& module)
  {
    module.deleteSelection();
  }

  static void handleCommand(EditorScene& module, EditorCommand command)
  {
    module.handleCommand(command);
  }

  static EditorToolbar* toolbar(EditorScene& module)
  {
    return module.m_toolbar.get();
  }

  static EditorToolsPanel* tools(EditorScene& module)
  {
    return module.m_tools.get();
  }

  static EditorInspector* inspector(EditorScene& module)
  {
    return module.m_inspector.get();
  }

  static GuiPanelDock& dock(EditorScene& module) { return module.m_dock; }

  static EditorSceneGraphView* sceneGraphView(EditorScene& module)
  {
    return module.m_sceneGraphView.get();
  }

  static EditorCommand activeTool(const EditorScene& module)
  {
    return module.m_activeTool;
  }

  static void applyActiveToolAt(EditorScene& module,
                                float worldX,
                                float worldY)
  {
    module.applyActiveToolAt(worldX, worldY);
  }

  static bool screenToWorld(const EditorScene& module,
                            float screenX,
                            float screenY,
                            float* worldX,
                            float* worldY)
  {
    return module.screenToWorld(screenX, screenY, worldX, worldY);
  }

  static bool screenToWorldRay(const EditorScene& module,
                               float screenX,
                               float screenY,
                               glm::vec3* rayOrigin,
                               glm::vec3* rayDir)
  {
    return module.screenToWorldRay(screenX, screenY, rayOrigin, rayDir);
  }

  static float gizmoScale(const EditorScene& module, const glm::vec3& worldPos)
  {
    return module.gizmoScale(worldPos);
  }

  static GizmoPart hitTestGizmo(const EditorScene& module,
                                float screenX,
                                float screenY,
                                const glm::vec3& gizmoOrigin,
                                float gizmoScale)
  {
    return module.hitTestGizmo(screenX, screenY, gizmoOrigin, gizmoScale);
  }

  static GizmoPart activeGizmoPart(const EditorScene& module)
  {
    return module.m_activeGizmoPart;
  }

  static GizmoPart hoveredGizmoPart(const EditorScene& module)
  {
    return module.m_hoveredGizmoPart;
  }

  static bool isDragging(const EditorScene& module)
  {
    return module.m_dragging;
  }

  static bool worldToScreen(const EditorScene& module,
                            const Vector3& world,
                            float* screenX,
                            float* screenY)
  {
    return module.worldToScreen(world, screenX, screenY);
  }

  static void boxSelect(EditorScene& module,
                        float x0,
                        float y0,
                        float x1,
                        float y1,
                        bool additive)
  {
    module.boxSelect(x0, y0, x1, y1, additive);
  }

  static bool boxSelecting(const EditorScene& module)
  {
    return module.m_boxSelecting;
  }

  static bool pasteText(EditorScene& module, const std::string& text)
  {
    return module.pasteText(text);
  }

  static void placeDroppedAsset(EditorScene& module,
                                const std::string& path,
                                float screenX,
                                float screenY)
  {
    module.placeDroppedAsset(path, screenX, screenY);
  }

  static void openLocation(EditorScene& module, const IllEdLocation& location)
  {
    module.loadDocument(location, false);
  }

  static EditorAssetBrowser* assetBrowser(EditorScene& module)
  {
    return module.m_assetBrowser.get();
  }
};
