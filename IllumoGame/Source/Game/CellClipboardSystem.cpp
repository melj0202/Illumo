#include "CellClipboard.h"
#include <Illumo/Platform/Clipboard.h>

// Native-only: reads the OS clipboard synchronously. Product code pastes
// through CSimPlatform::readClipboard, which a WASM guest completes later.
bool
CellClipboard::pasteAtCursor(SparseCellGrid* grid,
                             CanvasView* canvas,
                             std::int64_t hoverX,
                             std::int64_t hoverY,
                             std::string* error)
{
  return pasteText(grid, canvas, Clipboard::GetText(), hoverX, hoverY, error);
}
