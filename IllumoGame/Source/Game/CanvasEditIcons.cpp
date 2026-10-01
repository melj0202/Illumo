#include "CanvasEditIcons.h"

#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <algorithm>
#include <cmath>

// Icons are drawn in a unit square centered on the origin (x right, y down,
// -0.5..0.5) and scaled into place.
struct IconFrame
{
  float centerX = 0.0f;
  float centerY = 0.0f;
  float size = 0.0f;
  float thickness = 0.0f;
  ColorRgba color{ 255, 255, 255, 255 };
};

// A stroked path through `count` unit-space (x, y) pairs.
static void
stroke(GameVisual& visual,
       const IconFrame& frame,
       const float* coordinates,
       int count,
       bool closed)
{
  GuiPoint2 points[16];
  const int used = std::min(count, 16);
  for (int index = 0; index < used; ++index) {
    points[index].x = frame.centerX + coordinates[index * 2] * frame.size;
    points[index].y = frame.centerY + coordinates[index * 2 + 1] * frame.size;
  }
  GuiKit::drawPolyline(
    visual, points, used, frame.thickness, frame.color, closed, false);
}

static void
line(GameVisual& visual,
     const IconFrame& frame,
     float x0,
     float y0,
     float x1,
     float y1)
{
  const float coordinates[] = { x0, y0, x1, y1 };
  stroke(visual, frame, coordinates, 2, false);
}

// A circle outline as a closed twelve-sided path.
static void
ring(GameVisual& visual, const IconFrame& frame, float x, float y, float radius)
{
  float coordinates[24];
  for (int index = 0; index < 12; ++index) {
    const float angle = static_cast<float>(index) * 6.2831853f / 12.0f;
    coordinates[index * 2] = x + std::cos(angle) * radius;
    coordinates[index * 2 + 1] = y + std::sin(angle) * radius;
  }
  stroke(visual, frame, coordinates, 12, true);
}

// The open tray under the save and load arrows.
static void
tray(GameVisual& visual, const IconFrame& frame)
{
  const float coordinates[] = { -0.42f, 0.1f,  -0.42f, 0.42f,
                                0.42f,  0.42f, 0.42f,  0.1f };
  stroke(visual, frame, coordinates, 4, false);
}

