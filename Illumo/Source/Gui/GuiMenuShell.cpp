#include <Illumo/Gui/GuiMenuShell.h>

#include <Illumo/Gui/GuiTypes.h>
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

// Below this envelope a spring's remaining ringing is a small fraction of a
// pixel even over a full panel lift, so clock-driven springs land exactly.
static const float kRestEnvelope = 0.0005f;

float
GuiEasing::springStep(float seconds, float frequencyHz, float dampingRatio)
{
  if (!std::isfinite(seconds) || seconds <= 0.0f) {
    return 0.0f;
  }
  if (!std::isfinite(frequencyHz) || frequencyHz <= 0.0f ||
      !std::isfinite(dampingRatio) || dampingRatio <= 0.0f) {
    return 1.0f;
  }
  const float omega = 6.28318531f * frequencyHz;
  if (dampingRatio >= 1.0f) {
    const float decay = std::exp(-omega * seconds);
    if (decay * (1.0f + omega * seconds) < kRestEnvelope) {
      return 1.0f;
    }
    return 1.0f - decay * (1.0f + omega * seconds);
  }
  const float zeta = dampingRatio;
  const float damped = omega * std::sqrt(1.0f - zeta * zeta);
  const float decay = std::exp(-zeta * omega * seconds);
  if (decay < kRestEnvelope) {
    return 1.0f;
  }
  return 1.0f - decay * (std::cos(damped * seconds) +
                         (zeta * omega / damped) * std::sin(damped * seconds));
}

