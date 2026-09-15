#include "ExitConfirmDialog.h"

ExitConfirmDialog::ExitConfirmDialog(IRenderWindow* window, Renderer* renderer)
  : m_dialog(window, renderer)
{
  m_dialog.setRoundedStyle(true);
  m_dialog.setPanelDimensions(600.0f, 270.0f);
  m_dialog.setTitle("SIMULATION PAUSED");
  m_dialog.setMessage("Take a breath. Your world can wait.");

  GuiButtonDef resumeBtn;
  resumeBtn.label = "Resume";
  resumeBtn.actionId = static_cast<int>(ExitConfirmAction::Cancel);
  resumeBtn.shortcutKey = KeyCode::N;
  resumeBtn.shortcut = "ESC / N";
  resumeBtn.isCancel = true;
  resumeBtn.isDefault = true;

  GuiButtonDef menuBtn;
  menuBtn.label = "Main Menu";
  menuBtn.actionId = static_cast<int>(ExitConfirmAction::MainMenu);
  menuBtn.shortcutKey = KeyCode::M;
  menuBtn.shortcut = "M";

  GuiButtonDef exitBtn;
  exitBtn.label = "Exit App";
  exitBtn.actionId = static_cast<int>(ExitConfirmAction::Confirm);
  exitBtn.shortcutKey = KeyCode::Y;
  exitBtn.shortcut = "Y";
  exitBtn.isDestructive = true;

  m_dialog.addButton(resumeBtn);
  m_dialog.addButton(menuBtn);
  m_dialog.addButton(exitBtn);

  setVisible(false);
}

void
ExitConfirmDialog::open()
{
  m_dialog.selectButton(0);
  m_dialog.open();
  setVisible(true);
}

void
ExitConfirmDialog::close()
{
  m_dialog.close();
  setVisible(false);
}

void
ExitConfirmDialog::tick(float deltaSeconds)
{
  m_dialog.tick(deltaSeconds);
}

ExitConfirmAction
ExitConfirmDialog::update(InputManager* inputManager)
{
  // The product ticks once with the frame delta, including console-open frames.
  const int action = m_dialog.update(inputManager, 0.0f);
  switch (action) {
    case static_cast<int>(ExitConfirmAction::Cancel):
      return ExitConfirmAction::Cancel;
    case static_cast<int>(ExitConfirmAction::MainMenu):
      return ExitConfirmAction::MainMenu;
    case static_cast<int>(ExitConfirmAction::Confirm):
      return ExitConfirmAction::Confirm;
    default:
      return ExitConfirmAction::None;
  }
}

bool
ExitConfirmDialog::AppendCommands(Renderer* renderer)
{
  return m_dialog.AppendCommands(renderer);
}
