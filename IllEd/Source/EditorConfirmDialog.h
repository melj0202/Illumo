#pragma once

#include <Illumo/Gui/GuiDialog.h>
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
  bool isOpen() const { return m_dialog.isOpen(); }
  void setFontSize(float sizePt);
  float fontSize() const { return m_dialog.fontSize(); }
  EditorConfirmAction update(InputManager* inputManager, float dt = 0.016f);
  GameVisual& getVisual() { return m_dialog.getVisual(); }
  const std::string& messageForTesting() const { return m_dialog.getMessage(); }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  GuiDialog m_dialog;
};
