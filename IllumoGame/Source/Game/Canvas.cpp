#include "Canvas.h"
#include "Rulesets/RuleSet.h"
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/Logger.h>
#include <array>
#include <cmath>
#include <cstring>
#include <glm/fwd.hpp>

Canvas::Canvas(int width,
               int height,
               IRenderWindow* window,
               Camera* camera,
               Renderer* renderer)
  : CellGrid(width, height)
{
  this->window = window;
  this->camera = camera;
  this->renderer = renderer;
  texCanvasBuffer = nullptr;
  displayRgb = nullptr;
  targetRgb = nullptr;
  fadeSpeed = 8.0f;
  displayTextureHandle = TextureHandle{};
  gpuReady = false;
  worldWidth = 0.0f;
  worldHeight = 0.0f;
  fadeActive = false;
  textureUploadPending = true;
  fadeDirtyRect.clear();
  uploadDirtyRect.clear();
  std::memset(paletteRgb, 255, sizeof(paletteRgb));
  initCanvas(width, height);
}

Canvas::~Canvas()
{
  if (renderer != nullptr && displayTextureHandle.isValid()) {
    renderer->destroyTexture(displayTextureHandle);
    displayTextureHandle = TextureHandle{};
  }
  freeCanvas();
}

void
Canvas::rebuildDefaultPalette()
{
  for (int s = 0; s < kPaletteSize; ++s) {
    const int base = s * 3;
    if (s == 0) {
      paletteRgb[base + 0] = 0;
      paletteRgb[base + 1] = 0;
      paletteRgb[base + 2] = 0;
    } else if (s == 1) {
      paletteRgb[base + 0] = 255;
      paletteRgb[base + 1] = 255;
      paletteRgb[base + 2] = 255;
    } else {
      paletteRgb[base + 0] = 0;
      paletteRgb[base + 1] = 164;
      paletteRgb[base + 2] = 128;
    }
  }
}

void
Canvas::rebuildPalette(const RuleSet* rules)
{
  ILLUMO_PROFILE_ZONE("Canvas.rebuildPalette");
  if (!rules) {
    rebuildDefaultPalette();
  } else {
    for (int s = 0; s < kPaletteSize; ++s) {
      unsigned char rgb[3] = { 255, 255, 255 };
      rules->evalCell(static_cast<unsigned char>(s), rgb);
      const int base = s * 3;
      paletteRgb[base + 0] = rgb[0];
      paletteRgb[base + 1] = rgb[1];
      paletteRgb[base + 2] = rgb[2];
    }
  }
  // New colors for existing life values — rebuild all display targets.
  markCellsDirty();
}

void
Canvas::initCanvas(const int& width, const int& height)
{
  // Domain storage comes from CellGrid base; only (re)allocate presentation.
  if (width != canvasWidth || height != canvasHeight || lifeCanvas == nullptr) {
    allocateLifeStorage(width, height);
  }

  fadeSpeed = 8.0f;

  delete[] texCanvasBuffer;
  delete[] displayRgb;
  delete[] targetRgb;

  texCanvasBuffer = new unsigned char[static_cast<size_t>(width * height * 3)];
  memset(texCanvasBuffer, 255, static_cast<size_t>(width * height * 3));

  const int rgbCount = width * height * 3;
  displayRgb = new float[static_cast<size_t>(rgbCount)];
  targetRgb = new float[static_cast<size_t>(rgbCount)];
  for (int i = 0; i < rgbCount; ++i) {
    displayRgb[i] = 1.0f;
    targetRgb[i] = 1.0f;
  }

  rebuildDefaultPalette();

  const float cellSize = 16.0f;
  worldWidth = static_cast<float>(width) * cellSize;
  worldHeight = static_cast<float>(height) * cellSize;

  fadeActive = false;
  fadeDirtyRect.clear();
  textureUploadPending = true;
  uploadDirtyRect.setFull(width, height);

  enrollGpuResources();
  Logger::LogTrace("Canvas initialized (domain CellGrid + RGB presentation)");
}

