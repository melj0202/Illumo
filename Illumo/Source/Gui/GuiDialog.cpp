#include <Illumo/Gui/GuiDialog.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>

GuiDialog::GuiDialog(IRenderWindow* window, Renderer* renderer)
  : m_window(window)
  , m_renderer(renderer)
  , m_visual(2048u)
  , m_open(false)
  , m_mouseWasDown(false)
  , m_animElapsed(0.0f)
  , m_fontSize(kDefaultFontSize)
  , m_panelWidth(kDefaultPanelWidth)
  , m_panelHeight(kDefaultPanelHeight)
  , m_buttonY(0.0f)
  , m_buttonWidth(110.0f)
  , m_buttonHeight(32.0f)
  , m_selectedButton(0)
  , m_hoveredButton(-1)
  , m_panelX(0.0f)
  , m_panelY(0.0f)
  , m_mouseX(0.0f)
  , m_mouseY(0.0f)
{
  m_visual.setSpace(PrimitiveSpace::Pixels);
  m_visual.setLayerHint(RenderLayerId::UI);
  m_visual.setWindow(window);
  m_visual.setRenderer(renderer);
  m_visual.prepare(renderer);
  setVisible(false);
  m_buttonFocus.configure(GuiMotion::kJelly);
}

void
GuiDialog::setTitle(const std::string& title)
{
  m_title = title;
}

void
GuiDialog::setMessage(const std::string& message)
{
  m_message = message;
}

void
GuiDialog::addButton(const GuiButtonDef& button)
{
  m_buttons.push_back(button);
  if (button.isDefault) {
    m_selectedButton = static_cast<int>(m_buttons.size()) - 1;
    m_selectionMotion.settleSelection();
  }
}

void
GuiDialog::clearButtons()
{
  m_buttons.clear();
  m_buttonX.clear();
  m_selectedButton = 0;
  m_hoveredButton = -1;
}

void
GuiDialog::open()
{
  m_open = true;
  m_mouseWasDown = m_roundedStyle;
  m_animElapsed = 0.0f;
  m_hoveredButton = -1;
  m_selectionMotion.setReducedMotion(m_reducedMotion);
  m_selectionMotion.restart();
  m_selectionMotion.settleSelection();
  m_ambientElapsed = 0.0f;
  m_tilt.level();
  // Until the first update samples the pointer, there is nothing to aim at.
  m_mouseX = -1.0f;
  m_mouseY = -1.0f;
  for (int index = 0; index < static_cast<int>(m_buttons.size()); ++index) {
    m_buttonFocus.snap(index, index == m_selectedButton ? 1.0f : 0.0f);
  }
  setVisible(true);
  updateLayout();
  rebuildVisual();
}

void
GuiDialog::open(const std::string& message)
{
  m_message = message;
  open();
}

void
GuiDialog::close()
{
  m_open = false;
  m_animElapsed = 0.0f;
  m_hoveredButton = -1;
  setVisible(false);
}

void
GuiDialog::setFontSize(float sizePt)
{
  const float clamped = std::clamp(sizePt, 8.0f, 48.0f);
  if (std::abs(m_fontSize - clamped) > 0.001f) {
    m_fontSize = clamped;
    updateLayout();
    if (m_open) {
      rebuildVisual();
    }
  }
}

void
GuiDialog::setPanelDimensions(float width, float height)
{
  m_panelWidth = std::max(200.0f, width);
  m_panelHeight = std::max(100.0f, height);
  updateLayout();
  if (m_open) {
    rebuildVisual();
  }
}

void
GuiDialog::tick(float dt)
{
  if (m_open && std::isfinite(dt) && dt > 0.0f) {
    m_animElapsed = std::min(kOpenSettleSeconds, m_animElapsed + dt);
    m_selectionMotion.setReducedMotion(m_reducedMotion);
    m_selectionMotion.tick(dt);
    m_ambientElapsed = m_reducedMotion
                         ? 0.0f
                         : std::fmod(m_ambientElapsed + std::min(dt, 0.1f),
                                     GuiMenuAnimator::kAmbientPeriodSeconds);
    for (int index = 0; index < static_cast<int>(m_buttons.size()); ++index) {
      const float target = index == m_selectedButton  ? 1.0f
                           : index == m_hoveredButton ? 0.55f
                                                      : 0.0f;
      m_buttonFocus.setTarget(index, target);
    }
    m_buttonFocus.tick(dt, m_reducedMotion);
    m_tilt.aim(
      m_mouseX, m_mouseY, m_virtualWidth, m_virtualHeight, m_roundedStyle);
    m_tilt.tick(dt, m_reducedMotion);
  }
}

