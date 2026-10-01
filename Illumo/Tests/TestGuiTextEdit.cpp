#include <Illumo/Gui/GuiTextEdit.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>

static int
testTextEditUtf8Caret()
{
  TestCounters counters;
  GuiTextEdit edit;
  // "aé€😀" is 1 + 2 + 3 + 4 bytes.
  edit.begin("a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
  testTrue(counters, edit.active(), "begin activates");
  testTrue(counters,
           edit.selectionStart() == 0 && edit.selectionEnd() == 10,
           "begin selects everything");
  edit.moveEnd(false);
  testEqSize(counters, edit.caret(), 10, "end of text");
  edit.moveLeft(false, false);
  testEqSize(counters, edit.caret(), 6, "left skips a 4-byte code point");
  edit.moveLeft(false, false);
  testEqSize(counters, edit.caret(), 3, "left skips a 3-byte code point");
  edit.moveLeft(false, false);
  testEqSize(counters, edit.caret(), 1, "left skips a 2-byte code point");
  edit.moveRight(false, false);
  testEqSize(counters, edit.caret(), 3, "right skips a 2-byte code point");
  edit.backspace();
  testEqStr(counters,
            edit.text(),
            "a\xe2\x82\xac\xf0\x9f\x98\x80",
            "backspace removes a whole code point");
  edit.deleteForward();
  testEqStr(counters,
            edit.text(),
            "a\xf0\x9f\x98\x80",
            "delete removes a whole code point");
  edit.moveHome(false);
  edit.insertText("x\ny\tz");
  testEqStr(counters,
            edit.text(),
            "xyza\xf0\x9f\x98\x80",
            "line breaks and tabs are dropped");

  GuiTextEdit limited;
  limited.begin("", 5);
  limited.insertText("ab\xe2\x82\xac\xe2\x82\xac");
  testEqStr(counters,
            limited.text(),
            "ab\xe2\x82\xac",
            "the byte limit never splits a code point");

  GuiTextEdit words;
  words.begin("move the  node");
  words.moveEnd(false);
  words.moveLeft(false, true);
  testEqSize(counters, words.caret(), 10, "word left lands on a word start");
  words.moveLeft(false, true);
  testEqSize(counters, words.caret(), 5, "word left skips spaces");
  words.moveRight(true, true);
  testEqStr(counters, words.selectedText(), "the", "word right extends");
  return counters.failures;
}

static int
testTextEditSelectionClipboard()
{
  TestCounters counters;
  InputManager input(nullptr);
  GuiTextEdit edit;
  edit.begin("12.5");
  // Typing replaces the initial select-all.
  input.getCharQueue().push('7');
  GuiTextEditEvents events = edit.handleInput(input);
  testTrue(counters, events.changed, "typing changes the text");
  testEqStr(counters, edit.text(), "7", "typing replaces the selection");
  input.getKeyQueue().push({ KeyCode::A, InputAction::Press, 0x2 });
  input.getKeyQueue().push({ KeyCode::C, InputAction::Press, 0x2 });
  events = edit.handleInput(input);
  testTrue(counters,
           events.copyRequested && events.copied == "7" && !events.changed,
           "Ctrl+A, Ctrl+C hands the selection to the caller");
  input.getKeyQueue().push({ KeyCode::X, InputAction::Press, 0x2 });
  events = edit.handleInput(input);
  testTrue(counters,
           events.copyRequested && events.copied == "7" &&
             edit.text().empty() && events.changed,
           "Ctrl+X copies and removes");
  input.getKeyQueue().push({ KeyCode::V, InputAction::Press, 0x2 });
  events = edit.handleInput(input);
  testTrue(counters,
           events.pasteRequested && !events.changed,
           "Ctrl+V requests clipboard text");
  edit.insertText("-3.25");
  testEqStr(counters, edit.text(), "-3.25", "the caller inserts pasted text");
  input.getKeyQueue().push({ KeyCode::Home, InputAction::Press, 0x1 });
  events = edit.handleInput(input);
  testTrue(counters,
           edit.selectedText() == "-3.25",
           "Shift+Home extends the selection");
  input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
  input.getKeyQueue().push({ KeyCode::Delete, InputAction::Press, 0 });
  input.getCharQueue().push('9');
  events = edit.handleInput(input);
  testTrue(counters, events.committed, "Enter commits");
  testEqStr(counters,
            edit.text(),
            "-3.25",
            "input after the commit is consumed, not applied");
  testTrue(counters,
           input.getKeyQueue().empty() && input.getCharQueue().empty(),
           "an active field consumes the queues so shortcuts cannot fire");
  input.getKeyQueue().push({ KeyCode::Escape, InputAction::Press, 0 });
  events = edit.handleInput(input);
  testTrue(counters, events.cancelled, "Escape cancels");
  edit.end();
  input.getKeyQueue().push({ KeyCode::Delete, InputAction::Press, 0 });
  edit.handleInput(input);
  testEqSize(counters,
             input.getKeyQueue().size(),
             1,
             "an inactive field leaves input alone");
  return counters.failures;
}

void
registerGuiTextEditTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Gui.TextEditUtf8Caret",
               []() { return testTextEditUtf8Caret(); });
  registry.add("Illumo.Gui.TextEditSelectionClipboard",
               []() { return testTextEditSelectionClipboard(); });
}
