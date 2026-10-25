#include "EditorRecoveryDialog.h"
#include "EditorToolbar.h"

EditorRecoveryDialog::EditorRecoveryDialog(IRenderWindow* window,
                                           Renderer* renderer)
  : m_dialog(window, renderer)
{
  m_dialog.setTitle("Unsaved Work Found");
  m_dialog.setFontSize(EditorToolbar::kDefaultFontSize);

  GuiButtonDef recover;
  recover.label = "Recover";
  recover.actionId = static_cast<int>(EditorRecoveryAction::Recover);
  recover.isDefault = true;
  recover.shortcutKey = KeyCode::Enter;

  GuiButtonDef discard;
  discard.label = "Discard";
  discard.actionId = static_cast<int>(EditorRecoveryAction::Discard);
  discard.isDestructive = true;
  discard.shortcutKey = KeyCode::D;

  GuiButtonDef later;
  later.label = "Later";
  later.actionId = static_cast<int>(EditorRecoveryAction::Later);
  later.isCancel = true;
  later.shortcutKey = KeyCode::Escape;

  m_dialog.addButton(recover);
  m_dialog.addButton(discard);
  m_dialog.addButton(later);

  setVisible(false);
}

void
EditorRecoveryDialog::setFontSize(float sizePt)
{
  m_dialog.setFontSize(sizePt);
}

void
EditorRecoveryDialog::open(const std::string& message)
{
  m_dialog.open(message);
  setVisible(true);
}

void
EditorRecoveryDialog::close()
{
  m_dialog.close();
  setVisible(false);
}

EditorRecoveryAction
EditorRecoveryDialog::update(InputManager* inputManager, float dt)
{
  const int action = m_dialog.update(inputManager, dt);
  switch (action) {
    case static_cast<int>(EditorRecoveryAction::Recover):
      return EditorRecoveryAction::Recover;
    case static_cast<int>(EditorRecoveryAction::Discard):
      return EditorRecoveryAction::Discard;
    case static_cast<int>(EditorRecoveryAction::Later):
      return EditorRecoveryAction::Later;
    default:
      return EditorRecoveryAction::None;
  }
}

bool
EditorRecoveryDialog::AppendCommands(Renderer* renderer)
{
  return m_dialog.AppendCommands(renderer);
}
