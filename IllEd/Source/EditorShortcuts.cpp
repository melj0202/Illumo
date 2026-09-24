#include "EditorShortcuts.h"

// Camera navigation uses the arrow keys, PageUp/PageDown and the mouse, so
// letters stay free for editing commands. Alt+F4 is the window's own close
// path (OnCloseRequested), so Exit has no entry here.
const std::vector<EditorShortcut>&
EditorShortcuts::all()
{
  static const std::vector<EditorShortcut> shortcuts = {
    { EditorCommand::NewDocument, KeyCode::N, true, false, false },
    { EditorCommand::OpenDocument, KeyCode::O, true, false, false },
    { EditorCommand::SaveDocument, KeyCode::S, true, false, false },
    { EditorCommand::SaveDocumentAs, KeyCode::S, true, true, false },
    { EditorCommand::Undo, KeyCode::Z, true, false, false },
    { EditorCommand::Redo, KeyCode::Y, true, false, false },
    { EditorCommand::Redo, KeyCode::Z, true, true, false },
    { EditorCommand::Cut, KeyCode::X, true, false, false },
    { EditorCommand::Copy, KeyCode::C, true, false, false },
    { EditorCommand::Paste, KeyCode::V, true, false, false },
    { EditorCommand::Duplicate, KeyCode::D, true, false, false },
    { EditorCommand::SelectAll, KeyCode::A, true, false, false },
    { EditorCommand::DeselectAll, KeyCode::Escape, false, false, false },
    { EditorCommand::Rename, KeyCode::F2, false, false, false },
    { EditorCommand::DeleteNode, KeyCode::Delete, false, false, false },
    { EditorCommand::UnparentNode, KeyCode::U, false, false, false },
    { EditorCommand::ToggleVisible, KeyCode::H, false, false, false },
    { EditorCommand::SelectTool, KeyCode::Q, false, false, false },
    { EditorCommand::TranslateMode, KeyCode::W, false, false, false },
    { EditorCommand::RotateMode, KeyCode::E, false, false, false },
    { EditorCommand::ScaleMode, KeyCode::R, false, false, false },
    { EditorCommand::ToggleGizmoSpace, KeyCode::X, false, false, false },
    { EditorCommand::ToggleSnap, KeyCode::G, false, false, false },
    { EditorCommand::SetMode2D, KeyCode::Num2, false, false, false },
    { EditorCommand::SetMode3D, KeyCode::Num3, false, false, false },
    { EditorCommand::ResetCamera, KeyCode::Home, false, false, false },
    { EditorCommand::FrameSelection, KeyCode::F, false, false, false },
  };
  return shortcuts;
}

std::string
EditorShortcuts::keyName(KeyCode key)
{
  const int value = static_cast<int>(key);
  if (key >= KeyCode::A && key <= KeyCode::Z) {
    return std::string(
      1, static_cast<char>('A' + (value - static_cast<int>(KeyCode::A))));
  }
  if (key >= KeyCode::Num0 && key <= KeyCode::Num9) {
    return std::string(
      1, static_cast<char>('0' + (value - static_cast<int>(KeyCode::Num0))));
  }
  if (key >= KeyCode::F1 && key <= KeyCode::F12) {
    return "F" + std::to_string(value - static_cast<int>(KeyCode::F1) + 1);
  }
  switch (key) {
    case KeyCode::Delete:
      return "Del";
    case KeyCode::Escape:
      return "Esc";
    case KeyCode::Home:
      return "Home";
    case KeyCode::End:
      return "End";
    case KeyCode::PageUp:
      return "PgUp";
    case KeyCode::PageDown:
      return "PgDn";
    case KeyCode::Space:
      return "Space";
    case KeyCode::Enter:
      return "Enter";
    case KeyCode::Tab:
      return "Tab";
    default:
      return "?";
  }
}

std::string
EditorShortcuts::labelFor(EditorCommand command)
{
  for (const EditorShortcut& shortcut : all()) {
    if (shortcut.command != command) {
      continue;
    }
    std::string label;
    if (shortcut.control) {
      label += "Ctrl+";
    }
    if (shortcut.shift) {
      label += "Shift+";
    }
    if (shortcut.alt) {
      label += "Alt+";
    }
    return label + keyName(shortcut.key);
  }
  return {};
}

EditorCommand
EditorShortcuts::match(KeyCode key, bool control, bool shift, bool alt)
{
  for (const EditorShortcut& shortcut : all()) {
    if (shortcut.key == key && shortcut.control == control &&
        shortcut.shift == shift && shortcut.alt == alt) {
      return shortcut.command;
    }
  }
  return EditorCommand::None;
}