float
GuiDialog::animationProgress() const
{
  if (!m_open) {
    return 0.0f;
  }
  return m_reducedMotion
           ? 1.0f
           : std::clamp(m_animElapsed / kOpenAnimationSeconds, 0.0f, 1.0f);
}

float
GuiDialog::panelOffsetY() const
{
  if (!m_roundedStyle) {
    const float ease = GuiEasing::outCubic(animationProgress());
    return (1.0f - ease) * 16.0f;
  }
  if (!m_open || m_reducedMotion) {
    return 0.0f;
  }
  // The rounded dialog drops in like a bead of liquid: it falls a little
  // past its resting place and bobs back up.
  return (1.0f - GuiEasing::springStep(m_animElapsed, 2.6f, 0.5f)) * 16.0f;
}

float
GuiDialog::selectionPosition() const
{
  if (m_buttons.empty()) {
    return 0.0f;
  }
  return m_selectionMotion.selectionPosition(
    static_cast<float>(m_selectedButton));
}

void
GuiDialog::selectButton(int buttonIndex)
{
  if (m_buttons.empty()) {
    return;
  }
  int next = buttonIndex;
  const int count = static_cast<int>(m_buttons.size());
  if (next < 0) {
    next = count - 1;
  } else if (next >= count) {
    next = 0;
  }
  if (next != m_selectedButton) {
    m_selectionMotion.beginSelectionTravel(static_cast<float>(m_selectedButton),
                                           static_cast<float>(next));
    m_selectedButton = next;
  }
}

int
GuiDialog::activateSelected() const
{
  if (m_selectedButton >= 0 &&
      m_selectedButton < static_cast<int>(m_buttons.size())) {
    return m_buttons[m_selectedButton].actionId;
  }
  return 0;
}

int
GuiDialog::clickAt(float x, float y)
{
  const float animY = m_buttonY - panelOffsetY();
  if (y < animY || y > animY + m_buttonHeight) {
    return 0;
  }
  for (size_t i = 0; i < m_buttons.size() && i < m_buttonX.size(); ++i) {
    if (x >= m_buttonX[i] && x <= m_buttonX[i] + m_buttonWidth) {
      selectButton(static_cast<int>(i));
      return m_buttons[i].actionId;
    }
  }
  return 0;
}

void
GuiDialog::updateLayout()
{
  int width = 1280;
  int height = 720;
  if (m_window != nullptr) {
    const std::array<int, 2> dimensions = m_window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }
  const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
  m_layoutScale = scale;
  if (m_roundedStyle) {
    const float fontScale = m_fontSize / kDefaultFontSize;
    m_layoutScale =
      std::min(scale,
               std::max(0.01f,
                        std::min(static_cast<float>(width) /
                                   (m_panelWidth * fontScale + 32.0f),
                                 static_cast<float>(height) /
                                   (m_panelHeight * fontScale + 32.0f))));
  }
  Transform2D fit;
  fit.scaleX = m_layoutScale / scale;
  fit.scaleY = fit.scaleX;
  m_visual.setTransform(fit);
  const float virtualWidth = static_cast<float>(width) / m_layoutScale;
  const float virtualHeight = static_cast<float>(height) / m_layoutScale;

  const float fontScale = m_fontSize / kDefaultFontSize;
  const float scaledWidth = m_panelWidth * fontScale;
  const float scaledHeight = m_panelHeight * fontScale;

  const float effectiveWidth = std::min(scaledWidth, virtualWidth - 32.0f);
  const float effectiveHeight = std::min(scaledHeight, virtualHeight - 32.0f);

  m_virtualWidth = virtualWidth;
  m_virtualHeight = virtualHeight;
  m_panelX = std::max(0.0f, (virtualWidth - effectiveWidth) * 0.5f);
  m_panelY = std::max(0.0f, (virtualHeight - effectiveHeight) * 0.5f);
  if (m_roundedStyle) {
    // The layout origin swivels with the body layer, so buttons are hit
    // where they are drawn while the dialog tilts toward the pointer.
    m_panelX += m_tilt.bodyShiftX();
    m_panelY += m_tilt.bodyShiftY();
  }

  m_buttonHeight = m_roundedStyle ? 62.0f * fontScale
                                  : std::clamp(32.0f * fontScale, 24.0f, 44.0f);
  m_buttonY = m_panelY + effectiveHeight - m_buttonHeight -
              (m_roundedStyle ? 48.0f : 16.0f) * fontScale;

  const size_t btnCount = m_buttons.size();
  m_buttonX.resize(btnCount, 0.0f);
  if (btnCount > 0) {
    const float buttonGap = 12.0f * fontScale;
    const float availWidth = effectiveWidth - 32.0f * fontScale;
    m_buttonWidth =
      std::clamp((availWidth - buttonGap * static_cast<float>(btnCount - 1)) /
                   static_cast<float>(btnCount),
                 40.0f,
                 (m_roundedStyle ? 172.0f : 120.0f) * fontScale);
    const float totalButtonsW = m_buttonWidth * static_cast<float>(btnCount) +
                                buttonGap * static_cast<float>(btnCount - 1);
    float startX = m_panelX + (effectiveWidth - totalButtonsW) * 0.5f;
    for (size_t i = 0; i < btnCount; ++i) {
      m_buttonX[i] = startX;
      startX += m_buttonWidth + buttonGap;
    }
  }
}

