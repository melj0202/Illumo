#include "EditorConfirmDialog.h"
#include "EditorToolbar.h"

EditorConfirmDialog::EditorConfirmDialog(IRenderWindow* window,
                                         Renderer* renderer)
  : m_dialog(window, renderer)
{
  m_dialog.setTitle("Unsaved Changes");
  m_dialog.setFontSize(EditorToolbar::kDefaultFontSize);

  GuiButtonDef saveBtn;
  saveBtn.label = "Save";
  saveBtn.actionId = static_cast<int>(EditorConfirmAction::Save);
  saveBtn.isDefault = true;
  saveBtn.shortcutKey = KeyCode::Enter;

  GuiButtonDef discardBtn;
  discardBtn.label = "Don't Save";
  discardBtn.actionId = static_cast<int>(EditorConfirmAction::Discard);
  discardBtn.isDestructive = true;
  discardBtn.shortcutKey = KeyCode::N;

  GuiButtonDef cancelBtn;
  cancelBtn.label = "Cancel";
  cancelBtn.actionId = static_cast<int>(EditorConfirmAction::Cancel);
  cancelBtn.isCancel = true;
  cancelBtn.shortcutKey = KeyCode::Escape;

  m_dialog.addButton(saveBtn);
  m_dialog.addButton(discardBtn);
  m_dialog.addButton(cancelBtn);

  setVisible(false);
}

void
EditorConfirmDialog::setFontSize(float sizePt)
{
  m_dialog.setFontSize(sizePt);
}

void
EditorConfirmDialog::open(const std::string& message)
{
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
