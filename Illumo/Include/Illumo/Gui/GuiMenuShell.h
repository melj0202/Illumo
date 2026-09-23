#pragma once

#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <array>

class GameVisual;
class InputManager;
class IRenderWindow;
class Renderer;

// Shared, stateless easing curves for Illumo's primitive-composed overlays.
class GuiEasing final
{
public:
  // Decelerating cubic used by every panel reveal and selection glide.
  static float outCubic(float progress);

  // Decelerating curve that overshoots its target before settling (clamped
  // progress; overshoot 1.2 peaks near 1.05).
  static float outBack(float progress, float overshoot = 1.2f);

  // Symmetric ease for back-and-forth motion.
  static float inOutCubic(float progress);

  // Frame-rate independent exponential approach toward a target, for per-item
  // emphasis that never quite arrives.
  static float approach(float current,
                        float target,
                        float responseRate,
                        float deltaSeconds);

  // Clamped linear approach that snaps once within snapEpsilon, for docked
  // panel slides that must settle exactly. A zero epsilon never snaps.
  static float approachLinear(float current,
                              float target,
                              float responseRate,
                              float deltaSeconds,
                              float snapEpsilon);
};

// Damped spring toward a target, for hover emphasis, knob travel and other
// motion that should feel physical. Each step uses the exact closed-form
// solution, so it is stable at any frame time; underdamped springs overshoot
// once and settle. Values snap exactly onto the target once at rest.
class GuiSpring final
{
public:
  static constexpr float kMaximumStepSeconds = 0.25f;

  void configure(float frequencyHz, float dampingRatio);
  void snapTo(float value);
  void setTarget(float target) { m_target = target; }
  // Adds velocity (units per second), e.g. a press squish or a value nudge.
  void kick(float velocity);
  // Reduced motion snaps to the target immediately.
  void tick(float deltaSeconds, bool reducedMotion);

  float value() const { return m_value; }
  float target() const { return m_target; }
  float velocity() const { return m_velocity; }
  bool settled() const { return m_value == m_target && m_velocity == 0.0f; }

private:
  float m_frequencyHz = 3.0f;
  float m_dampingRatio = 0.75f;
  float m_value = 0.0f;
  float m_target = 0.0f;
  float m_velocity = 0.0f;
};

// Fixed bank of springs indexed by row, for per-row hover and focus emphasis.
// Out-of-range rows read as zero and ignore writes.
class GuiSpringArray final
{
public:
  static constexpr int kCapacity = 64;

  void configure(float frequencyHz, float dampingRatio);
  void setTarget(int index, float target);
  // Target one for `row` and zero for every other row below `count`.
  void focusOnly(int row, int count);
  void snap(int index, float value);
  void snapAll();
  void tick(float deltaSeconds, bool reducedMotion);
  float value(int index) const;

private:
  std::array<GuiSpring, kCapacity> m_springs{};
};

// Leading and trailing edges of a travelling selection, in row units. While
// moving, the leading edge races ahead with a slight overshoot and the
// trailing edge follows, so a highlight spanning [min, max + 1] stretches.
struct GuiSelectionSpan
{
  float leading = 0.0f;
  float trailing = 0.0f;
};

// Reveal, selection, value-pulse, ambient, and caret clocks shared by the
// primitive-composed menus. The animator owns time only: it never draws, reads
// input, or models rows beyond their index, so each overlay keeps its own
// layout and GameVisual composition while sharing one motion vocabulary.
class GuiMenuAnimator final
{
public:
  static constexpr float kOpenSeconds = 0.42f;
  static constexpr float kPanelRevealSeconds = 0.24f;
  static constexpr float kRowRevealSeconds = 0.22f;
  static constexpr float kRowRevealStaggerSeconds = 0.012f;
  static constexpr float kSelectionSeconds = 0.14f;
  static constexpr float kSelectionLeadSeconds = 0.20f;
  static constexpr float kSelectionTrailSeconds = 0.32f;
  static constexpr float kSheenSeconds = 0.6f;
  static constexpr float kSelectionSettleSeconds =
    kSelectionTrailSeconds + kSheenSeconds;
  static constexpr float kPressSeconds = 0.32f;
  static constexpr float kValuePulseSeconds = 0.20f;
  static constexpr float kCaretBlinkPeriodSeconds = 1.0f;
  static constexpr float kAmbientPeriodSeconds = 12.0f;
  static constexpr float kPanelLiftPixels = 18.0f;
  static constexpr float kMaximumAmbientStepSeconds = 0.1f;

  void setReducedMotion(bool enabled) { m_reducedMotion = enabled; }
  bool reducedMotion() const { return m_reducedMotion; }

  // Restart every clock for a freshly opened overlay. Reduced motion opens
  // fully revealed and settled.
  void restart();

  // Advance every clock by one frame. Non-finite and non-positive deltas are
  // ignored so a stalled or rewound frame preserves animation state.
  void tick(float deltaSeconds);

