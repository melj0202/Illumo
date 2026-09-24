#pragma once

#include <Illumo/Gui/PanelSurfaces.h>
#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <array>

class GameVisual;
struct GuiGlassStyle;
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

  // Where a damped spring released from 0 toward 1 is after `seconds`, for
  // clock-driven motion that should land like a drop: it overshoots and rings
  // by the damping ratio, reads 0 before release and exactly 1 once the
  // ringing is imperceptible.
  static float springStep(float seconds, float frequencyHz, float dampingRatio);

  // A decaying jiggle that starts at 1 and swings through alternating signs
  // toward 0, for jelly squash after a press or a value change. Reads 0
  // before the start and once the swing is imperceptible.
  static float wobble(float seconds, float frequencyHz, float dampingRatio);
};

// A named spring feel. Screens configure springs from the GuiMotion presets
// so the whole product shares one physical character.
struct GuiSpringTuning
{
  float frequencyHz = 3.0f;
  float dampingRatio = 0.75f;
};

// The shared motion character: soft, liquid, and a little bouncy.
class GuiMotion final
{
public:
  // Row hover and focus: quick, overshooting like jelly before settling.
  static constexpr GuiSpringTuning kJelly{ 3.4f, 0.42f };
  // Small parts that snap between states (toggle knobs, lit chips): springy.
  static constexpr GuiSpringTuning kBoing{ 4.4f, 0.38f };
  // The head of a travelling selection: races ahead and overshoots.
  static constexpr GuiSpringTuning kLiquidHead{ 4.6f, 0.45f };
  // The tail of a travelling selection: lags, then catches up with a slosh.
  static constexpr GuiSpringTuning kLiquidTail{ 2.9f, 0.58f };
  // Large surfaces and crossfades: one soft swell, no visible ringing.
  static constexpr GuiSpringTuning kSwell{ 2.2f, 0.72f };
  // Scroll thumbs and followers: smooth, with a hint of follow-through.
  static constexpr GuiSpringTuning kGlide{ 3.0f, 0.82f };
  // Pointer-following light and parallax: lazy and fluid, like a current.
  static constexpr GuiSpringTuning kDrift{ 1.1f, 0.62f };
  // A whole panel swivelling toward the pointer: responsive, settling with
  // a gentle sway.
  static constexpr GuiSpringTuning kSway{ 1.8f, 0.5f };
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
  void configure(const GuiSpringTuning& tuning);
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
  void configure(const GuiSpringTuning& tuning);
  void setTarget(int index, float target);
  // Target one for `row` and zero for every other row below `count`.
  void focusOnly(int row, int count);
  void snap(int index, float value);
  void snapAll();
  void tick(float deltaSeconds, bool reducedMotion);
  float value(int index) const;
  // Units per second, for squash and stretch along the direction of travel.
  float velocity(int index) const;

private:
  std::array<GuiSpring, kCapacity> m_springs{};
};

// A glass panel that swivels toward the pointer. Each layer shifts toward the
// pointer by its depth (virtual pixels at full tilt), so nearer layers travel
// further and the pane reads as turning in space; applyTo() hands the tilt to
// GuiGlassStyle for its shadow, glare and edge light. A screen shifts its
// layout origin by the body layer (bodyShiftX/Y) so its hit testing follows
// the drawing, then offsets the glass and decorative layers relative to it.
// The tilt swings level whenever the panel is not the pointer's target.
class GuiPanelTilt final
{
public:
  static constexpr float kGlassDepth = 4.0f;
  static constexpr float kFooterDepth = 5.0f;
  static constexpr float kBodyDepth = 7.0f;
  static constexpr float kHeaderDepth = 10.0f;
  static constexpr float kAccentDepth = 16.0f;
  static constexpr float kMaximumTilt = 1.25f;

  GuiPanelTilt();

  // Aim at a pointer in the panel's virtual space (width x height); an
  // inactive panel, or a pointer outside that space, swings level.
  void aim(float pointerX,
           float pointerY,
           float width,
           float height,
           bool active);
  void tick(float deltaSeconds, bool reducedMotion);
  void level();

  // Tilt per axis, positive toward the right and bottom.
  float x() const;
  float y() const;
  // Absolute shift of a layer at `depth`.
  float shiftX(float depth) const { return x() * depth; }
  float shiftY(float depth) const { return y() * depth; }
  // Shift of the body layer, for a layout origin (and its hit testing).
  float bodyShiftX() const { return shiftX(kBodyDepth); }
  float bodyShiftY() const { return shiftY(kBodyDepth); }
  // Shift of a layer relative to a layout that already includes the body
  // shift.
  float layerX(float depth) const { return shiftX(depth - kBodyDepth); }
  float layerY(float depth) const { return shiftY(depth - kBodyDepth); }
  void applyTo(GuiGlassStyle& style) const;

private:
  GuiSpring m_x;
  GuiSpring m_y;
};

