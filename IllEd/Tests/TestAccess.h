#pragma once

#include "EditorModule.h"

class EditorModuleTestAccess
{
public:
  static EditorDocument& document(EditorModule& module)
  {
    return module.m_document;
  }

  static SceneGraph& graph(EditorModule& module) { return module.m_graph; }

  static const std::string& selectedId(const EditorModule& module)
  {
    return module.m_selectedId;
  }

  static void setSelectedId(EditorModule& module, const std::string& id)
  {
    module.m_selectedId = id;
  }

  static bool rebuildGraph(EditorModule& module)
  {
    return module.rebuildGraph();
  }

  static void createNode(EditorModule& module, SceneNodeKind kind)
  {
    module.createNode(kind);
  }

  static void deleteSelection(EditorModule& module)
  {
    module.deleteSelection();
  }

  static void handleCommand(EditorModule& module, EditorCommand command)
  {
    module.handleCommand(command);
  }

  static EditorToolbar* toolbar(EditorModule& module)
  {
    return module.m_toolbar.get();
  }

  static EditorSidebar* sidebar(EditorModule& module)
  {
    return module.m_sidebar.get();
  }

  static EditorSceneGraphView* sceneGraphView(EditorModule& module)
  {
    return module.m_sceneGraphView.get();
  }

  static EditorCommand activeTool(const EditorModule& module)
  {
    return module.m_activeTool;
  }

  static void applyActiveToolAt(EditorModule& module,
                                float worldX,
                                float worldY)
  {
    module.applyActiveToolAt(worldX, worldY);
  }

  static bool screenToWorld(const EditorModule& module,
                            float screenX,
                            float screenY,
                            float* worldX,
                            float* worldY)
  {
    return module.screenToWorld(screenX, screenY, worldX, worldY);
  }
};
