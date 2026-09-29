#include <Illumo/Gui/GuiDropdownList.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiPointerHint.h>
#include <Illumo/Gui/GuiTypes.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <cmath>
#include <queue>
#include <utility>

// Type-ahead compares ASCII letters, digits and spaces without case.
static char
typeAheadCharacter(unsigned int codepoint)
{
  if (codepoint >= 'A' && codepoint <= 'Z') {
    return static_cast<char>(codepoint - 'A' + 'a');
  }
  if ((codepoint >= 'a' && codepoint <= 'z') ||
      (codepoint >= '0' && codepoint <= '9') || codepoint == ' ') {
    return static_cast<char>(codepoint);
  }
  return '\0';
}

static bool
startsWithFolded(const std::string& label, const std::string& prefix)
{
  if (prefix.size() > label.size()) {
    return false;
  }
  for (std::size_t index = 0; index < prefix.size(); ++index) {
    char letter = label[index];
    if (letter >= 'A' && letter <= 'Z') {
      letter = static_cast<char>(letter - 'A' + 'a');
    }
    if (letter != prefix[index]) {
      return false;
    }
  }
  return true;
}

// The label cut to fit `width`, ending in "..." when it had to be cut. Cuts
// fall between UTF-8 characters.
static std::string
fitLabel(const std::string& label, float size, float emphasis, float width)
{
  if (GuiKit::measureEmphasizedText(label, size, emphasis) <= width) {
    return label;
  }
  std::string cut = label;
  while (!cut.empty()) {
    while (!cut.empty() &&
           (static_cast<unsigned char>(cut.back()) & 0xC0u) == 0x80u) {
      cut.pop_back();
    }
    if (!cut.empty()) {
      cut.pop_back();
    }
    while (!cut.empty() && cut.back() == ' ') {
      cut.pop_back();
    }
    if (GuiKit::measureEmphasizedText(cut + "...", size, emphasis) <= width) {
      break;
    }
  }
  return cut + "...";
}

GuiDropdownList::GuiDropdownList()
{
  m_reveal.configure(GuiMotion::kBoing);
  m_focus.configure(GuiMotion::kJelly);
}

void
GuiDropdownList::open(std::vector<GuiDropdownItem> items,
                      int currentIndex,
                      float fieldX,
                      float fieldY,
                      float fieldWidth,
                      float fieldHeight,
                      float spaceTop,
                      float spaceBottom,
                      bool reducedMotion)
{
  m_items = std::move(items);
  m_open = !m_items.empty();
  m_chosen = -1;
  if (!m_open) {
    return;
  }
  const int count = itemCount();
  m_reducedMotion = reducedMotion;
  m_current = currentIndex >= 0 && currentIndex < count ? currentIndex : -1;
  m_highlighted = m_current >= 0 ? m_current : 0;
  m_fieldX = fieldX;
  m_fieldY = fieldY;
  m_fieldWidth = fieldWidth;
  m_fieldHeight = fieldHeight;

  // Below the field unless the rows it wants fit only above it, or more of
  // them do.
  const int wanted = std::min(count, kMaximumVisibleRows);
  const float frame = kGap + kPadding * 2.0f;
  const int rowsBelow = static_cast<int>(
    std::floor((spaceBottom - (fieldY + fieldHeight) - frame) / kRowHeight));
  const int rowsAbove =
    static_cast<int>(std::floor((fieldY - spaceTop - frame) / kRowHeight));
  m_upward = rowsBelow < wanted && rowsAbove > rowsBelow;
  m_visibleRows = std::clamp(m_upward ? rowsAbove : rowsBelow, 1, wanted);
  m_x = fieldX;
  m_width = fieldWidth;
  m_height = static_cast<float>(m_visibleRows) * kRowHeight + kPadding * 2.0f;
  m_y = m_upward ? fieldY - kGap - m_height : fieldY + fieldHeight + kGap;
  // The current value opens in the middle of the window.
  m_firstVisible = GuiPanelLayout::clampFirstVisibleRow(
    m_highlighted - m_visibleRows / 2, count, m_visibleRows);

  m_motion.setReducedMotion(reducedMotion);
  m_motion.restart();
  m_motion.settleSelection();
  m_focus.focusOnly(m_highlighted, count);
  m_focus.snapAll();
  m_reveal.snapTo(reducedMotion ? 1.0f : 0.0f);
  m_reveal.setTarget(1.0f);
  m_typeAhead.clear();
  m_typeAheadAge = 0.0f;
}

