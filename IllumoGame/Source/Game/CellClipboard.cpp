#include "CellClipboard.h"
#include "BuiltinPatterns.h"
#include "PatternCodec.h"
#include <Illumo/Platform/Clipboard.h>
#include <limits>

void
CellClipboard::normalizeSelection(std::int64_t* x0,
                                  std::int64_t* y0,
                                  std::int64_t* x1,
                                  std::int64_t* y1)
{
  if (x0 == nullptr || y0 == nullptr || x1 == nullptr || y1 == nullptr) {
    return;
  }
  if (*x0 > *x1) {
    const std::int64_t swap = *x0;
    *x0 = *x1;
    *x1 = swap;
  }
  if (*y0 > *y1) {
    const std::int64_t swap = *y0;
    *y0 = *y1;
    *y1 = swap;
  }
}

void
CellClipboard::setSelection(std::int64_t x0,
                            std::int64_t y0,
                            std::int64_t x1,
                            std::int64_t y1)
{
  normalizeSelection(&x0, &y0, &x1, &y1);
  m_selectX0 = x0;
  m_selectY0 = y0;
  m_selectX1 = x1;
  m_selectY1 = y1;
  m_hasSelection = true;
}

void
CellClipboard::clearSelection()
{
  m_hasSelection = false;
  m_selecting = false;
}

void
CellClipboard::startSelection(std::int64_t anchorX, std::int64_t anchorY)
{
  m_selecting = true;
  m_selectAnchorX = anchorX;
  m_selectAnchorY = anchorY;
  m_selectX0 = anchorX;
  m_selectY0 = anchorY;
  m_selectX1 = anchorX;
  m_selectY1 = anchorY;
  m_hasSelection = true;
}

void
CellClipboard::updateSelectionDrag(std::int64_t currentX, std::int64_t currentY)
{
  m_selectX0 = m_selectAnchorX;
  m_selectY0 = m_selectAnchorY;
  m_selectX1 = currentX;
  m_selectY1 = currentY;
  m_hasSelection = true;
}

void
CellClipboard::stopSelectionDrag()
{
  m_selecting = false;
}

void
CellClipboard::getSelection(std::int64_t* x0,
                            std::int64_t* y0,
                            std::int64_t* x1,
                            std::int64_t* y1) const
{
  if (x0 != nullptr) {
    *x0 = m_selectX0;
  }
  if (y0 != nullptr) {
    *y0 = m_selectY0;
  }
  if (x1 != nullptr) {
    *x1 = m_selectX1;
  }
  if (y1 != nullptr) {
    *y1 = m_selectY1;
  }
}

void
CellClipboard::getNormalizedSelection(std::int64_t* x0,
                                      std::int64_t* y0,
                                      std::int64_t* x1,
                                      std::int64_t* y1) const
{
  getSelection(x0, y0, x1, y1);
  normalizeSelection(x0, y0, x1, y1);
}

bool
CellClipboard::captureSelection(const SparseCellGrid* grid,
                                CellPattern* pattern,
                                std::string* error) const
{
  if (pattern == nullptr) {
    if (error != nullptr) {
      *error = "pattern output is null";
    }
    return false;
  }
  if (!m_hasSelection) {
    if (error != nullptr) {
      *error = "no selection";
    }
    return false;
  }
  if (grid == nullptr) {
    if (error != nullptr) {
      *error = "grid is null";
    }
    return false;
  }
  std::int64_t x0 = m_selectX0;
  std::int64_t y0 = m_selectY0;
  std::int64_t x1 = m_selectX1;
  std::int64_t y1 = m_selectY1;
  normalizeSelection(&x0, &y0, &x1, &y1);
  if (x1 < x0 || y1 < y0) {
    return false;
  }
  if ((x0 < 0 && x1 >= CellPattern::kMaxWidth) ||
      (x0 <= -CellPattern::kMaxWidth && x1 >= 0) ||
      (x1 - x0 >= static_cast<std::int64_t>(CellPattern::kMaxWidth)) ||
      (y0 < 0 && y1 >= CellPattern::kMaxHeight) ||
      (y0 <= -CellPattern::kMaxHeight && y1 >= 0) ||
      (y1 - y0 >= static_cast<std::int64_t>(CellPattern::kMaxHeight))) {
    if (error != nullptr) {
      *error = "selection exceeds 256x256";
    }
    return false;
  }
  const std::int64_t width = x1 - x0 + 1;
  const std::int64_t height = y1 - y0 + 1;
  pattern->clear();
  if (!pattern->setExtent(static_cast<int>(width), static_cast<int>(height))) {
    if (error != nullptr) {
      *error = "selection exceeds pattern caps";
    }
    return false;
  }
  for (std::int64_t y = y0; y <= y1; ++y) {
    for (std::int64_t x = x0; x <= x1; ++x) {
      const CellAddress address{ x, y };
      if (!grid->isCellInWorldBounds(address)) {
        continue;
      }
      const unsigned char state = grid->getCell(address);
      if (state == SparseCellGrid::BackgroundState) {
        continue;
      }
      if (!pattern->addCell(static_cast<std::int32_t>(x - x0),
                            static_cast<std::int32_t>(y - y0),
                            state)) {
        if (error != nullptr) {
          *error = "selection exceeds occupancy cap";
        }
        return false;
      }
    }
  }
  return true;
}

