#pragma once

#include <Illumo/Gui/GuiPanelPointer.h>

// A main-window placement for driving one panel on its own.
inline GuiPanelPlacement
panelArea(float x, float y, float width, float height, bool visible = true)
{
  GuiPanelPlacement placement;
  placement.area = { x, y, width, height };
  placement.visible = visible;
  return placement;
}