void
GuiDropdownList::follow(float fieldX, float fieldY)
{
  if (!std::isfinite(fieldX) || !std::isfinite(fieldY)) {
    return;
  }
  m_x += fieldX - m_fieldX;
  m_y += fieldY - m_fieldY;
  m_fieldX = fieldX;
  m_fieldY = fieldY;
}

void
GuiDropdownList::tick(float deltaSeconds)
{
  if (!m_open || !std::isfinite(deltaSeconds)) {
    return;
  }
  const float step = std::clamp(deltaSeconds, 0.0f, 0.1f);
  m_reveal.tick(step, m_reducedMotion);
  m_motion.tick(step);
  m_focus.focusOnly(m_highlighted, itemCount());
  m_focus.tick(step, m_reducedMotion);
  m_typeAheadAge += step;
  if (m_typeAheadAge > kTypeAheadSeconds) {
    m_typeAhead.clear();
  }
}

void
GuiDropdownList::highlight(int index)
{
  const int target = std::clamp(index, 0, itemCount() - 1);
  if (target == m_highlighted) {
    return;
  }
  m_motion.beginSelectionTravel(static_cast<float>(m_highlighted),
                                static_cast<float>(target));
  m_highlighted = target;
  m_firstVisible =
    GuiPanelLayout::scrollToRow(m_firstVisible, target, m_visibleRows);
}

void
GuiDropdownList::scrollRows(int rows)
{
  m_firstVisible = GuiPanelLayout::clampFirstVisibleRow(
    m_firstVisible + rows, itemCount(), m_visibleRows);
}

GuiDropdownResult
GuiDropdownList::handleKey(KeyCode key)
{
  if (!m_open) {
    return GuiDropdownResult::None;
  }
  switch (key) {
    case KeyCode::Up:
      highlight(m_highlighted - 1);
      break;
    case KeyCode::Down:
      highlight(m_highlighted + 1);
      break;
    case KeyCode::PageUp:
      highlight(m_highlighted - m_visibleRows);
      break;
    case KeyCode::PageDown:
      highlight(m_highlighted + m_visibleRows);
      break;
    case KeyCode::Home:
      highlight(0);
      break;
    case KeyCode::End:
      highlight(itemCount() - 1);
      break;
    case KeyCode::Enter:
      m_chosen = m_highlighted;
      m_open = false;
      return GuiDropdownResult::Chosen;
    case KeyCode::Escape:
      m_open = false;
      return GuiDropdownResult::Dismissed;
    default:
      break;
  }
  return GuiDropdownResult::None;
}

void
GuiDropdownList::handleCharacter(unsigned int codepoint)
{
  const char letter = typeAheadCharacter(codepoint);
  if (!m_open || letter == '\0') {
    return;
  }
  m_typeAhead.push_back(letter);
  m_typeAheadAge = 0.0f;
  const int count = itemCount();
  // A single letter moves on to the next item that starts with it, so
  // repeating it walks them; a longer prefix keeps a highlight that still
  // matches.
  const auto search = [this, count](const std::string& prefix, int start) {
    for (int offset = 0; offset < count; ++offset) {
      const int index = (start + offset) % count;
      if (startsWithFolded(m_items[static_cast<std::size_t>(index)].label,
                           prefix)) {
        return index;
      }
    }
    return -1;
  };
  int found = search(
    m_typeAhead, m_typeAhead.size() == 1u ? m_highlighted + 1 : m_highlighted);
  if (found < 0 && m_typeAhead.size() > 1u) {
    m_typeAhead.assign(1u, letter);
    found = search(m_typeAhead, m_highlighted + 1);
  }
  if (found >= 0) {
    highlight(found);
  }
}

