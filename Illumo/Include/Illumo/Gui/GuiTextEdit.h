#pragma once

#include <cstddef>
#include <string>
#include <string_view>

class InputManager;

// What one input pass did to an edit. Clipboard work is the caller's: copy
// and cut hand over `copied`; a paste request asks the caller to fetch the
// clipboard (synchronously natively, asynchronously in guests) and call
// insertText with it.
struct GuiTextEditEvents
{
  bool changed = false;
  bool committed = false;
  bool cancelled = false;
  bool copyRequested = false;
  bool pasteRequested = false;
  std::string copied;
};

// Single-line UTF-8 text editing as value state: text, a caret and a
// selection anchor, both at code point boundaries. It owns no drawing and no
// widget tree; GuiKit::drawTextField draws it. While active it consumes the
// key and character queues it reads, so editor shortcuts never fire while a
// field has focus.
class GuiTextEdit
{
public:
  static constexpr std::size_t kDefaultMaximumBytes = 256;

  // Starts editing with the whole text selected.
  void begin(std::string_view text,
             std::size_t maximumBytes = kDefaultMaximumBytes);
  void end();
  bool active() const { return m_active; }

  const std::string& text() const { return m_text; }
  std::size_t caret() const { return m_caret; }
  std::size_t anchor() const { return m_anchor; }
  bool hasSelection() const { return m_caret != m_anchor; }
  std::size_t selectionStart() const;
  std::size_t selectionEnd() const;
  std::string selectedText() const;

  // Replaces the selection with text; control characters and line breaks are
  // dropped and the result is clipped to the byte limit at a code point.
  void insertText(std::string_view text);
  void backspace();
  void deleteForward();
  void moveLeft(bool extend, bool word);
  void moveRight(bool extend, bool word);
  void moveHome(bool extend);
  void moveEnd(bool extend);
  void selectAll();

  // Reads pending keys (Enter commits, Escape cancels, arrows, Home/End,
  // Backspace/Delete, Ctrl+A/C/X/V) and typed characters.
  GuiTextEditEvents handleInput(InputManager& input);

  // Caret blink clock; reset by every edit or caret move.
  void tick(float dt) { m_blink += dt; }
  bool caretVisible() const;

private:
  std::string m_text;
  std::size_t m_caret = 0;
  std::size_t m_anchor = 0;
  std::size_t m_maximumBytes = kDefaultMaximumBytes;
  bool m_active = false;
  float m_blink = 0.0f;

  void eraseSelection();
  std::size_t previousBoundary(std::size_t index) const;
  std::size_t nextBoundary(std::size_t index) const;
};