int
GuiDialog::update(InputManager* inputManager, float dt)
{
  if (!m_open) {
    return 0;
  }
  tick(dt);
  updateLayout();

  // Mouse hover tracking
  m_hoveredButton = -1;
  if (m_window != nullptr) {
    const std::array<double, 2> mouseCoords = m_window->getMouseCoords();
    const float scale = m_layoutScale;
    m_mouseX =
      static_cast<float>(mouseCoords[0]) / (scale > 0.0f ? scale : 1.0f);
    m_mouseY =
      static_cast<float>(mouseCoords[1]) / (scale > 0.0f ? scale : 1.0f);

    const float animY = m_buttonY - panelOffsetY();
    if (m_mouseY >= animY && m_mouseY <= animY + m_buttonHeight) {
      for (size_t i = 0; i < m_buttons.size() && i < m_buttonX.size(); ++i) {
        if (m_mouseX >= m_buttonX[i] &&
            m_mouseX <= m_buttonX[i] + m_buttonWidth) {
          m_hoveredButton = static_cast<int>(i);
          break;
        }
      }
    }
  }

  int action = 0;
  if (inputManager != nullptr) {
    std::queue<InputManager::KeyPressEvent>& keyQueue =
      inputManager->getKeyQueue();
    while (!keyQueue.empty()) {
      const InputManager::KeyPressEvent event = keyQueue.front();
      keyQueue.pop();
      if (event.action != InputAction::Press &&
          event.action != InputAction::Hold) {
        continue;
      }
      if (event.key == KeyCode::Escape) {
        for (const GuiButtonDef& btn : m_buttons) {
          if (btn.isCancel) {
            action = btn.actionId;
            break;
          }
        }
        if (action == 0 && !m_buttons.empty()) {
          action = m_buttons.back().actionId;
        }
      } else if (event.key == KeyCode::Enter) {
        action = activateSelected();
      } else if (event.key == KeyCode::Left) {
        selectButton(m_selectedButton - 1);
      } else if (event.key == KeyCode::Right || event.key == KeyCode::Tab) {
        selectButton(m_selectedButton + 1);
      } else {
        for (size_t i = 0; i < m_buttons.size(); ++i) {
          if (m_buttons[i].shortcutKey != KeyCode::None &&
              m_buttons[i].shortcutKey == event.key) {
            selectButton(static_cast<int>(i));
            action = m_buttons[i].actionId;
            break;
          }
        }
      }
    }

    // Clear char queue so typing doesn't bleed through
    std::queue<unsigned int>& charQueue = inputManager->getCharQueue();
    while (!charQueue.empty()) {
      charQueue.pop();
    }

    const bool mouseDown =
      inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
    if (mouseDown && !m_mouseWasDown) {
      const std::array<double, 2> mouse = m_window != nullptr
                                            ? m_window->getMouseCoords()
                                            : inputManager->getMousePosition();
      const float scale = m_layoutScale;
      const float clickX =
        static_cast<float>(mouse[0]) / (scale > 0.0f ? scale : 1.0f);
      const float clickY =
        static_cast<float>(mouse[1]) / (scale > 0.0f ? scale : 1.0f);
      action = clickAt(clickX, clickY);
    }
    m_mouseWasDown = mouseDown;
  }

  rebuildVisual();
  return action;
}