int
GuiDropdownList::rowAt(float x, float y) const
{
  const float rowsTop = m_y + kPadding;
  if (x < m_x || x >= m_x + m_width || y < rowsTop ||
      y >= rowsTop + static_cast<float>(m_visibleRows) * kRowHeight) {
    return -1;
  }
  const int index =
    m_firstVisible + static_cast<int>((y - rowsTop) / kRowHeight);
  return index < itemCount() ? index : -1;
}

GuiDropdownResult
GuiDropdownList::handlePointer(float x, float y, bool moved, bool clicked)
{
  if (!m_open) {
    return GuiDropdownResult::None;
  }
  const int row = rowAt(x, y);
  if (row >= 0) {
    GuiPointerHint::markInteractive();
  }
  if (row >= 0 && (moved || clicked)) {
    highlight(row);
  }
  if (!clicked) {
    return GuiDropdownResult::None;
  }
  if (row >= 0) {
    m_chosen = row;
    m_open = false;
    return GuiDropdownResult::Chosen;
  }
  if (GuiKit::isPointInRect(x, y, m_x, m_y, m_width, m_height)) {
    // The list's own padding: not a choice, and not outside either.
    return GuiDropdownResult::None;
  }
  // Anywhere else, the field it belongs to included, closes it.
  m_open = false;
  return GuiDropdownResult::Dismissed;
}

GuiDropdownResult
GuiDropdownList::update(InputManager* input, const GuiPointerTracker& pointer)
{
  if (!m_open) {
    return GuiDropdownResult::None;
  }
  GuiDropdownResult result = GuiDropdownResult::None;
  bool scrolled = false;
  if (input != nullptr) {
    std::queue<InputManager::KeyPressEvent> remaining;
    std::queue<InputManager::KeyPressEvent>& keys = input->getKeyQueue();
    while (!keys.empty()) {
      const InputManager::KeyPressEvent event = keys.front();
      keys.pop();
      if (event.key == KeyCode::Grave) {
        // The debug console's toggle is never ours.
        remaining.push(event);
        continue;
      }
      const bool repeatable =
        event.key != KeyCode::Enter && event.key != KeyCode::Escape;
      if (result == GuiDropdownResult::None &&
          (event.action == InputAction::Press ||
           (repeatable && event.action == InputAction::Hold))) {
        result = handleKey(event.key);
      }
    }
    keys.swap(remaining);
    std::queue<unsigned int>& characters = input->getCharQueue();
    while (!characters.empty()) {
      if (result == GuiDropdownResult::None) {
        handleCharacter(characters.front());
      }
      characters.pop();
    }
    if (result == GuiDropdownResult::None) {
      m_firstVisible = GuiPanelLayout::applyWheelScroll(
        input, m_firstVisible, itemCount(), m_visibleRows, &scrolled);
    }
  }
  if (result == GuiDropdownResult::None) {
    result = handlePointer(pointer.x(),
                           pointer.y(),
                           pointer.moved() && !scrolled,
                           pointer.clicked());
  }
  return result;
}

float
GuiDropdownList::revealedHeight() const
{
  return m_height * std::max(0.0f, m_reveal.value());
}

bool
GuiDropdownList::rowBounds(int index,
                           float* x,
                           float* y,
                           float* width,
                           float* height) const
{
  if (!m_open || index < m_firstVisible ||
      index >= m_firstVisible + m_visibleRows || index >= itemCount()) {
    return false;
  }
  if (x != nullptr) {
    *x = m_x;
  }
  if (y != nullptr) {
    *y =
      m_y + kPadding + static_cast<float>(index - m_firstVisible) * kRowHeight;
  }
  if (width != nullptr) {
    *width = m_width;
  }
  if (height != nullptr) {
    *height = kRowHeight;
  }
  return true;
}

