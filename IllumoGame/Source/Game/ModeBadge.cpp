#include "ModeBadge.h"

#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <algorithm>
#include <cmath>
#include <memory>

// The bead drops in first, then stretches into the pill; retracting melts
// back into the bead before it floats away.
static const float kStretchDelaySeconds = 0.08f;
static const float kMeltedGrow = 0.25f;
static const float kLabelInset = 12.0f;
static const float kLabelTrailingPad = 14.0f;

ModeBadge::ModeBadge()
  : m_visual(1024u)
{
  m_grow.configure(GuiMotion::kBoing);
  m_drop.configure(GuiMotion::kJelly);
  m_visual.setVisible(false);
}

void
ModeBadge::prepare(IRenderWindow* window, Renderer* renderer)
{
  m_visual.setRenderer(renderer);
  m_visual.setWindow(window);
  m_visual.setSpace(PrimitiveSpace::Pixels);
  m_visual.setLayerHint(RenderLayerId::UI);
  m_visual.prepare(renderer);
  m_visual.setVisible(false);
}

void
ModeBadge::show(const std::string& label, ColorRgba accent, bool live)
{
  if (m_showing) {
    // Already on screen: keep the motion and let the pill wobble as its
    // label and color change.
    m_previousAccent = UiTheme::mix(m_previousAccent, m_accent, m_colorBlend);
    m_colorBlend = 0.0f;
    m_grow.kick(-4.0f);
  } else {
    m_grow.snapTo(0.0f);
    m_drop.snapTo(0.0f);
    m_previousAccent = accent;
    m_colorBlend = 1.0f;
  }
  m_label = label;
  m_accent = accent;
  m_live = live;
  m_elapsed = 0.0f;
  m_showing = true;
  rebuild();
}

void
ModeBadge::hide()
{
  m_showing = false;
  m_grow.snapTo(0.0f);
  m_drop.snapTo(0.0f);
  m_visual.clearPrimitives();
  m_visual.setVisible(false);
}

float
ModeBadge::fullWidth() const
{
  const std::shared_ptr<Font> font = Font::getDefaultFont();
  const float labelWidth = font != nullptr
                             ? font->measureText(m_label, kLabelSize).width
                             : GuiKit::estimateTextWidth(m_label, kLabelSize);
  return kHeight * 0.5f + kLabelInset + labelWidth + kLabelTrailingPad;
}

void
ModeBadge::tick(float deltaSeconds, bool reducedMotion)
{
  m_reducedMotion = reducedMotion;
  if (!m_showing) {
    return;
  }
  const float step =
    std::isfinite(deltaSeconds) ? std::clamp(deltaSeconds, 0.0f, 0.1f) : 0.0f;
  m_elapsed += step;
  m_ambient = std::fmod(m_ambient + step, 12.0f);
  m_colorBlend =
    reducedMotion
      ? 1.0f
      : m_colorBlend + (1.0f - m_colorBlend) * (1.0f - std::exp(-10.0f * step));
  if (m_elapsed < kHoldSeconds) {
    m_drop.setTarget(1.0f);
    m_grow.setTarget(m_elapsed >= kStretchDelaySeconds || reducedMotion ? 1.0f
                                                                        : 0.0f);
  } else {
    m_grow.setTarget(0.0f);
    m_drop.setTarget(m_grow.value() > kMeltedGrow && !reducedMotion ? 1.0f
                                                                    : 0.0f);
  }
  m_grow.tick(step, reducedMotion);
  m_drop.tick(step, reducedMotion);
  if (m_elapsed >= kHoldSeconds && m_drop.settled() && m_drop.value() == 0.0f) {
    hide();
    return;
  }
  rebuild();
}

