#pragma once

// An editing command offered by the canvas toolbar (CanvasActionBar) and the
// selection's right-click menu (CanvasContextMenu). Those widgets only report
// which one was chosen; CanvasScene runs it.
enum class CanvasEditAction
{
  None,
  Copy,
  Cut,
  Paste,
  // Fill the selection with the paint brush's state.
  Fill,
  // Return the selection's cells to the empty state.
  Erase,
  Deselect,
  // Clear the whole canvas (asking first when Confirm clearing is on).
  ClearCanvas,
  // Save or load the world through the platform file pickers.
  Save,
  Load
};