// Head and tail of a travelling selection, in row units. The leading edge is
// the head of a liquid drop: it races ahead and overshoots, while the
// trailing edge lags and sloshes in after it, so a highlight spanning
// [min, max + 1] stretches, necks and snaps back together. `squash` is the
// drop's jelly response in [-1, 1]: negative while it pulls long and thin,
// positive while the tail catches up and the drop bulges.
struct GuiSelectionSpan
{
  float leading = 0.0f;
  float trailing = 0.0f;
  float squash = 0.0f;
};

// Reveal, selection, value-pulse, ambient, and caret clocks shared by the
// primitive-composed menus. The animator owns time only: it never draws, reads
// input, or models rows beyond their index, so each overlay keeps its own
// layout and GameVisual composition while sharing one motion vocabulary.
class GuiMenuAnimator final
{
public:
  static constexpr float kOpenSeconds = 0.42f;
  // The open clock keeps running past kOpenSeconds so springy entrances
  // (panel bounce, row drops) finish ringing.
  static constexpr float kOpenSettleSeconds = 1.8f;
  static constexpr float kPanelRevealSeconds = 0.24f;
  static constexpr float kRowRevealSeconds = 0.22f;
  static constexpr float kRowRevealStaggerSeconds = 0.03f;
  static constexpr float kSheenDelaySeconds = 0.12f;
  static constexpr float kSheenSeconds = 0.6f;
  static constexpr float kSelectionSettleSeconds =
    kSheenDelaySeconds + kSheenSeconds;
  static constexpr float kPressSeconds = 0.55f;
  static constexpr float kPressWobbleSeconds = 0.9f;
  static constexpr float kValuePulseSeconds = 0.20f;
  static constexpr float kValueWobbleSeconds = 0.7f;
  static constexpr float kCaretBlinkPeriodSeconds = 1.0f;
  static constexpr float kAmbientPeriodSeconds = 12.0f;
  static constexpr float kPanelLiftPixels = 22.0f;
  static constexpr float kMaximumAmbientStepSeconds = 0.1f;
  // Squash per row-per-second of closing speed between head and tail.
  static constexpr float kSquashPerRowSpeed = 0.06f;
  // Travel past the target row eases into this soft limit, so a long jump
  // splashes against its row instead of flying a spring's full overshoot.
  static constexpr float kSelectionOvershootRows = 0.3f;
  // Longest stall the selection springs integrate through in one frame.
  static constexpr float kMaximumSelectionStepSeconds = 1.0f;

  GuiMenuAnimator();

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

  // Panel opacity eases in; its offset drops in on a spring, dipping just
  // past its resting place before it settles.
  float panelReveal() const;
  float panelOffsetY() const;

  // Staggered per-row entrance opacity, 0..1; rows above the window carry no
  // extra delay.
  float rowReveal(int row, int firstVisibleRow) const;
  // The same staggered entrance as a springy drop for row offsets: 1 before
  // the row enters, 0 at rest, briefly negative as it bounces past.
  float rowDrop(int row, int firstVisibleRow) const;

  // Selection travel is expressed in row units so callers keep their own row
  // model. A travel that starts from rest leaves `fromRow`; one that
  // interrupts a moving selection keeps its momentum and redirects, like
  // liquid. Read the animated position against the row it is travelling to.
  void beginSelectionTravel(float fromRow, float toRow);
  void settleSelection();
  // The drop's center of mass between its head and tail.
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
  // Jelly response to the press: 1 (squashed) at the press, rebounding
  // through negative (stretched) and ringing out to 0.
  float pressWobble() const;

  void triggerValuePulse();
  // Records which way a stepped value moved (-1 or +1) for arrow nudges.
  void triggerValuePulse(int direction);
  float valuePulse() const;
  int valuePulseDirection() const { return m_valuePulseDirection; }
  // Signed springy nudge in the direction the value moved: it leaps out,
  // swings back past rest and rings out to 0.
  float valueWobble() const;

  float ambientPhase() const { return m_ambientPhase; }

  bool caretVisible() const;
  void resetCaret() { m_caretElapsed = 0.0f; }

private:
  bool selectionMoving(float selectedRow) const;
  float softOvershoot(float edge) const;

  bool m_reducedMotion = false;
  float m_openElapsed = 0.0f;
  GuiSpring m_selectionHead;
  GuiSpring m_selectionTail;
  // +1 when the selection travels toward higher rows, -1 toward lower.
  float m_selectionDirection = 1.0f;
  float m_selectionElapsed = kSelectionSettleSeconds;
  float m_pressElapsed = kPressWobbleSeconds;
  float m_valuePulseElapsed = kValueWobbleSeconds;
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
  // The same bookkeeping for a detached window's pointer (IPanelSurfaces),
  // whose position is in that window's pixels.
  void sample(const PanelSurfacePointer& pointer, float layoutScale);

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
