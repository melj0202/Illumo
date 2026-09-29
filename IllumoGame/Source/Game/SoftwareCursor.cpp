#include "SoftwareCursor.h"

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <algorithm>
#include <cmath>

// The arrow in local pixels with its tip at the origin (y grows down): a
// triangular head and a tail. Both are convex, so each is one primitive.
// Each shape is a few convex parts of 3, 4, 6 or 8 points. A part with more
// than four points is drawn as a fan of quads around its first point.
struct ShapePart
{
  int count;
  float point[8][2];
};
static const ShapePart kArrowParts[2] = {
  { 3, { { 0.0f, 0.0f }, { 0.0f, 16.5f }, { 11.7f, 11.7f } } },
  { 4, { { 3.6f, 12.4f }, { 6.3f, 11.5f }, { 9.6f, 18.6f }, { 6.9f, 19.8f } } }
};
static const float kArrowHeight = 19.8f;
// The pointing hand shown over things that can be pressed: the index
// fingertip sits on the tip, so clicks stay as precise as with the arrow.
// Every finger has chamfered corners at its tip; the rim's offset copies
// round them off.
static const int kHandPartCount = 7;
static const ShapePart kHandParts[kHandPartCount] = {
  // Index finger.
  { 8,
    { { -1.0f, 0.0f },
      { 1.0f, 0.0f },
      { 1.8f, 0.8f },
      { 1.9f, 2.0f },
      { 1.9f, 10.5f },
      { -1.9f, 10.5f },
      { -1.9f, 2.0f },
      { -1.8f, 0.8f } } },
  // Middle finger, curled shorter.
  { 6,
    { { 2.6f, 6.6f },
      { 4.4f, 6.6f },
      { 5.1f, 7.3f },
      { 5.1f, 10.5f },
      { 1.9f, 10.5f },
      { 1.9f, 7.3f } } },
  // Ring finger.
  { 6,
    { { 5.7f, 7.6f },
      { 7.4f, 7.6f },
      { 8.0f, 8.2f },
      { 8.0f, 11.0f },
      { 5.1f, 11.0f },
      { 5.1f, 8.2f } } },
  // Little finger.
  { 6,
    { { 8.6f, 9.0f },
      { 10.0f, 9.0f },
      { 10.6f, 9.6f },
      { 10.6f, 12.5f },
      { 8.0f, 12.5f },
      { 8.0f, 9.6f } } },
  // Palm, tapering to the wrist. Its top-left is carved back to the index
  // finger; the thumb below keeps the palm's full width.
  { 6,
    { { -1.9f, 9.6f },
      { 10.6f, 9.6f },
      { 10.6f, 16.4f },
      { 9.6f, 17.6f },
      { -0.9f, 17.6f },
      { -1.9f, 16.4f } } },
  // Wrist, narrower than the palm with straight sides.
  { 4, { { 0.5f, 17.0f }, { 8.2f, 17.0f }, { 8.2f, 21.0f }, { 0.5f, 21.0f } } },
  // Thumb: the palm's left side below the carved space.
  { 6,
    { { -3.0f, 9.0f },
      { -4.4f, 9.3f },
      { -5.1f, 10.4f },
      { -5.2f, 13.0f },
      { -1.9f, 17.2f },
      { -1.9f, 13.0f } } }
};
static const float kHandHeight = 21.0f;
// Lean per pixel-per-second of sideways speed, and its limit in radians.
static const float kLeanPerSpeed = 0.0012f;
static const float kMaximumLean = 0.55f;
// A ghost shows once it trails the tip by kGhostMinimumGap pixels and is at
// full strength kGhostFullGap pixels further back.
static const float kGhostMinimumGap = 5.0f;
static const float kGhostFullGap = 20.0f;
// The edge glow: rings of offset copies, outermost first, in 16 directions.
static const float kGlowRadii[4] = { 5.5f, 4.3f, 3.2f, 2.3f };
static const float kGlowAlpha[4] = { 0.022f, 0.03f, 0.04f, 0.05f };
static const int kGlowDirections = 16;
// Offset copies that build the dark halo and the lit rim.
static const int kOutlineDirections = 16;
// One placement of the arrow: tip position, lean and scale.
struct ArrowPose
{
  float x = 0.0f;
  float y = 0.0f;
  float cosine = 1.0f;
  float sine = 0.0f;
  float scale = 1.0f;
  bool hand = false;

