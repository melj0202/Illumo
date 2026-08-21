#include "CanvasView.h"
#include "Rulesets/RuleSet.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <glm/glm.hpp>
#include <limits>
#include <numeric>
#include <tracy/Tracy.hpp>

CanvasView::CanvasView(int width,
                       int height,
                       SparseCellGrid* targetGrid,
                       IRenderWindow* renderWindow,
                       Camera* renderCamera,
                       Renderer* renderRenderer)
  : window(renderWindow)
  , camera(renderCamera)
  , renderer(renderRenderer)
  , baseViewWidth(width < 1 ? 1 : width)
  , baseViewHeight(height < 1 ? 1 : height)
  , textureWidth(baseViewWidth)
  , textureHeight(baseViewHeight)
  , activeViewWidth(0)
  , activeViewHeight(0)
  , visibleViewWidth(0)
  , visibleViewHeight(0)
  , visibleCellWidth(0)
  , visibleCellHeight(0)
  , visibleFirstCell{ 0, 0 }
  , cacheCellWidth(0)
  , cacheCellHeight(0)
  , cellsPerTexel(1)
  , cacheFirstCell{ 0, 0 }
  , grid(targetGrid)
  , texBuffer(nullptr)
  , displayRgb(nullptr)
  , targetRgb(nullptr)
  , sampledRgb(nullptr)
  , lastSampledTexelCount(0u)
  , lastFadeVisitCount(0u)
  , lastSnapVisitCount(0u)
  , lastUploadByteCount(0u)
  , lastUploadRectCount(0u)
  , cacheRefillCount(0u)
  , fadeSpeed(8.0f)
  , displayTextureHandle()
  , gpuReady(false)
  , fadeActive(false)
  , textureUploadPending(false)
  , uploadMinX(0)
  , uploadMinY(0)
  , uploadMaxX(-1)
  , uploadMaxY(-1)
  , dirtyTileColumns((textureWidth + kDirtyTileDim - 1) / kDirtyTileDim)
  , dirtyTileRows((textureHeight + kDirtyTileDim - 1) / kDirtyTileDim)
  , quadFirstCell{ 0, 0 }
  , quadCellWidth(0)
  , quadCellHeight(0)
  , quadActiveWidth(0)
  , quadActiveHeight(0)
  , lastGridRevision(std::numeric_limits<std::uint64_t>::max())
  , regionReady(false)
  , paletteDirty(true)
  , worldQuadReady(false)
{
  const std::size_t texelCount = static_cast<std::size_t>(textureWidth) *
                                 static_cast<std::size_t>(textureHeight);
  texBuffer = new unsigned char[texelCount * 3u];
  displayRgb = new float[texelCount * 3u];
  targetRgb = new float[texelCount * 3u];
  sampledRgb = new float[texelCount * 3u];
  fadingFlags.assign(texelCount, 0u);
  changedSampleFlags.assign(texelCount, 0u);
  dirtyTiles.assign(static_cast<std::size_t>(dirtyTileColumns) *
                      static_cast<std::size_t>(dirtyTileRows),
                    0u);
  for (std::size_t i = 0; i < texelCount * 3u; ++i) {
    texBuffer[i] = 255;
    displayRgb[i] = 1.0f;
    targetRgb[i] = 1.0f;
    sampledRgb[i] = 1.0f;
  }
  rebuildDefaultPalette();
  resetUploadBounds();
  initializeGpuResources();
}

CanvasView::~CanvasView()
{
  if (renderer != nullptr && displayTextureHandle.isValid()) {
    renderer->destroyTexture(displayTextureHandle);
    displayTextureHandle = TextureHandle{};
  }
  gpuReady = false;
  delete[] texBuffer;
  delete[] displayRgb;
  delete[] targetRgb;
  delete[] sampledRgb;
}

std::int64_t
CanvasView::worldToCell(double worldCoordinate)
{
  return static_cast<std::int64_t>(
    std::floor(worldCoordinate / static_cast<double>(kCellSize) + 0.5));
}

bool
CanvasView::sameAddress(const CellAddress& left, const CellAddress& right)
{
  return left.x == right.x && left.y == right.y;
}

int
CanvasView::growTextureDimension(int current, int required)
{
  if (required <= current) {
    return current;
  }
  const int growth = std::max(1, current / 2);
  if (current > std::numeric_limits<int>::max() - growth) {
    return required;
  }
  return std::max(required, current + growth);
}

void
CanvasView::rebuildDefaultPalette()
{
  for (int state = 0; state < kPaletteSize; ++state) {
    const int base = state * 3;
    if (state == SparseCellGrid::CountedNeighborState) {
      paletteRgb[base + 0] = 0;
      paletteRgb[base + 1] = 0;
      paletteRgb[base + 2] = 0;
    } else if (state == SparseCellGrid::BackgroundState) {
      paletteRgb[base + 0] = 255;
      paletteRgb[base + 1] = 255;
      paletteRgb[base + 2] = 255;
    } else {
      paletteRgb[base + 0] = 0;
      paletteRgb[base + 1] = 164;
      paletteRgb[base + 2] = 128;
    }
  }
  paletteDirty = true;
}

void
CanvasView::rebuildPalette(const RuleSet* rules)
{
  ZoneScopedN("CanvasView.rebuildPalette");
  if (rules == nullptr) {
    rebuildDefaultPalette();
  } else {
    for (int state = 0; state < kPaletteSize; ++state) {
      unsigned char rgb[3] = { 255, 255, 255 };
      rules->evalCell(static_cast<unsigned char>(state), rgb);
      const int base = state * 3;
      paletteRgb[base + 0] = rgb[0];
      paletteRgb[base + 1] = rgb[1];
      paletteRgb[base + 2] = rgb[2];
    }
    paletteDirty = true;
  }
  rebuildTargetsFromGrid();
}

void
CanvasView::initializeGpuResources()
{
  if (renderer == nullptr) {
    return;
  }
  renderer->ensureBuiltinStyles();
  visual.setRenderer(renderer);
  visual.setWindow(window);
  visual.setCamera(camera);
  visual.setSpace(PrimitiveSpace::World);
  visual.setLayerHint(RenderLayerId::World);
  visual.prepare(renderer);
  TextureOptions textureOptions;
  textureOptions.filter = TextureFilter::Nearest;
  displayTextureHandle = renderer->enrollTexture(
    texBuffer, textureWidth, textureHeight, 3, textureOptions);
  gpuReady = true;
}

