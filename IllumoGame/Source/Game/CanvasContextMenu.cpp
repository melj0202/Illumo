#include "CanvasContextMenu.h"
#include "CSimSounds.h"
#include "CanvasEditIcons.h"

#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>

// Metrics in virtual pixels.
static const float kPad = 6.0f;
static const float kRowHeight = 26.0f;
static const float kSeparatorHeight = 9.0f;
static const float kFontSize = 12.0f;
static const float kKeySize = 8.5f;
static const float kLabelPad = 12.0f;
static const float kShortcutGap = 22.0f;
static const float kMinimumWidth = 184.0f;
static const float kRadius = 12.0f;
static const float kScreenMargin = 8.0f;
// Each row leads with its action's icon (Fill's is the brush swatch).
static const float kIconSize = 12.0f;
static const float kIconGap = 8.0f;
// How far the pointer travels before a right-button release over a row
// counts as choosing it (press, drag, release).
static const float kDragSlop = 4.0f;

struct MenuRowDefinition
{
  CanvasEditAction action;
  const char* label;
  const char* shortcut;
  bool destructive;
  bool separatorBefore;
};

// The Fill row's label is completed with the brush's state name.
static const MenuRowDefinition kMenuRows[] = {
  { CanvasEditAction::Cut, "Cut", "Ctrl+X", false, false },
  { CanvasEditAction::Copy, "Copy", "Ctrl+C", false, false },
  { CanvasEditAction::Paste, "Paste here", "Ctrl+V", false, false },
  { CanvasEditAction::Fill, "Fill with ", "", false, true },
  { CanvasEditAction::Erase, "Erase", "Del", true, false },
  { CanvasEditAction::Deselect, "Deselect", "", false, true },
};

// A keycap's own width: a key hint with no action, less its spacing.
static float
keycapWidth(const std::string& key)
{
  return GuiKit::measureKeyHint(key, std::string(), kKeySize) - kKeySize * 2.2f;
}

CanvasContextMenu::CanvasContextMenu()
  : m_panelVisual(1024u)
  , m_dropVisual(512u)
  , m_labelVisual(1024u)
{
  m_pop.configure(GuiMotion::kBoing);
  m_dropHead.configure(GuiMotion::kLiquidHead);
  m_dropTail.configure(GuiMotion::kLiquidTail);
  m_dropShown.configure(GuiMotion::kSwell);
  m_emphasis.configure(GuiMotion::kJelly);
  for (GameVisual* visual : { &m_panelVisual, &m_dropVisual, &m_labelVisual }) {
    visual->setVisible(false);
  }
}

void
CanvasContextMenu::prepare(IRenderWindow* window, Renderer* renderer)
{
  m_window = window;
  m_renderer = renderer;
  for (GameVisual* visual : { &m_panelVisual, &m_dropVisual, &m_labelVisual }) {
    visual->setRenderer(renderer);
    visual->setWindow(window);
    visual->setSpace(PrimitiveSpace::Pixels);
    visual->setLayerHint(RenderLayerId::UI);
    visual->prepare(renderer);
    visual->setVisible(false);
  }
  m_panelKey.invalidate();
}

float
CanvasContextMenu::uiScale() const
{
  const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
  return scale > 0.0f ? scale : 1.0f;
}

void
CanvasContextMenu::pointer(float* x, float* y) const
{
  std::array<double, 2> mouse{ 0.0, 0.0 };
  if (m_window != nullptr) {
    mouse = m_window->getMouseCoords();
  }
  const float scale = uiScale();
  *x = static_cast<float>(mouse[0]) / scale;
  *y = static_cast<float>(mouse[1]) / scale;
}

bool
CanvasContextMenu::contains(float x, float y) const
{
  return GuiKit::isPointInRect(x, y, m_x, m_y, m_width, m_height);
}

bool
CanvasContextMenu::containsPointer() const
{
  if (!m_open) {
    return false;
  }
  float x = 0.0f;
  float y = 0.0f;
  pointer(&x, &y);
  return contains(x, y);
}

