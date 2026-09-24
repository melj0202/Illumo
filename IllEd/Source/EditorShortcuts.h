#pragma once

#include "EditorCommand.h"
#include <Illumo/Services/KeyCode.h>
#include <string>
#include <vector>

// The one keyboard map. Menus show labelFor() and key handling uses match(),
// so a shortcut shown in a menu always works.
struct EditorShortcut
{
  EditorCommand command = EditorCommand::None;
  KeyCode key = KeyCode::None;
  bool control = false;
  bool shift = false;
  bool alt = false;
};

class EditorShortcuts
{
public:
  static const std::vector<EditorShortcut>& all();
  // "Ctrl+Shift+S", "Del", "F"... or empty when the command has no key.
  static std::string labelFor(EditorCommand command);
  // The command bound to a key press with these modifiers, or None.
  static EditorCommand match(KeyCode key, bool control, bool shift, bool alt);
  static std::string keyName(KeyCode key);
};
