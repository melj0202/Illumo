#include <Illumo/Gui/GuiTextEdit.h>

#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <cmath>
#include <queue>

static bool
continuationByte(unsigned char value)
{
  return (value & 0xc0u) == 0x80u;
}

static void
appendUtf8(std::string& output, unsigned int codepoint)
{
  if (codepoint < 0x80u) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800u) {
    output.push_back(static_cast<char>(0xc0u | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  } else if (codepoint < 0x10000u) {
    if (codepoint >= 0xd800u && codepoint <= 0xdfffu) {
      return;
    }
    output.push_back(static_cast<char>(0xe0u | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
    output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  } else if (codepoint <= 0x10ffffu) {
    output.push_back(static_cast<char>(0xf0u | (codepoint >> 18)));
    output.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3fu)));
    output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
    output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  }
}

void
GuiTextEdit::begin(std::string_view text, std::size_t maximumBytes)
{
  m_maximumBytes = std::max<std::size_t>(1, maximumBytes);
  m_text.clear();
  m_caret = 0;
  m_anchor = 0;
  m_active = true;
  insertText(text);
  selectAll();
  m_blink = 0.0f;
}

void
GuiTextEdit::end()
{
  m_active = false;
}

std::size_t
GuiTextEdit::selectionStart() const
{
  return std::min(m_caret, m_anchor);
}

std::size_t
GuiTextEdit::selectionEnd() const
{
  return std::max(m_caret, m_anchor);
}

std::string
GuiTextEdit::selectedText() const
{
  return m_text.substr(selectionStart(), selectionEnd() - selectionStart());
}

std::size_t
GuiTextEdit::previousBoundary(std::size_t index) const
{
  if (index == 0) {
    return 0;
  }
  --index;
  while (index > 0 &&
         continuationByte(static_cast<unsigned char>(m_text[index]))) {
    --index;
  }
  return index;
}

std::size_t
GuiTextEdit::nextBoundary(std::size_t index) const
{
  if (index >= m_text.size()) {
    return m_text.size();
  }
  ++index;
  while (index < m_text.size() &&
         continuationByte(static_cast<unsigned char>(m_text[index]))) {
    ++index;
  }
  return index;
}

void
GuiTextEdit::eraseSelection()
{
  const std::size_t start = selectionStart();
  m_text.erase(start, selectionEnd() - start);
  m_caret = start;
  m_anchor = start;
}

void
GuiTextEdit::insertText(std::string_view text)
{
  eraseSelection();
  std::string clean;
  clean.reserve(text.size());
  for (const char value : text) {
    const unsigned char character = static_cast<unsigned char>(value);
    if (character >= 32u && character != 127u) {
      clean.push_back(value);
    }
  }
  // Clip at a code point boundary so the text never ends mid-sequence.
  const std::size_t room =
    m_maximumBytes > m_text.size() ? m_maximumBytes - m_text.size() : 0;
  if (clean.size() > room) {
    std::size_t cut = room;
    while (cut > 0 &&
           continuationByte(static_cast<unsigned char>(clean[cut]))) {
      --cut;
    }
    clean.resize(cut);
  }
  m_text.insert(m_caret, clean);
  m_caret += clean.size();
  m_anchor = m_caret;
  m_blink = 0.0f;
}

void
GuiTextEdit::backspace()
{
  if (hasSelection()) {
    eraseSelection();
  } else if (m_caret > 0) {
    const std::size_t start = previousBoundary(m_caret);
    m_text.erase(start, m_caret - start);
    m_caret = start;
    m_anchor = start;
  }
  m_blink = 0.0f;
}

void
GuiTextEdit::deleteForward()
{
  if (hasSelection()) {
    eraseSelection();
  } else if (m_caret < m_text.size()) {
    const std::size_t end = nextBoundary(m_caret);
    m_text.erase(m_caret, end - m_caret);
  }
  m_blink = 0.0f;
}

static bool
wordCharacter(char value)
{
  const unsigned char character = static_cast<unsigned char>(value);
  return character >= 0x80u || (character >= '0' && character <= '9') ||
         (character >= 'a' && character <= 'z') ||
         (character >= 'A' && character <= 'Z') || character == '_';
}

