#pragma once

#include "Game/SparseCellGrid.h"
#include <Illumo/Foundation/RollingMetric.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <cstddef>
#include <cstdint>
#include <vector>

class Camera;
class IRenderWindow;
class Renderer;
class RuleSet;

// Bounded presentation of an unbounded SparseCellGrid. The view owns one
// reusable RGB staging texture and one world-space, cell-aligned quad;
// simulation chunks never become individual GPU resources or render commands.
class CanvasView : public Drawable<CanvasView>
{
public:
  CanvasView(int viewWidth,
             int viewHeight,
             SparseCellGrid* grid,
             IRenderWindow* window = nullptr,
             Camera* camera = nullptr,
             Renderer* renderer = nullptr);
  ~CanvasView();

  CanvasView(const CanvasView&) = delete;
  CanvasView& operator=(const CanvasView&) = delete;

  // Active display texels. These are one-to-one with cells near the camera and
  // become a bounded density overview only when zooming far out.
  int getViewWidth() const { return visibleViewWidth; }
  int getViewHeight() const { return visibleViewHeight; }
  int getTextureWidth() const { return textureWidth; }
  int getTextureHeight() const { return textureHeight; }
  int getCachedTexelWidth() const { return activeViewWidth; }
  int getCachedTexelHeight() const { return activeViewHeight; }
  int getCacheCellWidth() const { return cacheCellWidth; }
  int getCacheCellHeight() const { return cacheCellHeight; }
  int getCellsPerTexel() const { return cellsPerTexel; }
  CellAddress getCacheFirstCell() const { return cacheFirstCell; }
  std::size_t getCacheRefillCount() const { return cacheRefillCount; }
  int getVisibleCellWidth() const { return visibleCellWidth; }
  int getVisibleCellHeight() const { return visibleCellHeight; }
  CellAddress getVisibleFirstCell() const { return visibleFirstCell; }
  SparseCellGrid* getGrid() const { return grid; }
  void adoptGrid(SparseCellGrid* nextGrid, const SparseGenerationDelta& delta);

  static std::int64_t worldToCell(double worldCoordinate);

  void clearView();
  void clearCanvas() { clearView(); }
  unsigned char getCanvasPixel(std::int64_t x, std::int64_t y) const
  {
    const CellAddress address{ x, y };
    return grid == nullptr || !grid->isCellInWorldBounds(address)
             ? SparseCellGrid::BackgroundState
             : grid->getCell(address);
  }
  bool setCanvasPixel(std::int64_t x, std::int64_t y, unsigned char state)
  {
    const CellAddress address{ x, y };
    return grid != nullptr && grid->isCellInWorldBounds(address) &&
           grid->setCell(address, state);
  }
  void syncVisibleRegion();
  void rebuildTargetsFromGrid();
  void rebuildPalette(const RuleSet* rules);
  void rebuildDefaultPalette();
  void setFadeSpeed(float speed);
  float getFadeSpeed() const { return fadeSpeed; }
  void tickVisual(float dt);
  void snapVisualToTargets();

  void DrawImpl();
  bool AppendCommands(Renderer* renderer) override;

  GameVisual& getVisual() { return visual; }
  const GameVisual& getVisual() const { return visual; }

  CellAddress getVisibleCell(int x, int y) const;
  const unsigned char* getDisplayTexBuffer() const { return texBuffer; }
  const unsigned char* getPaletteRgb() const { return paletteRgb; }
  bool isFadeActive() const { return fadeActive; }
  bool isTextureUploadPending() const { return textureUploadPending; }
  std::size_t getFadingTexelCount() const { return fadingTexels.size(); }
  std::size_t getLastSampledTexelCount() const { return lastSampledTexelCount; }
  std::size_t getLastFadeVisitCount() const { return lastFadeVisitCount; }
  std::size_t getLastUploadByteCount() const { return lastUploadByteCount; }
  std::size_t getLastUploadRectCount() const { return lastUploadRectCount; }
  const RollingMetric& getCacheRefillMetric() const
  {
    return cacheRefillMetric;
  }
  const RollingMetric& getUploadByteMetric() const { return uploadByteMetric; }
  const RollingMetric& getUploadRectMetric() const { return uploadRectMetric; }
  std::size_t getLastSnapVisitCountForTesting() const
  {
    return lastSnapVisitCount;
  }

  // Kept public for the small headless fixture and diagnostics.
  IRenderWindow* window;
  Camera* camera;
  Renderer* renderer;

private:
  static const int kPaletteSize = 256;
  static constexpr float kCellSize = 16.0f;
  static const int kOverviewPixelsPerTexel = 4;
  static const int kCachePaddingChunks = 2;
  static const int kDirtyTileDim = 16;
  static const std::size_t kMaximumUploadRects = 8u;
  static const std::size_t kDenseChangedSampleDivisor = 4u;

