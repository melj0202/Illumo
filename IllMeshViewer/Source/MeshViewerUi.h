#pragma once

#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiTypes.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <string>

class InputManager;
class IRenderWindow;
class Renderer;

enum class MeshViewerAction
{
  None,
  OpenMesh,
  ResetView,
  ToggleGrid,
  ToggleWireframe,
  ToggleAxes,
};

struct MeshMetadata
{
  std::string filename;
  size_t vertexCount = 0;
  size_t triangleCount = 0;
  size_t submeshCount = 0;
  size_t materialCount = 0;
  glm::vec3 dimensions = glm::vec3(0.0f);
  bool hasMesh = false;
};

class MeshViewerUi : public DrawableBase
{
public:
  static constexpr float kHeaderHeight = 36.0f;
  static constexpr float kStatusHeight = 24.0f;
  static constexpr float kDefaultFontSize = 13.0f;

  MeshViewerUi(IRenderWindow* window, Renderer* renderer);
  ~MeshViewerUi() override = default;

  MeshViewerUi(const MeshViewerUi&) = delete;
  MeshViewerUi& operator=(const MeshViewerUi&) = delete;

  void setFontSize(float sizePt) { m_fontSize = sizePt; }
  float fontSize() const { return m_fontSize; }

  void setMeshMetadata(const MeshMetadata& metadata);
  const MeshMetadata& meshMetadata() const { return m_metadata; }

  void setDisplayOptions(bool showGrid, bool showWireframe, bool showAxes);
  void setCameraInfo(float yawDegrees, float pitchDegrees, float distance);

  void showToast(const std::string& message,
                 ColorRgba color = ColorRgba{ 60, 220, 120, 255 });

  MeshViewerAction update(InputManager* inputManager, float dt = 0.016f);
  bool containsScreenPoint(float x, float y) const;
  bool consumedPress() const { return m_consumedPress; }

  GameVisual& getVisual() { return m_visual; }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

  // Testing hooks
  void clickAtForTesting(float x, float y);
  size_t buttonCountForTesting() const { return m_buttonDefs.size(); }

private:
  void rebuildVisual(float virtualWidth, float virtualHeight);

  IRenderWindow* m_window;
  Renderer* m_renderer;
  GameVisual m_visual;

  float m_fontSize;
  bool m_mouseWasDown;
  bool m_consumedPress;

  MeshMetadata m_metadata;
  bool m_showGrid;
  bool m_showWireframe;
  bool m_showAxes;

  float m_yawDeg;
  float m_pitchDeg;
  float m_distance;

  std::string m_toastMessage;
  ColorRgba m_toastColor;
  float m_toastTimer;

  struct UiButton
  {
    std::string label;
    MeshViewerAction action;
    float x;
    float y;
    float width;
    float height;
  };
  std::vector<UiButton> m_buttonDefs;
  int m_hoveredButton;
};
