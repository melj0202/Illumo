#pragma once
#include "CellGrid.h"
#include <Illumo/Foundation/MacroDefs.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Services/PoolAlloc.h>
#include <array>
#include <cstring>
#include <vector>

class Camera;
class IRenderWindow;
class Renderer;
class RuleSet;

// Presentation + GPU adapter over CellGrid domain storage (D-C2).
// Domain: inherited CellGrid (lifeCanvas, dirty tracking).
// View: CPU palette → targetRgb; displayRgb eases toward targets; RGB texture.
// GPU: world-space sprite on embedded GameVisual + display texture
// (D-R14/D-R15). Render services are optional: null renderer/window/camera
// skips GPU enroll so domain+rules can run headless without a graphics stack.
struct Canvas
  : public CellGrid
  , public Drawable<Canvas>
{

public:
  static constexpr int kPaletteSize = 256;

  Canvas(int width,
         int height,
         IRenderWindow* window = nullptr,
         Camera* camera = nullptr,
         Renderer* renderer = nullptr);
  ~Canvas();

  __ILLUMO_FORCE_INLINE__ std::array<int, 2> getDimensions()
  {
    return std::array<int, 2>{ canvasWidth, canvasHeight };
  }

  // Domain clear + presentation buffers reset.
  void clearCanvas();

  void initCanvas(const int& width, const int& height);
  void freeCanvas();

  void DrawImpl();
  bool AppendCommands(Renderer* renderer) override;

  // Scene-friendly host (world-space display sprite). Prefer adding this to
  // Scene when composing; Canvas::AppendCommands still forwards for tests.
  GameVisual& getVisual() { return visual; }
  const GameVisual& getVisual() const { return visual; }

  // Map cell state → target display color via palette (used by
  // updateVisualTargets).
  void setTargetColor(int cellIndex,
                      unsigned char r,
                      unsigned char g,
                      unsigned char b);
  // Apply palette[life] as targets for dirty life region (or full grid).
  void rebuildTargetsFromLife();
  void rebuildPalette(const RuleSet* rules);
  void rebuildDefaultPalette();

  void setFadeSpeed(float speed);
  float getFadeSpeed() const { return fadeSpeed; }
  void tickVisual(float dt);
  void snapVisualToTargets();

  void onTargetsRebuilt();
  bool isFadeActive() const { return fadeActive; }
  bool isTextureUploadPending() const { return textureUploadPending; }
  const DirtyRect& getUploadDirtyRegion() const { return uploadDirtyRect; }
  const unsigned char* getPaletteRgb() const { return paletteRgb; }
  const unsigned char* getDisplayTexBuffer() const { return texCanvasBuffer; }

  unsigned char* texCanvasBuffer;
  IRenderWindow* window;
  Camera* camera;
  Renderer* renderer;

private:
  void enrollGpuResources();
  void noteTexelChanged(int cellX, int cellY);
  // cellX/cellY avoid div/mod in the hot fade path when the caller already
  // has coordinates (D-P6).
  void writeTexelFromDisplay(int cellIndex,
                             int cellX,
                             int cellY,
                             bool* anyByteChange);
  // Rectangular target rebuild: one includeRect for fade dirty tracking.
  void setTargetColorRect(int cellIndex,
                          int cellX,
                          int cellY,
                          unsigned char r,
                          unsigned char g,
                          unsigned char b,
                          bool* anyTargetChange);

  unsigned char paletteRgb[kPaletteSize * 3];
  float* displayRgb;
  float* targetRgb;
  float fadeSpeed;

  GameVisual visual;
  TextureHandle displayTextureHandle{};
  bool gpuReady;
  float worldWidth;
  float worldHeight;

  bool fadeActive;
  bool textureUploadPending;

  DirtyRect fadeDirtyRect;
  DirtyRect uploadDirtyRect;
};