void
CanvasView::resizeBuffers(int width, int height)
{
  if (width <= textureWidth && height <= textureHeight) {
    return;
  }

  const int newWidth = growTextureDimension(textureWidth, width);
  const int newHeight = growTextureDimension(textureHeight, height);
  const std::size_t texelCount =
    static_cast<std::size_t>(newWidth) * static_cast<std::size_t>(newHeight);

  delete[] texBuffer;
  delete[] displayRgb;
  delete[] targetRgb;
  delete[] sampledRgb;

  textureWidth = newWidth;
  textureHeight = newHeight;
  texBuffer = new unsigned char[texelCount * 3u];
  displayRgb = new float[texelCount * 3u];
  targetRgb = new float[texelCount * 3u];
  sampledRgb = new float[texelCount * 3u];
  fadingTexels.clear();
  fadingFlags.assign(texelCount, 0u);
  changedSampleTexels.clear();
  changedSampleFlags.assign(texelCount, 0u);
  dirtyTileColumns = (textureWidth + kDirtyTileDim - 1) / kDirtyTileDim;
  dirtyTileRows = (textureHeight + kDirtyTileDim - 1) / kDirtyTileDim;
  dirtyTiles.assign(static_cast<std::size_t>(dirtyTileColumns) *
                      static_cast<std::size_t>(dirtyTileRows),
                    0u);
  for (std::size_t i = 0; i < texelCount * 3u; ++i) {
    texBuffer[i] = 255;
    displayRgb[i] = 1.0f;
    targetRgb[i] = 1.0f;
    sampledRgb[i] = 1.0f;
  }

  fadeActive = false;
  lastSampledTexelCount = 0u;
  lastFadeVisitCount = 0u;
  lastSnapVisitCount = 0u;
  textureUploadPending = false;
  resetUploadBounds();
  regionReady = false;
  worldQuadReady = false;
  lastGridRevision = std::numeric_limits<std::uint64_t>::max();

  if (gpuReady && renderer != nullptr) {
    TextureOptions textureOptions;
    textureOptions.filter = TextureFilter::Nearest;
    renderer->replaceTexture(displayTextureHandle,
                             texBuffer,
                             textureWidth,
                             textureHeight,
                             3,
                             textureOptions);
  }
}

void
CanvasView::resetUploadBounds()
{
  uploadMinX = textureWidth;
  uploadMinY = textureHeight;
  uploadMaxX = -1;
  uploadMaxY = -1;
  std::fill(dirtyTiles.begin(), dirtyTiles.end(), 0u);
}

void
CanvasView::markFullActiveUpload()
{
  if (activeViewWidth <= 0 || activeViewHeight <= 0) {
    return;
  }
  textureUploadPending = true;
  uploadMinX = 0;
  uploadMinY = 0;
  uploadMaxX = activeViewWidth - 1;
  uploadMaxY = activeViewHeight - 1;
  for (int tileY = 0;
       tileY * kDirtyTileDim < activeViewHeight && tileY < dirtyTileRows;
       ++tileY) {
    for (int tileX = 0;
         tileX * kDirtyTileDim < activeViewWidth && tileX < dirtyTileColumns;
         ++tileX) {
      dirtyTiles[static_cast<std::size_t>(tileY * dirtyTileColumns + tileX)] =
        1u;
    }
  }
}

void
CanvasView::markDirtyTile(int x, int y)
{
  if (x < 0 || y < 0 || x >= activeViewWidth || y >= activeViewHeight) {
    return;
  }
  const int tileX = x / kDirtyTileDim;
  const int tileY = y / kDirtyTileDim;
  dirtyTiles[static_cast<std::size_t>(tileY * dirtyTileColumns + tileX)] = 1u;
}

void
CanvasView::buildUploadRects()
{
  uploadRectScratch.clear();
  if (!textureUploadPending || uploadMaxX < uploadMinX ||
      uploadMaxY < uploadMinY) {
    return;
  }

  const UploadRect boundingRect{ uploadMinX,
                                 uploadMinY,
                                 uploadMaxX - uploadMinX + 1,
                                 uploadMaxY - uploadMinY + 1 };
  uploadRunScratch.clear();
  for (int tileY = 0;
       tileY * kDirtyTileDim < activeViewHeight && tileY < dirtyTileRows;
       ++tileY) {
    int tileX = 0;
    while (tileX * kDirtyTileDim < activeViewWidth &&
           tileX < dirtyTileColumns) {
      while (tileX < dirtyTileColumns &&
             dirtyTiles[static_cast<std::size_t>(tileY * dirtyTileColumns +
                                                 tileX)] == 0u) {
        tileX += 1;
      }
      if (tileX >= dirtyTileColumns ||
          tileX * kDirtyTileDim >= activeViewWidth) {
        break;
      }
      const int startTileX = tileX;
      while (tileX < dirtyTileColumns &&
             tileX * kDirtyTileDim < activeViewWidth &&
             dirtyTiles[static_cast<std::size_t>(tileY * dirtyTileColumns +
                                                 tileX)] != 0u) {
        tileX += 1;
      }
      UploadRect run;
      run.x = startTileX * kDirtyTileDim;
      run.y = tileY * kDirtyTileDim;
      run.width = std::min(activeViewWidth, tileX * kDirtyTileDim) - run.x;
      run.height = std::min(kDirtyTileDim, activeViewHeight - run.y);
      uploadRunScratch.push_back(run);
    }
  }

  for (const UploadRect& run : uploadRunScratch) {
    bool merged = false;
    for (UploadRect& rectangle : uploadRectScratch) {
      if (rectangle.x == run.x && rectangle.width == run.width &&
          rectangle.y + rectangle.height == run.y) {
        rectangle.height += run.height;
        merged = true;
        break;
      }
    }
    if (!merged) {
      uploadRectScratch.push_back(run);
    }
    if (uploadRectScratch.size() > kMaximumUploadRects) {
      uploadRectScratch.clear();
      uploadRectScratch.push_back(boundingRect);
      return;
    }
  }

  std::size_t rectangleArea = 0u;
  for (const UploadRect& rectangle : uploadRectScratch) {
    rectangleArea += static_cast<std::size_t>(rectangle.width) *
                     static_cast<std::size_t>(rectangle.height);
  }
  const std::size_t boundingArea =
    static_cast<std::size_t>(boundingRect.width) *
    static_cast<std::size_t>(boundingRect.height);
  if (uploadRectScratch.empty() || rectangleArea * 2u > boundingArea) {
    uploadRectScratch.clear();
    uploadRectScratch.push_back(boundingRect);
  }
}

