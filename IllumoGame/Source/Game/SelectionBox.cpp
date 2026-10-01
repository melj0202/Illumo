#include "SelectionBox.h"
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <algorithm>
#include <cmath>

// Seconds per breath, and how far the brackets flare when a selection
// appears, as a spring velocity in cells per second. The hover cursor uses
// the same values so the two feel like one piece of chrome.
static const float kBreathSeconds = 1.6f;
static const float kAppearKick = 14.0f;

SelectionBox::SelectionBox()
{
  for (GameVisual* layer : { &glowVisual, &rimVisual }) {
    layer->setSpace(PrimitiveSpace::World);
    layer->setLayerHint(RenderLayerId::UI);
  }
  glideX.configure(GuiMotion::kTrack);
  glideY.configure(GuiMotion::kTrack);
  glideWidth.configure(GuiMotion::kTrack);
  glideHeight.configure(GuiMotion::kTrack);
  appearPop.configure(GuiMotion::kJelly);
  reveal.configure(GuiMotion::kTrack);
  reveal.snapTo(0.0f);
}

void
SelectionBox::init(Renderer* rend, IRenderWindow* win, Camera* cam)
{
  for (GameVisual* layer : { &glowVisual, &rimVisual }) {
    layer->setRenderer(rend);
    layer->setWindow(win);
    layer->setCamera(cam);
    layer->setSpace(PrimitiveSpace::World);
    layer->setLayerHint(RenderLayerId::UI);
    if (rend) {
      layer->prepare(rend);
    }
  }
  initialized = true;
  rebuild();
}

void
SelectionBox::setCamera(Camera* cam)
{
  glowVisual.setCamera(cam);
  rimVisual.setCamera(cam);
}

void
SelectionBox::setCellSize(float size)
{
  if (size > 0.0f && cellSize != size) {
    cellSize = size;
    rebuild();
  }
}

void
SelectionBox::setCells(std::int64_t x0,
                       std::int64_t y0,
                       std::int64_t x1,
                       std::int64_t y1)
{
  const float worldX = static_cast<float>(x0) * cellSize - cellSize * 0.5f;
  const float worldY = static_cast<float>(y0) * cellSize - cellSize * 0.5f;
  const float width = static_cast<float>(x1 - x0 + 1) * cellSize;
  const float height = static_cast<float>(y1 - y0 + 1) * cellSize;
  if (!std::isfinite(worldX) || !std::isfinite(worldY) ||
      !std::isfinite(width) || !std::isfinite(height)) {
    return;
  }
  if (snapNext || reducedMotion) {
    const bool appearing = snapNext;
    glideX.snapTo(worldX);
    glideY.snapTo(worldY);
    glideWidth.snapTo(width);
    glideHeight.snapTo(height);
    snapNext = false;
    if (appearing) {
      // A fresh selection fades in while its brackets flare and settle.
      reveal.snapTo(reducedMotion ? 1.0f : 0.0f);
      appearPop.snapTo(0.0f);
      if (!reducedMotion) {
        appearPop.kick(kAppearKick);
      }
    }
  } else {
    glideX.setTarget(worldX);
    glideY.setTarget(worldY);
    glideWidth.setTarget(width);
    glideHeight.setTarget(height);
  }
  reveal.setTarget(1.0f);
  rebuild();
}

void
SelectionBox::hide()
{
  snapNext = true;
  setVisible(false);
  glowVisual.clearPrimitives();
  rimVisual.clearPrimitives();
  glowKey.invalidate();
  rimKey.invalidate();
}

void
SelectionBox::tick(float deltaSeconds, bool reduced)
{
  ILLUMO_PROFILE_ZONE("SelectionBox.tick");
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
  glideWidth.tick(step, reduced);
  glideHeight.tick(step, reduced);
  appearPop.tick(step, reduced);
  reveal.tick(step, reduced);
  rebuild();
}

