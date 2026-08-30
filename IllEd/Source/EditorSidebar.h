#pragma once

#include "EditorDocument.h"
#include "EditorToolbar.h"
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <string>
#include <vector>

class InputManager;
class IRenderWindow;
class Renderer;

class EditorSidebar : public DrawableBase
{
public:
  static constexpr float kDefaultWidth = 200.0f;
  static constexpr float kWidth = kDefaultWidth;

  EditorSidebar(IRenderWindow* window, Renderer* renderer);
  ~EditorSidebar() override = default;

  EditorSidebar(const EditorSidebar&) = delete;
  EditorSidebar& operator=(const EditorSidebar&) = delete;

  void setFontSize(float sizePt);
  float fontSize() const { return m_fontSize; }
  void setToolbarDimensions(float barHeight, float statusHeight);
  float panelWidth() const { return m_width; }

  EditorCommand update(InputManager* inputManager, float dt = 0.016f);
  bool consumedPress() const { return m_consumedPress; }
  void setAtlas(TextureHandle atlas);
  TextureHandle atlas() const { return m_atlas; }
  void setDetail(const EditorSceneDetail& detail);
  void setActiveTool(EditorCommand tool);
  EditorCommand activeTool() const { return m_activeTool; }
  bool containsScreenPoint(float x, float y) const;
  EditorCommand clickAtForTesting(float x, float y);
  float sidebarX() const { return m_x; }
  GameVisual& getVisual() { return m_visual; }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  struct ToolRow
  {
    std::string label;
    EditorCommand command = EditorCommand::None;
    float y = 0.0f;
  };

  IRenderWindow* m_window;
  Renderer* m_renderer;
  GameVisual m_visual;
  TextureHandle m_atlas{};
  EditorSceneDetail m_detail;
  EditorCommand m_activeTool;
  bool m_mouseWasDown;
  bool m_consumedPress;
  float m_fontSize;
  float m_width;
  float m_barHeight;
  float m_statusHeight;
  float m_toolRowHeight;
  float m_modeButtonHeight;
  float m_x;
  float m_y;
  float m_height;
  float m_modeY;
  float m_inspectorY;
  float m_animTime;
  float m_modeAnim;
  int m_hoverTool;
  bool m_hoverMode2D;
  bool m_hoverMode3D;
  bool m_hoverNudge;
  bool m_hoverColor;
  float m_mouseX;
  float m_mouseY;
  std::vector<ToolRow> m_tools;

  void updateLayout();
  void rebuildVisual();
  EditorCommand clickAt(float x, float y);
};