void
CanvasView::rebuildWorldQuad()
{
  if (worldQuadReady && sameAddress(quadFirstCell, cacheFirstCell) &&
      quadCellWidth == cacheCellWidth && quadCellHeight == cacheCellHeight &&
      quadActiveWidth == activeViewWidth &&
      quadActiveHeight == activeViewHeight) {
    return;
  }

  const float worldLeft =
    static_cast<float>(cacheFirstCell.x) * kCellSize - kCellSize * 0.5f;
  const float worldTop =
    static_cast<float>(cacheFirstCell.y) * kCellSize + kCellSize * 0.5f;
  const float worldBottom =
    worldTop - static_cast<float>(cacheCellHeight) * kCellSize;
  const float u1 =
    static_cast<float>(activeViewWidth) / static_cast<float>(textureWidth);
  const float v0 =
    static_cast<float>(activeViewHeight) / static_cast<float>(textureHeight);
  visual.clearPrimitives();
  visual.addSprite(displayTextureHandle,
                   worldLeft,
                   worldBottom,
                   static_cast<float>(cacheCellWidth) * kCellSize,
                   static_cast<float>(cacheCellHeight) * kCellSize,
                   ColorRgba{ 255, 255, 255, 255 },
                   0.0f,
                   v0,
                   u1,
                   0.0f);
  quadFirstCell = cacheFirstCell;
  quadCellWidth = cacheCellWidth;
  quadCellHeight = cacheCellHeight;
  quadActiveWidth = activeViewWidth;
  quadActiveHeight = activeViewHeight;
  worldQuadReady = true;
}

void
CanvasView::includeUpload(int x, int y)
{
  textureUploadPending = true;
  markDirtyTile(x, y);
  if (uploadMaxX < uploadMinX || uploadMaxY < uploadMinY) {
    uploadMinX = x;
    uploadMaxX = x;
    uploadMinY = y;
    uploadMaxY = y;
    return;
  }
  uploadMinX = std::min(uploadMinX, x);
  uploadMinY = std::min(uploadMinY, y);
  uploadMaxX = std::max(uploadMaxX, x);
  uploadMaxY = std::max(uploadMaxY, y);
}

void
CanvasView::writeTexel(int index, int x, int y)
{
  const int base = index * 3;
  bool changed = false;
  for (int channel = 0; channel < 3; ++channel) {
    const float scaled = displayRgb[base + channel] * 255.0f + 0.5f;
    const unsigned char value =
      static_cast<unsigned char>(std::clamp(scaled, 0.0f, 255.0f));
    if (texBuffer[base + channel] != value) {
      texBuffer[base + channel] = value;
      changed = true;
    }
  }
  if (changed) {
    includeUpload(x, y);
  }
}

void
CanvasView::setTargetForSlot(int index, float r, float g, float b, bool snap)
{
  const int base = index * 3;
  const bool changed = targetRgb[base + 0] != r || targetRgb[base + 1] != g ||
                       targetRgb[base + 2] != b;
  targetRgb[base + 0] = r;
  targetRgb[base + 1] = g;
  targetRgb[base + 2] = b;
  if (snap) {
    displayRgb[base + 0] = r;
    displayRgb[base + 1] = g;
    displayRgb[base + 2] = b;
  } else if (changed) {
    if (fadingFlags[static_cast<std::size_t>(index)] == 0u) {
      fadingFlags[static_cast<std::size_t>(index)] = 1u;
      fadingTexels.push_back(index);
    }
    fadeActive = true;
  }
}

void
CanvasView::clearFadingTexels()
{
  for (int index : fadingTexels) {
    fadingFlags[static_cast<std::size_t>(index)] = 0u;
  }
  fadingTexels.clear();
  fadeActive = false;
}

int
CanvasView::getSlotSampleCount(int x, int y) const
{
  const int sourceWidth =
    std::min(cellsPerTexel, cacheCellWidth - x * cellsPerTexel);
  const int sourceHeight =
    std::min(cellsPerTexel, cacheCellHeight - y * cellsPerTexel);
  return std::max(1, sourceWidth * sourceHeight);
}

void
CanvasView::applySampledTargets(bool snap)
{
  ZoneScopedN("CanvasView.applySampledTargets");
  if (snap) {
    applySnappedSampledTargets();
    return;
  }
  for (int y = 0; y < activeViewHeight; ++y) {
    for (int x = 0; x < activeViewWidth; ++x) {
      const int index = y * textureWidth + x;
      const int base = index * 3;
      setTargetForSlot(index,
                       sampledRgb[base + 0],
                       sampledRgb[base + 1],
                       sampledRgb[base + 2],
                       false);
    }
  }
}

void
CanvasView::applySnappedSampledTargets()
{
  const std::size_t rowBytes =
    static_cast<std::size_t>(activeViewWidth) * 3u * sizeof(float);
  for (int y = 0; y < activeViewHeight; ++y) {
    const int row = y * textureWidth * 3;
    std::memcpy(targetRgb + row, sampledRgb + row, rowBytes);
    std::memcpy(displayRgb + row, sampledRgb + row, rowBytes);
    for (int x = 0; x < activeViewWidth; ++x) {
      const int base = row + x * 3;
      for (int channel = 0; channel < 3; ++channel) {
        const float scaled = sampledRgb[base + channel] * 255.0f + 0.5f;
        texBuffer[base + channel] =
          static_cast<unsigned char>(std::clamp(scaled, 0.0f, 255.0f));
      }
    }
  }
  clearFadingTexels();
  markFullActiveUpload();
}