void
SelectionBox::rebuild()
{
  if (!initialized) {
    glowVisual.clearPrimitives();
    rimVisual.clearPrimitives();
    glowKey.invalidate();
    rimKey.invalidate();
    return;
  }
  const float size = cellSize;
  const float width = std::max(size * 0.5f, glideWidth.value());
  const float height = std::max(size * 0.5f, glideHeight.value());
  const float x = glideX.value();
  const float y = glideY.value();
  const float centerX = x + width * 0.5f;
  const float centerY = y + height * 0.5f;
  // The rim matches the rounded corners of the LED keys, as on the cursor.
  const float radius = size * 0.22f;
  const float pop = std::clamp(appearPop.value(), -0.3f, 0.8f);
  const float shown = std::clamp(reveal.value(), 0.0f, 1.0f);
  const float breathe =
    reducedMotion
      ? 0.0f
      : 0.5f + 0.5f * std::sin(breathPhase * 6.28318531f / kBreathSeconds);
  const ColorRgba solid{ 80, 220, 255, 255 };

  glowKey.begin().add({ x, y, width, height, size, breathe, pop, shown });
  if (glowKey.changed()) {
    ILLUMO_PROFILE_ZONE("SelectionBox.rebuildGlow");
    glowVisual.clearPrimitives();
    glowVisual.setVisible(true);
    GuiKit::drawRoundedBand(glowVisual,
                            x,
                            y,
                            width,
                            height,
                            radius,
                            0.0f,
                            size * 0.3f,
                            UiTheme::fade(solid, (0.26f + 0.1f * breathe) * shown),
                            UiTheme::transparentOf(solid));

    // Viewfinder brackets just outside each corner; they breathe and flare
    // when the selection appears. Long boxes get longer arms, but never more
    // than a third of a side.
    const float gap = size * (0.09f + 0.03f * breathe + 0.12f * pop);
    const float arm =
      std::min(size * 0.5f, 0.33f * std::min(width, height)) * shown;
    const float thickness = size * 0.075f;
    const ColorRgba bracket = UiTheme::fade(solid, shown);
    const float signs[2] = { -1.0f, 1.0f };
    for (float signX : signs) {
      for (float signY : signs) {
        const float cornerX = centerX + signX * (width * 0.5f + gap);
        const float cornerY = centerY + signY * (height * 0.5f + gap);
        // Each arm runs past the corner by half its thickness, so the joint
        // is square rather than notched.
        glowVisual.addLine(cornerX + signX * thickness * 0.5f,
                           cornerY,
                           cornerX - signX * arm,
                           cornerY,
                           bracket,
                           thickness);
        glowVisual.addLine(cornerX,
                           cornerY + signY * thickness * 0.5f,
                           cornerX,
                           cornerY - signY * arm,
                           bracket,
                           thickness);
      }
    }
  }

  rimKey.begin().add({ x, y, width, height, size, breathe, shown });
  if (!rimKey.changed()) {
    return;
  }
  ILLUMO_PROFILE_ZONE("SelectionBox.rebuildRim");
  rimVisual.clearPrimitives();
  rimVisual.setVisible(true);
  // A faint gradient fill that breathes with the glow, cyan over violet.
  GuiKit::drawRoundedGradientRect(
    rimVisual,
    x,
    y,
    width,
    height,
    radius,
    UiTheme::fade(solid, (0.07f + 0.04f * breathe) * shown),
    UiTheme::fade(UiTheme::accentViolet(), (0.05f + 0.03f * breathe) * shown));
  GuiKit::drawRoundedOutline(rimVisual,
                             x,
                             y,
                             width,
                             height,
                             radius,
                             size * 0.05f,
                             UiTheme::fade(solid, 0.75f * shown));
}

bool
SelectionBox::AppendCommands(Renderer* renderer)
{
  ILLUMO_PROFILE_ZONE("SelectionBox.AppendCommands");
  glowVisual.setVisible(isVisible());
  rimVisual.setVisible(isVisible());
  // The breathing glow and brackets sit behind the fill and rim.
  const bool glowAppended = glowVisual.AppendCommands(renderer);
  return rimVisual.AppendCommands(renderer) && glowAppended;
}
