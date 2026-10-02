#include "CanvasChromeButton.h"
#include "CSimSounds.h"
#include "CanvasChromeStyle.h"
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiPointerHint.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <algorithm>
#include <cmath>
#include <utility>

CanvasChromeButton::CanvasChromeButton(CanvasChromeIcon icon,
                                       std::string label,
                                       std::string key)
  : m_icon(icon)
  , m_label(std::move(label))
  , m_key(std::move(key))
{
  for (GameVisual* visual : { &m_halo, &m_tile }) {
    visual->setSpace(PrimitiveSpace::Pixels);
    visual->setLayerHint(RenderLayerId::UI);
    visual->setVisible(false);
  }
}

void
CanvasChromeButton::prepare(IRenderWindow* window, Renderer* renderer)
{
  for (GameVisual* visual : { &m_halo, &m_tile }) {
    visual->setRenderer(renderer);
    visual->setWindow(window);
    visual->setVisible(false);
    if (renderer != nullptr) {
      visual->prepare(renderer);
    }
  }
  m_haloKey.invalidate();
  m_tileKey.invalidate();
  m_pop.configure(GuiMotion::kBoing);
  m_hover.configure(GuiMotion::kJelly);
  m_tip.configure(GuiMotion::kBoing);
  for (GuiSpring& row : m_rows) {
    row.configure(GuiMotion::kBoing);
  }
  m_hovered = false;
  m_shown = false;
}

void
CanvasChromeButton::hide()
{
  if (m_shown) {
    m_halo.clearPrimitives();
    m_tile.clearPrimitives();
    m_haloKey.invalidate();
    m_tileKey.invalidate();
  }
  m_halo.setVisible(false);
  m_tile.setVisible(false);
  m_hovered = false;
  m_shown = false;
}

bool
CanvasChromeButton::containsPoint(float x, float y) const
{
  return x >= m_x && x <= m_x + m_size && y >= m_y && y <= m_y + m_size;
}