void
CanvasView::sampleGrid(bool snap)
{
  ZoneScopedN("CanvasView.sampleGrid");
  const std::chrono::steady_clock::time_point sampleStart =
    std::chrono::steady_clock::now();
  if (activeViewWidth <= 0 || activeViewHeight <= 0) {
    lastSampledTexelCount = 0u;
    return;
  }
  lastSampledTexelCount = static_cast<std::size_t>(activeViewWidth) *
                          static_cast<std::size_t>(activeViewHeight);

  const int backgroundBase = SparseCellGrid::BackgroundState * 3;
  const float backgroundR =
    static_cast<float>(paletteRgb[backgroundBase + 0]) / 255.0f;
  const float backgroundG =
    static_cast<float>(paletteRgb[backgroundBase + 1]) / 255.0f;
  const float backgroundB =
    static_cast<float>(paletteRgb[backgroundBase + 2]) / 255.0f;
  for (int y = 0; y < activeViewHeight; ++y) {
    for (int x = 0; x < activeViewWidth; ++x) {
      const int index = y * textureWidth + x;
      const int base = index * 3;
      sampledRgb[base + 0] = backgroundR;
      sampledRgb[base + 1] = backgroundG;
      sampledRgb[base + 2] = backgroundB;
    }
  }

  if (grid != nullptr) {
    const std::int64_t maximumX =
      cacheFirstCell.x + static_cast<std::int64_t>(cacheCellWidth) - 1;
    const std::int64_t minimumY =
      cacheFirstCell.y - static_cast<std::int64_t>(cacheCellHeight) + 1;
    const ChunkAddress minimumChunk = SparseCellGrid::chunkAddressForCell(
      CellAddress{ cacheFirstCell.x, minimumY });
    const ChunkAddress maximumChunk = SparseCellGrid::chunkAddressForCell(
      CellAddress{ maximumX, cacheFirstCell.y });
    float paletteOffset[kPaletteSize * 3];
    for (int state = 0; state < kPaletteSize; ++state) {
      const int paletteIndex = state * 3;
      paletteOffset[paletteIndex + 0] =
        static_cast<float>(paletteRgb[paletteIndex + 0]) / 255.0f - backgroundR;
      paletteOffset[paletteIndex + 1] =
        static_cast<float>(paletteRgb[paletteIndex + 1]) / 255.0f - backgroundG;
      paletteOffset[paletteIndex + 2] =
        static_cast<float>(paletteRgb[paletteIndex + 2]) / 255.0f - backgroundB;
    }
    const int fullColumns = cacheCellWidth / cellsPerTexel;
    const int fullRows = cacheCellHeight / cellsPerTexel;
    const float interiorInverse =
      1.0f / static_cast<float>(std::max(1, cellsPerTexel * cellsPerTexel));
    grid->visitOccupiedChunksInBounds(
      minimumChunk,
      maximumChunk,
      [this,
       maximumX,
       minimumY,
       &paletteOffset,
       fullColumns,
       fullRows,
       interiorInverse](const ChunkAddress& chunkAddress,
                        const SparseCellGrid::ChunkCells& cells,
                        const SparseChunkMask& occupied) {
        for (std::size_t wordIndex = 0u; wordIndex < occupied.size();
             ++wordIndex) {
          std::uint64_t bits = occupied[wordIndex];
          while (bits != 0u) {
            const unsigned int bitOffset = std::countr_zero(bits);
            const std::size_t index = wordIndex * 64u + bitOffset;
            bits &= bits - 1u;
            const int localX =
              static_cast<int>(index % SparseCellGrid::kChunkDim);
            const int localY =
              static_cast<int>(index / SparseCellGrid::kChunkDim);
            const std::int64_t cellX =
              chunkAddress.x * SparseCellGrid::kChunkDim + localX;
            const std::int64_t cellY =
              chunkAddress.y * SparseCellGrid::kChunkDim + localY;
            if (cellX < cacheFirstCell.x || cellX > maximumX ||
                cellY < minimumY || cellY > cacheFirstCell.y) {
              continue;
            }
            const unsigned char state = cells[index];
            if (state == SparseCellGrid::BackgroundState) {
              continue;
            }
            const int outputX =
              static_cast<int>((cellX - cacheFirstCell.x) / cellsPerTexel);
            const int outputY =
              static_cast<int>((cacheFirstCell.y - cellY) / cellsPerTexel);
            const float inverseSample =
              (outputX < fullColumns && outputY < fullRows)
                ? interiorInverse
                : 1.0f /
                    static_cast<float>(getSlotSampleCount(outputX, outputY));
            const int base = (outputY * textureWidth + outputX) * 3;
            const int paletteIndex = static_cast<int>(state) * 3;
            sampledRgb[base + 0] +=
              paletteOffset[paletteIndex + 0] * inverseSample;
            sampledRgb[base + 1] +=
              paletteOffset[paletteIndex + 1] * inverseSample;
            sampledRgb[base + 2] +=
              paletteOffset[paletteIndex + 2] * inverseSample;
          }
        }
      });
  }
  applySampledTargets(snap);
  cacheRefillCount += 1u;
  const double elapsedMilliseconds =
    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                              sampleStart)
      .count();
  cacheRefillMetric.add(elapsedMilliseconds);
}

void
CanvasView::sampleCacheTexel(int x, int y, bool snap)
{
  const std::int64_t sourceLeft =
    cacheFirstCell.x + static_cast<std::int64_t>(x * cellsPerTexel);
  const std::int64_t sourceTop =
    cacheFirstCell.y - static_cast<std::int64_t>(y * cellsPerTexel);
  const int sourceWidth =
    std::min(cellsPerTexel, cacheCellWidth - x * cellsPerTexel);
  const int sourceHeight =
    std::min(cellsPerTexel, cacheCellHeight - y * cellsPerTexel);
  const int sampleCount = std::max(1, sourceWidth * sourceHeight);
  float red = 0.0f;
  float green = 0.0f;
  float blue = 0.0f;
  for (int sourceY = 0; sourceY < sourceHeight; ++sourceY) {
    for (int sourceX = 0; sourceX < sourceWidth; ++sourceX) {
      const CellAddress sourceAddress{ sourceLeft + sourceX,
                                       sourceTop - sourceY };
      const unsigned char state = grid->isCellInWorldBounds(sourceAddress)
                                    ? grid->getCell(sourceAddress)
                                    : SparseCellGrid::BackgroundState;
      const int paletteIndex = static_cast<int>(state) * 3;
      red += static_cast<float>(paletteRgb[paletteIndex + 0]) / 255.0f;
      green += static_cast<float>(paletteRgb[paletteIndex + 1]) / 255.0f;
      blue += static_cast<float>(paletteRgb[paletteIndex + 2]) / 255.0f;
    }
  }
  const float inverseSampleCount = 1.0f / static_cast<float>(sampleCount);
  const int index = y * textureWidth + x;
  setTargetForSlot(index,
                   red * inverseSampleCount,
                   green * inverseSampleCount,
                   blue * inverseSampleCount,
                   snap);
  if (snap) {
    writeTexel(index, x, y);
  }
}

