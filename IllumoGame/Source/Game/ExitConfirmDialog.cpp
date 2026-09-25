#include "ExitConfirmDialog.h"
#include "CSimSounds.h"

ExitConfirmDialog::ExitConfirmDialog(IRenderWindow* window, Renderer* renderer)
  : m_dialog(window, renderer)
{
  m_dialog.setRoundedStyle(true);
  m_dialog.setPanelDimensions(600.0f, 270.0f);
  configureExit();
  setVisible(false);
}

void
ExitConfirmDialog::configureExit()
{
  m_dialog.setTitle("SIMULATION PAUSED");
  m_dialog.setMessage("Take a breath. Your world can wait.");
  m_dialog.clearButtons();

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
}

void
ExitConfirmDialog::configureClearCanvas()
{
  m_dialog.setTitle("CLEAR THE CANVAS?");
  m_dialog.setMessage("Every cell goes back to empty. This can't be undone.");
  m_dialog.clearButtons();

  GuiButtonDef keepBtn;
  keepBtn.label = "Keep";
  keepBtn.actionId = static_cast<int>(ExitConfirmAction::Cancel);
  keepBtn.shortcutKey = KeyCode::N;
  keepBtn.shortcut = "ESC / N";
  keepBtn.isCancel = true;
  keepBtn.isDefault = true;

  GuiButtonDef clearBtn;
  clearBtn.label = "Clear";
  clearBtn.actionId = static_cast<int>(ExitConfirmAction::ClearCanvas);
  clearBtn.shortcutKey = KeyCode::Y;
  clearBtn.shortcut = "Y";
  clearBtn.isDestructive = true;

  m_dialog.addButton(keepBtn);
  m_dialog.addButton(clearBtn);
}

void
ExitConfirmDialog::configureRestart(bool inCanvas)
{
  m_dialog.setTitle("RESTART CSIM?");
  m_dialog.setMessage(
    inCanvas ? "Anti-aliasing changes need a restart. Unsaved world changes "
               "will be lost."
             : "Anti-aliasing changes need a restart. Restart now?");
  m_dialog.clearButtons();

  GuiButtonDef laterBtn;
  laterBtn.label = "Later";
  laterBtn.actionId = static_cast<int>(ExitConfirmAction::Cancel);
  laterBtn.shortcutKey = KeyCode::N;
  laterBtn.shortcut = "ESC / N";
  laterBtn.isCancel = true;
  laterBtn.isDefault = true;

  GuiButtonDef restartBtn;
  restartBtn.label = "Restart now";
  restartBtn.actionId = static_cast<int>(ExitConfirmAction::Restart);
  restartBtn.shortcutKey = KeyCode::Y;
  restartBtn.shortcut = "Y";

  m_dialog.addButton(laterBtn);
  m_dialog.addButton(restartBtn);
}

void
ExitConfirmDialog::openRestart(bool inCanvas)
{
  configureRestart(inCanvas);
  m_dialog.selectButton(0);
  m_dialog.open();
  m_lastHovered = -1;
  setVisible(true);
}

void
ExitConfirmDialog::open()
{
  configureExit();
  m_dialog.selectButton(0);
  m_dialog.open();
  m_lastHovered = -1;
  setVisible(true);
}

void
ExitConfirmDialog::openClearCanvas()
{
  configureClearCanvas();
  m_dialog.selectButton(0);
  m_dialog.open();
  m_lastHovered = -1;
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
  const int selectedBefore = m_dialog.selectedButton();
  const int action = m_dialog.update(inputManager, 0.0f);
  const int hovered = m_dialog.hoveredButton();
  if (m_dialog.selectedButton() != selectedBefore ||
      (hovered >= 0 && hovered != m_lastHovered)) {
    CSimSounds::play(CSimSound::MenuHover);
  }
  m_lastHovered = hovered;
  switch (action) {
    case static_cast<int>(ExitConfirmAction::Cancel):
      CSimSounds::play(CSimSound::MenuBack);
      return ExitConfirmAction::Cancel;
    case static_cast<int>(ExitConfirmAction::MainMenu):
      // The canvas voices its own exit as it returns to the menu.
      return ExitConfirmAction::MainMenu;
    case static_cast<int>(ExitConfirmAction::Confirm):
      CSimSounds::play(CSimSound::MenuSelect);
      return ExitConfirmAction::Confirm;
    case static_cast<int>(ExitConfirmAction::ClearCanvas):
      CSimSounds::play(CSimSound::MenuSelect);
      return ExitConfirmAction::ClearCanvas;
    case static_cast<int>(ExitConfirmAction::Restart):
      CSimSounds::play(CSimSound::MenuSelect);
      return ExitConfirmAction::Restart;
    default:
      return ExitConfirmAction::None;
  }
}

bool
ExitConfirmDialog::AppendCommands(Renderer* renderer)
{
  return m_dialog.AppendCommands(renderer);
}