void
GuiDialog::rebuildVisual()
{
  m_visual.clearPrimitives();
  if (!m_open) {
    m_visual.setVisible(false);
    return;
  }
  m_visual.setVisible(true);
  updateLayout();

  int width = 1280;
  int height = 720;
  if (m_window != nullptr) {
    const std::array<int, 2> dimensions = m_window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }
  const float scale = m_layoutScale;
  const float virtualWidth =
    static_cast<float>(width) / (scale > 0.0f ? scale : 1.0f);
  const float virtualHeight =
    static_cast<float>(height) / (scale > 0.0f ? scale : 1.0f);

  const float fontScale = m_fontSize / kDefaultFontSize;
  const float titleFontSize = std::max(12.0f, std::round(16.0f * fontScale));
  const float messageFontSize = m_fontSize;
  const float buttonFontSize = m_fontSize;

  const float t = animationProgress();
  const float ease = GuiEasing::outCubic(t);
  const float slideY = panelOffsetY();
  const unsigned char bgAlpha = static_cast<unsigned char>(190.0f * ease);

  const float curPanelY = m_panelY - slideY;
  const float curButtonY = m_buttonY - slideY;
  const float effectiveWidth =
    std::min(m_panelWidth * fontScale, virtualWidth - 32.0f);
  const float effectiveHeight =
    std::min(m_panelHeight * fontScale, virtualHeight - 32.0f);

  if (m_roundedStyle) {
    // Radial scrim: the world stays faintly visible at the center.
    GuiKit::drawVignette(m_visual,
                         virtualWidth,
                         virtualHeight,
                         UiTheme::fade(UiTheme::scrimCenter(), ease),
                         UiTheme::fade(UiTheme::scrimEdge(), ease),
                         0.3f);
    drawRoundedContents(curPanelY,
                        effectiveWidth,
                        effectiveHeight,
                        fontScale,
                        static_cast<unsigned char>(255.0f * ease));
    return;
  }

  // Full-screen backdrop
  GuiKit::drawBackdrop(m_visual, virtualWidth, virtualHeight, bgAlpha);

  // Panel card
  GuiPanelChrome chrome;
  chrome.background = ColorRgba{ 14, 21, 32, 255 };
  chrome.border = UiTheme::panelBorder();
  chrome.shadow = ColorRgba{ 0, 0, 0, static_cast<unsigned char>(150 * ease) };
  chrome.shadowOffset = 8.0f * fontScale;
  chrome.borderWidth = 1.0f;
  chrome.drawShadow = true;
  chrome.drawAccent = false;
  GuiKit::drawPanel(
    m_visual, m_panelX, curPanelY, effectiveWidth, effectiveHeight, chrome);

  // Accent stripe at top of dialog
  m_visual.addFilledRect(
    m_panelX, curPanelY, effectiveWidth, 3.0f * fontScale, UiTheme::accent());

  // Title
  if (!m_title.empty()) {
    GuiKit::drawTextCentered(m_visual,
                             m_title,
                             m_panelX + effectiveWidth * 0.5f,
                             curPanelY + 28.0f * fontScale,
                             titleFontSize,
                             UiTheme::textPrimary());
  }

  // Message
  if (!m_message.empty()) {
    const float msgY = m_title.empty() ? (curPanelY + 36.0f * fontScale)
                                       : (curPanelY + 54.0f * fontScale);
    GuiKit::drawTextCentered(m_visual,
                             m_message,
                             m_panelX + effectiveWidth * 0.5f,
                             msgY,
                             messageFontSize,
                             UiTheme::textMuted());
  }

  // Action buttons
  for (size_t i = 0; i < m_buttons.size() && i < m_buttonX.size(); ++i) {
    const GuiButtonDef& btn = m_buttons[i];
    GuiButtonState state = GuiButtonState::Normal;
    if (static_cast<int>(i) == m_hoveredButton) {
      state = GuiButtonState::Hover;
    } else if (static_cast<int>(i) == m_selectedButton) {
      state = GuiButtonState::Hover;
    }

    ColorRgba accent = btn.customAccent;
    if (accent.a == 0) {
      if (btn.isDestructive) {
        accent = UiTheme::error();
      } else if (btn.isDefault) {
        accent = UiTheme::accent();
      }
    }

    GuiKit::drawButton(m_visual,
                       m_buttonX[i],
                       curButtonY,
                       m_buttonWidth,
                       m_buttonHeight,
                       btn.label,
                       buttonFontSize,
                       state,
                       accent);
  }
}

