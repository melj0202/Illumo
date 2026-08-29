#pragma once

#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <string>
#include <vector>

class InputManager;
class IRenderWindow;
class Renderer;

enum class EditorCommand
{
  None,
  NewDocument,
  OpenDocument,
  SaveDocument,
  SaveDocumentAs,
  ExitEditor,
  DeleteNode,
  UnparentNode,
  CreateEmpty,
  CreateRect,
  CreateEllipse,
  CreateTriangle,
  CreateCube,
  CreatePyramid,
  CreateSphere,
  SelectTool,
  SetMode2D,
  SetMode3D,
  CycleColor,
  NudgeExtent,
  ResetCamera
};

class EditorToolbar : public DrawableBase
{
public:
  static constexpr float kBarHeight = 28.0f;
  static constexpr float kStatusHeight = 22.0f;

  EditorToolbar(IRenderWindow* window, Renderer* renderer);
  ~EditorToolbar() override = default;

  EditorToolbar(const EditorToolbar&) = delete;
  EditorToolbar& operator=(const EditorToolbar&) = delete;

  EditorCommand update(InputManager* inputManager);
  void setAtlas(TextureHandle atlas);
  TextureHandle atlas() const { return m_atlas; }
  void setStatus(const std::string& text);
  void closeMenus();
  bool isMenuOpen() const { return m_openMenu >= 0; }
  bool consumedPress() const { return m_consumedPress; }
  bool containsScreenPoint(float x, float y) const;
  EditorCommand clickAtForTesting(float x, float y);
  int openMenuForTesting() const { return m_openMenu; }
  const std::string& statusForTesting() const { return m_status; }
  GameVisual& getVisual() { return m_visual; }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  struct MenuItem
  {
    std::string label;
    EditorCommand command = EditorCommand::None;
  };

  struct Menu
  {
    std::string title;
    float x = 0.0f;
    float width = 0.0f;
    std::vector<MenuItem> items;
  };

  IRenderWindow* m_window;
  Renderer* m_renderer;
  GameVisual m_visual;
  TextureHandle m_atlas{};
  std::vector<Menu> m_menus;
  int m_openMenu;
  bool m_mouseWasDown;
  bool m_consumedPress;
  std::string m_status;
  float m_barWidth;
  float m_dropdownX;
  float m_dropdownY;
  float m_dropdownWidth;
  float m_dropdownHeight;

  void rebuildMenus();
  void updateLayout();
  void rebuildVisual();
  EditorCommand clickAt(float x, float y);
  static float estimateTextWidth(const std::string& text, float sizePt);
};
