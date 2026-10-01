#pragma once

#include "MeshViewerUi.h"
#include <Illumo/Gui/GuiPanelPointer.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <glm/vec3.hpp>
#include <string>
#include <vector>

class InputManager;
class IRenderWindow;
class Renderer;

// The Info dock panel's content: what is open (mesh geometry or scene
// structure), drawn into the rectangle and surface it is given.
class MeshViewerInfoPanel : public DrawableBase
{
public:
  MeshViewerInfoPanel(IRenderWindow* window, Renderer* renderer);
  ~MeshViewerInfoPanel() override = default;
  MeshViewerInfoPanel(const MeshViewerInfoPanel&) = delete;
  MeshViewerInfoPanel& operator=(const MeshViewerInfoPanel&) = delete;

  void setPlacement(const GuiPanelPlacement& placement);
  const GuiPanelPlacement& placement() const { return m_placement; }
  void setMetadata(const MeshMetadata& metadata) { m_metadata = metadata; }
  void update();
  GameVisual& getVisual() { return m_visual; }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  GameVisual m_visual;
  GuiPanelPlacement m_placement;
  MeshMetadata m_metadata;
};

// What the Display panel shows: the viewer's display toggles and the
// lighting, shadow and motion blur settings (the env vars it edits).
struct MeshViewerDisplaySettings
{
  bool grid = true;
  bool axes = true;
  bool sky = true;
  bool wireframe = false;
  bool lighting = true;
  glm::vec3 lightDirection = glm::vec3(0.5f, 1.0f, 0.3f);
  // Ambient brightness: the green channel of the default blue-grey tint.
  float ambient = 0.22f;
  bool shadows = true;
  bool softShadows = true;
  float shadowRadius = 2.5f;
  float shadowBias = 0.001f;
  bool motionBlur = true;
  float motionBlurAmount = 0.5f;
};

// One change the Display panel asks for: a display toggle the module owns,
// or a setting (env var key) and its new value.
struct MeshViewerDisplayEdit
{
  MeshViewerAction action = MeshViewerAction::None;
  std::string key;
  std::string value;
};

// The Display dock panel's content: toggles and sliders drawn into the
// rectangle and surface it is given. It edits nothing itself; the module
// applies its edits (settings take effect on the next frame's env read).
class MeshViewerDisplayPanel : public DrawableBase
{
public:
  MeshViewerDisplayPanel(IRenderWindow* window, Renderer* renderer);
  ~MeshViewerDisplayPanel() override = default;
  MeshViewerDisplayPanel(const MeshViewerDisplayPanel&) = delete;
  MeshViewerDisplayPanel& operator=(const MeshViewerDisplayPanel&) = delete;

  void setPlacement(const GuiPanelPlacement& placement);
  const GuiPanelPlacement& placement() const { return m_placement; }
  void setSettings(const MeshViewerDisplaySettings& settings);
  const MeshViewerDisplaySettings& settings() const { return m_settings; }

  std::vector<MeshViewerDisplayEdit> update(InputManager* input, float dt);
  bool consumedPress() const { return m_consumedPress; }
  // A slider drag in progress: the camera must not orbit.
  bool dragging() const { return m_activeSlider >= 0; }
  bool containsScreenPoint(float x, float y) const;
  // The point of a labelled control; for a slider, at a fraction of its
  // track. False when no control has that label.
  bool controlPointForTesting(const std::string& label,
                              float fraction,
                              float* x,
                              float* y) const;
  GameVisual& getVisual() { return m_visual; }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  enum class ControlKind
  {
    Toggle,
    Slider
  };
  struct Control
  {
    ControlKind kind = ControlKind::Toggle;
    std::string label;
    MeshViewerAction action = MeshViewerAction::None;
    std::string key;
    bool on = false;
    float value = 0.0f;
    float minimum = 0.0f;
    float maximum = 1.0f;
    int digits = 2;
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
  GuiPanelPlacement m_placement;
  GuiPanelPointer m_pointer;
  MeshViewerDisplaySettings m_settings;
  std::vector<Control> m_controls;
  std::vector<Section> m_sections;
  bool m_consumedPress = false;
  int m_hover = -1;
  int m_activeSlider = -1;
  float m_scroll = 0.0f;
  float m_contentHeight = 0.0f;

  void layout();
  float addSection(const std::string& label, float y);
  float addToggle(const std::string& label,
                  MeshViewerAction action,
                  const std::string& key,
                  bool on,
                  float y);
  float addSlider(const std::string& label,
                  const std::string& key,
                  float value,
                  float minimum,
                  float maximum,
                  int digits,
                  float y);
  int controlAt(float x, float y) const;
  // The edits a slider at a new value makes (ambient writes three keys).
  void sliderEdits(const Control& control,
                   float value,
                   std::vector<MeshViewerDisplayEdit>& edits) const;
  static std::string format(float value, int digits);
  void rebuildVisual();
};