void
GuiDropdownList::draw(GameVisual& card,
                      GameVisual& highlight,
                      GameVisual& text,
                      unsigned char opacity) const
{
  if (!m_open) {
    return;
  }
  // The card pops open from the field's edge on a bouncy spring, briefly
  // stretching past its size; rows appear as it uncovers them.
  const float height = revealedHeight();
  if (height < 2.0f) {
    return;
  }
  const float reveal = std::clamp(m_reveal.value(), 0.0f, 1.0f);
  const unsigned char alpha = static_cast<unsigned char>(
    static_cast<float>(opacity) * std::min(1.0f, reveal * 1.6f));
  const float top = m_upward ? m_y + m_height - height : m_y;
  const float bottom = top + height;
  const float radius = std::min(10.0f, height * 0.5f);
  const ColorRgba cyan = UiTheme::accentCool();
  const ColorRgba violet = UiTheme::accentViolet();

  GuiKit::drawSoftShadow(card,
                         m_x,
                         top,
                         m_width,
                         height,
                         radius,
                         14.0f,
                         5.0f,
                         UiTheme::applyOpacity(UiTheme::glowShadow(), alpha));
  GuiKit::drawRoundedGradientRect(
    card,
    m_x,
    top,
    m_width,
    height,
    radius,
    UiTheme::applyOpacity(UiTheme::mix(UiTheme::glassRim(), cyan, 0.4f), alpha),
    UiTheme::applyOpacity(UiTheme::mix(UiTheme::glassRim(), violet, 0.4f),
                          alpha));
  // An opaque face, so the rows under the list never show through.
  GuiKit::drawRoundedGradientRect(
    card,
    m_x + 1.0f,
    top + 1.0f,
    m_width - 2.0f,
    height - 2.0f,
    std::max(0.0f, radius - 1.0f),
    UiTheme::applyOpacity(UiTheme::menuCard(), alpha),
    UiTheme::applyOpacity(UiTheme::menuSurface(), alpha));
  if (m_width - radius * 2.0f > 2.0f) {
    card.addGradientRect(
      m_x + radius,
      top + 1.0f,
      m_width - radius * 2.0f,
      1.5f,
      UiTheme::applyOpacity(UiTheme::fade(cyan, 0.7f), alpha),
      UiTheme::applyOpacity(UiTheme::fade(violet, 0.7f), alpha),
      UiTheme::applyOpacity(UiTheme::fade(violet, 0.7f), alpha),
      UiTheme::applyOpacity(UiTheme::fade(cyan, 0.7f), alpha));
  }

  const int count = itemCount();
  const bool scrolling = count > m_visibleRows;
  const float rowsTop = m_y + kPadding;
  const float rowsHeight = static_cast<float>(m_visibleRows) * kRowHeight;
  const float scrollBarSpace = scrolling ? 10.0f : 0.0f;
  const bool settled = reveal >= 0.9f;

  if (scrolling && settled) {
    // A slim track with a thumb sized to the window and placed by it.
    const float trackX = m_x + m_width - 8.0f;
    const float trackY = rowsTop + 3.0f;
    const float trackHeight = rowsHeight - 6.0f;
    GuiKit::drawRoundedRect(
      card,
      trackX,
      trackY,
      3.0f,
      trackHeight,
      1.5f,
      UiTheme::applyOpacity(UiTheme::fade(UiTheme::menuBorder(), 0.6f), alpha));
    const float thumbHeight =
      std::max(14.0f,
               trackHeight * static_cast<float>(m_visibleRows) /
                 static_cast<float>(count));
    const float travel = static_cast<float>(count - m_visibleRows);
    const float thumbY = trackY + (trackHeight - thumbHeight) *
                                    static_cast<float>(m_firstVisible) /
                                    std::max(1.0f, travel);
    GuiKit::drawRoundedRect(
      card,
      trackX,
      thumbY,
      3.0f,
      thumbHeight,
      1.5f,
      UiTheme::applyOpacity(UiTheme::fade(cyan, 0.75f), alpha));
  }

  // The highlight pours between rows like the menus' selection; it shows
  // once the card is open and while its row is in view.
  const GuiSelectionSpan span =
    m_motion.selectionSpan(static_cast<float>(m_highlighted));
  const float firstRow = static_cast<float>(m_firstVisible);
  const float lastRow = firstRow + static_cast<float>(m_visibleRows - 1);
  if (settled && span.leading >= firstRow - 0.5f &&
      span.leading <= lastRow + 0.5f && span.trailing >= firstRow - 0.5f &&
      span.trailing <= lastRow + 0.5f) {
    GuiLiquidSelection drop;
    drop.crossStart = m_x + 4.0f;
    drop.crossSize = m_width - 8.0f - scrollBarSpace;
    drop.headStart = rowsTop + (span.leading - firstRow) * kRowHeight + 1.0f;
    drop.tailStart = rowsTop + (span.trailing - firstRow) * kRowHeight + 1.0f;
    drop.cellLength = kRowHeight - 2.0f;
    drop.radius = 7.0f;
    drop.squash = span.squash;
    drop.glowSpread = 8.0f;
    drop.glow = UiTheme::applyOpacity(UiTheme::fade(cyan, 0.18f), alpha);
    drop.glowBottom = UiTheme::accentBlendOf(drop.glow);
    drop.rim = UiTheme::applyOpacity(UiTheme::fade(cyan, 0.85f), alpha);
    drop.rimBottom = UiTheme::accentBlendOf(drop.rim);
    drop.faceTop = UiTheme::applyOpacity(UiTheme::selectionTop(), alpha);
    drop.faceBottom = UiTheme::applyOpacity(UiTheme::selectionBottom(), alpha);
    drop.sheen = m_motion.selectionSheen();
    drop.sheenColor =
      UiTheme::applyOpacity(ColorRgba{ 210, 250, 255, 40 }, alpha);
    GuiKit::drawLiquidSelection(highlight, drop);
  }

  const float labelSize = 13.0f;
  const float detailSize = 10.5f;
  const float labelX = m_x + 24.0f;
  for (int slot = 0; slot < m_visibleRows; ++slot) {
    const int index = m_firstVisible + slot;
    if (index >= count) {
      break;
    }
    const float rowY = rowsTop + static_cast<float>(slot) * kRowHeight;
    // Rows appear as the opening card uncovers them.
    if (rowY < top || rowY + kRowHeight > bottom + 0.5f) {
      continue;
    }
    const GuiDropdownItem& item = m_items[static_cast<std::size_t>(index)];
    const float centerY = rowY + kRowHeight * 0.5f;
    const float emphasis = std::clamp(m_focus.value(index), 0.0f, 1.3f);
    const float lit = std::min(1.0f, emphasis);
    const bool current = index == m_current;
    float detailWidth = 0.0f;
    if (!item.detail.empty()) {
      detailWidth =
        GuiKit::measureEmphasizedText(item.detail, detailSize, 0.0f);
      text.addText(
        item.detail,
        m_x + m_width - 12.0f - scrollBarSpace - detailWidth,
        centerY - detailSize * 0.5f,
        detailSize,
        UiTheme::applyOpacity(
          UiTheme::mix(UiTheme::textMuted(), UiTheme::textSecondary(), lit),
          alpha));
      detailWidth += 10.0f;
    }
    if (current) {
      // The value the field holds now.
      text.addFilledEllipse(m_x + 10.0f,
                            centerY - 3.0f,
                            6.0f,
                            6.0f,
                            UiTheme::applyOpacity(cyan, alpha));
    }
    const float available =
      m_width - (labelX - m_x) - 12.0f - scrollBarSpace - detailWidth;
    const ColorRgba rest = current
                             ? UiTheme::mix(UiTheme::textPrimary(), cyan, 0.5f)
                             : UiTheme::textSecondary();
    GuiKit::drawEmphasizedText(
      text,
      fitLabel(item.label, labelSize, emphasis, std::max(20.0f, available)),
      labelX + 3.0f * lit,
      centerY - labelSize * 0.5f,
      labelSize,
      UiTheme::applyOpacity(UiTheme::mix(rest, UiTheme::textPrimary(), lit),
                            alpha),
      emphasis);
  }
}