void
CanvasEditIcons::draw(GameVisual& visual,
                      CanvasEditAction action,
                      float centerX,
                      float centerY,
                      float size,
                      ColorRgba color,
                      ColorRgba brushColor)
{
  if (size <= 0.0f || color.a == 0) {
    return;
  }
  IconFrame frame;
  frame.centerX = centerX;
  frame.centerY = centerY;
  frame.size = size;
  frame.thickness = std::max(1.2f, size * 0.11f);
  frame.color = color;
  switch (action) {
    case CanvasEditAction::Save: {
      // An arrow down into a tray.
      tray(visual, frame);
      line(visual, frame, 0.0f, -0.46f, 0.0f, 0.16f);
      const float head[] = { -0.22f, -0.06f, 0.0f, 0.16f, 0.22f, -0.06f };
      stroke(visual, frame, head, 3, false);
      break;
    }
    case CanvasEditAction::Load: {
      // An arrow up out of a tray.
      tray(visual, frame);
      line(visual, frame, 0.0f, 0.2f, 0.0f, -0.44f);
      const float head[] = { -0.22f, -0.22f, 0.0f, -0.44f, 0.22f, -0.22f };
      stroke(visual, frame, head, 3, false);
      break;
    }
    case CanvasEditAction::Paste: {
      // A clipboard with its clip and two lines of text.
      const float board[] = { -0.36f, -0.34f, 0.36f,  -0.34f,
                              0.36f,  0.46f,  -0.36f, 0.46f };
      stroke(visual, frame, board, 4, true);
      GuiKit::drawRoundedRect(visual,
                              centerX - 0.18f * size,
                              centerY - 0.48f * size,
                              0.36f * size,
                              0.2f * size,
                              0.06f * size,
                              color);
      line(visual, frame, -0.18f, 0.02f, 0.18f, 0.02f);
      line(visual, frame, -0.18f, 0.22f, 0.08f, 0.22f);
      break;
    }
    case CanvasEditAction::ClearCanvas: {
      // A trash can: lid, handle, tapered body and a rib.
      line(visual, frame, -0.46f, -0.3f, 0.46f, -0.3f);
      const float handle[] = { -0.14f, -0.3f,  -0.14f, -0.46f,
                               0.14f,  -0.46f, 0.14f,  -0.3f };
      stroke(visual, frame, handle, 4, false);
      const float body[] = { -0.33f, -0.3f, -0.26f, 0.46f,
                             0.26f,  0.46f, 0.33f,  -0.3f };
      stroke(visual, frame, body, 4, false);
      line(visual, frame, 0.0f, -0.1f, 0.0f, 0.28f);
      break;
    }
    case CanvasEditAction::ResetCanvas: {
      // A counter-clockwise arrow coming back round to its start: a 270
      // degree arc open across the top, with a chevron at its end pointing
      // along its travel.
      const float degrees = 6.2831853f / 360.0f;
      const float radius = 0.34f;
      const float centerOffset = 0.04f;
      float arc[26];
      for (int index = 0; index < 13; ++index) {
        const float angle =
          (-60.0f + 22.5f * static_cast<float>(index)) * degrees;
        arc[index * 2] = -std::cos(angle) * radius;
        arc[index * 2 + 1] = std::sin(angle) * radius + centerOffset;
      }
      stroke(visual, frame, arc, 13, false);
      // The arms lean back from the tip, 40 degrees either side of the arc's
      // reversed direction of travel at 210 degrees.
      const float endAngle = 210.0f * degrees;
      const float backX = -std::sin(endAngle);
      const float backY = -std::cos(endAngle);
      const float spread = 40.0f * degrees;
      const float arm = 0.2f;
      const float tipX = arc[24];
      const float tipY = arc[25];
      const float chevron[] = {
        tipX + (backX * std::cos(spread) - backY * std::sin(spread)) * arm,
        tipY + (backX * std::sin(spread) + backY * std::cos(spread)) * arm,
        tipX,
        tipY,
        tipX + (backX * std::cos(spread) + backY * std::sin(spread)) * arm,
        tipY + (-backX * std::sin(spread) + backY * std::cos(spread)) * arm,
      };
      stroke(visual, frame, chevron, 3, false);
      break;
    }
    case CanvasEditAction::Copy: {
      // Two overlapping sheets; the back one shows only its free edges.
      const float front[] = { -0.1f, -0.1f, 0.44f, -0.1f,
                              0.44f, 0.44f, -0.1f, 0.44f };
      stroke(visual, frame, front, 4, true);
      const float back[] = { -0.24f, 0.1f, -0.44f, 0.1f, -0.44f,
                             -0.44f, 0.1f, -0.44f, 0.1f, -0.24f };
      stroke(visual, frame, back, 5, false);
      break;
    }
    case CanvasEditAction::Cut: {
      // Scissors: two finger rings and crossing blades.
      ring(visual, frame, -0.24f, 0.28f, 0.15f);
      ring(visual, frame, 0.24f, 0.28f, 0.15f);
      line(visual, frame, -0.15f, 0.16f, 0.3f, -0.46f);
      line(visual, frame, 0.15f, 0.16f, -0.3f, -0.46f);
      break;
    }
    case CanvasEditAction::Fill: {
      // A swatch of the brush in hand inside a lit rim.
      const float swatch = 0.8f * size;
      GuiKit::drawRoundedRect(
        visual,
        centerX - swatch * 0.5f - 1.0f,
        centerY - swatch * 0.5f - 1.0f,
        swatch + 2.0f,
        swatch + 2.0f,
        swatch * 0.5f + 1.0f,
        UiTheme::fade(UiTheme::glassRimLit(),
                      static_cast<float>(color.a) / 255.0f));
      GuiKit::drawRoundedRect(
        visual,
        centerX - swatch * 0.5f,
        centerY - swatch * 0.5f,
        swatch,
        swatch,
        swatch * 0.5f,
        UiTheme::fade(brushColor, static_cast<float>(color.a) / 255.0f));
      break;
    }
    case CanvasEditAction::Erase: {
      // A tilted eraser with its sleeve line, resting on a baseline.
      const float block[] = { -0.42f, 0.14f, 0.1f,   -0.38f,
                              0.38f,  -0.1f, -0.14f, 0.42f };
      stroke(visual, frame, block, 4, true);
      line(visual, frame, -0.16f, -0.12f, 0.12f, 0.16f);
      line(visual, frame, 0.0f, 0.46f, 0.46f, 0.46f);
      break;
    }
    case CanvasEditAction::Deselect: {
      // A marquee's four corners around a small cross.
      const float corner = 0.2f;
      const float edge = 0.44f;
      const float topLeft[] = { -edge, -edge + corner, -edge,
                                -edge, -edge + corner, -edge };
      const float topRight[] = { edge - corner, -edge, edge,
                                 -edge,         edge,  -edge + corner };
      const float bottomRight[] = { edge, edge - corner, edge,
                                    edge, edge - corner, edge };
      const float bottomLeft[] = { -edge + corner, edge,  -edge,
                                   edge,           -edge, edge - corner };
      stroke(visual, frame, topLeft, 3, false);
      stroke(visual, frame, topRight, 3, false);
      stroke(visual, frame, bottomRight, 3, false);
      stroke(visual, frame, bottomLeft, 3, false);
      line(visual, frame, -0.15f, -0.15f, 0.15f, 0.15f);
      line(visual, frame, 0.15f, -0.15f, -0.15f, 0.15f);
      break;
    }
    case CanvasEditAction::None:
      break;
  }
}
