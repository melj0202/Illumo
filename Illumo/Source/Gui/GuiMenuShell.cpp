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
GuiEasing::outBack(float progress, float overshoot)
{
  const float t = std::clamp(progress, 0.0f, 1.0f) - 1.0f;
  const float s = std::isfinite(overshoot) ? std::max(0.0f, overshoot) : 0.0f;
  return 1.0f + t * t * ((s + 1.0f) * t + s);
}

float
GuiEasing::inOutCubic(float progress)
{
  const float t = std::clamp(progress, 0.0f, 1.0f);
  if (t < 0.5f) {
    return 4.0f * t * t * t;
  }
  const float remaining = -2.0f * t + 2.0f;
  return 1.0f - remaining * remaining * remaining * 0.5f;
}

void
GuiSpring::configure(float frequencyHz, float dampingRatio)
{
  if (std::isfinite(frequencyHz) && frequencyHz > 0.0f) {
    m_frequencyHz = frequencyHz;
  }
  if (std::isfinite(dampingRatio) && dampingRatio > 0.0f) {
    m_dampingRatio = dampingRatio;
  }
}

void
GuiSpring::snapTo(float value)
{
  if (!std::isfinite(value)) {
    return;
  }
  m_value = value;
  m_target = value;
  m_velocity = 0.0f;
}

void
GuiSpring::kick(float velocity)
{
  if (std::isfinite(velocity)) {
    m_velocity += velocity;
  }
}

void
GuiSpring::tick(float deltaSeconds, bool reducedMotion)
{
  if (!std::isfinite(m_target)) {
    m_target = m_value;
  }
  if (reducedMotion) {
    m_value = m_target;
    m_velocity = 0.0f;
    return;
  }
  if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
    return;
  }
  const float t = std::min(deltaSeconds, kMaximumStepSeconds);
  const float omega = 6.28318531f * m_frequencyHz;
  const float x0 = m_value - m_target;
  const float v0 = m_velocity;
  float x = 0.0f;
  float v = 0.0f;
  if (m_dampingRatio < 1.0f) {
    const float zeta = m_dampingRatio;
    const float damped = omega * std::sqrt(1.0f - zeta * zeta);
    const float decay = std::exp(-zeta * omega * t);
    const float c = std::cos(damped * t);
    const float s = std::sin(damped * t);
    x = decay * (x0 * c + ((v0 + zeta * omega * x0) / damped) * s);
    v = decay *
        (v0 * c - ((omega * omega * x0 + zeta * omega * v0) / damped) * s);
  } else {
    // Critically damped: the fastest approach that never overshoots.
    const float decay = std::exp(-omega * t);
    const float slope = v0 + omega * x0;
    x = (x0 + slope * t) * decay;
    v = (v0 - omega * slope * t) * decay;
  }
  if (!std::isfinite(x) || !std::isfinite(v) ||
      (std::abs(x) < 0.001f && std::abs(v) < 0.01f)) {
    m_value = m_target;
    m_velocity = 0.0f;
    return;
  }
  m_value = m_target + x;
  m_velocity = v;
}

void
GuiSpringArray::configure(float frequencyHz, float dampingRatio)
{
  for (GuiSpring& spring : m_springs) {
    spring.configure(frequencyHz, dampingRatio);
  }
}

void
GuiSpringArray::setTarget(int index, float target)
{
  if (index >= 0 && index < kCapacity) {
    m_springs[static_cast<size_t>(index)].setTarget(target);
  }
}

void
GuiSpringArray::focusOnly(int row, int count)
{
  const int limit = std::clamp(count, 0, kCapacity);
  for (int index = 0; index < limit; ++index) {
    m_springs[static_cast<size_t>(index)].setTarget(index == row ? 1.0f : 0.0f);
  }
}

void
GuiSpringArray::snap(int index, float value)
{
  if (index >= 0 && index < kCapacity) {
    m_springs[static_cast<size_t>(index)].snapTo(value);
  }
}

