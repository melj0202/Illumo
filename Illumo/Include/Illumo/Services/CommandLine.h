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
// The console deliberately uses its own flat terminal palette rather than the
// product UiTheme so developer tooling never reads as product UI.
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
  ~CommandLine() override;
  CommandLine(const CommandLine&) = delete;
  CommandLine& operator=(const CommandLine&) = delete;
  CommandLine(CommandLine&&) = delete;
  CommandLine& operator=(CommandLine&&) = delete;

  void Toggle();
  // Output scrolling: one visual line, one page, or either end.
  void ScrollUp();
  void ScrollDown();
  void ScrollPageUp();
  void ScrollPageDown();
  void ScrollToTop();
  void ScrollToBottom();
  void HandleMousePress(double mouseX, double mouseY, bool isDrag = false);
  void HandleMouseDrag(double mouseX, double mouseY);
  void HandleMouseRelease();
  void HandleScroll(double yOffset);
  void DrawImpl();
  bool AppendCommands(Renderer* renderer) override;
  GameVisual& getVisual() { return visual; }
  const GameVisual& getVisual() const { return visual; }
  bool isOpen;
  // True while open, still sliding, or showing the closed-console alert badge
  // (avoid dispatch otherwise).
  bool wantsDraw() const
  {
    return isVisible() && !detached &&
           (isOpen || animationProgress > 0.0f || hasUnseenAlerts());
  }

  // Detached mode (D-UI5): the console lives in its own OS window owned by
  // DebugModule. Layout fills that window at UI scale 1, nothing is submitted
  // to the Renderer, and isOpen stays false so the game keeps its input.
  // CommandLine only raises requests; the host creates and destroys windows.
  enum class WindowRequestKind : unsigned char
  {
    None,
    Detach, // pop out into a separate window
    Dock,   // return into the game, open
    Close   // return into the game, closed
  };
  struct WindowRequest
  {
    WindowRequestKind kind = WindowRequestKind::None;
    // Desired client-area origin relative to the game window's client area,
    // and client size, in window pixels.
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
  };
  WindowRequest takeWindowRequest();
  void requestDetach();
  void requestDock();
  void setDetachAvailable(bool available) { detachAvailable = available; }
  bool isDetachAvailable() const { return detachAvailable; }
  void setDetached(bool isDetachedNow, int width, int height);
  void setDetachedSize(int width, int height);
  bool isDetached() const { return detached; }
  // Accepts keyboard/mouse editing: open in the game or detached.
  bool isInteractive() const { return isOpen || detached; }
  // Rebuilds the detached window's primitives when dirty; true when rebuilt.
  bool ComposeDetached();
  // Errors and warnings logged while the console was closed.
  int getUnseenErrorCount() const { return unseenErrors; }
  int getUnseenWarningCount() const { return unseenWarnings; }
  bool hasUnseenAlerts() const
  {
    return getAlertsEnabled() && (unseenErrors > 0 || unseenWarnings > 0);
  }
  // Visual lines that arrived while the reader was scrolled back.
  int getNewLinesWhileScrolled() const { return newLinesWhileScrolled; }

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
  void onHistoryBackUpdated() override;
  void onViewChanged() override;
  bool writeClipboard(const std::string& text) override;
  std::string readClipboard() const override;
  void onCloseRequested() override;
  void onQuitRequested() override;
  void onToggleFullscreen() override;
  void queryWindowDimensions(int* width, int* height) const override;

  void markCompositionDirty();
  void invalidateWrapCache();

private:
  // Shared panel metrics so scroll handlers, hit tests, and draw agree. All
  // values are in UI-scaled virtual pixels, the same space GameVisual draws in.
  struct PanelLayout
  {
    float panelX0;
    float panelY0;
    float panelX1;
    float panelY1;
    float headerHeight;
    float watchHeight;
    float historyTop;
    float historyBottom;
    float inputTop;
    float inputBottom;
    float statusTop;
    float textX;
    float gutterWidth;
    float scrollbarX0;
    float scrollbarX1;
    float lineSpacing;
    int maxHistoryLines;
    float wrapWidth;
  };

  // A drawn output line that echoes a command; clicking it recalls the command.
  struct DrawnCommandLine
  {
    float y;
    std::string command;
  };

  bool detached;
  bool detachAvailable;
  int detachedWidth;
  int detachedHeight;
  WindowRequest pendingWindowRequest;
  bool composedEmpty;

  int scrollOffset;
  int newLinesWhileScrolled;
  int unseenErrors;
  int unseenWarnings;
  std::vector<DrawnCommandLine> drawnCommandLines;
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

  // Smoothed panel rectangle (virtual pixels) that the draw path eases toward
  // the target layout.
  float currentPanelX;
  float currentPanelY;
  float currentPanelW;
  float currentPanelH;

  IRenderWindow* window;
  Renderer* renderer;

  // Chrome + text composed as GameVisual primitives (D-R15).
  GameVisual visual;
  bool gpuReady;

  // Open/close slide progress (0 closed, 1 open).
  float animationProgress;
  std::chrono::high_resolution_clock::time_point lastAnimTime;

  // Wrapped visual lines stay equivalent to direct wrapTextToWidth (D-UI2).
  // Entries hidden by the view filters wrap to zero lines.
  mutable std::vector<std::vector<std::string>> wrappedHistory;
  mutable float wrappedHistoryWidth;
  mutable int wrappedHistoryTotalLines;
  mutable int wrappedVisibleEntries;

  // Settled composition is replayed until a dirty reason fires (D-P2).
  bool compositionDirty;
  int composedCaretPhase;
  int composedScrollOffset;
  int composedWindowW;
  int composedWindowH;
  int composedFps;
  std::string composedWatchLine;
  std::string composedBadgeKey;
  bool composedAsBadge;
  float composedPanelX;
  float composedPanelY;
  float composedPanelW;
  float composedPanelH;

  // Approximate capacity tracking for chrome/text emission via GameVisual.
  static const unsigned int kUiQuadCap = 8000;
  static const unsigned int kUiVertCap = kUiQuadCap * 4;

  void enrollGpuResources();
  void unregisterConsoleCommands();
  void wrapEntry(const historyBuffer& entry,
                 float width,
                 std::vector<std::string>* lines) const;
  void rebuildWrapCache(float width) const;
  void ensureWrapCache(float width) const;
  float virtualScale() const;
  void virtualWindowSize(float* width, float* height) const;
  void computeTargetPanel(float* x, float* y, float* w, float* h) const;
  PanelLayout computePanelLayout(bool useSmoothedPanel) const;
  int countWrappedHistoryLines(float availableWidth) const;
  void computeHistoryScrollLimits(int* maxHistoryLines,
                                  int* maxScroll,
                                  float* historyWidth) const;
  void clampScrollOffset();
  void scrollBy(int lines);
  int currentFps() const;
  void countAlert(const historyBuffer& entry);
  // "name=value" pairs for the watch strip; also the strip's dirty key.
  std::string buildWatchLine() const;
  bool appendAlertBadge(Renderer* renderer);
  // Builds panel primitives when dirty (shared by the in-game and detached
  // paths); true when the visual was rebuilt.
  bool composePanel();
  std::array<int, 2> layoutDimensions() const;
  // Header control geometry shared by drawing and hit tests.
  struct HeaderControls
  {
    float closeX0;
    float buttonX0;
    float buttonX1;
    float modeRight;
    std::string buttonLabel;
  };
  HeaderControls computeHeaderControls(const PanelLayout& layout) const;
  WindowRequest makeWindowRequest(WindowRequestKind kind) const;
};