void
CanvasView::markChangedCacheChunk(const ChunkAddress& address)
{
  if (tooManyChangedSampleTexels()) {
    return;
  }
  const std::int64_t cacheMaximumX =
    cacheFirstCell.x + static_cast<std::int64_t>(cacheCellWidth) - 1;
  const std::int64_t cacheMinimumY =
    cacheFirstCell.y - static_cast<std::int64_t>(cacheCellHeight) + 1;
  const std::int64_t chunkMinimumX = address.x * SparseCellGrid::kChunkDim;
  const std::int64_t chunkMinimumY = address.y * SparseCellGrid::kChunkDim;
  const std::int64_t chunkMaximumX =
    chunkMinimumX + SparseCellGrid::kChunkDim - 1;
  const std::int64_t chunkMaximumY =
    chunkMinimumY + SparseCellGrid::kChunkDim - 1;
  const std::int64_t minimumX = std::max(chunkMinimumX, cacheFirstCell.x);
  const std::int64_t maximumX = std::min(chunkMaximumX, cacheMaximumX);
  const std::int64_t minimumY = std::max(chunkMinimumY, cacheMinimumY);
  const std::int64_t maximumY = std::min(chunkMaximumY, cacheFirstCell.y);
  if (minimumX > maximumX || minimumY > maximumY) {
    return;
  }

  const int minimumOutputX =
    static_cast<int>((minimumX - cacheFirstCell.x) / cellsPerTexel);
  const int maximumOutputX =
    static_cast<int>((maximumX - cacheFirstCell.x) / cellsPerTexel);
  const int minimumOutputY =
    static_cast<int>((cacheFirstCell.y - maximumY) / cellsPerTexel);
  const int maximumOutputY =
    static_cast<int>((cacheFirstCell.y - minimumY) / cellsPerTexel);
  for (int outputY = minimumOutputY; outputY <= maximumOutputY; ++outputY) {
    for (int outputX = minimumOutputX; outputX <= maximumOutputX; ++outputX) {
      const int index = outputY * textureWidth + outputX;
      if (changedSampleFlags[static_cast<std::size_t>(index)] == 0u) {
        changedSampleFlags[static_cast<std::size_t>(index)] = 1u;
        changedSampleTexels.push_back(index);
      }
    }
  }
}

void
CanvasView::clearChangedSampleTexels()
{
  for (int index : changedSampleTexels) {
    changedSampleFlags[static_cast<std::size_t>(index)] = 0u;
  }
  changedSampleTexels.clear();
}

bool
CanvasView::shouldSnapSample() const
{
  // Exact-cell fadeSpeed 0 still samples without a full-cache snap so dirty
  // tiles stay sparse; density overviews snap to avoid enrolling fade texels.
  return fadeSpeed <= 0.0f || cellsPerTexel > 1;
}

bool
CanvasView::tooManyChangedSampleTexels() const
{
  if (activeViewWidth <= 0 || activeViewHeight <= 0) {
    return true;
  }
  const std::size_t cacheTexels = static_cast<std::size_t>(activeViewWidth) *
                                  static_cast<std::size_t>(activeViewHeight);
  return changedSampleTexels.size() * kDenseChangedSampleDivisor >= cacheTexels;
}

void
CanvasView::resampleMarkedCacheTexels(bool snap)
{
  ZoneScopedN("CanvasView.resampleMarkedCacheTexels");
  for (int index : changedSampleTexels) {
    sampleCacheTexel(index % textureWidth, index / textureWidth, snap);
    changedSampleFlags[static_cast<std::size_t>(index)] = 0u;
  }
  lastSampledTexelCount = changedSampleTexels.size();
  changedSampleTexels.clear();
}

bool
CanvasView::sampleChangedChunks(std::uint64_t previousRevision)
{
  ZoneScopedN("CanvasView.sampleChangedChunks");
  if (grid == nullptr || grid->isToroidal()) {
    // A changed canonical torus chunk can appear at several visible aliases.
    // A complete refill stays bounded by the presentation cache and avoids
    // missing an alias when the camera spans a wrapped edge.
    return false;
  }

  lastSampledTexelCount = 0u;
  changedSampleTexels.clear();
  const bool visited = grid->visitChangedChunksSince(
    previousRevision,
    [this](const ChunkAddress& address, const SparseCellGrid::ChunkCells*) {
      markChangedCacheChunk(address);
    });
  if (!visited) {
    clearChangedSampleTexels();
    return false;
  }
  if (tooManyChangedSampleTexels()) {
    clearChangedSampleTexels();
    return false;
  }
  resampleMarkedCacheTexels(shouldSnapSample());
  return true;
}

void
CanvasView::adoptGrid(SparseCellGrid* nextGrid,
                      const SparseGenerationDelta& delta)
{
  if (nextGrid == nullptr || nextGrid == grid) {
    return;
  }
  grid = nextGrid;
  if (!regionReady) {
    lastGridRevision = std::numeric_limits<std::uint64_t>::max();
    return;
  }
  const bool snapChanged = shouldSnapSample();
  if (delta.fullReplacement || delta.fromRevision != lastGridRevision ||
      delta.toRevision != grid->getRevision()) {
    sampleGrid(cellsPerTexel > 1);
  } else {
    changedSampleTexels.clear();
    for (const SparseChangedChunkRecord& record : delta.changedChunks) {
      if (tooManyChangedSampleTexels()) {
        break;
      }
      markChangedCacheChunk(record.address);
    }
    if (tooManyChangedSampleTexels()) {
      clearChangedSampleTexels();
      sampleGrid(cellsPerTexel > 1);
    } else {
      resampleMarkedCacheTexels(snapChanged);
    }
  }
  lastGridRevision = grid->getRevision();
  paletteDirty = false;
  if (snapChanged) {
    snapVisualToTargets();
  }
}

int
CanvasView::resolveCellsPerTexel(int required,
                                 int outputBudgetWidth,
                                 int outputBudgetHeight,
                                 int nextVisibleCellWidth,
                                 int nextVisibleCellHeight) const
{
  if (!regionReady || required >= cellsPerTexel) {
    return required;
  }
  int resolved = cellsPerTexel;
  while (resolved > required) {
    const int finer = resolved - 1;
    const int widthThreshold =
      static_cast<int>(static_cast<double>(outputBudgetWidth * finer) * 0.8);
    const int heightThreshold =
      static_cast<int>(static_cast<double>(outputBudgetHeight * finer) * 0.8);
    if (nextVisibleCellWidth > widthThreshold ||
        nextVisibleCellHeight > heightThreshold) {
      break;
    }
    resolved = finer;
  }
  return resolved;
}