void
CanvasChromeButton::update(double dt,
                           float x,
                           float y,
                           float size,
                           float pointerX,
                           float pointerY,
                           bool mouseDown,
                           bool reducedMotion)
{
  ILLUMO_PROFILE_ZONE("CanvasChromeButton.update");
  m_x = x;
  m_y = y;
  m_size = size;
  const bool wasHovered = m_hovered;
  m_hovered = containsPoint(pointerX, pointerY);
  if (m_hovered) {
    GuiPointerHint::markInteractive();
  }
  if (m_hovered && !wasHovered) {
    CSimSounds::play(CSimSound::MenuHover);
  }
  const bool isPressed = m_hovered && mouseDown;
  const float step =
    std::isfinite(dt) && dt > 0.0 ? static_cast<float>(dt) : 0.0f;

  // Reappearing (entering the canvas, or closing a menu, the console or a
  // dialog) pops the button back in from nothing.
  if (!m_shown) {
    m_shown = true;
    m_pop.snapTo(0.0f);
    m_hover.snapTo(0.0f);
    m_tip.snapTo(0.0f);
    for (GuiSpring& row : m_rows) {
      row.snapTo(0.0f);
    }
    m_hoverClock = 0.0;
  }
  m_pop.setTarget(1.0f);
  m_pop.tick(step, reducedMotion);

  m_hover.setTarget(m_hovered ? 1.0f : 0.0f);
  m_hover.tick(step, reducedMotion);
  m_hoverClock = m_hovered != wasHovered ? 0.0 : m_hoverClock + step;
  // The glyph's rows follow in a cascade: top first as the pointer arrives,
  // bottom first as it leaves.
  for (std::size_t i = 0; i < m_rows.size(); ++i) {
    const std::size_t order = m_hovered ? i : m_rows.size() - 1u - i;
    if (reducedMotion || m_hoverClock >= 0.05 * static_cast<double>(order)) {
      m_rows[i].setTarget(m_hovered ? 1.0f : 0.0f);
    }
    m_rows[i].tick(step, reducedMotion);
  }

  // The hint follows a beat after the pointer arrives, on its own springier
  // spring, and tucks away at once when it leaves.
  m_tip.setTarget(m_hovered && (reducedMotion || m_hoverClock >= 0.04) ? 1.0f
                                                                       : 0.0f);
  m_tip.tick(step, reducedMotion);
  // A slow clock for the idle breath and the hovered glyph's ripple.
  m_clock = std::fmod(m_clock + step, 60.0);
  const float clock = static_cast<float>(m_clock);
  const float breathe = CanvasChromeStyle::breathe(clock, reducedMotion);

  const float hoverSpring = m_hover.value();
  const float hover = std::clamp(hoverSpring, 0.0f, 1.0f);
  const float pop = std::max(0.0f, m_pop.value());
  const ColorRgba cyan = UiTheme::accentCool();
  const ColorRgba violet = UiTheme::accentViolet();

  // Jelly: the tile stretches tall while it grows and bulges wide as it
  // recoils, driven by how fast the pop and hover springs are moving. A held
  // press squashes it flat.
  const float squash =
    CanvasChromeStyle::squash(m_pop.velocity(), m_hover.velocity(), isPressed);
  const float grow = pop * (1.0f + 0.1f * hoverSpring);
  const float tileWidth = m_size * grow * (1.0f + squash);
  const float tileHeight = m_size * grow * (1.0f - squash);
  const bool tileShown = tileWidth > 2.0f && tileHeight > 2.0f;
  // An opaque icon tile scaled about its centre. A press sinks it.
  const float centerX = m_x + m_size * 0.5f;
  const float centerY = m_y + m_size * 0.5f + (isPressed ? 1.0f : 0.0f);
  const float tileX = centerX - tileWidth * 0.5f;
  const float tileY = centerY - tileHeight * 0.5f;
  const float radius =
    std::min(10.0f * grow, std::min(tileWidth, tileHeight) * 0.5f);

  // Behind the tile: a soft drop shadow and a cyan-to-violet halo that
  // breathes at rest and blooms on hover.
  m_haloKey.begin().add(
    { tileShown ? 1.0f : 0.0f, tileX, tileY, tileWidth, tileHeight, radius });
  m_haloKey.add({ isPressed ? 1.0f : 0.0f, hover, breathe, pop });
  if (m_haloKey.changed()) {
    ILLUMO_PROFILE_ZONE("CanvasChromeButton.rebuildHalo");
    m_halo.clearPrimitives();
    if (tileShown) {
      GuiKit::drawSoftShadow(m_halo,
                             tileX,
                             tileY,
                             tileWidth,
                             tileHeight,
                             radius,
                             10.0f,
                             (isPressed ? 1.0f : 3.0f) + 2.0f * hover,
                             UiTheme::glowShadow());
      const ColorRgba halo = CanvasChromeStyle::halo(hover);
      GuiKit::drawRoundedBand(
        m_halo,
        tileX,
        tileY,
        tileWidth,
        tileHeight,
        radius,
        0.0f,
        CanvasChromeStyle::haloWidth(breathe, hover),
        UiTheme::fade(halo,
                      CanvasChromeStyle::haloOpacity(breathe, hover) * pop),
        UiTheme::transparentOf(halo));
    }
  }
  m_halo.setVisible(true);

  // Each glyph row grows on its own spring, overshooting before it settles,
  // then ripples gently while the pointer stays.
  std::array<float, 3> rowGrowth{};
  for (std::size_t i = 0; i < m_rows.size(); ++i) {
    const float ripple =
      reducedMotion
        ? 0.0f
        : 0.08f * hover * std::sin(clock * 7.0f - static_cast<float>(i) * 0.9f);
    rowGrowth[i] = m_rows[i].value() + ripple;
  }

  const float tipSpring = m_tip.value();
  const float tipShown = std::clamp(tipSpring * 1.4f, 0.0f, 1.0f);
  const bool tipDrawn = tipShown > 0.01f && pop > 0.5f;
  const float tipSquash = std::clamp(0.01f * m_tip.velocity(), -0.15f, 0.15f);

  // The tile, glyph and hint change only on interaction, so they redraw only
  // when their inputs do.
  m_tileKey.begin().add({ tileShown ? 1.0f : 0.0f,
                          m_x,
                          m_y,
                          centerY,
                          tileWidth,
                          tileHeight,
                          radius,
                          hover,
                          isPressed ? 1.0f : 0.0f,
                          grow,
                          squash });
  m_tileKey.add({ rowGrowth[0], rowGrowth[1], rowGrowth[2] });
  m_tileKey.add({ tipDrawn ? 1.0f : 0.0f,
                  tipDrawn ? tipSpring : 0.0f,
                  tipDrawn ? tipSquash : 0.0f });
  m_tile.setVisible(true);
  if (!m_tileKey.changed()) {
    return;
  }
  m_tile.clearPrimitives();

  if (tileShown) {
    ILLUMO_PROFILE_ZONE("CanvasChromeButton.rebuildTile");
    // A rim lit cyan at the top and violet at the bottom, and a
    // teal-to-indigo face that brightens toward the accents on hover.
    GuiKit::drawRoundedGradientRect(
      m_tile,
      tileX,
      tileY,
      tileWidth,
      tileHeight,
      radius,
      isPressed ? cyan : CanvasChromeStyle::rimTop(hover),
      isPressed ? cyan : CanvasChromeStyle::rimBottom(hover));
    GuiKit::drawRoundedGradientRect(m_tile,
                                    tileX + 1.0f,
                                    tileY + 1.0f,
                                    tileWidth - 2.0f,
                                    tileHeight - 2.0f,
                                    std::max(0.0f, radius - 1.0f),
                                    CanvasChromeStyle::faceTop(hover),
                                    CanvasChromeStyle::faceBottom(hover));
    // A cyan-to-violet hairline crowns the flat top, as on the paint drawer.
    const float crownInset = std::max(3.0f, radius);
    if (tileWidth - 2.0f * crownInset > 2.0f) {
      const unsigned char crownOpacity =
        static_cast<unsigned char>(255.0f * (0.55f + 0.45f * hover));
      m_tile.addGradientRect(tileX + crownInset,
                             tileY + 1.0f,
                             tileWidth - 2.0f * crownInset,
                             1.5f,
                             UiTheme::applyOpacity(cyan, crownOpacity),
                             UiTheme::applyOpacity(violet, crownOpacity),
                             UiTheme::applyOpacity(violet, crownOpacity),
                             UiTheme::applyOpacity(cyan, crownOpacity));
    }
    drawGlyph(centerX, centerY, grow, squash, hover, isPressed, rowGrowth);
  }

  if (tipDrawn) {
    drawHint(tipSpring, tipShown, tipSquash);
  }
}

