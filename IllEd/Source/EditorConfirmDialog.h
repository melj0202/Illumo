#pragma once

#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <string>

class InputManager;
class IRenderWindow;
class Renderer;

enum class EditorConfirmAction
{
  None,
  Save,
  Discard,
  Cancel
};

class EditorConfirmDialog : public DrawableBase
{
public:
  EditorConfirmDialog(IRenderWindow* window, Renderer* renderer);
  ~EditorConfirmDialog() override = default;

  EditorConfirmDialog(const EditorConfirmDialog&) = delete;
  EditorConfirmDialog& operator=(const EditorConfirmDialog&) = delete;

  void open(const std::string& message);
  void close();
  bool isOpen() const { return m_open; }
  EditorConfirmAction update(InputManager* inputManager, float dt = 0.016f);
  GameVisual& getVisual() { return m_visual; }
  const std::string& messageForTesting() const { return m_message; }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  IRenderWindow* m_window;
  Renderer* m_renderer;
  GameVisual m_visual;
  bool m_open;
  bool m_mouseWasDown;
  float m_animElapsed;
  std::string m_message;
  float m_panelX;
  float m_panelY;
  float m_panelWidth;
  float m_panelHeight;
  float m_buttonY;
  float m_buttonWidth;
  float m_buttonHeight;
  float m_saveX;
  float m_discardX;
  float m_cancelX;

  void updateLayout();
  void rebuildVisual();
  EditorConfirmAction clickAt(float x, float y);
};