  void place(float localX, float localY, float* outX, float* outY) const
  {
    const float sx = localX * scale;
    const float sy = localY * scale;
    *outX = x + sx * cosine - sy * sine;
    *outY = y + sx * sine + sy * cosine;
  }
};

// Draws the arrow at `pose`, offset by (dx, dy), with a vertical gradient
// from `top` to `bottom` across its height.
static void
drawArrow(GameVisual& visual,
          const ArrowPose& pose,
          float dx,
          float dy,
          ColorRgba top,
          ColorRgba bottom)
{
  const ShapePart* parts = pose.hand ? kHandParts : kArrowParts;
  const int partCount = pose.hand ? kHandPartCount : 2;
  const float height = pose.hand ? kHandHeight : kArrowHeight;
  for (int part = 0; part < partCount; ++part) {
    const ShapePart& shape = parts[part];
    float px[8];
    float py[8];
    ColorRgba colors[8];
    for (int point = 0; point < shape.count; ++point) {
      pose.place(shape.point[point][0], shape.point[point][1], &px[point], &py[point]);
      px[point] += dx;
      py[point] += dy;
      colors[point] = UiTheme::mix(top, bottom, shape.point[point][1] / height);
    }
    if (shape.count == 3) {
      visual.addGradientTriangle(
        px[0], py[0], px[1], py[1], px[2], py[2], colors[0], colors[1], colors[2]);
    } else {
      for (int first = 1; first + 2 < shape.count; first += 2) {
        const int a = first;
        const int b = first + 1;
        const int c = first + 2;
        visual.addGradientQuad(px[0],
                               py[0],
                               px[a],
                               py[a],
                               px[b],
                               py[b],
                               px[c],
                               py[c],
                               colors[0],
                               colors[a],
                               colors[b],
                               colors[c]);
      }
    }
  }
}

SoftwareCursor::SoftwareCursor()
  : m_visual(2048u)
{
  // The body swings like a pendulum; a press squishes and boings back.
  m_lean.configure(GuiMotion::kJelly);
  m_press.configure(GuiMotion::kBoing);
  m_press.snapTo(1.0f);
  m_swap.configure(GuiMotion::kBoing);
  m_swap.snapTo(1.0f);
  m_visual.setVisible(false);
}

void
SoftwareCursor::prepare(IRenderWindow* window, Renderer* renderer)
{
  m_visual.setRenderer(renderer);
  m_visual.setWindow(window);
  m_visual.setSpace(PrimitiveSpace::Pixels);
  m_visual.setLayerHint(RenderLayerId::UI);
  m_visual.prepare(renderer);
  m_visual.setVisible(false);
}

void
SoftwareCursor::update(float deltaSeconds,
                       float pointerX,
                       float pointerY,
                       bool visible,
                       bool pressed,
                       bool reducedMotion,
                       bool interactive)
{
  ILLUMO_PROFILE_ZONE("SoftwareCursor.update");
  m_reducedMotion = reducedMotion;
  if (!visible || !std::isfinite(pointerX) || !std::isfinite(pointerY)) {
    // Forget the motion so the pointer reappears still, where it is.
    m_placed = false;
    m_velocityX = 0.0f;
    m_velocityY = 0.0f;
    m_historyCount = 0;
    m_lean.snapTo(0.0f);
    m_press.snapTo(1.0f);
    m_swap.snapTo(1.0f);
    m_hand = false;
    m_splashElapsed = kSplashSeconds;
    m_wasPressed = pressed;
    m_visual.clearPrimitives();
    m_visual.setVisible(false);
    return;
  }
  const float step =
    std::isfinite(deltaSeconds) ? std::clamp(deltaSeconds, 0.0f, 0.1f) : 0.0f;
  if (m_placed && step > 0.0f) {
    // Smoothed pointer velocity drives the sway and the trail.
    const float rawX = (pointerX - m_x) / step;
    const float rawY = (pointerY - m_y) / step;
    const float blend = 1.0f - std::exp(-18.0f * step);
    m_velocityX += (rawX - m_velocityX) * blend;
    m_velocityY += (rawY - m_velocityY) * blend;
  }
  m_x = pointerX;
  m_y = pointerY;
  m_placed = true;
  m_ambient = std::fmod(m_ambient + step, 12.0f);

  // Moving right swings the tail left, and the reverse.
  m_lean.setTarget(
    reducedMotion
      ? 0.0f
      : std::clamp(m_velocityX * kLeanPerSpeed, -kMaximumLean, kMaximumLean));
  m_press.setTarget(pressed ? 0.8f : 1.0f);
  if (interactive != m_hand) {
    // The swap squeezes the body small and lets it boing back.
    m_hand = interactive;
    m_swap.snapTo(0.72f);
  }
  m_swap.setTarget(1.0f);
  m_lean.tick(step, reducedMotion);
  m_press.tick(step, reducedMotion);
  m_swap.tick(step, reducedMotion);

  if (pressed && !m_wasPressed && !reducedMotion) {
    m_splashElapsed = 0.0f;
  }
  m_wasPressed = pressed;
  m_splashElapsed = std::min(kSplashSeconds, m_splashElapsed + step);

  // Record the tip's path, newest first, for the afterimage.
  m_clock += step;
  if (reducedMotion) {
    m_historyCount = 0;
  } else {
    for (int index = std::min(m_historyCount, kHistoryLength - 1); index > 0;
         --index) {
      const std::size_t to = static_cast<std::size_t>(index);
      m_historyX[to] = m_historyX[to - 1u];
      m_historyY[to] = m_historyY[to - 1u];
      m_historyTime[to] = m_historyTime[to - 1u];
    }
    m_historyX[0] = m_x;
    m_historyY[0] = m_y;
    m_historyTime[0] = m_clock;
    m_historyCount = std::min(kHistoryLength, m_historyCount + 1);
  }
  rebuild();
}