void
CanvasChromeButton::drawGlyph(float centerX,
                              float centerY,
                              float grow,
                              float squash,
                              float hover,
                              bool pressed,
                              const std::array<float, 3>& rowGrowth)
{
  const ColorRgba cyan = UiTheme::accentCool();
  const ColorRgba violet = UiTheme::accentViolet();
  // Rows run cyan to violet from top to bottom, pastel at rest and vivid on
  // hover; a press turns them near white.
  const ColorRgba rowTints[3] = { cyan,
                                  UiTheme::mix(cyan, violet, 0.5f),
                                  violet };
  const auto rowColor = [&](std::size_t row) {
    return pressed ? ColorRgba{ 236, 250, 255, 255 }
                   : UiTheme::mix(UiTheme::mix(ColorRgba{ 214, 230, 246, 255 },
                                               rowTints[row],
                                               0.45f),
                                  rowTints[row],
                                  hover);
  };
  const float wide = grow * (1.0f + squash);
  const float tall = grow * (1.0f - squash);

  if (m_icon == CanvasChromeIcon::Settings) {
    // Three rounded bars that widen on hover.
    const float barHeight = 2.5f * tall;
    const float spacing = 5.5f * tall;
    for (std::size_t i = 0; i < 3u; ++i) {
      const float barWidth = (14.0f + 6.0f * rowGrowth[i]) * wide;
      const float y =
        centerY + (static_cast<float>(i) - 1.0f) * spacing - barHeight * 0.5f;
      GuiKit::drawRoundedRect(m_tile,
                              centerX - barWidth * 0.5f,
                              y,
                              barWidth,
                              barHeight,
                              barHeight * 0.5f,
                              rowColor(i));
    }
    return;
  }

  // A glider on a 3x3 neighborhood: its five live cells light and swell row
  // by row on hover; the dead cells stay as faint sockets.
  static constexpr bool kGlider[3][3] = { { false, true, false },
                                          { false, false, true },
                                          { true, true, true } };
  const float pitchX = 6.5f * wide;
  const float pitchY = 6.5f * tall;
  const ColorRgba socket =
    UiTheme::mix(ColorRgba{ 60, 82, 110, 255 }, cyan, 0.15f * hover);
  for (std::size_t row = 0; row < 3u; ++row) {
    for (std::size_t column = 0; column < 3u; ++column) {
      const bool live = kGlider[row][column];
      const float cell = live ? (4.2f + 1.0f * rowGrowth[row]) : 2.6f;
      const float cellWidth = cell * wide;
      const float cellHeight = cell * tall;
      const float cx = centerX + (static_cast<float>(column) - 1.0f) * pitchX;
      const float cy = centerY + (static_cast<float>(row) - 1.0f) * pitchY;
      GuiKit::drawRoundedRect(m_tile,
                              cx - cellWidth * 0.5f,
                              cy - cellHeight * 0.5f,
                              cellWidth,
                              cellHeight,
                              std::min(cellWidth, cellHeight) * 0.3f,
                              live ? rowColor(row) : socket);
    }
  }
}