  struct UploadRect
  {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
  };

  int baseViewWidth;
  int baseViewHeight;
  int textureWidth;
  int textureHeight;
  int activeViewWidth;
  int activeViewHeight;
  int visibleViewWidth;
  int visibleViewHeight;
  int visibleCellWidth;
  int visibleCellHeight;
  CellAddress visibleFirstCell;
  int cacheCellWidth;
  int cacheCellHeight;
  int cellsPerTexel;
  CellAddress cacheFirstCell;
  SparseCellGrid* grid;
  unsigned char paletteRgb[kPaletteSize * 3];
  unsigned char* texBuffer;
  float* displayRgb;
  float* targetRgb;
  float* sampledRgb;
  std::vector<int> fadingTexels;
  std::vector<unsigned char> fadingFlags;
  std::vector<int> changedSampleTexels;
  std::vector<unsigned char> changedSampleFlags;
  std::vector<unsigned char> dirtyTiles;
  std::vector<UploadRect> uploadRectScratch;
  std::vector<UploadRect> uploadRunScratch;
  std::size_t lastSampledTexelCount;
  std::size_t lastFadeVisitCount;
  std::size_t lastSnapVisitCount;
  std::size_t lastUploadByteCount;
  std::size_t lastUploadRectCount;
  std::size_t cacheRefillCount;
  RollingMetric cacheRefillMetric;
  RollingMetric uploadByteMetric;
  RollingMetric uploadRectMetric;
  float fadeSpeed;

  GameVisual visual;
  TextureHandle displayTextureHandle{};
  bool gpuReady;
  bool fadeActive;
  bool textureUploadPending;
  int uploadMinX;
  int uploadMinY;
  int uploadMaxX;
  int uploadMaxY;
  int dirtyTileColumns;
  int dirtyTileRows;
  CellAddress quadFirstCell;
  int quadCellWidth;
  int quadCellHeight;
  int quadActiveWidth;
  int quadActiveHeight;
  std::uint64_t lastGridRevision;
  bool regionReady;
  bool paletteDirty;
  bool worldQuadReady;

  struct CacheLayout
  {
    CellAddress firstCell{ 0, 0 };
    int cellWidth = 0;
    int cellHeight = 0;
    int activeWidth = 0;
    int activeHeight = 0;
  };

  static bool sameAddress(const CellAddress& left, const CellAddress& right);
  static int growTextureDimension(int current, int required);
  void initializeGpuResources();
  void resizeBuffers(int width, int height);
  void resetUploadBounds();
  void markFullActiveUpload();
  void markDirtyTile(int x, int y);
  void buildUploadRects();
  void rebuildWorldQuad();
  CacheLayout computeCacheLayout(const CellAddress& visibleFirst,
                                 int visibleCellsX,
                                 int visibleCellsY,
                                 int nextCellsPerTexel) const;
  bool tryScrollCache(const CacheLayout& nextLayout);
  void copyCacheOverlap(int deltaTexelsX, int deltaTexelsY);
  void copyCacheRow(int dstY, int srcY, int dstX, int srcX, int count);
  void remapFadingTexels(int deltaTexelsX, int deltaTexelsY);
  void sampleCacheRectangle(int minimumX,
                            int maximumX,
                            int minimumY,
                            int maximumY);
  void sampleExposedCacheStrips(int deltaTexelsX, int deltaTexelsY);
  void sampleGrid(bool snap);
  bool sampleChangedChunks(std::uint64_t previousRevision);
  void sampleCacheTexel(int x, int y, bool snap);
  void markChangedCacheChunk(const ChunkAddress& address);
  void clearChangedSampleTexels();
  bool shouldSnapSample() const;
  bool tooManyChangedSampleTexels() const;
  void resampleMarkedCacheTexels(bool snap);
  void applySampledTargets(bool snap);
  void applySnappedSampledTargets();
  void clearFadingTexels();
  int getSlotSampleCount(int x, int y) const;
  int resolveCellsPerTexel(int required,
                           int outputBudgetWidth,
                           int outputBudgetHeight,
                           int nextVisibleCellWidth,
                           int nextVisibleCellHeight) const;
  bool cacheContains(const CellAddress& firstCell,
                     int cellWidth,
                     int cellHeight) const;
  void includeUpload(int x, int y);
  void writeTexel(int index, int x, int y);
  void setTargetForSlot(int index, float r, float g, float b, bool snap);
};