bool
SoftwareCursor::pathAt(float delay, float* x, float* y) const
{
  if (m_historyCount == 0) {
    return false;
  }
  const double target = m_clock - static_cast<double>(delay);
  for (int index = 1; index < m_historyCount; ++index) {
    const std::size_t older = static_cast<std::size_t>(index);
    if (m_historyTime[older] <= target) {
      const double span = m_historyTime[older - 1u] - m_historyTime[older];
      const float blend =
        span > 0.0 ? static_cast<float>((target - m_historyTime[older]) / span)
                   : 0.0f;
      *x = m_historyX[older] +
           (m_historyX[older - 1u] - m_historyX[older]) * blend;
      *y = m_historyY[older] +
           (m_historyY[older - 1u] - m_historyY[older]) * blend;
      return true;
    }
  }
  // Older than the history reaches: the oldest known position.
  const std::size_t oldest = static_cast<std::size_t>(m_historyCount - 1);
  *x = m_historyX[oldest];
  *y = m_historyY[oldest];
  return true;
}

void
SoftwareCursor::rebuild()
{
  ILLUMO_PROFILE_ZONE("SoftwareCursor.rebuild");
  m_visual.clearPrimitives();
  const ColorRgba cyan = UiTheme::accentCool();
  const ColorRgba violet = UiTheme::accentViolet();

  ArrowPose pose;
  pose.x = m_x;
  pose.y = m_y;
  pose.cosine = std::cos(m_lean.value());
  pose.sine = std::sin(m_lean.value());
  pose.scale = std::clamp(m_press.value() * m_swap.value(), 0.5f, 1.4f);
  pose.hand = m_hand;

  // A holographic afterimage: ghost arrows where the tip was a moment ago,
  // each split into cyan and magenta-violet copies across the motion and
  // shimmering through the accent hues. A ghost fades in as it falls behind
  // the tip, so a still pointer has none and a stopping one leaves a brief
  // afterglow that catches up.
  if (!m_reducedMotion) {
    const ColorRgba magenta{ 226, 124, 236, 255 };
    for (int ghost = kGhostCount; ghost >= 1; --ghost) {
      float ghostX = 0.0f;
      float ghostY = 0.0f;
      if (!pathAt(
            static_cast<float>(ghost) * kGhostSpacing, &ghostX, &ghostY)) {
        break;
      }
      const float towardX = m_x - ghostX;
      const float towardY = m_y - ghostY;
      const float gap = std::sqrt(towardX * towardX + towardY * towardY);
      const float presence = std::clamp((gap - kGhostMinimumGap) /
                                          (kGhostFullGap - kGhostMinimumGap),
                                        0.0f,
                                        1.0f);
      if (presence <= 0.0f) {
        continue;
      }
      const float age =
        static_cast<float>(ghost) / static_cast<float>(kGhostCount + 1);
      const float shimmer =
        0.5f + 0.5f * std::sin(static_cast<float>(m_clock) * 5.0f +
                               static_cast<float>(ghost));
      const float flicker =
        0.85f + 0.15f * std::sin(static_cast<float>(m_clock) * 47.0f +
                                 static_cast<float>(ghost) * 1.7f);
      const float alpha = 0.42f * (1.0f - age) * presence * flicker;
      const float acrossX = -towardY / gap;
      const float acrossY = towardX / gap;
      const float split = 1.0f + 1.6f * age;
      ArrowPose placed = pose;
      placed.x = ghostX;
      placed.y = ghostY;
      placed.scale = pose.scale * (1.0f - 0.15f * age);
      drawArrow(
        m_visual,
        placed,
        -acrossX * split,
        -acrossY * split,
        UiTheme::fade(UiTheme::mix(cyan, violet, shimmer * age), alpha),
        UiTheme::fade(UiTheme::mix(violet, magenta, shimmer), alpha * 0.6f));
      drawArrow(m_visual,
                placed,
                acrossX * split,
                acrossY * split,
                UiTheme::fade(magenta, alpha * 0.7f),
                UiTheme::fade(violet, alpha * 0.4f));
    }
  }

  // Soft drop shadow, then a glow at the tip that breathes gently.
  drawArrow(m_visual,
            pose,
            1.6f,
            2.8f,
            UiTheme::glowShadow(),
            UiTheme::fade(UiTheme::glowShadow(), 0.4f));
  const float breathe =
    m_reducedMotion ? 0.5f
                    : 0.5f + 0.5f * std::sin(m_ambient * 6.28318531f / 1.5f);
  GuiKit::drawSoftGlow(m_visual,
                       m_x,
                       m_y,
                       6.0f + 1.5f * breathe,
                       6.0f + 1.5f * breathe,
                       UiTheme::fade(cyan, 0.22f + 0.1f * breathe),
                       16);

  // Built like the menu glass. A glow hugs the whole edge: rings of faint
  // copies pushed outward in every direction stack into a soft falloff about
  // five pixels wide. A thin dark halo inside it keeps the pointer readable
  // on light cells, a lit rim runs cyan at the tip to violet at the tail,
  // and the body is deep glass with a cool lift toward the tip.
  const float glowBreath = 0.85f + 0.3f * breathe;
  for (int ring = 0; ring < 4; ++ring) {
    const float alpha = kGlowAlpha[ring] * glowBreath;
    const ColorRgba top = UiTheme::fade(cyan, alpha);
    const ColorRgba bottom = UiTheme::fade(violet, alpha);
    for (int direction = 0; direction < kGlowDirections; ++direction) {
      const float angle = static_cast<float>(direction) * 6.28318531f /
                          static_cast<float>(kGlowDirections);
      drawArrow(m_visual,
                pose,
                std::cos(angle) * kGlowRadii[ring],
                std::sin(angle) * kGlowRadii[ring],
                top,
                bottom);
    }
  }
  // Sixteen directions keep the outline's corners round: with eight, each
  // corner of the offset copies shows as a flat facet.
  float directions[kOutlineDirections][2];
  for (int direction = 0; direction < kOutlineDirections; ++direction) {
    const float angle = static_cast<float>(direction) * 6.28318531f /
                        static_cast<float>(kOutlineDirections);
    directions[direction][0] = std::cos(angle);
    directions[direction][1] = std::sin(angle);
  }
  const ColorRgba haloColor = UiTheme::fade(UiTheme::keycapEdge(), 0.7f);
  for (const float* direction : directions) {
    drawArrow(m_visual,
              pose,
              direction[0] * 1.7f,
              direction[1] * 1.7f,
              haloColor,
              haloColor);
  }
  const ColorRgba rimTop = UiTheme::mix(UiTheme::glassRimLit(), cyan, 0.35f);
  const ColorRgba rimBottom =
    UiTheme::mix(violet, UiTheme::glassRimLit(), 0.2f);
  for (const float* direction : directions) {
    drawArrow(m_visual,
              pose,
              direction[0] * 1.15f,
              direction[1] * 1.15f,
              rimTop,
              rimBottom);
  }
  drawArrow(m_visual,
            pose,
            0.0f,
            0.0f,
            UiTheme::mix(UiTheme::cardTop(), cyan, 0.22f),
            UiTheme::glassBottom());

  // A press throws a small splash from the tip.
  if (m_splashElapsed < kSplashSeconds) {
    GuiKit::drawSplash(
      m_visual, m_x, m_y, m_splashElapsed / kSplashSeconds, 0.45f, cyan);
  }
  m_visual.setVisible(true);
}