void
CanvasChromeButton::drawHint(float tipSpring, float tipShown, float tipSquash)
{
  ILLUMO_PROFILE_ZONE("CanvasChromeButton.rebuildHint");
  const ColorRgba cyan = UiTheme::accentCool();
  const ColorRgba violet = UiTheme::accentViolet();
  // A glass hint springs out of the button: it grows from its end nearest
  // the button, sails past its spot and bounces back, stretching along its
  // travel and bulging as it recoils. It shows the action, then its key as
  // a keycap, the way the menus' footers show shortcuts.
  const unsigned char opacity = static_cast<unsigned char>(255.0f * tipShown);
  const float tipScale = std::max(0.2f, 0.6f + 0.4f * tipSpring);
  const float labelSize = 11.0f * tipScale;
  const float keySize = 8.5f * tipScale;
  const float tipHeight = 26.0f * tipScale * (1.0f - tipSquash);
  const float labelWidth =
    GuiKit::measureEmphasizedText(m_label, labelSize, 0.5f);
  // The keycap alone: a hint with no action, less its spacing.
  const float keyWidth =
    GuiKit::measureKeyHint(m_key, std::string(), keySize) - keySize * 2.2f;
  const float restWidth = 14.0f * tipScale + labelWidth + 8.0f * tipScale +
                          keyWidth + 6.0f * tipScale;
  const float tipWidth = restWidth * (1.0f + tipSquash);
  const float tipRight = m_x - 8.0f + (1.0f - tipSpring) * 18.0f;
  const float tipX = tipRight - tipWidth;
  const float tipY = m_y + (m_size - tipHeight) * 0.5f;
  const float contentX = tipX + (tipWidth - restWidth) * 0.5f;
  GuiKit::drawSoftShadow(m_tile,
                         tipX,
                         tipY,
                         tipWidth,
                         tipHeight,
                         tipHeight * 0.5f,
                         10.0f,
                         3.0f,
                         UiTheme::applyOpacity(UiTheme::glowShadow(), opacity));
  GuiKit::drawRoundedGradientRect(
    m_tile,
    tipX,
    tipY,
    tipWidth,
    tipHeight,
    tipHeight * 0.5f,
    UiTheme::applyOpacity(UiTheme::mix(UiTheme::glassRim(), cyan, 0.45f),
                          opacity),
    UiTheme::applyOpacity(UiTheme::mix(UiTheme::glassRim(), violet, 0.45f),
                          opacity));
  GuiKit::drawRoundedGradientRect(
    m_tile,
    tipX + 1.0f,
    tipY + 1.0f,
    tipWidth - 2.0f,
    tipHeight - 2.0f,
    std::max(0.0f, tipHeight * 0.5f - 1.0f),
    UiTheme::applyOpacity(UiTheme::glassTop(), opacity),
    UiTheme::applyOpacity(UiTheme::glassBottom(), opacity));
  GuiKit::drawEmphasizedText(
    m_tile,
    m_label,
    contentX + 14.0f * tipScale,
    tipY + (tipHeight - labelSize) * 0.5f,
    labelSize,
    UiTheme::applyOpacity(UiTheme::textPrimary(), opacity),
    0.5f);
  GuiKit::drawKeycap(m_tile,
                     contentX + 14.0f * tipScale + labelWidth + 8.0f * tipScale,
                     tipY + (tipHeight - keySize * 1.75f) * 0.5f -
                       0.75f * tipScale,
                     m_key,
                     keySize,
                     opacity);
}
