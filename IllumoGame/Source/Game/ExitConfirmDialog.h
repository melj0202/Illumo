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
  MainMenu,
  ClearCanvas,
  Restart
};

// Primitive-composed confirmation overlay backed by Illumo::Gui::GuiDialog.
// It asks before leaving (open), before clearing the canvas
// (openClearCanvas), or whether to restart now for settings that only a
// restart applies (openRestart); the owner treats each as the same modal.
class ExitConfirmDialog : public DrawableBase
{
public:
  ExitConfirmDialog(IRenderWindow* window, Renderer* renderer);
  ~ExitConfirmDialog() override = default;

  ExitConfirmDialog(const ExitConfirmDialog&) = delete;
  ExitConfirmDialog& operator=(const ExitConfirmDialog&) = delete;

  void open();
  void openClearCanvas();
  // inCanvas warns that the open world closes with the restart.
  void openRestart(bool inCanvas);
  void close();
  bool isOpen() const { return m_dialog.isOpen(); }
  void tick(float deltaSeconds);
  void setReducedMotion(bool enabled) { m_dialog.setReducedMotion(enabled); }
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
  void configureExit();
  void configureClearCanvas();
  void configureRestart(bool inCanvas);

  GuiDialog m_dialog;
  // The dialog recomputes hover every frame; cues fire when it changes.
  int m_lastHovered = -1;
};
