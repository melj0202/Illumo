#include "EditorConfirmDialog.h"

#include "EditorToolbar.h"
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <queue>

namespace {
float
estimateTextWidth(const std::string& text, float sizePt)
{
  return static_cast<float>(text.size()) * sizePt * 0.6f;
}
} // namespace

EditorConfirmDialog::EditorConfirmDialog(IRenderWindow* window,
                                         Renderer* renderer)
  : m_window(window)
  , m_renderer(renderer)
  , m_visual(256u)
  , m_open(false)
  , m_mouseWasDown(false)
  , m_animElapsed(0.0f)
  , m_fontSize(EditorToolbar::kDefaultFontSize)
  , m_panelX(0.0f)
  , m_panelY(0.0f)
  , m_panelWidth(420.0f)
  , m_panelHeight(160.0f)
  , m_buttonY(0.0f)
  , m_buttonWidth(110.0f)
  , m_buttonHeight(32.0f)
  , m_saveX(0.0f)
  , m_discardX(0.0f)
  , m_cancelX(0.0f)
  , m_hoveredButton(EditorConfirmAction::None)
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
EditorConfirmDialog::setFontSize(float sizePt)
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
EditorConfirmDialog::open(const std::string& message)
{
  m_message = message;
  m_open = true;
  m_mouseWasDown = false;
  m_animElapsed = 0.0f;
  m_hoveredButton = EditorConfirmAction::None;
  setVisible(true);
  updateLayout();
  rebuildVisual();
}

void
EditorConfirmDialog::close()
{
  m_open = false;
  m_animElapsed = 0.0f;
  m_hoveredButton = EditorConfirmAction::None;
  setVisible(false);
}

void
EditorConfirmDialog::updateLayout()
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

  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  m_buttonHeight = std::max(32.0f, std::round(32.0f * fontScale));
  m_buttonWidth = std::max(110.0f, std::round(110.0f * fontScale));
  const float gap = std::max(10.0f, std::round(10.0f * fontScale));
  const float buttonsWidth = m_buttonWidth * 3.0f + gap * 2.0f;

  m_panelWidth =
    std::max(buttonsWidth + 32.0f,
             std::min(std::max(440.0f, std::round(440.0f * fontScale)),
                      virtualWidth - 32.0f));
  m_panelHeight = std::max(168.0f, std::round(168.0f * fontScale));
  m_panelX = std::max(0.0f, (virtualWidth - m_panelWidth) * 0.5f);
  m_panelY = std::max(0.0f, (virtualHeight - m_panelHeight) * 0.5f);
  m_buttonY =
    m_panelY + m_panelHeight - m_buttonHeight - std::round(16.0f * fontScale);
  m_saveX = m_panelX + (m_panelWidth - buttonsWidth) * 0.5f;
  m_discardX = m_saveX + m_buttonWidth + gap;
  m_cancelX = m_discardX + m_buttonWidth + gap;
}

EditorConfirmAction
EditorConfirmDialog::clickAt(float x, float y)
{
  if (y < m_buttonY || y > m_buttonY + m_buttonHeight) {
    return EditorConfirmAction::None;
  }
  if (x >= m_saveX && x <= m_saveX + m_buttonWidth) {
    return EditorConfirmAction::Save;
  }
  if (x >= m_discardX && x <= m_discardX + m_buttonWidth) {
    return EditorConfirmAction::Discard;
  }
  if (x >= m_cancelX && x <= m_cancelX + m_buttonWidth) {
    return EditorConfirmAction::Cancel;
  }
  return EditorConfirmAction::None;
}

EditorConfirmAction
EditorConfirmDialog::update(InputManager* inputManager, float dt)
{
  if (!m_open) {
    return EditorConfirmAction::None;
  }
  m_animElapsed += std::max(0.0f, dt);
  updateLayout();

  // Mouse hover tracking
  m_hoveredButton = EditorConfirmAction::None;
  if (m_window != nullptr) {
    const std::array<double, 2> mouseCoords = m_window->getMouseCoords();
    const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
    m_mouseX =
      static_cast<float>(mouseCoords[0]) / (scale > 0.0f ? scale : 1.0f);
    m_mouseY =
      static_cast<float>(mouseCoords[1]) / (scale > 0.0f ? scale : 1.0f);
    m_hoveredButton = clickAt(m_mouseX, m_mouseY);
  }

  EditorConfirmAction action = EditorConfirmAction::None;
  if (inputManager != nullptr) {
    std::queue<InputManager::KeyPressEvent>& keyQueue =
      inputManager->getKeyQueue();
    while (!keyQueue.empty()) {
      const InputManager::KeyPressEvent event = keyQueue.front();
      keyQueue.pop();
      if (event.action != InputAction::Press) {
        continue;
      }
      if (event.key == KeyCode::Escape) {
        action = EditorConfirmAction::Cancel;
      } else if (event.key == KeyCode::Enter) {
        action = EditorConfirmAction::Save;
      }
    }
    const bool mouseDown =
      inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
    if (mouseDown && !m_mouseWasDown) {
      const std::array<double, 2> mouse = inputManager->getMousePosition();
      const float scale =
        m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
      action =
        clickAt(static_cast<float>(mouse[0]) / (scale > 0.0f ? scale : 1.0f),
                static_cast<float>(mouse[1]) / (scale > 0.0f ? scale : 1.0f));
    }
    m_mouseWasDown = mouseDown;
  }
  rebuildVisual();
  return action;
}

