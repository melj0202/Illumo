#pragma once

#include "CanvasView.h"
#include "CellPattern.h"
#include "SparseCellGrid.h"

#include <cstdint>
#include <string>

class CellClipboard
{
public:
  CellClipboard() = default;

  static void normalizeSelection(std::int64_t* x0,
                                 std::int64_t* y0,
                                 std::int64_t* x1,
                                 std::int64_t* y1);

  void setSelection(std::int64_t x0,
                    std::int64_t y0,
                    std::int64_t x1,
                    std::int64_t y1);
  void clearSelection();
  void startSelection(std::int64_t anchorX, std::int64_t anchorY);
  void updateSelectionDrag(std::int64_t currentX, std::int64_t currentY);
  void stopSelectionDrag();

  bool hasSelection() const { return m_hasSelection; }
  bool isSelecting() const { return m_selecting; }

  void getSelection(std::int64_t* x0,
                    std::int64_t* y0,
                    std::int64_t* x1,
                    std::int64_t* y1) const;

  void getNormalizedSelection(std::int64_t* x0,
                              std::int64_t* y0,
                              std::int64_t* x1,
                              std::int64_t* y1) const;

  const CellPattern& getClipboardPattern() const { return m_clipboardPattern; }
  CellPattern& getClipboardPattern() { return m_clipboardPattern; }
  void setClipboardPattern(const CellPattern& pattern)
  {
    m_clipboardPattern = pattern;
  }

  bool captureSelection(const SparseCellGrid* grid,
                        CellPattern* pattern,
                        std::string* error = nullptr) const;

  bool copySelection(const SparseCellGrid* grid,
                     std::string* error = nullptr);

  bool cutSelection(SparseCellGrid* grid,
                    CanvasView* canvas,
                    std::string* error = nullptr);

  bool fillSelection(SparseCellGrid* grid,
                     CanvasView* canvas,
                     unsigned char state);

  bool pastePatternAt(SparseCellGrid* grid,
                      CanvasView* canvas,
                      const CellPattern& pattern,
                      std::int64_t originX,
                      std::int64_t originY,
                      std::string* error = nullptr);

  bool pasteAtCursor(SparseCellGrid* grid,
                     CanvasView* canvas,
                     std::int64_t hoverX,
                     std::int64_t hoverY,
                     std::string* error = nullptr);

  bool stampNamed(SparseCellGrid* grid,
                  CanvasView* canvas,
                  const std::string& name,
                  std::int64_t originX,
                  std::int64_t originY,
                  std::string* error = nullptr);

  bool importPatternText(SparseCellGrid* grid,
                         CanvasView* canvas,
                         const std::string& text,
                         std::int64_t originX,
                         std::int64_t originY,
                         std::string* error = nullptr);

  bool rotateCw();
  bool flipHorizontal();
  bool flipVertical();

private:
  bool m_hasSelection = false;
  bool m_selecting = false;
  std::int64_t m_selectAnchorX = 0;
  std::int64_t m_selectAnchorY = 0;
  std::int64_t m_selectX0 = 0;
  std::int64_t m_selectY0 = 0;
  std::int64_t m_selectX1 = 0;
  std::int64_t m_selectY1 = 0;
  CellPattern m_clipboardPattern;
};