bool
CanvasView::cacheContains(const CellAddress& firstCell,
                          int cellWidth,
                          int cellHeight) const
{
  if (!regionReady) {
    return false;
  }
  const std::int64_t visibleMaximumX =
    firstCell.x + static_cast<std::int64_t>(cellWidth) - 1;
  const std::int64_t visibleMinimumY =
    firstCell.y - static_cast<std::int64_t>(cellHeight) + 1;
  const std::int64_t cacheMaximumX =
    cacheFirstCell.x + static_cast<std::int64_t>(cacheCellWidth) - 1;
  const std::int64_t cacheMinimumY =
    cacheFirstCell.y - static_cast<std::int64_t>(cacheCellHeight) + 1;
  return firstCell.x >= cacheFirstCell.x && visibleMaximumX <= cacheMaximumX &&
         firstCell.y <= cacheFirstCell.y && visibleMinimumY >= cacheMinimumY;
}

CanvasView::CacheLayout
CanvasView::computeCacheLayout(const CellAddress& visibleFirst,
                               int visibleCellsX,
                               int visibleCellsY,
                               int nextCellsPerTexel) const
{
  CacheLayout layout;
  const int padding = kCachePaddingChunks * SparseCellGrid::kChunkDim;
  const int alignment = std::lcm(SparseCellGrid::kChunkDim, nextCellsPerTexel);
  const std::int64_t desiredMinimumX = visibleFirst.x - padding;
  const std::int64_t desiredMaximumX =
    visibleFirst.x + static_cast<std::int64_t>(visibleCellsX) - 1 + padding;
  const std::int64_t desiredMaximumY = visibleFirst.y + padding;
  const std::int64_t desiredMinimumY =
    visibleFirst.y - static_cast<std::int64_t>(visibleCellsY) + 1 - padding;
  const std::int64_t cacheMinimumX =
    SparseCellGrid::floorDivide(desiredMinimumX, alignment) * alignment;
  const std::int64_t cacheMaximumX =
    (SparseCellGrid::floorDivide(desiredMaximumX, alignment) + 1) * alignment -
    1;
  const std::int64_t cacheMinimumY =
    SparseCellGrid::floorDivide(desiredMinimumY, alignment) * alignment;
  const std::int64_t cacheMaximumY =
    (SparseCellGrid::floorDivide(desiredMaximumY, alignment) + 1) * alignment -
    1;
  layout.firstCell = CellAddress{ cacheMinimumX, cacheMaximumY };
  layout.cellWidth = static_cast<int>(cacheMaximumX - cacheMinimumX + 1);
  layout.cellHeight = static_cast<int>(cacheMaximumY - cacheMinimumY + 1);
  layout.activeWidth = layout.cellWidth / nextCellsPerTexel;
  layout.activeHeight = layout.cellHeight / nextCellsPerTexel;
  return layout;
}

void
CanvasView::copyCacheRow(int dstY, int srcY, int dstX, int srcX, int count)
{
  if (count <= 0) {
    return;
  }
  const std::size_t dstIndex =
    static_cast<std::size_t>(dstY * textureWidth + dstX);
  const std::size_t srcIndex =
    static_cast<std::size_t>(srcY * textureWidth + srcX);
  const std::size_t texelCount = static_cast<std::size_t>(count);
  std::memmove(
    texBuffer + dstIndex * 3u, texBuffer + srcIndex * 3u, texelCount * 3u);
  std::memmove(displayRgb + dstIndex * 3u,
               displayRgb + srcIndex * 3u,
               texelCount * 3u * sizeof(float));
  std::memmove(targetRgb + dstIndex * 3u,
               targetRgb + srcIndex * 3u,
               texelCount * 3u * sizeof(float));
  std::memmove(sampledRgb + dstIndex * 3u,
               sampledRgb + srcIndex * 3u,
               texelCount * 3u * sizeof(float));
  std::memmove(
    fadingFlags.data() + dstIndex, fadingFlags.data() + srcIndex, texelCount);
}

void
CanvasView::copyCacheOverlap(int deltaTexelsX, int deltaTexelsY)
{
  const int overlapWidth = activeViewWidth - std::abs(deltaTexelsX);
  const int overlapHeight = activeViewHeight - std::abs(deltaTexelsY);
  if (overlapWidth <= 0 || overlapHeight <= 0) {
    return;
  }
  const int dstX = deltaTexelsX > 0 ? 0 : -deltaTexelsX;
  const int srcX = deltaTexelsX > 0 ? deltaTexelsX : 0;
  if (deltaTexelsY >= 0) {
    for (int row = 0; row < overlapHeight; ++row) {
      copyCacheRow(row, row + deltaTexelsY, dstX, srcX, overlapWidth);
    }
  } else {
    for (int row = overlapHeight - 1; row >= 0; --row) {
      copyCacheRow(row - deltaTexelsY, row, dstX, srcX, overlapWidth);
    }
  }
}

void
CanvasView::remapFadingTexels(int deltaTexelsX, int deltaTexelsY)
{
  std::size_t retainedCount = 0u;
  for (int index : fadingTexels) {
    const int sourceX = index % textureWidth;
    const int sourceY = index / textureWidth;
    const int destinationX = sourceX - deltaTexelsX;
    const int destinationY = sourceY - deltaTexelsY;
    if (destinationX < 0 || destinationY < 0 ||
        destinationX >= activeViewWidth || destinationY >= activeViewHeight) {
      continue;
    }
    fadingTexels[retainedCount] = destinationY * textureWidth + destinationX;
    retainedCount += 1u;
  }
  fadingTexels.resize(retainedCount);
  fadeActive = !fadingTexels.empty();
}

void
CanvasView::sampleCacheRectangle(int minimumX,
                                 int maximumX,
                                 int minimumY,
                                 int maximumY)
{
  ZoneScopedN("CanvasView.sampleCacheRectangle");
  for (int y = minimumY; y <= maximumY; ++y) {
    for (int x = minimumX; x <= maximumX; ++x) {
      const int index = y * textureWidth + x;
      fadingFlags[static_cast<std::size_t>(index)] = 0u;
      sampleCacheTexel(x, y, true);
      lastSampledTexelCount += 1u;
    }
  }
}

