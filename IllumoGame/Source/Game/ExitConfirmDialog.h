#pragma once

#include <Illumo/Gui/GuiDialog.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>

class InputManager;
class IRenderWindow;
class Renderer;

enum class ExitConfirmAction
{
  None,
  Confirm,
  Cancel,
  MainMenu
};

// Primitive-composed exit confirmation overlay backed by
// Illumo::Gui::GuiDialog.
class ExitConfirmDialog : public DrawableBase
{
public:
  ExitConfirmDialog(IRenderWindow* window, Renderer* renderer);
  ~ExitConfirmDialog() override = default;

  ExitConfirmDialog(const ExitConfirmDialog&) = delete;
  ExitConfirmDialog& operator=(const ExitConfirmDialog&) = delete;

  void open();
  void close();
  bool isOpen() const { return m_dialog.isOpen(); }
  void tick(float deltaSeconds);
  ExitConfirmAction update(InputManager* inputManager);
  GameVisual& getVisual() { return m_dialog.getVisual(); }

  int getSelectedButtonForTesting() const { return m_dialog.selectedButton(); }
  float getAnimationProgressForTesting() const
  {
    return m_dialog.animationProgress();
  }
  float getSelectionPositionForTesting() const
  {
    return m_dialog.selectionPosition();
  }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  GuiDialog m_dialog;
};
