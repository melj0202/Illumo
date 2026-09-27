#include "CanvasActionBar.h"
#include "CSimSounds.h"
#include "CanvasEditIcons.h"

#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <cmath>

// Rest metrics in virtual pixels; a narrow window scales them all by one fit.
static const float kFontSize = 12.0f;
static const float kButtonHeight = 26.0f;
static const float kButtonPad = 11.0f;
static const float kButtonGap = 2.0f;
static const float kBarPad = 4.0f;
static const float kDividerSpace = 13.0f;
static const float kChipFontSize = 10.5f;
static const float kChipPad = 8.0f;
static const float kChipHeight = 18.0f;
// Every button leads with its action's icon.
static const float kIconSize = 12.0f;
static const float kIconGap = 6.0f;
// Room kept clear at both ends of the top edge: the mode badge on the left,
// the settings button on the right.
static const float kSideClearance = 56.0f;
static const float kMinimumFit = 0.55f;

// Selection-group items appear one by one as the bar grows over them: each
// fades in with the share of it the bar already covers (eased, so a part
// still outside the bar stays faint), and they vanish in reverse as it folds
// away.
static float
groupItemReveal(float innerRight, float itemX, float itemWidth)
{
  if (itemWidth <= 0.0f) {
    return 0.0f;
  }
  const float covered =
    std::clamp((innerRight - itemX) / itemWidth, 0.0f, 1.0f);
  return covered * covered;
}

CanvasActionBar::CanvasActionBar()
  : m_visual(2048u)
{
  const struct
  {
    CanvasEditAction action;
    const char* label;
    bool selectionOnly;
    bool destructive;
  } definitions[kButtonCount] = {
    { CanvasEditAction::Save, "Save", false, false },
    { CanvasEditAction::Load, "Load", false, false },
    { CanvasEditAction::Paste, "Paste", false, false },
    { CanvasEditAction::ClearCanvas, "Clear", false, true },
    { CanvasEditAction::Copy, "Copy", true, false },
    { CanvasEditAction::Cut, "Cut", true, false },
    { CanvasEditAction::Fill, "Fill", true, false },
    { CanvasEditAction::Erase, "Erase", true, true },
    { CanvasEditAction::Deselect, "Deselect", true, false },
  };
  for (int index = 0; index < kButtonCount; ++index) {
    Button& button = m_buttons[static_cast<std::size_t>(index)];
    button.action = definitions[index].action;
    button.label = definitions[index].label;
    button.selectionOnly = definitions[index].selectionOnly;
    button.destructive = definitions[index].destructive;
    button.hover.configure(GuiMotion::kJelly);
    button.squish.configure(GuiMotion::kJelly);
  }
  m_group.configure(GuiMotion::kJelly);
  m_chipPop.configure(GuiMotion::kBoing);
  m_visual.setVisible(false);
}

void
CanvasActionBar::prepare(IRenderWindow* window, Renderer* renderer)
{
  m_window = window;
  m_renderer = renderer;
  m_visual.setRenderer(renderer);
  m_visual.setWindow(window);
  m_visual.setSpace(PrimitiveSpace::Pixels);
  m_visual.setLayerHint(RenderLayerId::UI);
  m_visual.prepare(renderer);
  m_visual.setVisible(false);
  m_key.invalidate();
}

float
CanvasActionBar::uiScale() const
{
  const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
  return scale > 0.0f ? scale : 1.0f;
}

void
CanvasActionBar::hide()
{
  m_visual.setVisible(false);
  m_pointerOver = false;
  m_hovered = -1;
  m_barDrawn = false;
  for (Button& button : m_buttons) {
    button.pressable = false;
  }
}

bool
CanvasActionBar::buttonCenter(CanvasEditAction action, float* x, float* y) const
{
  const float scale = uiScale();
  for (const Button& button : m_buttons) {
    if (button.action != action || !button.pressable) {
      continue;
    }
    if (x != nullptr) {
      *x = (button.x + button.width * 0.5f) * scale;
    }
    if (y != nullptr) {
      *y = (button.y + button.height * 0.5f) * scale;
    }
    return true;
  }
  return false;
}

