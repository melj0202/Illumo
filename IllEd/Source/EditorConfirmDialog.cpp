#include "EditorConfirmDialog.h"
#include "EditorToolbar.h"

static GuiButtonDef
cancelButton()
{
  GuiButtonDef cancel;
  cancel.label = "Cancel";
  cancel.actionId = static_cast<int>(EditorConfirmAction::Cancel);
  cancel.isCancel = true;
  cancel.shortcutKey = KeyCode::Escape;
  return cancel;
}

EditorConfirmDialog::EditorConfirmDialog(IRenderWindow* window,
                                         Renderer* renderer)
  : m_dialog(window, renderer)
{
  m_dialog.setTitle("Unsaved Changes");
  m_dialog.setFontSize(EditorToolbar::kDefaultFontSize);
  setVisible(false);
}

void
EditorConfirmDialog::setFontSize(float sizePt)
{
  m_dialog.setFontSize(sizePt);
}

void
EditorConfirmDialog::open(const std::string& message, bool offerSaveAll)
{
  GuiButtonDef save;
  save.label = "Save";
  save.actionId = static_cast<int>(EditorConfirmAction::Save);
  save.isDefault = true;
  save.shortcutKey = KeyCode::Enter;

  GuiButtonDef saveAll;
  saveAll.label = "Save All";
  saveAll.actionId = static_cast<int>(EditorConfirmAction::SaveAll);
  saveAll.shortcutKey = KeyCode::A;

  GuiButtonDef discard;
  discard.label = "Don't Save";
  discard.actionId = static_cast<int>(EditorConfirmAction::Discard);
  discard.isDestructive = true;
  discard.shortcutKey = KeyCode::N;

  m_dialog.clearButtons();
  m_dialog.addButton(save);
  if (offerSaveAll) {
    m_dialog.addButton(saveAll);
  }
  m_dialog.addButton(discard);
  m_dialog.addButton(cancelButton());
  m_dialog.setTitle("Unsaved Changes");
  m_dialog.open(message);
  setVisible(true);
}

void
EditorConfirmDialog::openDiscard(const std::string& message,
                                 const std::string& discardLabel)
{
  GuiButtonDef discard;
  discard.label = discardLabel;
  discard.actionId = static_cast<int>(EditorConfirmAction::Discard);
  discard.isDestructive = true;
  discard.isDefault = true;
  discard.shortcutKey = KeyCode::Enter;

  m_dialog.clearButtons();
  m_dialog.addButton(discard);
  m_dialog.addButton(cancelButton());
  m_dialog.setTitle(discardLabel);
  m_dialog.open(message);
  setVisible(true);
}

void
EditorConfirmDialog::close()
{
  m_dialog.close();
  setVisible(false);
}

EditorConfirmAction
EditorConfirmDialog::update(InputManager* inputManager, float dt)
{
  const int action = m_dialog.update(inputManager, dt);
  switch (action) {
    case static_cast<int>(EditorConfirmAction::Save):
      return EditorConfirmAction::Save;
    case static_cast<int>(EditorConfirmAction::SaveAll):
      return EditorConfirmAction::SaveAll;
    case static_cast<int>(EditorConfirmAction::Discard):
      return EditorConfirmAction::Discard;
    case static_cast<int>(EditorConfirmAction::Cancel):
      return EditorConfirmAction::Cancel;
    default:
      return EditorConfirmAction::None;
  }
}

bool
EditorConfirmDialog::AppendCommands(Renderer* renderer)
{
  return m_dialog.AppendCommands(renderer);
}
