#pragma once

#include "CanvasEditAction.h"
#include <Illumo/Rendering/Primitives/GameVisual.h>

// Line icons for the canvas edit actions, shared by the toolbar and the
// context menu so each action reads the same in both. Strokes are mitered
// GuiKit polylines scaled to `size` (the icon's square, in virtual pixels);
// Fill is a swatch of the brush color rather than a line drawing.
class CanvasEditIcons
{
public:
  static void draw(GameVisual& visual,
                   CanvasEditAction action,
                   float centerX,
                   float centerY,
                   float size,
                   ColorRgba color,
                   ColorRgba brushColor);
};