CanvasEditAction
CanvasActionBar::update(const CanvasActionBarState& state,
                        InputManager* input,
                        float deltaSeconds,
                        bool acceptInput)
{
  const float step = std::isfinite(deltaSeconds) && deltaSeconds > 0.0f
                       ? std::min(deltaSeconds, 0.1f)
                       : 0.0f;
  const bool leftDown =
    input != nullptr && input->isMouseButtonPressed(KeyCode::MouseLeft);
  const bool leftClicked = leftDown && !m_leftWasDown;
  m_leftWasDown = leftDown;
  if (m_window == nullptr || state.lift <= 0.001f) {
    hide();
    return CanvasEditAction::None;
  }

  // Hover and presses test the bar as it was drawn last frame.
  const float scale = uiScale();
  const std::array<double, 2> mouse = m_window->getMouseCoords();
  const float mx = static_cast<float>(mouse[0]) / scale;
  const float my = static_cast<float>(mouse[1]) / scale;
  m_pointerOver =
    state.interactive && m_barDrawn &&
    GuiKit::isPointInRect(mx, my, m_barX, m_barY, m_barWidth, m_barHeight);
  int hovered = -1;
  if (m_pointerOver && acceptInput) {
    for (int index = 0; index < kButtonCount; ++index) {
      const Button& button = m_buttons[static_cast<std::size_t>(index)];
      if (button.pressable &&
          GuiKit::isPointInRect(
            mx, my, button.x, button.y, button.width, button.height)) {
        hovered = index;
      }
    }
  }
  if (hovered >= 0 && hovered != m_hovered && !leftDown) {
    CSimSounds::play(CSimSound::MenuHover);
  }
  m_hovered = hovered;
  CanvasEditAction action = CanvasEditAction::None;
  if (hovered >= 0 && leftClicked) {
    Button& pressed = m_buttons[static_cast<std::size_t>(hovered)];
    action = pressed.action;
    // The pressed button flattens, then jiggles back.
    pressed.squish.kick(6.0f);
    CSimSounds::play(CSimSound::MenuSelect);
  }

  m_group.setTarget(state.selection ? 1.0f : 0.0f);
  m_group.tick(step, state.reducedMotion);
  // The size chip keeps its last label while the group folds away, and pops
  // whenever the selection changes shape.
  if (state.selection && state.selectionLabel != m_chipLabel) {
    if (!m_chipLabel.empty() && m_group.value() > 0.5f) {
      m_chipPop.kick(8.0f);
    }
    m_chipLabel = state.selectionLabel;
    ++m_chipRevision;
  }
  m_chipPop.tick(step, state.reducedMotion);
  for (int index = 0; index < kButtonCount; ++index) {
    Button& button = m_buttons[static_cast<std::size_t>(index)];
    button.hover.setTarget(index == hovered ? 1.0f : 0.0f);
    button.hover.tick(step, state.reducedMotion);
    button.squish.tick(step, state.reducedMotion);
  }

  // Rest layout: the base group, then the selection group (divider, size
  // chip and its buttons), measured at full emphasis so hover never widens a
  // button past its slot.
  std::array<float, kButtonCount> widths{};
  float baseWidth = 0.0f;
  float groupWidth = kDividerSpace;
  for (int index = 0; index < kButtonCount; ++index) {
    const Button& button = m_buttons[static_cast<std::size_t>(index)];
    const float width =
      GuiKit::measureEmphasizedText(button.label, kFontSize, 1.0f) + kIconSize +
      kIconGap + kButtonPad * 2.0f;
    widths[static_cast<std::size_t>(index)] = width;
    if (button.selectionOnly) {
      groupWidth += width + kButtonGap;
    } else {
      baseWidth += width + kButtonGap;
    }
  }
  baseWidth -= kButtonGap;
  const float chipWidth =
    GuiKit::measureEmphasizedText(m_chipLabel, kChipFontSize, 0.6f) +
    kChipPad * 2.0f;
  groupWidth += chipWidth;
  const std::array<int, 2> dimensions = m_window->getWindowDimensions();
  const float virtualWidth = static_cast<float>(dimensions[0]) / scale;
  const float fullWidth = kBarPad * 2.0f + baseWidth + groupWidth;
  const float fit = std::clamp(
    (virtualWidth - kSideClearance * 2.0f) / fullWidth, kMinimumFit, 1.0f);
  // The group's spring overshoots, so the bar bulges past its width and
  // settles back as the buttons pour out.
  const float groupOpen = std::max(0.0f, m_group.value());
  const float barWidth =
    (kBarPad * 2.0f + baseWidth + groupWidth * groupOpen) * fit;
  const float barHeight = kHeight * fit;
  const float barX = virtualWidth * 0.5f - barWidth * 0.5f;
  // It rides the edit chrome's lift: hidden above the window at 0, bouncing
  // past its slot as the chrome springs in.
  const float hiddenY = -barHeight - 10.0f;
  const float barY = hiddenY + (kTop - hiddenY) * state.lift;
  const float innerRight = barX + barWidth - kBarPad * fit;

  // Place every button where it is drawn this frame; next frame's hover and
  // presses test these rectangles.
  std::array<float, kButtonCount> opacity{};
  float cursor = barX + kBarPad * fit;
  const float buttonY = barY + kBarPad * fit;
  for (int index = 0; index < kButtonCount; ++index) {
    Button& button = m_buttons[static_cast<std::size_t>(index)];
    if (button.selectionOnly) {
      continue;
    }
    button.x = cursor;
    button.y = buttonY;
    button.width = widths[static_cast<std::size_t>(index)] * fit;
    button.height = kButtonHeight * fit;
    button.pressable = state.interactive;
    opacity[static_cast<std::size_t>(index)] = 1.0f;
    cursor += button.width + kButtonGap * fit;
  }
  const float dividerX = cursor - kButtonGap * fit + kDividerSpace * fit * 0.5f;
  cursor += kDividerSpace * fit - kButtonGap * fit;
  const float chipX = cursor;
  const float chipOpacity = groupItemReveal(innerRight, chipX, chipWidth * fit);
  cursor += chipWidth * fit + kButtonGap * fit;
  for (int index = 0; index < kButtonCount; ++index) {
    Button& button = m_buttons[static_cast<std::size_t>(index)];
    if (!button.selectionOnly) {
      continue;
    }
    button.x = cursor;
    button.y = buttonY;
    button.width = widths[static_cast<std::size_t>(index)] * fit;
    button.height = kButtonHeight * fit;
    const float shown = groupItemReveal(innerRight, button.x, button.width);
    opacity[static_cast<std::size_t>(index)] = shown;
    button.pressable = state.interactive && state.selection && shown >= 0.99f &&
                       groupOpen > 0.9f;
    cursor += button.width + kButtonGap * fit;
  }
  m_barX = barX;
  m_barY = barY;
  m_barWidth = barWidth;
  m_barHeight = barHeight;
  m_barDrawn = true;
  m_visual.setVisible(true);

  // The drawing depends only on these inputs; an idle bar keeps its
  // primitives and sends nothing.
  m_key.begin().add({ barX,
                      barY,
                      barWidth,
                      fit,
                      groupOpen,
                      chipOpacity,
                      m_chipPop.value(),
                      state.interactive ? 1.0f : 0.0f,
                      static_cast<float>(state.brushColor.r),
                      static_cast<float>(state.brushColor.g),
                      static_cast<float>(state.brushColor.b),
                      static_cast<float>(m_chipRevision) });
  for (int index = 0; index < kButtonCount; ++index) {
    const Button& button = m_buttons[static_cast<std::size_t>(index)];
    m_key.add({ button.hover.value(),
                button.squish.value(),
                opacity[static_cast<std::size_t>(index)],
                button.pressable ? 1.0f : 0.0f });
  }
  if (!m_key.changed()) {
    return action;
  }
  m_visual.clearPrimitives();

  const ColorRgba cyan = UiTheme::accentCool();
  const ColorRgba violet = UiTheme::accentViolet();
  const float radius = barHeight * 0.5f;
  GuiKit::drawSoftShadow(m_visual,
                         barX,
                         barY,
                         barWidth,
                         barHeight,
                         radius,
                         12.0f * fit,
                         4.0f * fit,
                         UiTheme::glowShadow());
  // A lit rim, cyan along the top and violet along the bottom, around a
  // glass face, crowned by the menus' cyan-to-violet hairline.
  GuiKit::drawRoundedGradientRect(
    m_visual,
    barX,
    barY,
    barWidth,
    barHeight,
    radius,
    UiTheme::mix(UiTheme::glassRim(), cyan, 0.4f),
    UiTheme::mix(UiTheme::glassRim(), violet, 0.4f));
  GuiKit::drawRoundedGradientRect(m_visual,
                                  barX + 1.0f,
                                  barY + 1.0f,
                                  barWidth - 2.0f,
                                  barHeight - 2.0f,
                                  std::max(0.0f, radius - 1.0f),
                                  UiTheme::glassTop(),
                                  UiTheme::glassBottom());
  if (barWidth - radius * 2.0f > 2.0f) {
    const ColorRgba crownCyan = UiTheme::fade(cyan, 0.7f);
    const ColorRgba crownViolet = UiTheme::fade(violet, 0.7f);
    m_visual.addGradientRect(barX + radius,
                             barY + 1.0f,
                             barWidth - radius * 2.0f,
                             1.5f,
                             crownCyan,
                             crownViolet,
                             crownViolet,
                             crownCyan);
  }

  // A hairline divides the base buttons from the selection group.
  if (chipOpacity > 0.0f) {
    const ColorRgba line = UiTheme::fade(UiTheme::divider(), chipOpacity);
    m_visual.addGradientRect(dividerX,
                             barY + 7.0f * fit,
                             1.0f,
                             barHeight - 14.0f * fit,
                             line,
                             line,
                             UiTheme::transparentOf(line),
                             UiTheme::transparentOf(line));
    // The selection's size in a small cyan chip that pops as it changes.
    const float pop = 1.0f + 0.12f * m_chipPop.value();
    const float chipHeight = kChipHeight * fit * pop;
    const float chipDrawnWidth = chipWidth * fit * pop;
    const float chipCenterX = chipX + chipWidth * fit * 0.5f;
    const float chipCenterY = barY + barHeight * 0.5f;
    GuiKit::drawRoundedRect(m_visual,
                            chipCenterX - chipDrawnWidth * 0.5f,
                            chipCenterY - chipHeight * 0.5f,
                            chipDrawnWidth,
                            chipHeight,
                            chipHeight * 0.5f,
                            UiTheme::fade(cyan, 0.16f * chipOpacity));
    GuiKit::drawEmphasizedTextCentered(m_visual,
                                       m_chipLabel,
                                       chipCenterX,
                                       chipCenterY,
                                       kChipFontSize * fit * pop,
                                       UiTheme::fade(cyan, chipOpacity),
                                       0.6f);
  }

  for (int index = 0; index < kButtonCount; ++index) {
    const Button& button = m_buttons[static_cast<std::size_t>(index)];
    const float shown = opacity[static_cast<std::size_t>(index)];
    if (shown <= 0.0f) {
      continue;
    }
    const float hover = std::max(0.0f, button.hover.value());
    const float lit = std::min(1.0f, hover);
    // Hover lifts a button at most a couple of pixels; a press flattens it
    // and it jiggles back. Neither moves its hit area.
    const float squish = std::clamp(button.squish.value(), -0.3f, 0.3f) * 0.5f;
    const float faceWidth = button.width * (1.0f + squish * 0.4f);
    const float faceHeight = button.height * (1.0f - squish);
    const float centerX = button.x + button.width * 0.5f;
    const float centerY = button.y + button.height * 0.5f - 1.5f * fit * lit;
    const float faceX = centerX - faceWidth * 0.5f;
    const float faceY = centerY - faceHeight * 0.5f;
    const float faceRadius = faceHeight * 0.5f;
    const ColorRgba tint = button.destructive ? UiTheme::error() : cyan;
    if (lit > 0.01f) {
      GuiKit::drawSoftGlow(m_visual,
                           centerX,
                           centerY,
                           faceWidth * 0.7f,
                           faceHeight * 0.9f,
                           UiTheme::fade(tint, 0.16f * lit * shown));
      const ColorRgba top =
        button.destructive
          ? UiTheme::mix(UiTheme::selectionTop(), UiTheme::error(), 0.55f)
          : UiTheme::selectionTop();
      const ColorRgba bottom =
        button.destructive
          ? UiTheme::mix(UiTheme::selectionBottom(), UiTheme::error(), 0.4f)
          : UiTheme::selectionBottom();
      GuiKit::drawRoundedGradientRect(
        m_visual,
        faceX,
        faceY,
        faceWidth,
        faceHeight,
        faceRadius,
        UiTheme::fade(top, 0.9f * lit * shown),
        UiTheme::fade(bottom, 0.9f * lit * shown));
      GuiKit::drawRoundedOutline(m_visual,
                                 faceX,
                                 faceY,
                                 faceWidth,
                                 faceHeight,
                                 faceRadius,
                                 1.0f,
                                 UiTheme::fade(tint, 0.55f * lit * shown));
    }
    const ColorRgba restText = button.pressable || !state.interactive
                                 ? UiTheme::textSecondary()
                                 : UiTheme::textMuted();
    const ColorRgba hotText = button.destructive
                                ? ColorRgba{ 255, 188, 194, 255 }
                                : UiTheme::textPrimary();
    const ColorRgba textColor =
      UiTheme::fade(UiTheme::mix(restText, hotText, lit), shown);
    // The action's icon leads the label; it swells a little and warms
    // toward the button's tint on hover.
    const float fontSize = kFontSize * fit;
    const float labelWidth =
      GuiKit::measureEmphasizedText(button.label, fontSize, 1.0f);
    const float contentWidth = (kIconSize + kIconGap) * fit + labelWidth;
    const float contentX = centerX - contentWidth * 0.5f;
    const ColorRgba iconColor = UiTheme::fade(
      UiTheme::mix(restText, UiTheme::mix(hotText, tint, 0.5f), lit), shown);
    CanvasEditIcons::draw(m_visual,
                          button.action,
                          contentX + kIconSize * fit * 0.5f,
                          centerY,
                          kIconSize * fit * (1.0f + 0.1f * lit),
                          iconColor,
                          state.brushColor);
    GuiKit::drawEmphasizedTextCentered(m_visual,
                                       button.label,
                                       contentX + (kIconSize + kIconGap) * fit +
                                         labelWidth * 0.5f,
                                       centerY,
                                       fontSize,
                                       textColor,
                                       hover);
  }
  return action;
}
