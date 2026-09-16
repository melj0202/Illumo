#include <Illumo/Gui/GuiMenuShell.h>

#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/KeyCode.h>
#include <algorithm>
#include <array>
#include <cmath>

float
GuiEasing::outCubic(float progress)
{
  const float remaining = 1.0f - std::clamp(progress, 0.0f, 1.0f);
  return 1.0f - remaining * remaining * remaining;
}

float
GuiEasing::approach(float current,
                    float target,
                    float responseRate,
                    float deltaSeconds)
{
  if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f ||
      responseRate <= 0.0f) {
    return current;
  }
  return current +
         (target - current) * (1.0f - std::exp(-responseRate * deltaSeconds));
}

float
GuiEasing::approachLinear(float current,
                          float target,
                          float responseRate,
                          float deltaSeconds,
                          float snapEpsilon)
{
  if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f ||
      responseRate <= 0.0f) {
    return current;
  }
  const float blend = std::min(1.0f, deltaSeconds * responseRate);
  const float next = current + (target - current) * blend;
  if (snapEpsilon > 0.0f && std::abs(next - target) < snapEpsilon) {
    return target;
  }
  return next;
}

void
GuiMenuAnimator::restart()
{
  m_openElapsed = m_reducedMotion ? kOpenSeconds : 0.0f;
  m_selectionFromRow = 0.0f;
  m_selectionElapsed = kSelectionSeconds;
  m_valuePulseElapsed = kValuePulseSeconds;
  m_ambientPhase = 0.0f;
  m_caretElapsed = 0.0f;
}

void
GuiMenuAnimator::tick(float deltaSeconds)
{
  if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
    return;
  }
  if (m_reducedMotion) {
    m_openElapsed = kOpenSeconds;
    m_selectionElapsed = kSelectionSeconds;
    m_valuePulseElapsed = kValuePulseSeconds;
    m_ambientPhase = 0.0f;
    m_caretElapsed = 0.0f;
    return;
  }
  m_openElapsed = std::min(kOpenSeconds, m_openElapsed + deltaSeconds);
  m_selectionElapsed =
    std::min(kSelectionSeconds, m_selectionElapsed + deltaSeconds);
  m_valuePulseElapsed =
    std::min(kValuePulseSeconds, m_valuePulseElapsed + deltaSeconds);
  m_ambientPhase = std::fmod(
    m_ambientPhase + std::min(deltaSeconds, kMaximumAmbientStepSeconds),
    kAmbientPeriodSeconds);
  m_caretElapsed =
    std::fmod(m_caretElapsed + deltaSeconds, kCaretBlinkPeriodSeconds);
}

float
GuiMenuAnimator::openProgress() const
{
  if (m_reducedMotion) {
    return 1.0f;
  }
  return std::clamp(m_openElapsed / kOpenSeconds, 0.0f, 1.0f);
}

float
GuiMenuAnimator::openReveal(float seconds) const
{
  if (m_reducedMotion) {
    return 1.0f;
  }
  if (seconds <= 0.0f) {
    return 1.0f;
  }
  return std::clamp(m_openElapsed / seconds, 0.0f, 1.0f);
}

float
GuiMenuAnimator::panelReveal() const
{
  return GuiEasing::outCubic(openReveal(kPanelRevealSeconds));
}

float
GuiMenuAnimator::panelOffsetY() const
{
  return (1.0f - panelReveal()) * kPanelLiftPixels;
}

float
GuiMenuAnimator::rowReveal(int row, int firstVisibleRow) const
{
  if (m_reducedMotion) {
    return 1.0f;
  }
  const float delay = static_cast<float>(std::max(0, row - firstVisibleRow)) *
                      kRowRevealStaggerSeconds;
  return GuiEasing::outCubic(
    std::clamp((m_openElapsed - delay) / kRowRevealSeconds, 0.0f, 1.0f));
}

void
GuiMenuAnimator::beginSelectionTravel(float fromPosition)
{
  m_selectionFromRow = fromPosition;
  m_selectionElapsed = 0.0f;
}

void
GuiMenuAnimator::settleSelection()
{
  m_selectionElapsed = kSelectionSeconds;
}

float
GuiMenuAnimator::selectionPosition(float selectedRow) const
{
  if (m_reducedMotion) {
    return selectedRow;
  }
  const float progress = GuiEasing::outCubic(
    std::clamp(m_selectionElapsed / kSelectionSeconds, 0.0f, 1.0f));
  return m_selectionFromRow + (selectedRow - m_selectionFromRow) * progress;
}

void
GuiMenuAnimator::triggerValuePulse()
{
  m_valuePulseElapsed = 0.0f;
}

float
GuiMenuAnimator::valuePulse() const
{
  if (m_reducedMotion) {
    return 0.0f;
  }
  return 1.0f -
         std::clamp(m_valuePulseElapsed / kValuePulseSeconds, 0.0f, 1.0f);
}

bool
GuiMenuAnimator::caretVisible() const
{
  return m_reducedMotion || m_caretElapsed < kCaretBlinkPeriodSeconds * 0.55f;
}

