#include <Illumo/Gui/GuiDialog.h>
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
  , m_selectionFromButton(0.0f)
  , m_selectionAnimElapsed(kSelectionAnimationSeconds)
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
    m_selectionFromButton = static_cast<float>(m_selectedButton);
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
  m_mouseWasDown = false;
  m_animElapsed = 0.0f;
  m_hoveredButton = -1;
  m_selectionAnimElapsed = kSelectionAnimationSeconds;
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
  if (m_open) {
    m_animElapsed += dt;
    m_selectionAnimElapsed += dt;
  }
}

float
GuiDialog::animationProgress() const
{
  if (!m_open) {
    return 0.0f;
  }
  return std::clamp(m_animElapsed / kOpenAnimationSeconds, 0.0f, 1.0f);
}

float
GuiDialog::panelOffsetY() const
{
  const float t = animationProgress();
  const float ease = 1.0f - std::pow(1.0f - t, 3.0f);
  return (1.0f - ease) * 16.0f;
}

float
GuiDialog::selectionPosition() const
{
  if (m_buttons.empty()) {
    return 0.0f;
  }
  const float t =
    std::clamp(m_selectionAnimElapsed / kSelectionAnimationSeconds, 0.0f, 1.0f);
  const float ease = 1.0f - std::pow(1.0f - t, 3.0f);
  return m_selectionFromButton +
         (static_cast<float>(m_selectedButton) - m_selectionFromButton) * ease;
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
    m_selectionFromButton = selectionPosition();
    m_selectedButton = next;
    m_selectionAnimElapsed = 0.0f;
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
  const float virtualWidth =
    static_cast<float>(width) / (scale > 0.0f ? scale : 1.0f);
  const float virtualHeight =
    static_cast<float>(height) / (scale > 0.0f ? scale : 1.0f);

  const float fontScale = m_fontSize / kDefaultFontSize;
  const float scaledWidth = m_panelWidth * fontScale;
  const float scaledHeight = m_panelHeight * fontScale;

  const float effectiveWidth = std::min(scaledWidth, virtualWidth - 32.0f);
  const float effectiveHeight = std::min(scaledHeight, virtualHeight - 32.0f);

  m_panelX = std::max(0.0f, (virtualWidth - effectiveWidth) * 0.5f);
  m_panelY = std::max(0.0f, (virtualHeight - effectiveHeight) * 0.5f);

  m_buttonHeight = std::clamp(32.0f * fontScale, 24.0f, 44.0f);
  m_buttonY = m_panelY + effectiveHeight - m_buttonHeight - 16.0f * fontScale;

  const size_t btnCount = m_buttons.size();
  m_buttonX.resize(btnCount, 0.0f);
  if (btnCount > 0) {
    const float buttonGap = 12.0f * fontScale;
    const float availWidth = effectiveWidth - 32.0f * fontScale;
    m_buttonWidth =
      std::clamp((availWidth - buttonGap * static_cast<float>(btnCount - 1)) /
                   static_cast<float>(btnCount),
                 40.0f,
                 120.0f * fontScale);
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
    const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
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
      const std::array<double, 2> mouse = inputManager->getMousePosition();
      const float scale =
        m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
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
  const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
  const float virtualWidth =
    static_cast<float>(width) / (scale > 0.0f ? scale : 1.0f);
  const float virtualHeight =
    static_cast<float>(height) / (scale > 0.0f ? scale : 1.0f);

  const float fontScale = m_fontSize / kDefaultFontSize;
  const float titleFontSize = std::max(12.0f, std::round(16.0f * fontScale));
  const float messageFontSize = m_fontSize;
  const float buttonFontSize = m_fontSize;

  const float t = animationProgress();
  const float ease = 1.0f - std::pow(1.0f - t, 3.0f);
  const float slideY = (1.0f - ease) * 16.0f;
  const unsigned char bgAlpha = static_cast<unsigned char>(190.0f * ease);

  // Full-screen backdrop
  GuiKit::drawBackdrop(m_visual, virtualWidth, virtualHeight, bgAlpha);

  const float curPanelY = m_panelY - slideY;
  const float curButtonY = m_buttonY - slideY;
  const float effectiveWidth =
    std::min(m_panelWidth * fontScale, virtualWidth - 32.0f);
  const float effectiveHeight =
    std::min(m_panelHeight * fontScale, virtualHeight - 32.0f);

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

bool
GuiDialog::AppendCommands(Renderer* renderer)
{
  if (!m_open) {
    return true;
  }
  return m_visual.AppendCommands(renderer);
}
