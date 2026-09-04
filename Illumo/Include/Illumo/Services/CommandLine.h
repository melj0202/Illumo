#pragma once

#include <Illumo/Foundation/MathTypes.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Services/CommandLineCore.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Renderer;

// Console UI drawable (token path). No longer inherits SceneObject (D-E4).
class CommandLine
  : public CommandLineCore
  , public Drawable<CommandLine>
{
public:
  using historyBuffer = CommandLineCore::historyBuffer;

  CommandLine(IEnvVars* vars,
              CommandRegistry* commandRegistry,
              IRenderWindow* win,
              Renderer* renderer = nullptr,
              const std::string& applicationName = "Illumo");

  void Toggle();
  void ScrollUp();
  void ScrollDown();
  void HandleMousePress(double mouseX, double mouseY, bool isDrag = false);
  void HandleMouseDrag(double mouseX, double mouseY);
  void HandleMouseRelease();
  void HandleScroll(double yOffset);
  void DrawImpl();
  bool AppendCommands(Renderer* renderer) override;
  GameVisual& getVisual() { return visual; }
  const GameVisual& getVisual() const { return visual; }
  bool isOpen;
  // True while open or still sliding (avoid dispatch when fully closed).
  bool wantsDraw() const
  {
    return isVisible() && (isOpen || animationProgress > 0.0f);
  }

  int getScrollOffset() const { return scrollOffset; }
  bool getFloatingMode() const { return isFloating; }
  void setFloatingMode(bool floating);
  void ToggleFloatingMode();
  float getFloatingWidth() const { return floatingW; }
  float getFloatingHeight() const { return floatingH; }
  void setFloatingSize(float w, float h);

  // Virtual hooks from CommandLineCore
  void onInputChanged() override;
  void onHistoryAppended(const historyBuffer& item, bool erasedFront) override;
  void onHistoryCleared() override;
  void onCloseRequested() override;
  void onQuitRequested() override;
  void onToggleFullscreen() override;
  void queryWindowDimensions(int* width, int* height) const override;

  void markCompositionDirty();
  void invalidateWrapCache();

private:
  // Shared panel metrics so scroll handlers, hit tests, and draw agree.
  struct PanelLayout
  {
    float panelX0;
    float panelY0;
    float panelX1;
    float panelY1;
    float headerHeight;
    float inputRowHeight;
    float historyTop;
    float inputTop;
    float historyBottom;
    float lineSpacing;
    int maxHistoryLines;
    float historyBaseWidth;
  };

  int scrollOffset;
  bool consoleInitialized;
  bool isDraggingScrollbar;
  float dragStartY;
  int dragStartScrollOffset;

  // Floating, Window Drag & Resize State
  bool isFloating;
  float floatingX;
  float floatingY;
  float floatingW;
  float floatingH;
  bool isDraggingWindow;
  bool isResizingWindow;
  float dragWindowOffsetX;
  float dragWindowOffsetY;
  float resizeStartW;
  float resizeStartH;
  float resizeStartMouseX;
  float resizeStartMouseY;
  std::chrono::high_resolution_clock::time_point lastHeaderClickTime;

  // Animated Layout Smoothing & FX
  float currentPanelX;
  float currentPanelY;
  float currentPanelW;
  float currentPanelH;

  IRenderWindow* window;
  Renderer* renderer;

  // Chrome + text composed as GameVisual primitives (D-R15).
  GameVisual visual;
  bool gpuReady;

  // Animation (was static in DrawImpl)
  float animationProgress;
  std::chrono::high_resolution_clock::time_point lastAnimTime;

  // Wrapped visual lines stay equivalent to direct wrapTextToWidth (D-UI2).
  mutable std::vector<std::vector<std::string>> wrappedHistory;
  mutable float wrappedHistoryWidth;
  mutable int wrappedHistoryTotalLines;

  // Settled composition is replayed until a dirty reason fires (D-P2).
  bool compositionDirty;
  int composedCaretPhase;
  int composedPulseStep;
  int composedScrollOffset;
  int composedWindowW;
  int composedWindowH;
  float composedPanelX;
  float composedPanelY;
  float composedPanelW;
  float composedPanelH;

  // Approximate capacity tracking for chrome/text emission via GameVisual.
  static const unsigned int kUiQuadCap = 8000;
  static const unsigned int kUiVertCap = kUiQuadCap * 4;

  void enrollGpuResources();
  void rebuildWrapCache(float width) const;
  void ensureWrapCache(float width) const;
  PanelLayout computePanelLayout(bool useSmoothedPanel) const;
  int countWrappedHistoryLines(float availableWidth) const;
  void computeHistoryScrollLimits(int* maxHistoryLines,
                                  int* maxScroll,
                                  float* historyWidth) const;
  void clampScrollOffset();
};