GuiPanelFit
GuiPanelLayout::fit(IRenderWindow* window,
                    Renderer* renderer,
                    GameVisual* visual)
{
  int width = kFallbackWindowWidth;
  int height = kFallbackWindowHeight;
  if (window != nullptr) {
    const std::array<int, 2> dimensions = window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }
  // The renderer's UI scale is a floor of one; the window may still be too
  // small to host it, so the panel scales down uniformly rather than clipping.
  const float preferredScale =
    renderer != nullptr ? std::max(1.0f, renderer->getUiScale()) : 1.0f;
  GuiPanelFit result;
  result.layoutScale =
    std::min(preferredScale,
             std::max(kMinimumScale,
                      std::min(static_cast<float>(width) / kDesignWidth,
                               static_cast<float>(height) / kDesignHeight)));
  result.virtualWidth = static_cast<float>(width) / result.layoutScale;
  result.virtualHeight = static_cast<float>(height) / result.layoutScale;
  if (visual != nullptr) {
    Transform2D transform;
    transform.scaleX = result.layoutScale / preferredScale;
    transform.scaleY = transform.scaleX;
    visual->setTransform(transform);
  }
  return result;
}

GuiPanelFit
GuiPanelLayout::viewport(IRenderWindow* window, Renderer* renderer)
{
  int width = kFallbackWindowWidth;
  int height = kFallbackWindowHeight;
  if (window != nullptr) {
    const std::array<int, 2> dimensions = window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }
  const float scale = renderer != nullptr ? renderer->getUiScale() : 1.0f;
  GuiPanelFit result;
  result.layoutScale = scale > 0.0f ? scale : 1.0f;
  result.virtualWidth = static_cast<float>(width) / result.layoutScale;
  result.virtualHeight = static_cast<float>(height) / result.layoutScale;
  return result;
}

int
GuiPanelLayout::visibleRowCount(float availableHeight, int rowCount)
{
  if (rowCount <= 1) {
    return std::max(1, rowCount);
  }
  return std::clamp(
    static_cast<int>(std::max(0.0f, availableHeight) / kMinimumRowHeight),
    1,
    rowCount);
}

int
GuiPanelLayout::clampFirstVisibleRow(int firstVisibleRow,
                                     int rowCount,
                                     int visibleRows)
{
  return std::clamp(firstVisibleRow, 0, std::max(0, rowCount - visibleRows));
}

int
GuiPanelLayout::scrollToRow(int firstVisibleRow, int row, int visibleRows)
{
  if (visibleRows <= 0) {
    return firstVisibleRow;
  }
  if (row < firstVisibleRow) {
    return row;
  }
  if (row >= firstVisibleRow + visibleRows) {
    return row - visibleRows + 1;
  }
  return firstVisibleRow;
}

int
GuiPanelLayout::applyWheelScroll(InputManager* input,
                                 int firstVisibleRow,
                                 int rowCount,
                                 int visibleRows,
                                 bool* scrolled)
{
  if (scrolled != nullptr) {
    *scrolled = false;
  }
  if (input == nullptr) {
    return firstVisibleRow;
  }
  double* wheel = input->getMouseScrollOffset();
  if (wheel == nullptr || !std::isfinite(*wheel) || *wheel == 0.0) {
    return firstVisibleRow;
  }
  const int rows = static_cast<int>(std::ceil(
    std::min(std::abs(*wheel), static_cast<double>(std::max(1, rowCount)))));
  const int next = clampFirstVisibleRow(
    firstVisibleRow + (*wheel > 0.0 ? -rows : rows), rowCount, visibleRows);
  *wheel = 0.0;
  if (scrolled != nullptr) {
    *scrolled = true;
  }
  return next;
}

void
GuiPointerTracker::reset(bool pressed)
{
  m_x = -1.0f;
  m_y = -1.0f;
  m_moved = false;
  m_pressed = pressed;
  m_clicked = false;
  m_released = false;
}

void
GuiPointerTracker::adoptPressed(bool pressed)
{
  m_pressed = pressed;
  m_clicked = false;
  m_released = false;
}

void
GuiPointerTracker::forgetPosition()
{
  m_x = -1.0f;
  m_y = -1.0f;
  m_moved = false;
  m_clicked = false;
  m_released = false;
}

void
GuiPointerTracker::sample(IRenderWindow* window,
                          InputManager* input,
                          float layoutScale)
{
  std::array<double, 2> coords{ 0.0, 0.0 };
  if (window != nullptr) {
    coords = window->getMouseCoords();
  } else if (input != nullptr) {
    coords = input->getMousePosition();
  }
  const float scale = layoutScale > 0.0f ? layoutScale : 1.0f;
  const float pointerX = static_cast<float>(coords[0]) / scale;
  const float pointerY = static_cast<float>(coords[1]) / scale;
  m_moved = pointerX != m_x || pointerY != m_y;
  m_x = pointerX;
  m_y = pointerY;
  if (input == nullptr) {
    // No input device this frame: report position only. Inventing a release
    // here would fire a spurious click once the device returns.
    m_clicked = false;
    m_released = false;
    return;
  }
  const bool down = input->isMouseButtonPressed(KeyCode::MouseLeft);
  m_clicked = down && !m_pressed;
  m_released = !down && m_pressed;
  m_pressed = down;
}