void
Canvas::enrollGpuResources()
{
  gpuReady = false;
  if (!renderer) {
    // Domain-only / headless construction is valid (D-C2).
    return;
  }

  // Display is a world-space sprite on GameVisual (D-R15).
  renderer->ensureBuiltinStyles();
  visual.setRenderer(renderer);
  visual.setWindow(window);
  visual.setCamera(camera);
  visual.setSpace(PrimitiveSpace::World);
  visual.setLayerHint(RenderLayerId::World);
  visual.prepare(renderer);

  // RGB display texture (faded colors). Domain remains lifeCanvas on CPU.
  displayTextureHandle =
    renderer->enrollTexture(texCanvasBuffer, canvasWidth, canvasHeight, 3);

  ColorRgba white{ 255, 255, 255, 255 };
  visual.clearPrimitives();
  // Origin bottom-left in world space (matches historical Canvas mesh).
  visual.addSprite(
    displayTextureHandle, 0.0f, 0.0f, worldWidth, worldHeight, white);

  gpuReady = true;
  textureUploadPending = true;
  uploadDirtyRect.setFull(canvasWidth, canvasHeight);
}

void
Canvas::freeCanvas()
{
  delete[] texCanvasBuffer;
  delete[] displayRgb;
  delete[] targetRgb;
  texCanvasBuffer = nullptr;
  displayRgb = nullptr;
  targetRgb = nullptr;
  gpuReady = false;
  // lifeCanvas is owned by CellGrid base destructor / freeLifeStorage.
}

void
Canvas::clearCanvas()
{
  clearCells();
  if (texCanvasBuffer != nullptr && displayRgb != nullptr &&
      targetRgb != nullptr) {
    const int n = canvasWidth * canvasHeight * 3;
    for (int i = 0; i < n; ++i) {
      displayRgb[i] = 1.0f;
      targetRgb[i] = 1.0f;
      texCanvasBuffer[i] = 255;
    }
  }
  fadeActive = false;
  fadeDirtyRect.clear();
  textureUploadPending = true;
  uploadDirtyRect.setFull(canvasWidth, canvasHeight);
}

void
Canvas::setFadeSpeed(float speed)
{
  if (speed < 0.0f) {
    speed = 0.0f;
  }
  if (speed != fadeSpeed) {
    fadeSpeed = speed;
    if (fadeActive || cellsDirty) {
      fadeActive = true;
      if (!fadeDirtyRect.valid()) {
        fadeDirtyRect.setFull(canvasWidth, canvasHeight);
      }
    }
  }
}

void
Canvas::setTargetColor(int cellIndex,
                       unsigned char r,
                       unsigned char g,
                       unsigned char b)
{
  if (cellIndex < 0 || cellIndex >= canvasWidth * canvasHeight) {
    return;
  }
  const int cellX = cellIndex % canvasWidth;
  const int cellY = cellIndex / canvasWidth;
  bool any = false;
  setTargetColorRect(cellIndex, cellX, cellY, r, g, b, &any);
  if (any) {
    fadeActive = true;
    fadeDirtyRect.include(cellX, cellY);
  }
}

void
Canvas::setTargetColorRect(int cellIndex,
                           int cellX,
                           int cellY,
                           unsigned char r,
                           unsigned char g,
                           unsigned char b,
                           bool* anyTargetChange)
{
  (void)cellX;
  (void)cellY;
  const int base = cellIndex * 3;
  const float rf = static_cast<float>(r) * (1.0f / 255.0f);
  const float gf = static_cast<float>(g) * (1.0f / 255.0f);
  const float bf = static_cast<float>(b) * (1.0f / 255.0f);
  if (targetRgb[base + 0] != rf || targetRgb[base + 1] != gf ||
      targetRgb[base + 2] != bf) {
    targetRgb[base + 0] = rf;
    targetRgb[base + 1] = gf;
    targetRgb[base + 2] = bf;
    if (anyTargetChange) {
      *anyTargetChange = true;
    }
  }
}

