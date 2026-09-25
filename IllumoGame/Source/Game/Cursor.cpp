#include "Cursor.h"
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <algorithm>
#include <cmath>

// Seconds per bracket breath, and how far the brackets pop out on a move, as
// a spring velocity in cells per second.
static const float kBreathSeconds = 1.6f;
static const float kLandingKick = 14.0f;

Cursor::Cursor()
{
  visual.setSpace(PrimitiveSpace::World);
  visual.setLayerHint(RenderLayerId::UI);
  glideX.configure(GuiMotion::kTrack);
  glideY.configure(GuiMotion::kTrack);
  landingPop.configure(GuiMotion::kJelly);
}

void
Cursor::init(Renderer* rend, IRenderWindow* win, Camera* cam)
{
  visual.setRenderer(rend);
  visual.setWindow(win);
  visual.setCamera(cam);
  visual.setSpace(PrimitiveSpace::World);
  visual.setLayerHint(RenderLayerId::UI);
  if (rend) {
    visual.prepare(rend);
  }
  initialized = true;
  rebuild();
}

void
Cursor::setCellSize(float size)
{
  if (size > 0.0f && cellSize != size) {
    cellSize = size;
    rebuild();
  }
}

void
Cursor::setColor(ColorRgba c)
{
  color = c;
  rebuild();
}

void
Cursor::setWorldPosition(float x, float y)
{
  retarget(x, y);
}

void
Cursor::setFromCell(std::int64_t cellX, std::int64_t cellY)
{
  retarget(static_cast<float>(cellX) * cellSize - cellSize * 0.5f,
           static_cast<float>(cellY) * cellSize - cellSize * 0.5f);
}

void
Cursor::retarget(float x, float y)
{
  if (!std::isfinite(x) || !std::isfinite(y)) {
    return;
  }
  if (snapNext || reducedMotion) {
    glideX.snapTo(x);
    glideY.snapTo(y);
    landingPop.snapTo(0.0f);
    snapNext = false;
  } else if (x != glideX.target() || y != glideY.target()) {
    glideX.setTarget(x);
    glideY.setTarget(y);
    // The brackets pop out and settle; a fast sweep keeps one pop going
    // rather than stacking them.
    if (landingPop.value() < 0.2f) {
      landingPop.kick(kLandingKick);
    }
  }
  rebuild();
}

void
Cursor::tick(float deltaSeconds, bool reduced)
{
  reducedMotion = reduced;
  if (!isVisible()) {
    snapNext = true;
    return;
  }
  const float step =
    std::isfinite(deltaSeconds) ? std::clamp(deltaSeconds, 0.0f, 0.1f) : 0.0f;
  breathPhase = std::fmod(breathPhase + step, kBreathSeconds);
  glideX.tick(step, reduced);
  glideY.tick(step, reduced);
  landingPop.tick(step, reduced);
  rebuild();
}

void
Cursor::rebuild()
{
  visual.clearPrimitives();
  if (!initialized) {
    return;
  }
  const float size = cellSize;
  const float centerX = glideX.value() + size * 0.5f;
  const float centerY = glideY.value() + size * 0.5f;
  // Stretch a little along the glide and thin across it.
  const float travel = size * 35.0f;
  const float stretchX =
    std::min(0.18f, std::abs(glideX.velocity()) / std::max(1.0f, travel));
  const float stretchY =
    std::min(0.18f, std::abs(glideY.velocity()) / std::max(1.0f, travel));
  const float width = size * (1.0f + stretchX - 0.5f * stretchY);
  const float height = size * (1.0f + stretchY - 0.5f * stretchX);
  const float x = centerX - width * 0.5f;
  const float y = centerY - height * 0.5f;
  // The rim matches the rounded corners of the LED keys.
  const float radius = size * 0.22f;
  const float pop = std::clamp(landingPop.value(), -0.3f, 0.8f);
  const float breathe =
    reducedMotion
      ? 0.0f
      : 0.5f + 0.5f * std::sin(breathPhase * 6.28318531f / kBreathSeconds);
  ColorRgba solid = color;
  solid.a = 255;

  GuiKit::drawRoundedBand(visual,
                          x,
                          y,
                          width,
                          height,
                          radius,
                          0.0f,
                          size * 0.3f,
                          UiTheme::fade(solid, 0.26f + 0.1f * breathe),
                          UiTheme::transparentOf(solid));
  GuiKit::drawRoundedOutline(visual,
                             x,
                             y,
                             width,
                             height,
                             radius,
                             size * 0.05f,
                             UiTheme::fade(solid, 0.75f));

  // Viewfinder brackets just outside each corner; they breathe and pop out
  // when the cursor moves.
  const float gap = size * (0.09f + 0.03f * breathe + 0.12f * pop);
  const float arm = size * 0.26f;
  const float thickness = size * 0.075f;
  const float signs[2] = { -1.0f, 1.0f };
  for (float signX : signs) {
    for (float signY : signs) {
      const float cornerX = centerX + signX * (width * 0.5f + gap);
      const float cornerY = centerY + signY * (height * 0.5f + gap);
      // Each arm runs past the corner by half its thickness, so the joint is
      // square rather than notched.
      visual.addLine(cornerX + signX * thickness * 0.5f,
                     cornerY,
                     cornerX - signX * arm,
                     cornerY,
                     solid,
                     thickness);
      visual.addLine(cornerX,
                     cornerY + signY * thickness * 0.5f,
                     cornerX,
                     cornerY - signY * arm,
                     solid,
                     thickness);
    }
  }

  // A small centre cross marks the exact cell.
  const float crossArm = size * 0.1f;
  const float crossThickness = size * 0.05f;
  const ColorRgba cross = UiTheme::fade(solid, 0.9f);
  visual.addLine(centerX - crossArm,
                 centerY,
                 centerX + crossArm,
                 centerY,
                 cross,
                 crossThickness);
  visual.addLine(centerX,
                 centerY - crossArm,
                 centerX,
                 centerY + crossArm,
                 cross,
                 crossThickness);
}

bool
Cursor::AppendCommands(Renderer* renderer)
{
  visual.setVisible(isVisible());
  return visual.AppendCommands(renderer);
}
