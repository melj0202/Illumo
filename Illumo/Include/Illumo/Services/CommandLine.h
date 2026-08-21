#pragma once
#include <Illumo/Foundation/MathTypes.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Services/ArenaAlloc.h>
#include <Illumo/Services/ChainedStackAlloc.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/InputContext.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define MAX_CHARS_PER_LINE 1024
#define MAX_CMD_HISTORY 256

class Renderer;

// Console UI drawable (token path). No longer inherits SceneObject (D-E4).
class CommandLine : public Drawable<CommandLine>
{
public:
  struct historyBuffer
  {
    unsigned char r, g, b, a;
    std::string content;
  };
  CommandLine(IEnvVars* vars,
              CommandRegistry* commandRegistry,
              IRenderWindow* win,
              Renderer* renderer = nullptr,
              const std::string& applicationName = "Illumo");
  void Toggle();
  void AddCharacter(unsigned int codepoint);
  void HandleBackspace(bool byWord = false);
  void HandleDelete(bool byWord = false);
  void MoveCursorLeft(bool byWord = false, bool select = false);
  void MoveCursorRight(bool byWord = false, bool select = false);
  void MoveCursorHome(bool select = false);
  void MoveCursorEnd(bool select = false);
  void SelectAll();
  void ClearInput();
  void Complete();
  void ExecuteCommand();
  void HistoryUp();
  void HistoryDown();
  void AddToHistory(std::string command);
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
  void logNormal(const std::string& str);
  void logError(const std::string& str);
  void logWarning(const std::string& str);
  void logSuccess(const std::string& str);
  void logTrace(const std::string& str);
  void AppendStringLn(unsigned char r,
                      unsigned char g,
                      unsigned char b,
                      unsigned char a,
                      std::string str);
  void AppendString(unsigned char r,
                    unsigned char g,
                    unsigned char b,
                    unsigned char a,
                    std::string str);
  std::vector<std::string> ParseCommandArgs(const std::string& text,
                                            const std::string& delim) const;
  std::vector<std::string> SplitCommandChain(const std::string& text) const;
  void SetAlias(const std::string& name, const std::string& expansion);
  void RemoveAlias(const std::string& name);
  bool HasAlias(const std::string& name) const;
  std::string GetAlias(const std::string& name) const;
  const std::unordered_map<std::string, std::string>& GetAliases() const
  {
    return aliases;
  }
  std::string getGhostSuggestion() const;
  const std::string& getCurrentInput() const { return currentInput; }
  const std::string& getCompletionHint() const { return completionHint; }
  std::size_t getCursorPosition() const { return cursorPosition; }
  bool hasSelection() const { return cursorPosition != selectionAnchor; }
  const std::vector<historyBuffer>& getHistory() const { return history; }
  int getScrollOffset() const { return scrollOffset; }
  bool getFloatingMode() const { return isFloating; }
  void setFloatingMode(bool floating);
  void ToggleFloatingMode();
  float getFloatingWidth() const { return floatingW; }
  float getFloatingHeight() const { return floatingH; }
  void setFloatingSize(float w, float h);

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

  std::string currentInput;
  std::string tempInput;
  std::string completionHint;
  std::vector<historyBuffer> history;
  std::vector<std::string> commandHistory;
  std::unordered_map<std::string, std::string> aliases;
  std::size_t cursorPosition;
  std::size_t selectionAnchor;
  int historyIndex;
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

  IEnvVars* envVars;
  IRenderWindow* window;
  CommandRegistry* commandRegistry;
  Renderer* renderer;
  std::string applicationName;

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
  void clearCompletionHint();
  void eraseSelection();
  void resetCursorToEnd();
  void markCompositionDirty();
  void invalidateWrapCache();
  void rebuildWrapCache(float width) const;
  void ensureWrapCache(float width) const;
  std::size_t findPreviousWordBoundary() const;
  std::size_t findNextWordBoundary() const;
  std::vector<std::string> getCompletionCandidates(
    const std::string& leadingText) const;
  void ExecuteSingleCommand(const std::string& singleCmd,
                            int expansionDepth = 0);
  std::string getParameterHint(const std::string& inputLine) const;
  PanelLayout computePanelLayout(bool useSmoothedPanel) const;
  int countWrappedHistoryLines(float availableWidth) const;
  void computeHistoryScrollLimits(int* maxHistoryLines,
                                  int* maxScroll,
                                  float* historyWidth) const;
  void clampScrollOffset();

  // Scratch for one parse/complete/dispatch session (cleared at entry).
  // mutable so const helpers (completion / hints) can reuse the same pool.
  mutable ArenaAlloc parseArena;
  // Nested alias expansion temps: push expanded text, recurse, LIFO free.
  mutable ChainedStackAlloc aliasExpandStack;

  // Arena-backed token helpers. Pointers are valid until parseArena.Clear().
  // Falls back to heap std::string copies when the arena is exhausted.
  bool parseArgsInto(const std::string& text,
                     std::vector<std::string>& outArgs) const;
  bool splitChainInto(const std::string& text,
                      std::vector<std::string>& outCommands) const;
};