bool
CellClipboard::copySelection(const SparseCellGrid* grid, std::string* error)
{
  if (!captureSelection(grid, &m_clipboardPattern, error)) {
    return false;
  }
  const std::string rle = PatternCodec::encodeRle(m_clipboardPattern);
  Clipboard::SetText(rle);
  return true;
}

bool
CellClipboard::fillSelection(SparseCellGrid* grid,
                             CanvasView* canvas,
                             unsigned char state)
{
  if (!m_hasSelection || grid == nullptr || canvas == nullptr) {
    return false;
  }
  std::int64_t x0 = m_selectX0;
  std::int64_t y0 = m_selectY0;
  std::int64_t x1 = m_selectX1;
  std::int64_t y1 = m_selectY1;
  normalizeSelection(&x0, &y0, &x1, &y1);
  if (x1 < x0 || y1 < y0) {
    return false;
  }
  if (x1 == std::numeric_limits<std::int64_t>::max() ||
      y1 == std::numeric_limits<std::int64_t>::max()) {
    return false;
  }
  if ((x0 < 0 && x1 >= 4096) || (x0 <= -4096 && x1 >= 0) ||
      (x1 - x0 >= 4096) || (y0 < 0 && y1 >= 4096) ||
      (y0 <= -4096 && y1 >= 0) || (y1 - y0 >= 4096)) {
    return false;
  }
  for (std::int64_t y = y0; y <= y1; ++y) {
    for (std::int64_t x = x0; x <= x1; ++x) {
      const CellAddress address{ x, y };
      if (!grid->isCellInWorldBounds(address)) {
        continue;
      }
      canvas->setCanvasPixel(x, y, state);
    }
  }
  return true;
}

bool
CellClipboard::cutSelection(SparseCellGrid* grid,
                            CanvasView* canvas,
                            std::string* error)
{
  if (!copySelection(grid, error)) {
    return false;
  }
  return fillSelection(grid, canvas, SparseCellGrid::BackgroundState);
}

bool
CellClipboard::pastePatternAt(SparseCellGrid* grid,
                              CanvasView* canvas,
                              const CellPattern& pattern,
                              std::int64_t originX,
                              std::int64_t originY,
                              std::string* error)
{
  if (grid == nullptr || canvas == nullptr) {
    if (error != nullptr) {
      *error = "grid or canvas is null";
    }
    return false;
  }
  if (pattern.empty() && pattern.getWidth() <= 0 && pattern.getHeight() <= 0) {
    if (error != nullptr) {
      *error = "pattern buffer is empty";
    }
    return false;
  }
  for (const CellPatternCell& cell : pattern.getCells()) {
    if ((cell.dx > 0 &&
         originX > std::numeric_limits<std::int64_t>::max() - cell.dx) ||
        (cell.dx < 0 &&
         originX < std::numeric_limits<std::int64_t>::min() - cell.dx) ||
        (cell.dy > 0 &&
         originY > std::numeric_limits<std::int64_t>::max() - cell.dy) ||
        (cell.dy < 0 &&
         originY < std::numeric_limits<std::int64_t>::min() - cell.dy)) {
      continue;
    }
    const CellAddress address{ originX + cell.dx, originY + cell.dy };
    if (!grid->isCellInWorldBounds(address)) {
      continue;
    }
    canvas->setCanvasPixel(address.x, address.y, cell.state);
  }
  return true;
}

bool
CellClipboard::pasteAtCursor(SparseCellGrid* grid,
                             CanvasView* canvas,
                             std::int64_t hoverX,
                             std::int64_t hoverY,
                             std::string* error)
{
  CellPattern pattern = m_clipboardPattern;
  const std::string clipboardText = Clipboard::GetText();
  if (!clipboardText.empty()) {
    CellPattern parsed;
    std::string parseError;
    if (PatternCodec::parse(clipboardText, &parsed, &parseError) &&
        !parsed.empty()) {
      pattern = parsed;
    }
  }
  if (!pastePatternAt(grid, canvas, pattern, hoverX, hoverY, error)) {
    return false;
  }
  m_clipboardPattern = pattern;
  return true;
}

bool
CellClipboard::stampNamed(SparseCellGrid* grid,
                          CanvasView* canvas,
                          const std::string& name,
                          std::int64_t originX,
                          std::int64_t originY,
                          std::string* error)
{
  CellPattern pattern;
  if (!BuiltinPatterns::find(name, &pattern)) {
    if (error != nullptr) {
      *error = "unknown stamp '" + name + "'";
    }
    return false;
  }
  return pastePatternAt(grid, canvas, pattern, originX, originY, error);
}

bool
CellClipboard::importPatternText(SparseCellGrid* grid,
                                 CanvasView* canvas,
                                 const std::string& text,
                                 std::int64_t originX,
                                 std::int64_t originY,
                                 std::string* error)
{
  CellPattern pattern;
  if (!PatternCodec::parse(text, &pattern, error)) {
    return false;
  }
  m_clipboardPattern = pattern;
  return pastePatternAt(grid, canvas, pattern, originX, originY, error);
}

bool
CellClipboard::rotateCw()
{
  return m_clipboardPattern.rotateCw();
}

bool
CellClipboard::flipHorizontal()
{
  return m_clipboardPattern.flipX();
}

bool
CellClipboard::flipVertical()
{
  return m_clipboardPattern.flipY();
}