  // Normalized open progress, and the same clock read over a custom duration
  // for overlays that fade chrome faster than the panel itself.
  float openProgress() const;
  float openReveal(float seconds) const;

  float panelReveal() const;
  float panelOffsetY() const;

  // Staggered per-row entrance; rows above the window carry no extra delay.
  float rowReveal(int row, int firstVisibleRow) const;

  // Selection travel is expressed in row units so callers keep their own row
  // model. Pass the position the highlight is leaving, then read the animated
  // position against the row it is travelling to.
  void beginSelectionTravel(float fromPosition);
  void settleSelection();
  float selectionPosition(float selectedRow) const;
  // Stretching edges of the same travel (see GuiSelectionSpan).
  GuiSelectionSpan selectionSpan(float selectedRow) const;
  // Highlight sweep across the arrived selection, 0..1, or -1 when idle.
  float selectionSheen() const;

  // Short press feedback (squish, ripple): 1 at the press decaying to 0.
  void triggerPress();
  float pressPulse() const;
  // 0..1 progress through the press, or -1 when no press is animating.
  float pressProgress() const;

  void triggerValuePulse();
  // Records which way a stepped value moved (-1 or +1) for arrow nudges.
  void triggerValuePulse(int direction);
  float valuePulse() const;
  int valuePulseDirection() const { return m_valuePulseDirection; }

  float ambientPhase() const { return m_ambientPhase; }

  bool caretVisible() const;
  void resetCaret() { m_caretElapsed = 0.0f; }

private:
  bool m_reducedMotion = false;
  float m_openElapsed = 0.0f;
  float m_selectionFromRow = 0.0f;
  float m_selectionElapsed = kSelectionSettleSeconds;
  float m_pressElapsed = kPressSeconds;
  float m_valuePulseElapsed = kValuePulseSeconds;
  int m_valuePulseDirection = 0;
  float m_ambientPhase = 0.0f;
  float m_caretElapsed = 0.0f;
};

// Window size and UI scale resolved into the shared menu virtual space.
struct GuiPanelFit
{
  float layoutScale = 1.0f;
  float virtualWidth = 640.0f;
  float virtualHeight = 480.0f;
};

// Uniform virtual-resolution fitting and row-window arithmetic shared by the
// overlays. Every menu lays out against the same minimum design space and
// scales down only when the window cannot host the user's UI scale, so hit
// testing and drawing always share one coordinate system.
class GuiPanelLayout final
{
public:
  static constexpr float kDesignWidth = 640.0f;
  static constexpr float kDesignHeight = 480.0f;
  static constexpr float kMinimumScale = 0.25f;
  static constexpr float kMinimumRowHeight = 34.0f;
  static constexpr int kFallbackWindowWidth = 1280;
  static constexpr int kFallbackWindowHeight = 720;

  // Resolve the fit and, when a visual is supplied, apply the compensating
  // transform that cancels the renderer's UI scale.
  static GuiPanelFit fit(IRenderWindow* window,
                         Renderer* renderer,
                         GameVisual* visual);

  // The window expressed in UI-scale units, with no design-size floor and no
  // transform. Docked panels and bars anchor to the real viewport rather than
  // fitting a centered panel into it, so they size against this instead.
  static GuiPanelFit viewport(IRenderWindow* window, Renderer* renderer);

  // How many uniform rows fit in the body area, never fewer than one.
  static int visibleRowCount(float availableHeight, int rowCount);

  static int clampFirstVisibleRow(int firstVisibleRow,
                                  int rowCount,
                                  int visibleRows);

  // Smallest scroll offset that keeps the given row inside the window.
  static int scrollToRow(int firstVisibleRow, int row, int visibleRows);

  // Consume a pending wheel delta as whole rows. Reports whether the wheel
  // moved so callers can suppress hover changes during the same frame.
  static int applyWheelScroll(InputManager* input,
                              int firstVisibleRow,
                              int rowCount,
                              int visibleRows,
                              bool* scrolled);
};

// Pointer state sampled in menu virtual space, with movement and press-edge
// detection. Overlays open under a held button, so seeding the pressed state
// swallows the click that opened them.
class GuiPointerTracker final
{
public:
  void reset(bool pressed);
  void forgetPosition();

  // Adopt the current button state on a frame where another surface owns
  // input, so the edge it consumed is not replayed here. This keeps the last
  // sampled position, which reset() deliberately discards.
  void adoptPressed(bool pressed);

  // Sample once per update; the press edge is consumed by that single call.
  void sample(IRenderWindow* window, InputManager* input, float layoutScale);

  float x() const { return m_x; }
  float y() const { return m_y; }
  bool moved() const { return m_moved; }
  bool pressed() const { return m_pressed; }
  bool clicked() const { return m_clicked; }
  // The matching release edge, for gestures that complete on button up.
  bool released() const { return m_released; }

private:
  float m_x = -1.0f;
  float m_y = -1.0f;
  bool m_moved = false;
  bool m_pressed = false;
  bool m_clicked = false;
  bool m_released = false;
};
