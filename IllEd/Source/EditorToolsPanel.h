#pragma once

#include "EditorCommand.h"
#include "EditorGizmo.h"
#include <Illumo/Gui/GuiPanelPointer.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <string>
#include <utility>
#include <vector>

class InputManager;
class IRenderWindow;
class Renderer;

// What the Tools panel shows as active.
struct EditorToolsState
{
  bool is3D = false;
  EditorCommand activeTool = EditorCommand::SelectTool;
  GizmoMode gizmoMode = GizmoMode::Translate;
  GizmoSpace gizmoSpace = GizmoSpace::World;
  bool snap = false;
};

// The Tools dock panel's content: world mode, transform tool, gizmo space,
// snapping and the Create tools, drawn into the rectangle and surface it is
// given. Every control issues an EditorCommand; the module dispatches it.
class EditorToolsPanel : public DrawableBase
{
public:
  EditorToolsPanel(IRenderWindow* window, Renderer* renderer);
  ~EditorToolsPanel() override = default;

  EditorToolsPanel(const EditorToolsPanel&) = delete;
  EditorToolsPanel& operator=(const EditorToolsPanel&) = delete;

  void setFontSize(float sizePt);
  float fontSize() const { return m_fontSize; }
  void setPlacement(const GuiPanelPlacement& placement);
  const GuiPanelPlacement& placement() const { return m_placement; }
  void setAtlas(TextureHandle atlas);
  TextureHandle atlas() const { return m_atlas; }
  void setState(const EditorToolsState& state) { m_state = state; }
  const EditorToolsState& state() const { return m_state; }

  EditorCommand update(InputManager* inputManager, float dt = 0.016f);
  bool consumedPress() const { return m_consumedPress; }
  bool containsScreenPoint(float x, float y) const;
  EditorCommand clickAtForTesting(float x, float y);
  // The center of the control that issues a command; false when none.
  bool controlCenterForTesting(EditorCommand command, float* x, float* y) const;
  // Content height the controls need at the current font size.
  float contentHeight() const { return m_contentHeight; }
  GameVisual& getVisual() { return m_visual; }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  enum class ControlKind
  {
    Button,
    Toggle,
    Tool
  };
  struct Control
  {
    ControlKind kind = ControlKind::Button;
    std::string label;
    EditorCommand command = EditorCommand::None;
    GuiToolRect rect;
  };
  struct Section
  {
    std::string label;
    GuiToolRect rect;
  };

  IRenderWindow* m_window;
  Renderer* m_renderer;
  GameVisual m_visual;
  TextureHandle m_atlas{};
  EditorToolsState m_state;
  GuiPanelPlacement m_placement;
  GuiPanelPointer m_pointer;
  bool m_consumedPress = false;
  float m_fontSize;
  float m_scroll = 0.0f;
  float m_contentHeight = 0.0f;
  int m_hover = -1;
  std::vector<Control> m_controls;
  std::vector<Section> m_sections;

  float rowHeight() const;
  // Layout helpers: each returns the y below what it added.
  float addSection(const std::string& label, float y);
  float addButtons(
    const std::vector<std::pair<std::string, EditorCommand>>& entries,
    float y);
  void layout();
  bool active(const Control& control) const;
  int controlAt(float x, float y) const;
  void rebuildVisual();
};