float
GuiEasing::wobble(float seconds, float frequencyHz, float dampingRatio)
{
  if (!std::isfinite(seconds) || seconds < 0.0f ||
      !std::isfinite(frequencyHz) || frequencyHz <= 0.0f ||
      !std::isfinite(dampingRatio) || dampingRatio <= 0.0f) {
    return 0.0f;
  }
  const float omega = 6.28318531f * frequencyHz;
  const float zeta = std::min(dampingRatio, 0.99f);
  const float decay = std::exp(-zeta * omega * seconds);
  if (decay < kRestEnvelope * 4.0f) {
    return 0.0f;
  }
  return decay * std::cos(omega * std::sqrt(1.0f - zeta * zeta) * seconds);
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
GuiSpring::configure(const GuiSpringTuning& tuning)
{
  configure(tuning.frequencyHz, tuning.dampingRatio);
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
GuiSpringArray::configure(const GuiSpringTuning& tuning)
{
  configure(tuning.frequencyHz, tuning.dampingRatio);
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
GuiSpringArray::velocity(int index) const
{
  if (index < 0 || index >= kCapacity) {
    return 0.0f;
  }
  return m_springs[static_cast<size_t>(index)].velocity();
}

GuiPanelTilt::GuiPanelTilt()
{
  m_x.configure(GuiMotion::kSway);
  m_y.configure(GuiMotion::kSway);
}

void
GuiPanelTilt::aim(float pointerX,
                  float pointerY,
                  float width,
                  float height,
                  bool active)
{
  const bool inside = active && std::isfinite(pointerX) &&
                      std::isfinite(pointerY) && width > 0.0f &&
                      height > 0.0f && pointerX >= 0.0f && pointerY >= 0.0f &&
                      pointerX <= width && pointerY <= height;
  if (!inside) {
    m_x.setTarget(0.0f);
    m_y.setTarget(0.0f);
    return;
  }
  m_x.setTarget(std::clamp(pointerX / width * 2.0f - 1.0f, -1.0f, 1.0f));
  m_y.setTarget(std::clamp(pointerY / height * 2.0f - 1.0f, -1.0f, 1.0f));
}

void
GuiPanelTilt::tick(float deltaSeconds, bool reducedMotion)
{
  if (reducedMotion) {
    m_x.setTarget(0.0f);
    m_y.setTarget(0.0f);
  }
  m_x.tick(deltaSeconds, reducedMotion);
  m_y.tick(deltaSeconds, reducedMotion);
}

void
GuiPanelTilt::level()
{
  m_x.snapTo(0.0f);
  m_y.snapTo(0.0f);
}

float
GuiPanelTilt::x() const
{
  return std::clamp(m_x.value(), -kMaximumTilt, kMaximumTilt);
}

float
GuiPanelTilt::y() const
{
  return std::clamp(m_y.value(), -kMaximumTilt, kMaximumTilt);
}

void
GuiPanelTilt::applyTo(GuiGlassStyle& style) const
{
  style.tiltX = std::clamp(x(), -1.0f, 1.0f);
  style.tiltY = std::clamp(y(), -1.0f, 1.0f);
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

// Entrance, press and value springs. The selection's head and tail take
// their feel from GuiMotion; these clock-driven ones are tuned here once.
static const float kPanelDropHz = 2.4f;
static const float kPanelDropDamping = 0.52f;
static const float kRowDropHz = 2.8f;
static const float kRowDropDamping = 0.45f;
static const float kPressWobbleHz = 3.4f;
static const float kPressWobbleDamping = 0.3f;
static const float kValueWobbleHz = 4.2f;
static const float kValueWobbleDamping = 0.34f;

GuiMenuAnimator::GuiMenuAnimator()
{
  m_selectionHead.configure(GuiMotion::kLiquidHead);
  m_selectionTail.configure(GuiMotion::kLiquidTail);
}

void
GuiMenuAnimator::restart()
{
  m_openElapsed = m_reducedMotion ? kOpenSettleSeconds : 0.0f;
  m_selectionHead.snapTo(m_selectionHead.target());
  m_selectionTail.snapTo(m_selectionTail.target());
  m_selectionElapsed = kSelectionSettleSeconds;
  m_pressElapsed = kPressWobbleSeconds;
  m_valuePulseElapsed = kValueWobbleSeconds;
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
    m_openElapsed = kOpenSettleSeconds;
    m_selectionHead.tick(deltaSeconds, true);
    m_selectionTail.tick(deltaSeconds, true);
    m_selectionElapsed = kSelectionSettleSeconds;
    m_pressElapsed = kPressWobbleSeconds;
    m_valuePulseElapsed = kValueWobbleSeconds;
    m_ambientPhase = 0.0f;
    m_caretElapsed = 0.0f;
    return;
  }
  m_openElapsed = std::min(kOpenSettleSeconds, m_openElapsed + deltaSeconds);
  // A long stall integrates in spring-sized steps so the drop still lands.
  float remaining = std::min(deltaSeconds, kMaximumSelectionStepSeconds);
  while (remaining > 0.0f &&
         (!m_selectionHead.settled() || !m_selectionTail.settled())) {
    const float step = std::min(remaining, GuiSpring::kMaximumStepSeconds);
    m_selectionHead.tick(step, false);
    m_selectionTail.tick(step, false);
    remaining -= step;
  }
  m_selectionElapsed =
    std::min(kSelectionSettleSeconds, m_selectionElapsed + deltaSeconds);
  m_pressElapsed = std::min(kPressWobbleSeconds, m_pressElapsed + deltaSeconds);
  m_valuePulseElapsed =
    std::min(kValueWobbleSeconds, m_valuePulseElapsed + deltaSeconds);
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
  if (m_reducedMotion) {
    return 0.0f;
  }
  return (1.0f - GuiEasing::springStep(
                   m_openElapsed, kPanelDropHz, kPanelDropDamping)) *
         kPanelLiftPixels;
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

float
GuiMenuAnimator::rowDrop(int row, int firstVisibleRow) const
{
  if (m_reducedMotion) {
    return 0.0f;
  }
  const float delay = static_cast<float>(std::max(0, row - firstVisibleRow)) *
                      kRowRevealStaggerSeconds;
  return 1.0f - GuiEasing::springStep(
                  m_openElapsed - delay, kRowDropHz, kRowDropDamping);
}

void
GuiMenuAnimator::beginSelectionTravel(float fromRow, float toRow)
{
  if (!std::isfinite(fromRow) || !std::isfinite(toRow)) {
    return;
  }
  // From rest the drop leaves the old row; mid-flight it keeps its momentum
  // and simply pours toward the new row.
  if (m_selectionHead.settled() && m_selectionTail.settled()) {
    m_selectionHead.snapTo(fromRow);
    m_selectionTail.snapTo(fromRow);
  }
  m_selectionHead.setTarget(toRow);
  m_selectionTail.setTarget(toRow);
  m_selectionDirection = toRow >= m_selectionHead.value() ? 1.0f : -1.0f;
  m_selectionElapsed = 0.0f;
}

float
GuiMenuAnimator::softOvershoot(float edge) const
{
  const float target = m_selectionHead.target();
  const float beyond = (edge - target) * m_selectionDirection;
  if (beyond <= 0.0f) {
    return edge;
  }
  return target + m_selectionDirection * kSelectionOvershootRows *
                    std::tanh(beyond / kSelectionOvershootRows);
}

void
GuiMenuAnimator::settleSelection()
{
  m_selectionHead.snapTo(m_selectionHead.target());
  m_selectionTail.snapTo(m_selectionTail.target());
  m_selectionElapsed = kSelectionSettleSeconds;
}

bool
GuiMenuAnimator::selectionMoving(float selectedRow) const
{
  // A selection changed without a travel (or already at rest) reads as its
  // row, so callers never see a stale target.
  return !m_reducedMotion &&
         (!m_selectionHead.settled() || !m_selectionTail.settled()) &&
         m_selectionHead.target() == selectedRow;
}

GuiSelectionSpan
GuiMenuAnimator::selectionSpan(float selectedRow) const
{
  GuiSelectionSpan span;
  span.leading = selectedRow;
  span.trailing = selectedRow;
  if (!selectionMoving(selectedRow)) {
    return span;
  }
  span.leading = softOvershoot(m_selectionHead.value());
  span.trailing = softOvershoot(m_selectionTail.value());
  // Closing speed along the drop: the tail catching the head bulges it, the
  // head pulling away stretches it thin.
  const float separation = span.leading - span.trailing;
  const float direction = separation != 0.0f
                            ? (separation > 0.0f ? 1.0f : -1.0f)
                            : (selectedRow >= span.trailing ? 1.0f : -1.0f);
  const float closing =
    (m_selectionTail.velocity() - m_selectionHead.velocity()) * direction;
  span.squash = std::clamp(closing * kSquashPerRowSpeed, -1.0f, 1.0f);
  return span;
}

float
GuiMenuAnimator::selectionSheen() const
{
  if (m_reducedMotion) {
    return -1.0f;
  }
  // The sweep starts as the head lands on the new row.
  const float elapsed = m_selectionElapsed - kSheenDelaySeconds;
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
GuiMenuAnimator::pressWobble() const
{
  if (m_reducedMotion || m_pressElapsed >= kPressWobbleSeconds) {
    return 0.0f;
  }
  return GuiEasing::wobble(m_pressElapsed, kPressWobbleHz, kPressWobbleDamping);
}

float
GuiMenuAnimator::selectionPosition(float selectedRow) const
{
  if (!selectionMoving(selectedRow)) {
    return selectedRow;
  }
  return (softOvershoot(m_selectionHead.value()) +
          softOvershoot(m_selectionTail.value())) *
         0.5f;
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

float
GuiMenuAnimator::valueWobble() const
{
  if (m_reducedMotion || m_valuePulseElapsed >= kValueWobbleSeconds) {
    return 0.0f;
  }
  // Starts at rest and leaps out: the sine-phased swing of a kicked spring,
  // normalized so its first peak reads 1.
  const float omega = 6.28318531f * kValueWobbleHz;
  const float decayRate = kValueWobbleDamping * omega;
  const float damped =
    omega * std::sqrt(1.0f - kValueWobbleDamping * kValueWobbleDamping);
  const float peakSeconds = std::atan2(damped, decayRate) / damped;
  const float peak =
    std::exp(-decayRate * peakSeconds) * std::sin(damped * peakSeconds);
  const float swing = std::exp(-decayRate * m_valuePulseElapsed) *
                      std::sin(damped * m_valuePulseElapsed);
  return static_cast<float>(m_valuePulseDirection) * swing / peak;
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