void
CanvasView::sampleExposedCacheStrips(int deltaTexelsX, int deltaTexelsY)
{
  lastSampledTexelCount = 0u;
  if (grid == nullptr || activeViewWidth <= 0 || activeViewHeight <= 0) {
    return;
  }

  if (deltaTexelsX > 0) {
    sampleCacheRectangle(activeViewWidth - deltaTexelsX,
                         activeViewWidth - 1,
                         0,
                         activeViewHeight - 1);
  } else if (deltaTexelsX < 0) {
    sampleCacheRectangle(0, -deltaTexelsX - 1, 0, activeViewHeight - 1);
  }

  const int retainedMinimumX = deltaTexelsX > 0 ? 0 : -deltaTexelsX;
  const int retainedMaximumX =
    deltaTexelsX > 0 ? activeViewWidth - deltaTexelsX - 1 : activeViewWidth - 1;
  if (deltaTexelsY > 0 && retainedMinimumX <= retainedMaximumX) {
    sampleCacheRectangle(retainedMinimumX,
                         retainedMaximumX,
                         activeViewHeight - deltaTexelsY,
                         activeViewHeight - 1);
  } else if (deltaTexelsY < 0 && retainedMinimumX <= retainedMaximumX) {
    sampleCacheRectangle(
      retainedMinimumX, retainedMaximumX, 0, -deltaTexelsY - 1);
  }
}

bool
CanvasView::tryScrollCache(const CacheLayout& nextLayout)
{
  if (!regionReady || paletteDirty || grid == nullptr || grid->isToroidal()) {
    return false;
  }
  if (nextLayout.cellWidth != cacheCellWidth ||
      nextLayout.cellHeight != cacheCellHeight ||
      nextLayout.activeWidth != activeViewWidth ||
      nextLayout.activeHeight != activeViewHeight) {
    return false;
  }
  const std::int64_t deltaCellsX = nextLayout.firstCell.x - cacheFirstCell.x;
  const std::int64_t deltaCellsY = cacheFirstCell.y - nextLayout.firstCell.y;
  if (cellsPerTexel <= 0 || deltaCellsX % cellsPerTexel != 0 ||
      deltaCellsY % cellsPerTexel != 0) {
    return false;
  }
  const std::int64_t deltaTexelsX64 = deltaCellsX / cellsPerTexel;
  const std::int64_t deltaTexelsY64 = deltaCellsY / cellsPerTexel;
  if (deltaTexelsX64 == 0 && deltaTexelsY64 == 0) {
    return false;
  }
  if (deltaTexelsX64 <= -activeViewWidth || deltaTexelsX64 >= activeViewWidth ||
      deltaTexelsY64 <= -activeViewHeight ||
      deltaTexelsY64 >= activeViewHeight) {
    return false;
  }

  ZoneNamedN(CanvasViewScrollZone, "CanvasView.scrollCache", true);
  const int deltaTexelsX = static_cast<int>(deltaTexelsX64);
  const int deltaTexelsY = static_cast<int>(deltaTexelsY64);
  copyCacheOverlap(deltaTexelsX, deltaTexelsY);
  remapFadingTexels(deltaTexelsX, deltaTexelsY);
  for (int index : changedSampleTexels) {
    changedSampleFlags[static_cast<std::size_t>(index)] = 0u;
  }
  changedSampleTexels.clear();
  cacheFirstCell = nextLayout.firstCell;
  worldQuadReady = false;
  rebuildWorldQuad();
  sampleExposedCacheStrips(deltaTexelsX, deltaTexelsY);
  markFullActiveUpload();
  return true;
}

void
CanvasView::syncVisibleRegion()
{
  ZoneScopedN("CanvasView.syncVisibleRegion");
  int width = 1280;
  int height = 720;
  double zoom = 1.0;
  glm::dvec2 position(0.0, 0.0);
  if (window != nullptr) {
    const std::array<int, 2> dimensions = window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }
  if (camera != nullptr) {
    zoom = std::max(0.1, static_cast<double>(camera->GetZoom()));
    position = camera->GetPositionPrecise();
  }

  const double worldWidth = static_cast<double>(width) / zoom;
  const double worldHeight = static_cast<double>(height) / zoom;
  const int nextCellWidth =
    static_cast<int>(std::ceil(worldWidth / static_cast<double>(kCellSize))) +
    2;
  const int nextCellHeight =
    static_cast<int>(std::ceil(worldHeight / static_cast<double>(kCellSize))) +
    2;
  const int outputBudgetWidth =
    std::max(baseViewWidth,
             (width + kOverviewPixelsPerTexel - 1) / kOverviewPixelsPerTexel);
  const int outputBudgetHeight =
    std::max(baseViewHeight,
             (height + kOverviewPixelsPerTexel - 1) / kOverviewPixelsPerTexel);
  const int requiredCellsPerTexel = std::max(
    1,
    std::max((nextCellWidth + outputBudgetWidth - 1) / outputBudgetWidth,
             (nextCellHeight + outputBudgetHeight - 1) / outputBudgetHeight));
  const int nextCellsPerTexel = resolveCellsPerTexel(requiredCellsPerTexel,
                                                     outputBudgetWidth,
                                                     outputBudgetHeight,
                                                     nextCellWidth,
                                                     nextCellHeight);
  const int nextVisibleViewWidth =
    (nextCellWidth + nextCellsPerTexel - 1) / nextCellsPerTexel;
  const int nextVisibleViewHeight =
    (nextCellHeight + nextCellsPerTexel - 1) / nextCellsPerTexel;
  const std::int64_t centerX = worldToCell(position.x);
  const std::int64_t centerY = worldToCell(position.y);
  const CellAddress nextFirstCell{ centerX - nextCellWidth / 2,
                                   centerY + (nextCellHeight - 1) / 2 };
  const bool refill =
    !regionReady || nextCellsPerTexel != cellsPerTexel ||
    !cacheContains(nextFirstCell, nextCellWidth, nextCellHeight);
  visibleFirstCell = nextFirstCell;
  visibleCellWidth = nextCellWidth;
  visibleCellHeight = nextCellHeight;
  visibleViewWidth = nextVisibleViewWidth;
  visibleViewHeight = nextVisibleViewHeight;
  if (!refill) {
    return;
  }

  const CacheLayout nextLayout = computeCacheLayout(
    nextFirstCell, nextCellWidth, nextCellHeight, nextCellsPerTexel);
  if (nextCellsPerTexel == cellsPerTexel && tryScrollCache(nextLayout)) {
    return;
  }

  ZoneNamedN(CanvasViewRefillZone, "CanvasView.refillCache", true);
  resizeBuffers(nextLayout.activeWidth, nextLayout.activeHeight);
  cellsPerTexel = nextCellsPerTexel;
  cacheFirstCell = nextLayout.firstCell;
  cacheCellWidth = nextLayout.cellWidth;
  cacheCellHeight = nextLayout.cellHeight;
  activeViewWidth = nextLayout.activeWidth;
  activeViewHeight = nextLayout.activeHeight;
  regionReady = true;
  worldQuadReady = false;
  rebuildWorldQuad();
  if (grid != nullptr) {
    sampleGrid(true);
    lastGridRevision = grid->getRevision();
    paletteDirty = false;
  }
}

