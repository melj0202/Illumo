#pragma once

#include <Illumo/Gui/GuiDialog.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <string>

class InputManager;
class IRenderWindow;
class Renderer;

enum class EditorRecoveryAction
{
  None,
  Recover,
  Discard,
  // Keeps the autosave for the next start.
  Later
};

// "Recover unsaved work?" at start, when an autosave slot holds a document
// (docs/illed-editing-wave4-plan.md, 5.2). Modal, like EditorConfirmDialog.
class EditorRecoveryDialog : public DrawableBase
{
public:
  EditorRecoveryDialog(IRenderWindow* window, Renderer* renderer);
  ~EditorRecoveryDialog() override = default;

  EditorRecoveryDialog(const EditorRecoveryDialog&) = delete;
  EditorRecoveryDialog& operator=(const EditorRecoveryDialog&) = delete;

  void open(const std::string& message);
  void close();
  bool isOpen() const { return m_dialog.isOpen(); }
  void setFontSize(float sizePt);
  EditorRecoveryAction update(InputManager* inputManager, float dt = 0.016f);
  const std::string& messageForTesting() const { return m_dialog.getMessage(); }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  GuiDialog m_dialog;
};