void
GuiTextEdit::moveLeft(bool extend, bool word)
{
  if (!extend && hasSelection()) {
    m_caret = selectionStart();
  } else if (word) {
    std::size_t index = m_caret;
    while (index > 0 && !wordCharacter(m_text[index - 1])) {
      index = previousBoundary(index);
    }
    while (index > 0 && wordCharacter(m_text[index - 1])) {
      index = previousBoundary(index);
    }
    m_caret = index;
  } else {
    m_caret = previousBoundary(m_caret);
  }
  if (!extend) {
    m_anchor = m_caret;
  }
  m_blink = 0.0f;
}

void
GuiTextEdit::moveRight(bool extend, bool word)
{
  if (!extend && hasSelection()) {
    m_caret = selectionEnd();
  } else if (word) {
    std::size_t index = m_caret;
    while (index < m_text.size() && !wordCharacter(m_text[index])) {
      index = nextBoundary(index);
    }
    while (index < m_text.size() && wordCharacter(m_text[index])) {
      index = nextBoundary(index);
    }
    m_caret = index;
  } else {
    m_caret = nextBoundary(m_caret);
  }
  if (!extend) {
    m_anchor = m_caret;
  }
  m_blink = 0.0f;
}

void
GuiTextEdit::moveHome(bool extend)
{
  m_caret = 0;
  if (!extend) {
    m_anchor = 0;
  }
  m_blink = 0.0f;
}

void
GuiTextEdit::moveEnd(bool extend)
{
  m_caret = m_text.size();
  if (!extend) {
    m_anchor = m_caret;
  }
  m_blink = 0.0f;
}

void
GuiTextEdit::selectAll()
{
  m_anchor = 0;
  m_caret = m_text.size();
  m_blink = 0.0f;
}

bool
GuiTextEdit::caretVisible() const
{
  return m_active && std::fmod(m_blink, 1.0f) < 0.6f;
}

GuiTextEditEvents
GuiTextEdit::handleInput(InputManager& input)
{
  GuiTextEditEvents events;
  if (!m_active) {
    return events;
  }
  std::queue<InputManager::KeyPressEvent>& keys = input.getKeyQueue();
  while (!keys.empty()) {
    const InputManager::KeyPressEvent event = keys.front();
    keys.pop();
    // After a commit or cancel the rest of this frame's keys are consumed
    // unapplied: they were typed at the field, not at the editor.
    if (event.action != InputAction::Press || events.committed ||
        events.cancelled) {
      continue;
    }
    const bool shift = input.isShiftPressed() || (event.modifiers & 0x1) != 0;
    const bool control =
      input.isControlPressed() || (event.modifiers & 0x2) != 0;
    const std::string before = m_text;
    switch (event.key) {
      case KeyCode::Enter:
        events.committed = true;
        break;
      case KeyCode::Escape:
        events.cancelled = true;
        break;
      case KeyCode::Tab:
        // Tab commits and lets the owner move focus.
        events.committed = true;
        break;
      case KeyCode::Left:
        moveLeft(shift, control);
        break;
      case KeyCode::Right:
        moveRight(shift, control);
        break;
      case KeyCode::Home:
        moveHome(shift);
        break;
      case KeyCode::End:
        moveEnd(shift);
        break;
      case KeyCode::Backspace:
        backspace();
        break;
      case KeyCode::Delete:
        deleteForward();
        break;
      case KeyCode::A:
        if (control) {
          selectAll();
        }
        break;
      case KeyCode::C:
      case KeyCode::X:
        if (control && hasSelection()) {
          events.copyRequested = true;
          events.copied = selectedText();
          if (event.key == KeyCode::X) {
            eraseSelection();
          }
        }
        break;
      case KeyCode::V:
        if (control) {
          events.pasteRequested = true;
        }
        break;
      default:
        break;
    }
    events.changed = events.changed || m_text != before;
  }
  // Characters typed with Ctrl held are shortcuts, not text.
  std::queue<unsigned int>& characters = input.getCharQueue();
  while (!characters.empty()) {
    const unsigned int codepoint = characters.front();
    characters.pop();
    if (events.committed || events.cancelled || input.isControlPressed()) {
      continue;
    }
    std::string utf8;
    appendUtf8(utf8, codepoint);
    if (!utf8.empty()) {
      const std::string before = m_text;
      insertText(utf8);
      events.changed = events.changed || m_text != before;
    }
  }
  return events;
}