void
CanvasView::rebuildTargetsFromGrid()
{
  ZoneScopedN("CanvasView.rebuildTargetsFromGrid");
  syncVisibleRegion();
  if (grid == nullptr) {
    return;
  }
  const std::uint64_t currentRevision = grid->getRevision();
  if (!paletteDirty && currentRevision == lastGridRevision) {
    return;
  }
  if (paletteDirty || !sampleChangedChunks(lastGridRevision)) {
    sampleGrid(cellsPerTexel > 1);
  }
  lastGridRevision = currentRevision;
  paletteDirty = false;
  if (shouldSnapSample()) {
    snapVisualToTargets();
  }
}

void
CanvasView::clearView()
{
  if (grid != nullptr) {
    grid->clear();
  }
  rebuildTargetsFromGrid();
  snapVisualToTargets();
}

void
CanvasView::setFadeSpeed(float speed)
{
  const float nextFadeSpeed = std::max(0.0f, speed);
  if (fadeSpeed == nextFadeSpeed) {
    return;
  }
  fadeSpeed = nextFadeSpeed;
  if (fadeSpeed <= 0.0f && fadeActive) {
    snapVisualToTargets();
  }
}

void
CanvasView::snapVisualToTargets()
{
  ZoneScopedN("CanvasView.snapVisualToTargets");
  lastSnapVisitCount = fadingTexels.size();
  for (int index : fadingTexels) {
    const int base = index * 3;
    displayRgb[base + 0] = targetRgb[base + 0];
    displayRgb[base + 1] = targetRgb[base + 1];
    displayRgb[base + 2] = targetRgb[base + 2];
    writeTexel(index, index % textureWidth, index / textureWidth);
  }
  clearFadingTexels();
}

void
CanvasView::tickVisual(float dt)
{
  ZoneScopedN("CanvasView.tickVisual");
  lastFadeVisitCount = fadingTexels.size();
  if (!fadeActive) {
    return;
  }
  if (dt < 0.0f) {
    dt = 0.0f;
  }
  if (fadeSpeed <= 0.0f) {
    snapVisualToTargets();
    return;
  }
  const float alpha = std::min(1.0f, 1.0f - std::exp(-fadeSpeed * dt));
  std::size_t retainedCount = 0u;
  for (int index : fadingTexels) {
    const int base = index * 3;
    bool cellStillFading = false;
    for (int channel = 0; channel < 3; ++channel) {
      const float target = targetRgb[base + channel];
      const float difference = target - displayRgb[base + channel];
      if (std::fabs(difference) > 0.002f) {
        displayRgb[base + channel] += difference * alpha;
        if (std::fabs(target - displayRgb[base + channel]) > 0.002f) {
          cellStillFading = true;
        } else {
          displayRgb[base + channel] = target;
        }
      } else {
        displayRgb[base + channel] = target;
      }
    }
    writeTexel(index, index % textureWidth, index / textureWidth);
    if (cellStillFading) {
      fadingTexels[retainedCount] = index;
      retainedCount += 1u;
    } else {
      fadingFlags[static_cast<std::size_t>(index)] = 0u;
    }
  }
  fadingTexels.resize(retainedCount);
  fadeActive = !fadingTexels.empty();
}

CellAddress
CanvasView::getVisibleCell(int x, int y) const
{
  if (!regionReady || x < 0 || y < 0 || x >= visibleViewWidth ||
      y >= visibleViewHeight) {
    return CellAddress{ std::numeric_limits<std::int64_t>::max(),
                        std::numeric_limits<std::int64_t>::max() };
  }
  const int sourceX = x * visibleCellWidth / visibleViewWidth;
  const int sourceY = y * visibleCellHeight / visibleViewHeight;
  return CellAddress{ visibleFirstCell.x + sourceX,
                      visibleFirstCell.y - sourceY };
}

void
CanvasView::DrawImpl()
{
}

bool
CanvasView::AppendCommands(Renderer* activeRenderer)
{
  ZoneScopedN("CanvasView.AppendCommands");
  if (!isVisible() || !gpuReady || activeRenderer == nullptr) {
    return isVisible();
  }
  if (textureUploadPending && activeViewWidth > 0 && activeViewHeight > 0) {
    ZoneScopedN("CanvasView.prepareUploadRectangles");
    buildUploadRects();
    lastUploadByteCount = 0u;
    lastUploadRectCount = uploadRectScratch.size();
    for (const UploadRect& rectangle : uploadRectScratch) {
      activeRenderer->pushUpdateTexture(
        displayTextureHandle,
        rectangle.x,
        rectangle.y,
        rectangle.width,
        rectangle.height,
        3,
        texBuffer + (static_cast<std::size_t>(rectangle.y) *
                       static_cast<std::size_t>(textureWidth) +
                     static_cast<std::size_t>(rectangle.x)) *
                      3u,
        textureWidth);
      lastUploadByteCount += static_cast<std::size_t>(rectangle.width) *
                             static_cast<std::size_t>(rectangle.height) * 3u;
    }
    uploadByteMetric.add(static_cast<double>(lastUploadByteCount));
    uploadRectMetric.add(static_cast<double>(lastUploadRectCount));
    textureUploadPending = false;
    resetUploadBounds();
  }
  visual.setRenderer(activeRenderer);
  visual.setVisible(isVisible());
  return visual.AppendCommands(activeRenderer);
}