int
CanvasContextMenu::rowAt(float x, float y) const
{
  if (!contains(x, y) || x < m_x + kPad || x >= m_x + m_width - kPad) {
    return -1;
  }
  for (std::size_t index = 0u; index < m_rows.size(); ++index) {
    const Row& row = m_rows[index];
    if (y >= row.y && y < row.y + kRowHeight) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

bool
CanvasContextMenu::rowCenter(CanvasEditAction action, float* x, float* y) const
{
  if (!m_open) {
    return false;
  }
  const float scale = uiScale();
  for (const Row& row : m_rows) {
    if (row.action != action) {
      continue;
    }
    if (x != nullptr) {
      *x = (m_x + m_width * 0.5f) * scale;
    }
    if (y != nullptr) {
      *y = (row.y + kRowHeight * 0.5f) * scale;
    }
    return true;
  }
  return false;
}

void
CanvasContextMenu::open(const std::string& fillLabel,
                        ColorRgba brushColor,
                        InputManager* input,
                        bool reducedMotion)
{
  m_rows.clear();
  for (const MenuRowDefinition& definition : kMenuRows) {
    Row row;
    row.action = definition.action;
    row.label = definition.label;
    if (row.action == CanvasEditAction::Fill) {
      row.label += fillLabel.empty() ? std::string("brush") : fillLabel;
    }
    row.shortcut = definition.shortcut;
    row.destructive = definition.destructive;
    row.separatorBefore = definition.separatorBefore;
    m_rows.push_back(row);
  }
  m_brushColor = brushColor;
  m_reducedMotion = reducedMotion;
  float pointerX = 0.0f;
  float pointerY = 0.0f;
  pointer(&pointerX, &pointerY);
  layout(pointerX, pointerY);
  m_open = true;
  m_selected = -1;
  m_dropPlaced = false;
  m_dropShown.snapTo(0.0f);
  m_emphasis.focusOnly(-1, rowCount());
  m_emphasis.snapAll();
  m_pop.snapTo(0.0f);
  m_pop.setTarget(1.0f);
  if (reducedMotion) {
    m_pop.snapTo(1.0f);
  }
  m_clock = 0.0f;
  m_openX = pointerX;
  m_openY = pointerY;
  m_lastX = pointerX;
  m_lastY = pointerY;
  m_movedSinceOpen = false;
  // The press that opened the menu is still held; only new edges count.
  m_leftWasDown =
    input != nullptr && input->isMouseButtonPressed(KeyCode::MouseLeft);
  m_rightWasDown =
    input != nullptr && input->isMouseButtonPressed(KeyCode::MouseRight);
  m_dismissedByPress = false;
  m_dismissedByRightPress = false;
  m_panelKey.invalidate();
  draw();
}

void
CanvasContextMenu::close()
{
  m_open = false;
  m_selected = -1;
  m_dropPlaced = false;
  for (GameVisual* visual : { &m_panelVisual, &m_dropVisual, &m_labelVisual }) {
    visual->clearPrimitives();
    visual->setVisible(false);
  }
  m_panelKey.invalidate();
}

void
CanvasContextMenu::layout(float pointerX, float pointerY)
{
  float widestLabel = 0.0f;
  float widestKey = 0.0f;
  for (const Row& row : m_rows) {
    widestLabel = std::max(
      widestLabel, GuiKit::measureEmphasizedText(row.label, kFontSize, 1.0f));
    if (!row.shortcut.empty()) {
      widestKey = std::max(widestKey, keycapWidth(row.shortcut));
    }
  }
  m_keyWidth = widestKey;
  m_width = std::max(kMinimumWidth,
                     kPad * 2.0f + kLabelPad * 2.0f + kIconSize + kIconGap +
                       widestLabel + kShortcutGap + widestKey);
  float y = kPad;
  for (Row& row : m_rows) {
    if (row.separatorBefore) {
      y += kSeparatorHeight;
    }
    row.y = y;
    y += kRowHeight;
  }
  m_height = y + kPad;

  // Open down and to the right of the pointer, flipping at the window's far
  // edges and clamping into view; the card grows out of the pointer's corner.
  float virtualWidth = 640.0f;
  float virtualHeight = 480.0f;
  if (m_window != nullptr) {
    const std::array<int, 2> dimensions = m_window->getWindowDimensions();
    virtualWidth = static_cast<float>(dimensions[0]) / uiScale();
    virtualHeight = static_cast<float>(dimensions[1]) / uiScale();
  }
  const bool flipX = pointerX + 2.0f + m_width > virtualWidth - kScreenMargin;
  const bool flipY = pointerY + 2.0f + m_height > virtualHeight - kScreenMargin;
  m_x = flipX ? pointerX - 2.0f - m_width : pointerX + 2.0f;
  m_y = flipY ? pointerY - 2.0f - m_height : pointerY + 2.0f;
  m_x = std::max(kScreenMargin,
                 std::min(m_x, virtualWidth - kScreenMargin - m_width));
  m_y = std::max(kScreenMargin,
                 std::min(m_y, virtualHeight - kScreenMargin - m_height));
  m_originX = flipX ? m_x + m_width : m_x;
  m_originY = flipY ? m_y + m_height : m_y;
  for (Row& row : m_rows) {
    row.y += m_y;
  }
}

CanvasEditAction
CanvasContextMenu::choose(int row)
{
  const CanvasEditAction action = m_rows[static_cast<std::size_t>(row)].action;
  CSimSounds::play(CSimSound::MenuSelect);
  close();
  return action;
}

CanvasEditAction
CanvasContextMenu::update(InputManager* input,
                          float deltaSeconds,
                          bool reducedMotion)
{
  m_dismissedByPress = false;
  m_dismissedByRightPress = false;
  if (!m_open) {
    return CanvasEditAction::None;
  }
  m_reducedMotion = reducedMotion;
  const float step = std::isfinite(deltaSeconds) && deltaSeconds > 0.0f
                       ? std::min(deltaSeconds, 0.1f)
                       : 0.0f;
  const bool leftDown =
    input != nullptr && input->isMouseButtonPressed(KeyCode::MouseLeft);
  const bool rightDown =
    input != nullptr && input->isMouseButtonPressed(KeyCode::MouseRight);
  const bool leftPressed = leftDown && !m_leftWasDown;
  const bool rightPressed = rightDown && !m_rightWasDown;
  const bool rightReleased = !rightDown && m_rightWasDown;
  m_leftWasDown = leftDown;
  m_rightWasDown = rightDown;

  float mx = 0.0f;
  float my = 0.0f;
  pointer(&mx, &my);
  const bool moved = mx != m_lastX || my != m_lastY;
  m_lastX = mx;
  m_lastY = my;
  if (std::abs(mx - m_openX) > kDragSlop ||
      std::abs(my - m_openY) > kDragSlop) {
    m_movedSinceOpen = true;
  }

  m_pop.setTarget(1.0f);
  m_pop.tick(step, reducedMotion);
  // Rows answer once they have faded in; they are hit where they rest.
  const bool ready = m_pop.value() >= 0.5f;
  const int hovered = ready ? rowAt(mx, my) : -1;
  if (moved && hovered != m_selected) {
    // The pointer leaving the rows drops the highlight, as on the desktop.
    if (hovered >= 0) {
      CSimSounds::play(CSimSound::MenuHover);
    }
    m_selected = hovered;
  }

  // Up/Down walk the rows (wrapping), Enter chooses, Escape closes. Only
  // those keys are taken from the queue.
  int chosen = -1;
  bool dismissed = false;
  if (input != nullptr) {
    std::queue<InputManager::KeyPressEvent>& keys = input->getKeyQueue();
    std::queue<InputManager::KeyPressEvent> remaining;
    const int count = rowCount();
    while (!keys.empty()) {
      const InputManager::KeyPressEvent event = keys.front();
      keys.pop();
      const bool press =
        event.action == InputAction::Press || event.action == InputAction::Hold;
      if (!press || count == 0) {
        remaining.push(event);
      } else if (event.key == KeyCode::Down) {
        m_selected = m_selected < 0 ? 0 : (m_selected + 1) % count;
        CSimSounds::play(CSimSound::MenuHover);
      } else if (event.key == KeyCode::Up) {
        m_selected = m_selected <= 0 ? count - 1 : m_selected - 1;
        CSimSounds::play(CSimSound::MenuHover);
      } else if (event.key == KeyCode::Enter || event.key == KeyCode::Space) {
        if (m_selected >= 0 && event.action == InputAction::Press) {
          chosen = m_selected;
        }
      } else if (event.key == KeyCode::Escape) {
        dismissed = true;
      } else {
        remaining.push(event);
      }
    }
    keys.swap(remaining);
  }

  // A press on a row chooses it; a press elsewhere closes the menu. Holding
  // the opening right button, moving onto a row and releasing chooses too.
  if (leftPressed || rightPressed) {
    if (contains(mx, my)) {
      if (hovered >= 0) {
        chosen = hovered;
      }
    } else {
      dismissed = true;
      m_dismissedByPress = true;
      m_dismissedByRightPress = rightPressed;
    }
  }
  if (rightReleased && m_movedSinceOpen && hovered >= 0) {
    chosen = hovered;
  }
  if (chosen >= 0) {
    return choose(chosen);
  }
  if (dismissed) {
    CSimSounds::play(CSimSound::MenuBack);
    const bool byPress = m_dismissedByPress;
    const bool byRightPress = m_dismissedByRightPress;
    close();
    m_dismissedByPress = byPress;
    m_dismissedByRightPress = byRightPress;
    return CanvasEditAction::None;
  }

  m_emphasis.focusOnly(m_selected, rowCount());
  m_emphasis.tick(step, reducedMotion);
  if (m_selected >= 0) {
    const float target = m_rows[static_cast<std::size_t>(m_selected)].y;
    if (!m_dropPlaced || m_dropShown.value() <= 0.01f) {
      // A drop that had faded away reappears on its row instead of pouring
      // across the card.
      m_dropHead.snapTo(target);
      m_dropTail.snapTo(target);
      m_dropPlaced = true;
    }
    m_dropHead.setTarget(target);
    m_dropTail.setTarget(target);
  }
  m_dropShown.setTarget(m_selected >= 0 ? 1.0f : 0.0f);
  m_dropHead.tick(step, reducedMotion);
  m_dropTail.tick(step, reducedMotion);
  m_dropShown.tick(step, reducedMotion);
  m_clock = std::fmod(m_clock + step, 60.0f);
  draw();
  return CanvasEditAction::None;
}

void
CanvasContextMenu::draw()
{
  const ColorRgba cyan = UiTheme::accentCool();
  const ColorRgba violet = UiTheme::accentViolet();
  // The card grows out of the pointer's corner on a bouncy spring while it
  // fades in; its rows fade in as it settles.
  const float pop = std::max(0.0f, m_pop.value());
  const float grow = 0.72f + 0.28f * pop;
  const float opacity = std::clamp(pop * 2.0f, 0.0f, 1.0f);
  const float contentReveal = std::clamp((pop - 0.45f) / 0.4f, 0.0f, 1.0f);
  const float cardWidth = m_width * grow;
  const float cardHeight = m_height * grow;
  const float cardX = m_originX + (m_x - m_originX) * grow;
  const float cardY = m_originY + (m_y - m_originY) * grow;

  m_panelKey.begin().add(
    { cardX, cardY, cardWidth, cardHeight, opacity, contentReveal });
  if (m_panelKey.changed()) {
    m_panelVisual.clearPrimitives();
    const float radius = std::min(kRadius, cardHeight * 0.5f);
    GuiKit::drawSoftShadow(m_panelVisual,
                           cardX,
                           cardY,
                           cardWidth,
                           cardHeight,
                           radius,
                           16.0f,
                           6.0f,
                           UiTheme::fade(UiTheme::glowShadow(), opacity));
    GuiKit::drawRoundedGradientRect(
      m_panelVisual,
      cardX,
      cardY,
      cardWidth,
      cardHeight,
      radius,
      UiTheme::fade(UiTheme::mix(UiTheme::glassRim(), cyan, 0.35f), opacity),
      UiTheme::fade(UiTheme::mix(UiTheme::glassRim(), violet, 0.35f), opacity));
    GuiKit::drawRoundedGradientRect(
      m_panelVisual,
      cardX + 1.0f,
      cardY + 1.0f,
      cardWidth - 2.0f,
      cardHeight - 2.0f,
      std::max(0.0f, radius - 1.0f),
      UiTheme::fade(UiTheme::glassTop(), opacity),
      UiTheme::fade(UiTheme::glassBottom(), opacity));
    if (cardWidth - radius * 2.0f > 2.0f) {
      const ColorRgba crownCyan = UiTheme::fade(cyan, 0.8f * opacity);
      const ColorRgba crownViolet = UiTheme::fade(violet, 0.8f * opacity);
      m_panelVisual.addGradientRect(cardX + radius,
                                    cardY + 1.0f,
                                    cardWidth - radius * 2.0f,
                                    1.5f,
                                    crownCyan,
                                    crownViolet,
                                    crownViolet,
                                    crownCyan);
    }
    // Hairlines between the row groups, fading toward the card's sides.
    for (const Row& row : m_rows) {
      if (!row.separatorBefore || contentReveal <= 0.0f) {
        continue;
      }
      const float lineY = row.y - kSeparatorHeight * 0.5f;
      const float lineX = m_x + kPad + kLabelPad * 0.5f;
      const float lineWidth = m_width - (kPad + kLabelPad * 0.5f) * 2.0f;
      const ColorRgba line = UiTheme::fade(UiTheme::divider(), contentReveal);
      m_panelVisual.addGradientRect(lineX,
                                    lineY,
                                    lineWidth * 0.5f,
                                    1.0f,
                                    UiTheme::transparentOf(line),
                                    line,
                                    line,
                                    UiTheme::transparentOf(line));
      m_panelVisual.addGradientRect(lineX + lineWidth * 0.5f,
                                    lineY,
                                    lineWidth * 0.5f,
                                    1.0f,
                                    line,
                                    UiTheme::transparentOf(line),
                                    UiTheme::transparentOf(line),
                                    line);
    }
  }
  m_panelVisual.setVisible(true);

  // The hovered row's liquid drop: the head pours to the new row and the
  // tail sloshes after it.
  m_dropVisual.clearPrimitives();
  const float dropAlpha =
    std::clamp(m_dropShown.value(), 0.0f, 1.0f) * contentReveal;
  if (dropAlpha > 0.01f && m_dropPlaced) {
    const bool destructive =
      m_selected >= 0 &&
      m_rows[static_cast<std::size_t>(m_selected)].destructive;
    const ColorRgba rimTop = destructive ? UiTheme::error() : cyan;
    const ColorRgba rimBottom =
      destructive ? UiTheme::mix(UiTheme::error(), violet, 0.4f) : violet;
    GuiLiquidSelection drop;
    drop.horizontal = false;
    drop.crossStart = m_x + kPad;
    drop.crossSize = m_width - kPad * 2.0f;
    drop.headStart = m_dropHead.value();
    drop.tailStart = m_dropTail.value();
    drop.cellLength = kRowHeight;
    drop.radius = 8.0f;
    drop.glowSpread = 10.0f;
    drop.glow = UiTheme::fade(rimTop, 0.22f * dropAlpha);
    drop.glowBottom = UiTheme::fade(rimBottom, 0.22f * dropAlpha);
    drop.rim = UiTheme::fade(rimTop, 0.75f * dropAlpha);
    drop.rimBottom = UiTheme::fade(rimBottom, 0.75f * dropAlpha);
    drop.faceTop = UiTheme::fade(
      destructive
        ? UiTheme::mix(UiTheme::selectionTop(), UiTheme::error(), 0.5f)
        : UiTheme::selectionTop(),
      dropAlpha);
    drop.faceBottom = UiTheme::fade(UiTheme::selectionBottom(), dropAlpha);
    GuiKit::drawLiquidSelection(m_dropVisual, drop);
  }
  m_dropVisual.setVisible(true);

  // Icons and labels lean in together with their emphasis; shortcuts sit
  // right as keycaps.
  m_labelVisual.clearPrimitives();
  if (contentReveal > 0.0f) {
    const unsigned char contentOpacity =
      static_cast<unsigned char>(std::lround(255.0f * contentReveal));
    for (int index = 0; index < rowCount(); ++index) {
      const Row& row = m_rows[static_cast<std::size_t>(index)];
      const float emphasis = std::max(0.0f, m_emphasis.value(index));
      const float lit = std::min(1.0f, emphasis);
      const float centerY = row.y + kRowHeight * 0.5f;
      const ColorRgba hot = row.destructive ? ColorRgba{ 255, 188, 194, 255 }
                                            : UiTheme::textPrimary();
      const ColorRgba tint = row.destructive ? UiTheme::error() : cyan;
      const float contentX = m_x + kPad + kLabelPad + 3.0f * emphasis;
      CanvasEditIcons::draw(
        m_labelVisual,
        row.action,
        contentX + kIconSize * 0.5f,
        centerY,
        kIconSize * (1.0f + 0.1f * lit),
        UiTheme::fade(UiTheme::mix(UiTheme::textMuted(),
                                   UiTheme::mix(hot, tint, 0.5f),
                                   lit),
                      contentReveal),
        m_brushColor);
      GuiKit::drawEmphasizedText(
        m_labelVisual,
        row.label,
        contentX + kIconSize + kIconGap,
        centerY - kFontSize * 0.5f,
        kFontSize,
        UiTheme::fade(UiTheme::mix(UiTheme::textSecondary(), hot, lit),
                      contentReveal),
        emphasis);
      const float keyRight = m_x + m_width - kPad - kLabelPad;
      if (!row.shortcut.empty()) {
        GuiKit::drawKeycap(m_labelVisual,
                           keyRight - keycapWidth(row.shortcut),
                           centerY - kKeySize * 1.75f * 0.5f - 0.75f,
                           row.shortcut,
                           kKeySize,
                           contentOpacity);
      }
    }
  }
  m_labelVisual.setVisible(true);
}
