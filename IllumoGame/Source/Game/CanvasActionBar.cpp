#include "CanvasActionBar.h"
#include "CSimSounds.h"
#include "CanvasChromeStyle.h"
#include "CanvasEditIcons.h"

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiPointerHint.h>
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

// The bar's pop and jelly squash: a scale about its centre applied to what
// is drawn, never to where buttons are hit.
struct CanvasActionBarDeform
{
  float pivotX = 0.0f;
  float pivotY = 0.0f;
  float scaleX = 1.0f;
  float scaleY = 1.0f;

  float x(float value) const { return pivotX + (value - pivotX) * scaleX; }
  float y(float value) const { return pivotY + (value - pivotY) * scaleY; }
};

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
    { CanvasEditAction::ResetCanvas, "Reset", false, true },
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
    button.hover.configure(GuiMotion::kBoing);
    button.squish.configure(GuiMotion::kBoing);
    button.iconPop.configure(GuiMotion::kBoing);
  }
  m_group.configure(GuiMotion::kBoing);
  m_chipPop.configure(GuiMotion::kBoing);
  m_pop.configure(GuiMotion::kBoing);
  m_barHover.configure(GuiMotion::kBoing);
  m_visual.setVisible(false);
  m_haloVisual.setVisible(false);
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
  m_haloVisual.setRenderer(renderer);
  m_haloVisual.setWindow(window);
  m_haloVisual.setSpace(PrimitiveSpace::Pixels);
  m_haloVisual.setLayerHint(RenderLayerId::UI);
  m_haloVisual.prepare(renderer);
  m_haloVisual.setVisible(false);
  m_haloKey.invalidate();
  m_shown = false;
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
  m_haloVisual.setVisible(false);
  // It pops back in from nothing when it next appears.
  m_shown = false;
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
  ILLUMO_PROFILE_ZONE("CanvasActionBar.update");
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
  // Appearing pops the bar in from nothing, as the settings button does.
  if (!m_shown) {
    m_shown = true;
    m_pop.snapTo(0.0f);
    m_barHover.snapTo(0.0f);
  }

  // Hover and presses test the bar as it was laid out last frame.
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
  if (hovered >= 0 && hovered != m_hovered) {
    // The newly hovered button's icon hops, and the bar gives a small
    // sympathetic wobble.
    m_buttons[static_cast<std::size_t>(hovered)].iconPop.kick(7.0f);
    m_pop.kick(0.6f);
    if (!leftDown) {
      CSimSounds::play(CSimSound::MenuHover);
    }
  }
  m_hovered = hovered;
  if (hovered >= 0) {
    GuiPointerHint::markInteractive();
  }
  CanvasEditAction action = CanvasEditAction::None;
  if (hovered >= 0 && leftClicked) {
    Button& pressed = m_buttons[static_cast<std::size_t>(hovered)];
    action = pressed.action;
    // The pressed button flattens, then boings back with its icon hopping,
    // and the whole bar jiggles.
    pressed.squish.kick(10.0f);
    pressed.iconPop.kick(10.0f);
    m_pop.kick(-3.0f);
    CSimSounds::play(CSimSound::MenuSelect);
  }
  m_pop.setTarget(1.0f);
  m_pop.tick(step, state.reducedMotion);
  m_barHover.setTarget(m_pointerOver ? 1.0f : 0.0f);
  m_barHover.tick(step, state.reducedMotion);
  m_clock = std::fmod(m_clock + static_cast<double>(step), 60.0);
  const bool pressHeld = hovered >= 0 && leftDown;

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
    button.iconPop.tick(step, state.reducedMotion);
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
  m_haloVisual.setVisible(true);

  // Jelly: the bar pops in from nothing, swells a little while the pointer
  // is over it, stretches tall as it grows and bulges wide as it recoils; a
  // held press squashes it. The long bar gives way less across than up and
  // down. This deforms the drawing about the bar's centre, never the layout.
  const float pop = std::max(0.0f, m_pop.value());
  const float barHover = std::clamp(m_barHover.value(), 0.0f, 1.0f);
  const float squash = CanvasChromeStyle::squash(
    m_pop.velocity(), m_barHover.velocity(), pressHeld);
  const float grow = pop * (1.0f + 0.03f * m_barHover.value());
  CanvasActionBarDeform deform;
  deform.pivotX = barX + barWidth * 0.5f;
  deform.pivotY = barY + barHeight * 0.5f;
  deform.scaleX = grow * (1.0f + 0.35f * squash);
  deform.scaleY = grow * (1.0f - squash);
  const float drawnX = deform.x(barX);
  const float drawnY = deform.y(barY);
  const float drawnWidth = barWidth * deform.scaleX;
  const float drawnHeight = barHeight * deform.scaleY;
  const bool barShown = drawnWidth > 2.0f && drawnHeight > 2.0f;
  const float radius = drawnHeight * 0.5f;

  // Beneath the bar: a soft drop shadow and a violet-to-cyan halo that
  // breathes at rest and blooms while the pointer is over the bar.
  const float breathe =
    CanvasChromeStyle::breathe(static_cast<float>(m_clock), state.reducedMotion);
  m_haloKey.begin().add({ barShown ? 1.0f : 0.0f,
                          drawnX,
                          drawnY,
                          drawnWidth,
                          drawnHeight,
                          fit,
                          barHover,
                          breathe,
                          std::min(1.0f, pop) });
  if (m_haloKey.changed()) {
    ILLUMO_PROFILE_ZONE("CanvasActionBar.rebuildHalo");
    m_haloVisual.clearPrimitives();
    if (barShown) {
      GuiKit::drawSoftShadow(m_haloVisual,
                             drawnX,
                             drawnY,
                             drawnWidth,
                             drawnHeight,
                             radius,
                             12.0f * fit,
                             (4.0f + 2.0f * barHover) * fit,
                             UiTheme::glowShadow());
      const ColorRgba halo = CanvasChromeStyle::halo(barHover);
      GuiKit::drawRoundedBand(
        m_haloVisual,
        drawnX,
        drawnY,
        drawnWidth,
        drawnHeight,
        radius,
        0.0f,
        CanvasChromeStyle::haloWidth(breathe, barHover) * fit,
        UiTheme::fade(halo,
                      CanvasChromeStyle::haloOpacity(breathe, barHover) *
                        std::min(1.0f, pop)),
        UiTheme::transparentOf(halo));
    }
  }

  // The drawing depends only on these inputs; an idle bar keeps its
  // primitives and sends nothing.
  m_key.begin().add({ drawnX,
                      drawnY,
                      drawnWidth,
                      drawnHeight,
                      fit,
                      groupOpen,
                      chipOpacity,
                      m_chipPop.value(),
                      state.interactive ? 1.0f : 0.0f,
                      static_cast<float>(state.brushColor.r),
                      static_cast<float>(state.brushColor.g),
                      static_cast<float>(state.brushColor.b),
                      static_cast<float>(m_chipRevision),
                      barHover });
  for (int index = 0; index < kButtonCount; ++index) {
    const Button& button = m_buttons[static_cast<std::size_t>(index)];
    m_key.add({ button.hover.value(),
                button.hover.velocity(),
                button.squish.value(),
                button.iconPop.value(),
                opacity[static_cast<std::size_t>(index)],
                button.pressable ? 1.0f : 0.0f,
                pressHeld && index == hovered ? 1.0f : 0.0f });
  }
  if (!m_key.changed()) {
    return action;
  }
  m_visual.clearPrimitives();
  if (!barShown) {
    return action;
  }

  const ColorRgba cyan = UiTheme::accentCool();
  const ColorRgba violet = UiTheme::accentViolet();
  // The settings button's tile: a rim lit teal at the top and indigo at the
  // bottom around a deep teal-to-indigo face, brightening on hover, crowned
  // by a cyan-to-violet hairline.
  GuiKit::drawRoundedGradientRect(m_visual,
                                  drawnX,
                                  drawnY,
                                  drawnWidth,
                                  drawnHeight,
                                  radius,
                                  CanvasChromeStyle::rimTop(barHover),
                                  CanvasChromeStyle::rimBottom(barHover));
  GuiKit::drawRoundedGradientRect(m_visual,
                                  drawnX + 1.0f,
                                  drawnY + 1.0f,
                                  drawnWidth - 2.0f,
                                  drawnHeight - 2.0f,
                                  std::max(0.0f, radius - 1.0f),
                                  CanvasChromeStyle::faceTop(barHover),
                                  CanvasChromeStyle::faceBottom(barHover));
  if (drawnWidth - radius * 2.0f > 2.0f) {
    const float crown = 0.55f + 0.45f * barHover;
    const ColorRgba crownCyan = UiTheme::fade(cyan, crown);
    const ColorRgba crownViolet = UiTheme::fade(violet, crown);
    m_visual.addGradientRect(drawnX + radius,
                             drawnY + 1.0f,
                             drawnWidth - radius * 2.0f,
                             1.5f,
                             crownCyan,
                             crownViolet,
                             crownViolet,
                             crownCyan);
  }

  // A hairline divides the base buttons from the selection group.
  if (chipOpacity > 0.0f) {
    const ColorRgba line = UiTheme::fade(UiTheme::divider(), chipOpacity);
    m_visual.addGradientRect(deform.x(dividerX),
                             deform.y(barY + 7.0f * fit),
                             1.0f,
                             (barHeight - 14.0f * fit) * deform.scaleY,
                             line,
                             line,
                             UiTheme::transparentOf(line),
                             UiTheme::transparentOf(line));
    // The selection's size in a small cyan chip that pops as it changes.
    const float chipScale = 1.0f + 0.12f * m_chipPop.value();
    const float chipHeight = kChipHeight * fit * chipScale * deform.scaleY;
    const float chipDrawnWidth =
      chipWidth * fit * chipScale * deform.scaleX;
    const float chipCenterX = deform.x(chipX + chipWidth * fit * 0.5f);
    const float chipCenterY = deform.y(barY + barHeight * 0.5f);
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
                                       kChipFontSize * fit * chipScale *
                                         deform.scaleY,
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
    const bool held = pressHeld && index == hovered;
    // Each button is a little settings-button tile: hovered, it swells past
    // its size and wobbles back, stretching tall as it grows and bulging
    // wide as it recoils; a held press squashes it flat and sinks it, and
    // the click's kick boings it back. Hover lifts it at most a couple of
    // pixels. Only the drawing moves: it is hit at its rest rectangle.
    const float jelly = std::clamp(
      CanvasChromeStyle::squash(0.0f, button.hover.velocity(), held) +
        std::clamp(button.squish.value(), -0.3f, 0.3f) * 0.5f,
      -0.2f,
      0.2f);
    const float grow = 1.0f + 0.1f * hover;
    const float iconPop = std::max(-0.5f, button.iconPop.value());
    const float faceWidth =
      button.width * deform.scaleX * grow * (1.0f + jelly * 0.6f);
    const float faceHeight =
      button.height * deform.scaleY * grow * (1.0f - jelly);
    const float centerX = deform.x(button.x + button.width * 0.5f);
    const float centerY = deform.y(button.y + button.height * 0.5f) -
                          1.5f * fit * lit + (held ? 1.0f * fit : 0.0f);
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
      // A lit tile of the bar's own teal-to-indigo, leaning violet at the
      // bottom; destructive buttons warm toward red.
      const ColorRgba litTop =
        UiTheme::mix(CanvasChromeStyle::faceTop(1.0f), cyan, 0.3f);
      const ColorRgba litBottom =
        UiTheme::mix(CanvasChromeStyle::faceBottom(1.0f), violet, 0.45f);
      const ColorRgba top =
        button.destructive ? UiTheme::mix(litTop, UiTheme::error(), 0.55f)
                           : litTop;
      const ColorRgba bottom =
        button.destructive ? UiTheme::mix(litBottom, UiTheme::error(), 0.4f)
                           : litBottom;
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
    // The action's icon leads the label and warms toward the button's tint
    // on hover. Both grow and squash with the tile, and the icon hops up and
    // swells as the pointer arrives or the button is pressed.
    const float contentScale = grow * (1.0f - jelly * 0.5f);
    const float fontSize = kFontSize * fit * deform.scaleY * contentScale;
    const float iconSize = kIconSize * fit * deform.scaleY * contentScale;
    const float iconGap = kIconGap * fit * deform.scaleX * grow;
    const float labelWidth =
      GuiKit::measureEmphasizedText(button.label, fontSize, 1.0f);
    const float contentWidth = iconSize + iconGap + labelWidth;
    const float contentX = centerX - contentWidth * 0.5f;
    const ColorRgba iconColor = UiTheme::fade(
      UiTheme::mix(restText, UiTheme::mix(hotText, tint, 0.5f), lit), shown);
    CanvasEditIcons::draw(m_visual,
                          button.action,
                          contentX + iconSize * 0.5f,
                          centerY - 3.0f * fit * std::max(0.0f, iconPop),
                          iconSize * (1.0f + 0.1f * lit + 0.3f * iconPop),
                          iconColor,
                          state.brushColor);
    GuiKit::drawEmphasizedTextCentered(m_visual,
                                       button.label,
                                       contentX + iconSize + iconGap +
                                         labelWidth * 0.5f,
                                       centerY,
                                       fontSize,
                                       textColor,
                                       hover);
  }
  return action;
}