void
Canvas::rebuildTargetsFromLife()
{
  ILLUMO_PROFILE_ZONE("Canvas.rebuildTargetsFromLife");
  if (!cellsDirty || !lifeCanvas) {
    return;
  }

  const int w = canvasWidth;
  bool anyTargetChange = false;
  int x0 = 0;
  int y0 = 0;
  int x1 = w - 1;
  int y1 = canvasHeight - 1;
  if (cellsDirtyRect.valid()) {
    x0 = cellsDirtyRect.minX;
    y0 = cellsDirtyRect.minY;
    x1 = cellsDirtyRect.maxX;
    y1 = cellsDirtyRect.maxY;
  }

  for (int y = y0; y <= y1; ++y) {
    const int rowBase = y * w;
    for (int x = x0; x <= x1; ++x) {
      const int i = rowBase + x;
      const unsigned char state = lifeCanvas[i];
      const int p = static_cast<int>(state) * 3;
      setTargetColorRect(i,
                         x,
                         y,
                         paletteRgb[p + 0],
                         paletteRgb[p + 1],
                         paletteRgb[p + 2],
                         &anyTargetChange);
    }
  }

  // One rectangular expand for the fade region instead of per-cell include.
  if (anyTargetChange) {
    fadeActive = true;
    fadeDirtyRect.includeRect(x0, y0, x1, y1);
  }
  onTargetsRebuilt();
}

void
Canvas::onTargetsRebuilt()
{
  cellsDirty = false;
  cellsDirtyRect.clear();
  if (fadeSpeed <= 0.0f) {
    snapVisualToTargets();
    fadeActive = false;
  }
}

void
Canvas::noteTexelChanged(int cellX, int cellY)
{
  textureUploadPending = true;
  uploadDirtyRect.include(cellX, cellY);
}

void
Canvas::writeTexelFromDisplay(int cellIndex,
                              int cellX,
                              int cellY,
                              bool* anyByteChange)
{
  const int base = cellIndex * 3;
  bool changed = false;
  for (int c = 0; c < 3; ++c) {
    const float v = displayRgb[base + c] * 255.0f + 0.5f;
    const unsigned char b =
      static_cast<unsigned char>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
    if (texCanvasBuffer[base + c] != b) {
      texCanvasBuffer[base + c] = b;
      changed = true;
    }
  }
  if (changed) {
    noteTexelChanged(cellX, cellY);
    if (anyByteChange) {
      *anyByteChange = true;
    }
  }
}

void
Canvas::snapVisualToTargets()
{
  ILLUMO_PROFILE_ZONE("Canvas.snapVisualToTargets");
  bool anyByteChange = false;

  int x0 = 0;
  int y0 = 0;
  int x1 = canvasWidth - 1;
  int y1 = canvasHeight - 1;
  if (fadeDirtyRect.valid()) {
    x0 = fadeDirtyRect.minX;
    y0 = fadeDirtyRect.minY;
    x1 = fadeDirtyRect.maxX;
    y1 = fadeDirtyRect.maxY;
  }

  for (int y = y0; y <= y1; ++y) {
    const int rowBase = y * canvasWidth;
    for (int x = x0; x <= x1; ++x) {
      const int cellIndex = rowBase + x;
      const int base = cellIndex * 3;
      displayRgb[base + 0] = targetRgb[base + 0];
      displayRgb[base + 1] = targetRgb[base + 1];
      displayRgb[base + 2] = targetRgb[base + 2];
      writeTexelFromDisplay(cellIndex, x, y, &anyByteChange);
    }
  }

  (void)anyByteChange;
  fadeActive = false;
  fadeDirtyRect.clear();
}