void
ModeBadge::rebuild()
{
  m_visual.clearPrimitives();
  if (!m_showing) {
    m_visual.setVisible(false);
    return;
  }
  const float grow = m_grow.value();
  const float drop = m_drop.value();
  // Jelly: a pill stretched past its width thins, a squeezed one fattens.
  const float stretch = std::clamp(grow - 1.0f, -0.4f, 0.4f);
  const float height = kHeight * (1.0f - (grow > 0.5f ? 0.3f * stretch : 0.0f));
  const float width =
    std::max(kHeight * 0.7f, kHeight + (fullWidth() - kHeight) * grow);
  const float x = kMargin;
  const float y = kMargin + (kHeight - height) * 0.5f -
                  (1.0f - drop) * (kHeight + kMargin + 8.0f);
  const float radius = std::max(0.0f, std::min(width, height) * 0.5f - 0.5f);
  const unsigned char opacity = static_cast<unsigned char>(
    std::round(255.0f * std::clamp(drop * 1.4f, 0.0f, 1.0f)));
  const ColorRgba accent =
    UiTheme::mix(m_previousAccent, m_accent, m_colorBlend);
  // A running simulation makes the status dot breathe.
  const float breathe =
    m_live && !m_reducedMotion
      ? 0.5f + 0.5f * std::sin(m_ambient * 6.28318531f / 1.5f)
      : 1.0f;

  GuiKit::drawSoftShadow(m_visual,
                         x,
                         y,
                         width,
                         height,
                         radius,
                         10.0f,
                         3.0f,
                         UiTheme::applyOpacity(UiTheme::glowShadow(), opacity));
  GuiKit::drawRoundedBand(
    m_visual,
    x,
    y,
    width,
    height,
    radius,
    0.0f,
    8.0f + 4.0f * breathe,
    UiTheme::applyOpacity(UiTheme::fade(accent, 0.16f + 0.14f * breathe),
                          opacity),
    UiTheme::transparentOf(accent));
  GuiKit::drawRoundedRect(
    m_visual,
    x,
    y,
    width,
    height,
    radius,
    UiTheme::applyOpacity(UiTheme::mix(UiTheme::glassRim(), accent, 0.6f),
                          opacity));
  GuiKit::drawRoundedGradientRect(
    m_visual,
    x + 1.0f,
    y + 1.0f,
    width - 2.0f,
    height - 2.0f,
    std::max(0.0f, radius - 1.0f),
    UiTheme::applyOpacity(UiTheme::glassTop(), opacity),
    UiTheme::applyOpacity(UiTheme::glassBottom(), opacity));

  // The status dot sits where the bead was, so the bead is a glowing drop.
  const float dotX = x + kHeight * 0.5f;
  const float dotY = y + height * 0.5f;
  const float dotRadius = 4.0f + (m_live ? 0.8f * breathe : 0.0f);
  GuiKit::drawSoftGlow(
    m_visual,
    dotX,
    dotY,
    12.0f,
    12.0f,
    UiTheme::applyOpacity(UiTheme::fade(accent, 0.35f + 0.3f * breathe),
                          opacity),
    16);
  m_visual.addFilledEllipse(dotX - dotRadius,
                            dotY - dotRadius,
                            dotRadius * 2.0f,
                            dotRadius * 2.0f,
                            UiTheme::applyOpacity(accent, opacity));

  // The label fades in once the pill has room for it and out as it melts.
  const float labelReveal = std::clamp((grow - 0.75f) / 0.2f, 0.0f, 1.0f);
  if (labelReveal > 0.01f) {
    const unsigned char labelOpacity = static_cast<unsigned char>(
      std::round(static_cast<float>(opacity) * labelReveal));
    m_visual.addText(
      m_label,
      x + kHeight * 0.5f + kLabelInset,
      y + (height - kLabelSize) * 0.5f + 0.5f,
      kLabelSize,
      UiTheme::applyOpacity(UiTheme::mix(accent, UiTheme::textPrimary(), 0.15f),
                            labelOpacity));
  }
  if (!m_reducedMotion) {
    GuiKit::drawSheen(
      m_visual,
      x,
      y,
      width,
      height,
      radius,
      (m_elapsed - 0.35f) / 0.7f,
      UiTheme::applyOpacity(ColorRgba{ 230, 250, 255, 56 }, opacity));
  }
  m_drawnWidth = width;
  m_drawnTop = y;
  m_visual.setVisible(true);
}