void
GuiDialog::drawRoundedContents(float panelY,
                               float width,
                               float height,
                               float fontScale,
                               unsigned char opacity)
{
  const float ambient = m_ambientElapsed;
  const float breathe = 0.5f + 0.5f * std::sin(ambient * 1.04719755f);
  GuiGlassStyle glass;
  glass.opacity = opacity;
  glass.ambientPhase = ambient;
  glass.glow = 0.4f + 0.3f * breathe;
  glass.accentReveal = static_cast<float>(opacity) / 255.0f;
  m_tilt.applyTo(glass);
  GuiKit::drawGlassPanel(m_visual,
                         m_panelX + m_tilt.layerX(GuiPanelTilt::kGlassDepth),
                         panelY + m_tilt.layerY(GuiPanelTilt::kGlassDepth),
                         width,
                         height,
                         glass);
  const float centerX = m_panelX + width * 0.5f;
  // The title, message and breathing cells float nearer than the buttons.
  const float headerX = centerX + m_tilt.layerX(GuiPanelTilt::kHeaderDepth);
  const float headerY = panelY + m_tilt.layerY(GuiPanelTilt::kHeaderDepth);
  GuiKit::drawTextCentered(
    m_visual,
    m_title,
    headerX,
    headerY + 46.0f * fontScale,
    25.0f * fontScale,
    UiTheme::applyOpacity(UiTheme::textPrimary(), opacity));
  GuiKit::drawTextCentered(
    m_visual,
    m_message,
    headerX,
    headerY + 86.0f * fontScale,
    m_fontSize,
    UiTheme::applyOpacity(UiTheme::textSecondary(), opacity));
  // Seven cells breathe in a wave, cyan into violet.
  for (int cell = 0; cell < 7; ++cell) {
    const float wave =
      m_reducedMotion ? 0.5f
                      : 0.5f + 0.5f * std::sin(ambient * 3.14159265f -
                                               static_cast<float>(cell) * 0.7f);
    const float size = (4.0f + 2.0f * wave) * fontScale;
    const float cellCenterX =
      headerX + (static_cast<float>(cell) - 3.0f) * 11.0f * fontScale + 2.5f;
    const float cellCenterY = headerY + 119.5f * fontScale;
    const ColorRgba tint = UiTheme::mix(UiTheme::accentCool(),
                                        UiTheme::accentViolet(),
                                        static_cast<float>(cell) / 6.0f);
    GuiKit::drawRoundedRect(
      m_visual,
      cellCenterX - size * 0.5f,
      cellCenterY - size * 0.5f,
      size,
      size,
      2.0f,
      UiTheme::applyOpacity(UiTheme::fade(tint, 0.55f + 0.45f * wave),
                            opacity));
  }

  const float y = m_buttonY - panelOffsetY();
  const float radius = 12.0f;
  // Buttons: soft shadow, a rim and face that warm with focus, and a small
  // lift (kept within the button so the label still hits it).
  for (size_t index = 0; index < m_buttons.size(); ++index) {
    const GuiButtonDef& button = m_buttons[index];
    const ColorRgba accent =
      button.isDestructive ? UiTheme::error() : UiTheme::accentCool();
    const float e =
      std::clamp(m_buttonFocus.value(static_cast<int>(index)), 0.0f, 1.0f);
    const float lift = 2.0f * e;
    const float x = m_buttonX[index];
    GuiKit::drawSoftShadow(
      m_visual,
      x,
      y - lift,
      m_buttonWidth,
      m_buttonHeight,
      radius,
      10.0f + 4.0f * e,
      4.0f + 2.0f * e,
      UiTheme::applyOpacity(UiTheme::glowShadow(), opacity));
    GuiKit::drawRoundedRect(
      m_visual,
      x,
      y - lift,
      m_buttonWidth,
      m_buttonHeight,
      radius,
      UiTheme::applyOpacity(UiTheme::mix(UiTheme::cardRim(), accent, 0.6f * e),
                            opacity));
    GuiKit::drawRoundedGradientRect(
      m_visual,
      x + 1.0f,
      y - lift + 1.0f,
      m_buttonWidth - 2.0f,
      m_buttonHeight - 2.0f,
      radius - 1.0f,
      UiTheme::applyOpacity(UiTheme::mix(UiTheme::cardTop(), accent, 0.12f * e),
                            opacity),
      UiTheme::applyOpacity(UiTheme::cardBottom(), opacity));
  }
  if (!m_buttons.empty() && m_selectedButton >= 0 &&
      m_selectedButton < static_cast<int>(m_buttons.size())) {
    // The highlight pours from the button it leaves to the one it lands on
    // like a drop of liquid, then a sheen sweeps across it.
    const ColorRgba accent = m_buttons[m_selectedButton].isDestructive
                               ? UiTheme::error()
                               : UiTheme::accentCool();
    const float gap = m_buttonX.size() > 1 ? m_buttonX[1] - m_buttonX[0] : 0.0f;
    const GuiSelectionSpan span =
      m_selectionMotion.selectionSpan(static_cast<float>(m_selectedButton));
    const float lift =
      2.0f * std::clamp(m_buttonFocus.value(m_selectedButton), 0.0f, 1.0f);
    GuiLiquidSelection drop;
    drop.horizontal = true;
    drop.crossStart = y - lift;
    drop.crossSize = m_buttonHeight;
    drop.headStart = m_buttonX[0] + span.leading * gap;
    drop.tailStart = m_buttonX[0] + span.trailing * gap;
    drop.cellLength = m_buttonWidth;
    drop.radius = radius;
    drop.squash = span.squash;
    drop.glowSpread = 12.0f + 4.0f * breathe;
    drop.glow = UiTheme::applyOpacity(
      UiTheme::fade(accent, 0.22f + 0.12f * breathe), opacity);
    drop.rim = UiTheme::applyOpacity(accent, opacity);
    drop.faceTop = UiTheme::applyOpacity(
      UiTheme::mix(UiTheme::cardTop(), accent, 0.38f), opacity);
    drop.faceBottom = UiTheme::applyOpacity(
      UiTheme::mix(UiTheme::cardBottom(), accent, 0.2f), opacity);
    drop.sheen = m_selectionMotion.selectionSheen();
    drop.sheenColor =
      UiTheme::applyOpacity(ColorRgba{ 220, 250, 255, 44 }, opacity);
    GuiKit::drawLiquidSelection(m_visual, drop);
  }
  for (size_t index = 0; index < m_buttons.size(); ++index) {
    const GuiButtonDef& button = m_buttons[index];
    const ColorRgba accent =
      button.isDestructive ? UiTheme::error() : UiTheme::accentCool();
    const float e =
      std::clamp(m_buttonFocus.value(static_cast<int>(index)), 0.0f, 1.0f);
    const float lift = 2.0f * e;
    const ColorRgba text =
      static_cast<int>(index) == m_selectedButton
        ? UiTheme::mix(accent, UiTheme::textPrimary(), 0.2f)
        : UiTheme::mix(UiTheme::textPrimary(), accent, e);
    GuiKit::drawTextCentered(m_visual,
                             button.label,
                             m_buttonX[index] + m_buttonWidth * 0.5f,
                             y + m_buttonHeight * 0.36f - lift,
                             17.0f * fontScale,
                             UiTheme::applyOpacity(text, opacity));
    GuiKit::drawTextCentered(
      m_visual,
      button.shortcut,
      m_buttonX[index] + m_buttonWidth * 0.5f,
      y + m_buttonHeight * 0.73f - lift,
      10.0f * fontScale,
      UiTheme::applyOpacity(
        UiTheme::mix(UiTheme::textMuted(), UiTheme::textSecondary(), e),
        opacity));
  }
  // Footer key hints, centered.
  const float hintSize = 9.0f * fontScale;
  const float hintWidth =
    GuiKit::measureKeyHint("LEFTRIGHT", "Select", hintSize) +
    GuiKit::measureKeyHint("ENTER", "Open", hintSize) - hintSize * 1.6f;
  float hintX = centerX - hintWidth * 0.5f;
  const float hintY = panelY + height - 30.0f * fontScale;
  hintX += GuiKit::drawKeyHint(
    m_visual, hintX, hintY, "LEFTRIGHT", "Select", hintSize, opacity);
  GuiKit::drawKeyHint(
    m_visual, hintX, hintY, "ENTER", "Open", hintSize, opacity);
}

bool
GuiDialog::AppendCommands(Renderer* renderer)
{
  if (!m_open) {
    return true;
  }
  rebuildVisual();
  return m_visual.AppendCommands(renderer);
}