void
GuiSpringArray::snapAll()
{
  for (GuiSpring& spring : m_springs) {
    spring.snapTo(spring.target());
  }
}

void
GuiSpringArray::tick(float deltaSeconds, bool reducedMotion)
{
  for (GuiSpring& spring : m_springs) {
    if (!spring.settled() || reducedMotion) {
      spring.tick(deltaSeconds, reducedMotion);
    }
  }
}

float
GuiSpringArray::value(int index) const
{
  if (index < 0 || index >= kCapacity) {
    return 0.0f;
  }
  return m_springs[static_cast<size_t>(index)].value();
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
  m_selectionElapsed = kSelectionSettleSeconds;
  m_pressElapsed = kPressSeconds;
  m_valuePulseElapsed = kValuePulseSeconds;
  m_valuePulseDirection = 0;
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
    m_selectionElapsed = kSelectionSettleSeconds;
    m_pressElapsed = kPressSeconds;
    m_valuePulseElapsed = kValuePulseSeconds;
    m_ambientPhase = 0.0f;
    m_caretElapsed = 0.0f;
    return;
  }
  m_openElapsed = std::min(kOpenSeconds, m_openElapsed + deltaSeconds);
  m_selectionElapsed =
    std::min(kSelectionSettleSeconds, m_selectionElapsed + deltaSeconds);
  m_pressElapsed = std::min(kPressSeconds, m_pressElapsed + deltaSeconds);
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
  m_selectionElapsed = kSelectionSettleSeconds;
}

GuiSelectionSpan
GuiMenuAnimator::selectionSpan(float selectedRow) const
{
  GuiSelectionSpan span;
  if (m_reducedMotion) {
    span.leading = selectedRow;
    span.trailing = selectedRow;
    return span;
  }
  const float travel = selectedRow - m_selectionFromRow;
  span.leading =
    m_selectionFromRow +
    travel * GuiEasing::outBack(m_selectionElapsed / kSelectionLeadSeconds);
  span.trailing =
    m_selectionFromRow +
    travel * GuiEasing::outCubic(m_selectionElapsed / kSelectionTrailSeconds);
  return span;
}

float
GuiMenuAnimator::selectionSheen() const
{
  if (m_reducedMotion) {
    return -1.0f;
  }
  // The sweep starts as the leading edge lands on the new row.
  const float elapsed = m_selectionElapsed - kSelectionLeadSeconds * 0.7f;
  if (elapsed <= 0.0f || elapsed >= kSheenSeconds) {
    return -1.0f;
  }
  return elapsed / kSheenSeconds;
}

void
GuiMenuAnimator::triggerPress()
{
  m_pressElapsed = 0.0f;
}

float
GuiMenuAnimator::pressPulse() const
{
  if (m_reducedMotion) {
    return 0.0f;
  }
  return 1.0f - std::clamp(m_pressElapsed / kPressSeconds, 0.0f, 1.0f);
}

float
GuiMenuAnimator::pressProgress() const
{
  if (m_reducedMotion || m_pressElapsed >= kPressSeconds) {
    return -1.0f;
  }
  return std::clamp(m_pressElapsed / kPressSeconds, 0.0f, 1.0f);
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
  m_valuePulseDirection = 0;
}

void
GuiMenuAnimator::triggerValuePulse(int direction)
{
  m_valuePulseElapsed = 0.0f;
  m_valuePulseDirection = direction < 0 ? -1 : (direction > 0 ? 1 : 0);
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

void
GuiPointerTracker::sample(const PanelSurfacePointer& pointer, float layoutScale)
{
  const float scale = layoutScale > 0.0f ? layoutScale : 1.0f;
  const float pointerX = static_cast<float>(pointer.x) / scale;
  const float pointerY = static_cast<float>(pointer.y) / scale;
  m_moved = pointerX != m_x || pointerY != m_y;
  m_x = pointerX;
  m_y = pointerY;
  m_clicked = pointer.left && !m_pressed;
  m_released = !pointer.left && m_pressed;
  m_pressed = pointer.left;
}