void
Canvas::tickVisual(float dt)
{
  ILLUMO_PROFILE_ZONE("Canvas.tickVisual");
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

  float alpha = 1.0f - expf(-fadeSpeed * dt);
  if (alpha > 1.0f) {
    alpha = 1.0f;
  }

  int x0 = 0;
  int y0 = 0;
  int x1 = canvasWidth - 1;
  int y1 = canvasHeight - 1;
  if (fadeDirtyRect.valid()) {
    x0 = fadeDirtyRect.minX;
    y0 = fadeDirtyRect.minY;
    x1 = fadeDirtyRect.maxX;
    y1 = fadeDirtyRect.maxY;
  }

  bool stillFading = false;
  bool anyByteChange = false;
  const float eps = 0.002f;

  for (int y = y0; y <= y1; ++y) {
    const int rowBase = y * canvasWidth;
    for (int x = x0; x <= x1; ++x) {
      const int cellIndex = rowBase + x;
      const int base = cellIndex * 3;
      bool cellStill = false;
      for (int c = 0; c < 3; ++c) {
        const float target = targetRgb[base + c];
        float d = displayRgb[base + c];
        const float diff = target - d;
        if (diff > eps || diff < -eps) {
          d = d + diff * alpha;
          displayRgb[base + c] = d;
          const float diff2 = target - d;
          if (diff2 > eps || diff2 < -eps) {
            cellStill = true;
          } else {
            displayRgb[base + c] = target;
          }
        } else {
          displayRgb[base + c] = target;
        }
      }
      if (cellStill) {
        stillFading = true;
      }
      writeTexelFromDisplay(cellIndex, x, y, &anyByteChange);
    }
  }

  (void)anyByteChange;
  fadeActive = stillFading;
  if (!stillFading) {
    fadeDirtyRect.clear();
  }
}

void
Canvas::DrawImpl()
{
}

bool
Canvas::AppendCommands(Renderer* r)
{
  ILLUMO_PROFILE_ZONE("Canvas.AppendCommands");
  if (!isVisible() || !gpuReady || !r) {
    if (!isVisible()) {
      return true;
    }
    return false;
  }

  // RGB display texture: dirty-rect upload (PBO path inside GLTexture).
  if (textureUploadPending && texCanvasBuffer) {
    ILLUMO_PROFILE_ZONE("Canvas.UpdateDisplayTexture");
    int x = 0;
    int y = 0;
    int w = canvasWidth;
    int h = canvasHeight;
    const void* data = texCanvasBuffer;
    int rowStride = 0;

    if (uploadDirtyRect.valid()) {
      x = uploadDirtyRect.minX;
      y = uploadDirtyRect.minY;
      w = uploadDirtyRect.width();
      h = uploadDirtyRect.height();
      if (x < 0) {
        x = 0;
      }
      if (y < 0) {
        y = 0;
      }
      if (x + w > canvasWidth) {
        w = canvasWidth - x;
      }
      if (y + h > canvasHeight) {
        h = canvasHeight - y;
      }
      if (w > 0 && h > 0) {
        const size_t offset =
          (static_cast<size_t>(y) * static_cast<size_t>(canvasWidth) +
           static_cast<size_t>(x)) *
          3u;
        data = texCanvasBuffer + offset;
        rowStride = canvasWidth;
      } else {
        x = 0;
        y = 0;
        w = canvasWidth;
        h = canvasHeight;
        data = texCanvasBuffer;
        rowStride = 0;
      }
    }

    r->pushUpdateTexture(displayTextureHandle, x, y, w, h, 3, data, rowStride);
    textureUploadPending = false;
    uploadDirtyRect.clear();
  }

  visual.setRenderer(r);
  visual.setWindow(window);
  visual.setCamera(camera);
  visual.setSpace(PrimitiveSpace::World);
  visual.setVisible(isVisible());
  return visual.AppendCommands(r);
}