void
EditorConfirmDialog::rebuildVisual()
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

  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  const float titleFontSize = std::max(12.0f, std::round(16.0f * fontScale));
  const float messageFontSize = m_fontSize;
  const float buttonFontSize = m_fontSize;

  const float t = std::clamp(m_animElapsed / 0.18f, 0.0f, 1.0f);
  const float ease = 1.0f - std::pow(1.0f - t, 3.0f);
  const float slideY = (1.0f - ease) * 16.0f;
  const unsigned char bgAlpha = static_cast<unsigned char>(190.0f * ease);

  // Full-screen animated backdrop
  m_visual.addFilledRect(
    0.0f, 0.0f, virtualWidth, virtualHeight, ColorRgba{ 0, 0, 0, bgAlpha });

  const float curPanelY = m_panelY - slideY;
  const float curButtonY = m_buttonY - slideY;

  // Dialog drop shadow
  m_visual.addFilledRect(
    m_panelX + 8.0f * fontScale,
    curPanelY + 8.0f * fontScale,
    m_panelWidth,
    m_panelHeight,
    ColorRgba{ 0, 0, 0, static_cast<unsigned char>(150 * ease) });

  // Dialog window surface & border
  m_visual.addFilledRect(m_panelX,
                         curPanelY,
                         m_panelWidth,
                         m_panelHeight,
                         ColorRgba{ 14, 21, 32, 255 });
  m_visual.addOutlineRect(m_panelX,
                          curPanelY,
                          m_panelWidth,
                          m_panelHeight,
                          ColorRgba{ 55, 80, 115, 255 },
                          1.0f);

  // Top glowing accent stripe
  const float stripePulse = 0.80f + 0.20f * std::sin(m_animElapsed * 6.0f);
  const unsigned char stripeAlpha =
    static_cast<unsigned char>(255.0f * stripePulse);
  m_visual.addFilledRect(m_panelX,
                         curPanelY,
                         m_panelWidth,
                         3.0f * fontScale,
                         ColorRgba{ 66, 214, 210, stripeAlpha });

  m_visual.addText("Unsaved Changes",
                   m_panelX + 18.0f * fontScale,
                   curPanelY + 18.0f * fontScale,
                   titleFontSize,
                   ColorRgba{ 255, 255, 255, 255 });
  m_visual.addText(m_message.empty() ? "Save the current scene?" : m_message,
                   m_panelX + 18.0f * fontScale,
                   curPanelY + 22.0f * fontScale + titleFontSize +
                     12.0f * fontScale,
                   messageFontSize,
                   UiTheme::textMuted());

  // Save button (primary with hover flare)
  const bool saveHovered = (m_hoveredButton == EditorConfirmAction::Save);
  m_visual.addFilledRect(m_saveX,
                         curButtonY,
                         m_buttonWidth,
                         m_buttonHeight,
                         saveHovered ? ColorRgba{ 35, 110, 155, 255 }
                                     : ColorRgba{ 25, 85, 120, 255 });
  m_visual.addOutlineRect(m_saveX,
                          curButtonY,
                          m_buttonWidth,
                          m_buttonHeight,
                          saveHovered ? ColorRgba{ 100, 240, 235, 255 }
                                      : ColorRgba{ 66, 214, 210, 200 },
                          1.0f);
  const float saveTextW = estimateTextWidth("Save", buttonFontSize);
  m_visual.addText("Save",
                   m_saveX + std::max(0.0f, (m_buttonWidth - saveTextW) * 0.5f),
                   curButtonY +
                     std::max(0.0f, (m_buttonHeight - buttonFontSize) * 0.5f),
                   buttonFontSize,
                   ColorRgba{ 255, 255, 255, 255 });

  // Don't Save button (destructive with hover flare)
  const bool discardHovered = (m_hoveredButton == EditorConfirmAction::Discard);
  m_visual.addFilledRect(m_discardX,
                         curButtonY,
                         m_buttonWidth,
                         m_buttonHeight,
                         discardHovered ? ColorRgba{ 90, 36, 45, 255 }
                                        : ColorRgba{ 65, 28, 35, 255 });
  m_visual.addOutlineRect(m_discardX,
                          curButtonY,
                          m_buttonWidth,
                          m_buttonHeight,
                          discardHovered ? ColorRgba{ 230, 80, 100, 255 }
                                         : ColorRgba{ 180, 60, 75, 200 },
                          1.0f);
  const float discardTextW = estimateTextWidth("Don't Save", buttonFontSize);
  m_visual.addText(
    "Don't Save",
    m_discardX + std::max(0.0f, (m_buttonWidth - discardTextW) * 0.5f),
    curButtonY + std::max(0.0f, (m_buttonHeight - buttonFontSize) * 0.5f),
    buttonFontSize,
    ColorRgba{ 255, 220, 220, 255 });

  // Cancel button (neutral with hover flare)
  const bool cancelHovered = (m_hoveredButton == EditorConfirmAction::Cancel);
  m_visual.addFilledRect(m_cancelX,
                         curButtonY,
                         m_buttonWidth,
                         m_buttonHeight,
                         cancelHovered ? ColorRgba{ 32, 46, 68, 255 }
                                       : ColorRgba{ 22, 32, 48, 255 });
  m_visual.addOutlineRect(m_cancelX,
                          curButtonY,
                          m_buttonWidth,
                          m_buttonHeight,
                          cancelHovered ? ColorRgba{ 70, 100, 135, 255 }
                                        : ColorRgba{ 45, 65, 90, 255 },
                          1.0f);
  const float cancelTextW = estimateTextWidth("Cancel", buttonFontSize);
  m_visual.addText(
    "Cancel",
    m_cancelX + std::max(0.0f, (m_buttonWidth - cancelTextW) * 0.5f),
    curButtonY + std::max(0.0f, (m_buttonHeight - buttonFontSize) * 0.5f),
    buttonFontSize,
    ColorRgba{ 200, 220, 240, 255 });
}

bool
EditorConfirmDialog::AppendCommands(Renderer* renderer)
{
  return m_visual.AppendCommands(renderer);
}
