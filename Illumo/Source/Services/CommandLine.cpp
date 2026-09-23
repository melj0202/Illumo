#if !defined(ILLUMO_SERIAL_GUEST)
#include <Illumo/Platform/Clipboard.h>
#endif
#include <Illumo/Rendering/Font.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

std::string
lowerCopy(const std::string& text)
{
  std::string lowered = text;
  for (std::size_t i = 0; i < lowered.size(); ++i) {
    lowered[i] =
      static_cast<char>(std::tolower(static_cast<unsigned char>(lowered[i])));
  }
  return lowered;
}

bool
parseLongStrict(const std::string& text, long* value)
{
  if (value == nullptr || text.empty()) {
    return false;
  }
  try {
    std::size_t consumed = 0;
    long parsed = std::stol(text, &consumed);
    if (consumed != text.size()) {
      return false;
    }
    *value = parsed;
    return true;
  } catch (...) {
    return false;
  }
}

constexpr float kConsoleFontSize = 14.0f;
constexpr float kChromeFontSize = 12.0f;
constexpr float kConsoleLineSpacing = 18.0f;
constexpr float kHeaderHeight = 22.0f;
constexpr float kInputHeight = 26.0f;
constexpr float kStatusHeight = 20.0f;
constexpr float kWatchHeight = 20.0f;
constexpr float kPadX = 10.0f;
constexpr float kScrollbarWidth = 4.0f;
constexpr float kGripSize = 14.0f;
constexpr float kCloseButtonWidth = 30.0f;
constexpr std::size_t kMaxCompletionRows = 8;
constexpr const char* kConsoleModeCommand = "console_mode";
constexpr const char* kConsoleSizeCommand = "console_size";
constexpr const char* kPrompt = "> ";

// Flat terminal palette. Kept local on purpose: the developer console should
// read as a plain tool, not as product UI styled by UiTheme or GuiKit.
struct ConsoleColors
{
  static constexpr ColorRgba background{ 14, 14, 14, 238 };
  static constexpr ColorRgba bar{ 30, 30, 30, 250 };
  static constexpr ColorRgba border{ 72, 72, 72, 255 };
  static constexpr ColorRgba rule{ 46, 46, 46, 255 };
  static constexpr ColorRgba text{ 204, 204, 204, 255 };
  static constexpr ColorRgba dim{ 128, 128, 128, 255 };
  static constexpr ColorRgba faint{ 88, 88, 88, 255 };
  static constexpr ColorRgba command{ 240, 240, 240, 255 };
  static constexpr ColorRgba success{ 128, 196, 120, 255 };
  static constexpr ColorRgba warning{ 222, 184, 96, 255 };
  static constexpr ColorRgba error{ 232, 104, 94, 255 };
  static constexpr ColorRgba trace{ 150, 134, 184, 255 };
  static constexpr ColorRgba selection{ 60, 78, 110, 255 };
  static constexpr ColorRgba caret{ 235, 235, 235, 255 };
  static constexpr ColorRgba scrollTrack{ 34, 34, 34, 255 };
  static constexpr ColorRgba scrollThumb{ 100, 100, 100, 255 };
  static constexpr ColorRgba popup{ 22, 22, 22, 250 };
  static constexpr ColorRgba closeButton{ 70, 30, 28, 255 };
  static constexpr ColorRgba match{ 78, 66, 28, 255 };
  static constexpr ColorRgba watchStrip{ 20, 20, 20, 250 };
  static constexpr ColorRgba button{ 42, 42, 42, 255 };
};

float
measureFontTextRange(const char* text, std::size_t length, float size)
{
  std::shared_ptr<Font> font = Font::getDefaultFont();
  if (font == nullptr || text == nullptr || length == 0) {
    return 0.0f;
  }
  return font->measureTextRange(text, length, size).width;
}

float
measureFontText(const std::string& text, float size = kConsoleFontSize)
{
  return measureFontTextRange(text.data(), text.size(), size);
}

std::string
truncateTextToWidth(const std::string& text,
                    float maxWidth,
                    float size = kConsoleFontSize)
{
  if (maxWidth <= 8.0f || text.empty()) {
    return "";
  }
  if (measureFontText(text, size) <= maxWidth) {
    return text;
  }
  std::shared_ptr<Font> font = Font::getDefaultFont();
  if (font == nullptr) {
    return "";
  }
  std::size_t end = 0;
  float currentWidth = 0.0f;
  while (end < text.size()) {
    float adv = font->getAdvance(
      static_cast<char32_t>(static_cast<unsigned char>(text[end])), size);
    if (currentWidth + adv > maxWidth) {
      break;
    }
    currentWidth += adv;
    ++end;
  }
  return text.substr(0, end);
}

void
wrapTextToWidth(const std::string& text,
                float maxWidth,
                std::vector<std::string>* lines)
{
  if (lines == nullptr) {
    return;
  }
  if (text.empty()) {
    lines->push_back("");
    return;
  }
  if (maxWidth <= 8.0f) {
    lines->push_back(text.substr(0, 1));
    return;
  }
  std::shared_ptr<Font> font = Font::getDefaultFont();
  std::size_t start = 0;
  while (start < text.size()) {
    if (measureFontTextRange(text.data() + start,
                             text.size() - start,
                             kConsoleFontSize) <= maxWidth) {
      lines->push_back(text.substr(start));
      return;
    }
    std::size_t end = start;
    float currentWidth = 0.0f;
    while (end < text.size()) {
      float adv =
        font ? font->getAdvance(
                 static_cast<char32_t>(static_cast<unsigned char>(text[end])),
                 kConsoleFontSize)
             : (kConsoleFontSize * 0.6f);
      if (currentWidth + adv > maxWidth && end > start) {
        break;
      }
      currentWidth += adv;
      ++end;
    }
    if (end == start) {
      end = start + 1;
    }
    lines->push_back(text.substr(start, end - start));
    start = end;
  }
}

// Horizontal window of the input line that keeps the caret on screen.
void
visibleInputRange(const std::string& input,
                  std::size_t cursor,
                  float availableWidth,
                  std::size_t* visibleStart,
                  std::size_t* visibleEnd)
{
  std::size_t start = 0;
  while (start < cursor) {
    if (measureFontTextRange(input.data() + start,
                             cursor - start,
                             kConsoleFontSize) <= availableWidth) {
      break;
    }
    ++start;
  }
  std::size_t end = cursor;
  while (end < input.size()) {
    if (measureFontTextRange(input.data() + start,
                             end + 1 - start,
                             kConsoleFontSize) > availableWidth) {
      break;
    }
    ++end;
  }
  *visibleStart = start;
  *visibleEnd = end;
}

ColorRgba
entryColor(const CommandLineCore::historyBuffer& entry)
{
  switch (entry.level) {
    case ConsoleLevel::Command:
      return ConsoleColors::command;
    case ConsoleLevel::Success:
      return ConsoleColors::success;
    case ConsoleLevel::Warning:
      return ConsoleColors::warning;
    case ConsoleLevel::Error:
      return ConsoleColors::error;
    case ConsoleLevel::Trace:
      return ConsoleColors::trace;
    case ConsoleLevel::Info:
      return ConsoleColors::text;
    case ConsoleLevel::Plain:
    default:
      break;
  }
  // Caller-chosen colors are honored, but near-white folds into the palette.
  if (entry.r >= 230 && entry.g >= 230 && entry.b >= 230) {
    return ConsoleColors::text;
  }
  return ColorRgba{ entry.r, entry.g, entry.b, entry.a };
}

// Emits chrome/text as GameVisual primitives (D-R15) while tracking an
// approximate vertex budget so a runaway frame stops adding geometry.
class ConsolePainter
{
public:
  ConsolePainter(GameVisual* target, unsigned int capacity)
    : visual(target)
    , cap(capacity)
    , used(0)
  {
  }

  void fill(float x, float y, float w, float h, ColorRgba color)
  {
    if (w <= 0.0f || h <= 0.0f || !reserve(4)) {
      return;
    }
    visual->addFilledRect(x, y, w, h, color);
  }

  void outline(float x, float y, float w, float h, ColorRgba color)
  {
    if (!reserve(16)) {
      return;
    }
    visual->addOutlineRect(x, y, w, h, color, 1.0f);
  }

  void line(float x0, float y0, float x1, float y1, ColorRgba color)
  {
    if (!reserve(4)) {
      return;
    }
    visual->addLine(x0, y0, x1, y1, color, 1.0f);
  }

  void text(const std::string& content,
            float x,
            float y,
            ColorRgba color,
            float size = kConsoleFontSize)
  {
    if (content.empty() ||
        !reserve(static_cast<unsigned int>(content.size() * 4u))) {
      return;
    }
    visual->addText(content, x, y, size, color);
  }

  unsigned int emitted() const { return used; }

private:
  bool reserve(unsigned int vertices)
  {
    if (visual == nullptr || used + vertices > cap) {
      return false;
    }
    used += vertices;
    return true;
  }

  GameVisual* visual;
  unsigned int cap;
  unsigned int used;
};

} // namespace

CommandLine::CommandLine(IEnvVars* vars,
                         CommandRegistry* commandRegistry,
                         IRenderWindow* win,
                         Renderer* rendererIn,
                         const std::string& applicationNameIn)
  : CommandLineCore(vars, commandRegistry, applicationNameIn)
  , detached(false)
  , detachAvailable(false)
  , detachedWidth(0)
  , detachedHeight(0)
  , pendingWindowRequest()
  , composedEmpty(false)
  , scrollOffset(0)
  , newLinesWhileScrolled(0)
  , unseenErrors(0)
  , unseenWarnings(0)
  , consoleInitialized(false)
  , isDraggingScrollbar(false)
  , dragStartY(0.0f)
  , dragStartScrollOffset(0)
  , isFloating(false)
  , floatingX(-1.0f)
  , floatingY(20.0f)
  , floatingW(-1.0f)
  , floatingH(-1.0f)
  , isDraggingWindow(false)
  , isResizingWindow(false)
  , dragWindowOffsetX(0.0f)
  , dragWindowOffsetY(0.0f)
  , resizeStartW(0.0f)
  , resizeStartH(0.0f)
  , resizeStartMouseX(0.0f)
  , resizeStartMouseY(0.0f)
  , lastHeaderClickTime(std::chrono::high_resolution_clock::time_point{})
  , currentPanelX(-1.0f)
  , currentPanelY(-1.0f)
  , currentPanelW(-1.0f)
  , currentPanelH(-1.0f)
  , window(win)
  , renderer(rendererIn)
  , visual(kUiQuadCap)
  , gpuReady(false)
  , animationProgress(0.0f)
  , lastAnimTime(std::chrono::high_resolution_clock::now())
  , wrappedHistoryWidth(-1.0f)
  , wrappedHistoryTotalLines(0)
  , wrappedVisibleEntries(0)
  , compositionDirty(true)
  , composedCaretPhase(-1)
  , composedScrollOffset(-1)
  , composedWindowW(-1)
  , composedWindowH(-1)
  , composedFps(-1)
  , composedAsBadge(false)
  , composedPanelX(-1.0f)
  , composedPanelY(-1.0f)
  , composedPanelW(-1.0f)
  , composedPanelH(-1.0f)
{
  isOpen = false;

  if (commandRegistry) {
    commandRegistry->RegisterCommand(
      kConsoleModeCommand,
      [this](const std::vector<std::string>& args) {
        if (args.empty()) {
          logNormal("Console mode: " + std::string(detached     ? "DETACHED"
                                                   : isFloating ? "FLOATING"
                                                                : "MOUNTED"));
          logNormal("Usage: console_mode "
                    "[floating|mounted|toggle|detached]");
          return;
        }
        std::string mode = lowerCopy(args[0]);
        if (mode == "detached" || mode == "detach" || mode == "window") {
          if (detached) {
            logNormal("Console is already in its own window");
          } else if (!detachAvailable) {
            logError("A separate console window is not available here");
          } else {
            requestDetach();
          }
          return;
        }
        // Any in-game mode brings a detached console back first.
        if (detached &&
            (mode == "floating" || mode == "float" || mode == "mounted" ||
             mode == "mount" || mode == "toggle")) {
          requestDock();
        }
        if (mode == "floating" || mode == "float") {
          setFloatingMode(true);
        } else if (mode == "mounted" || mode == "mount") {
          setFloatingMode(false);
        } else if (mode == "toggle") {
          ToggleFloatingMode();
        } else {
          logError("Unknown console mode: " + args[0]);
        }
      },
      "console_mode [floating|mounted|toggle|detached]",
      "Switch the console between floating, top-mounted, and its own window",
      { "detached", "floating", "mounted", "toggle" });

    commandRegistry->RegisterCommand(
      kConsoleSizeCommand,
      [this](const std::vector<std::string>& args) {
        if (args.empty()) {
          if (floatingW > 0.0f && floatingH > 0.0f) {
            logNormal(
              "Console size: " + std::to_string(static_cast<int>(floatingW)) +
              "x" + std::to_string(static_cast<int>(floatingH)));
          } else {
            logNormal("Console size: default/auto");
          }
          logNormal("Usage: console_size [<width> <height> | reset]");
          return;
        }
        std::string first = lowerCopy(args[0]);
        if (first == "reset" || first == "default") {
          floatingW = -1.0f;
          floatingH = -1.0f;
          markCompositionDirty();
          logSuccess("Console size reset to default");
          return;
        }
        if (args.size() >= 2) {
          long w = 0, h = 0;
          if (parseLongStrict(args[0], &w) && parseLongStrict(args[1], &h)) {
            setFloatingSize(static_cast<float>(w), static_cast<float>(h));
            return;
          }
        }
        logError("Usage: console_size [<width> <height> | reset]");
      },
      "console_size [<width> <height> | reset]",
      "Set or reset floating console window dimensions",
      { "800 500", "1000 600", "reset" });
  }

  enrollGpuResources();
}

CommandLine::~CommandLine()
{
  unregisterConsoleCommands();
}

void
CommandLine::unregisterConsoleCommands()
{
  if (commandRegistry == nullptr) {
    return;
  }
  commandRegistry->UnregisterCommand(kConsoleModeCommand);
  commandRegistry->UnregisterCommand(kConsoleSizeCommand);
}

void
CommandLine::ToggleFloatingMode()
{
  setFloatingMode(!isFloating);
}

CommandLine::WindowRequest
CommandLine::makeWindowRequest(WindowRequestKind kind) const
{
  WindowRequest request;
  request.kind = kind;
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  computeTargetPanel(&x, &y, &w, &h);
  const float scale = virtualScale();
  request.originX = static_cast<int>(x * scale);
  request.originY = static_cast<int>(y * scale);
  request.width = static_cast<int>(w * scale);
  request.height = static_cast<int>(h * scale);
  return request;
}

CommandLine::WindowRequest
CommandLine::takeWindowRequest()
{
  WindowRequest request = pendingWindowRequest;
  pendingWindowRequest = WindowRequest{};
  return request;
}

void
CommandLine::requestDetach()
{
  if (!detached && detachAvailable) {
    pendingWindowRequest = makeWindowRequest(WindowRequestKind::Detach);
  }
}

void
CommandLine::requestDock()
{
  if (detached) {
    pendingWindowRequest = WindowRequest{};
    pendingWindowRequest.kind = WindowRequestKind::Dock;
  }
}

void
CommandLine::setDetached(bool isDetachedNow, int width, int height)
{
  if (detached == isDetachedNow) {
    setDetachedSize(width, height);
    return;
  }
  detached = isDetachedNow;
  detachedWidth = std::max(1, width);
  detachedHeight = std::max(1, height);
  // Game input resumes while detached; the separate window has the console.
  isOpen = false;
  animationProgress = 0.0f;
  isDraggingWindow = false;
  isResizingWindow = false;
  isDraggingScrollbar = false;
  unseenErrors = 0;
  unseenWarnings = 0;
  pendingWindowRequest = WindowRequest{};
  drawnCommandLines.clear();
  composedAsBadge = false;
  invalidateWrapCache();
  markCompositionDirty();
}

void
CommandLine::setDetachedSize(int width, int height)
{
  const int w = std::max(1, width);
  const int h = std::max(1, height);
  if (w != detachedWidth || h != detachedHeight) {
    detachedWidth = w;
    detachedHeight = h;
    markCompositionDirty();
  }
}

std::array<int, 2>
CommandLine::layoutDimensions() const
{
  if (detached) {
    return { detachedWidth, detachedHeight };
  }
  return window ? window->getWindowDimensions()
                : std::array<int, 2>{ 1280, 720 };
}

bool
CommandLine::ComposeDetached()
{
  if (!detached) {
    return false;
  }
  return composePanel();
}

CommandLine::HeaderControls
CommandLine::computeHeaderControls(const PanelLayout& layout) const
{
  HeaderControls controls{};
  float right = layout.panelX1 - kPadX;
  controls.closeX0 = layout.panelX1;
  if (isFloating && !detached) {
    controls.closeX0 = layout.panelX1 - kCloseButtonWidth + 4.0f;
    right = controls.closeX0 - 8.0f;
  }
  controls.buttonX0 = right;
  controls.buttonX1 = right;
  if (detached || detachAvailable) {
    controls.buttonLabel = detached ? "dock" : "pop out";
    const float buttonWidth =
      measureFontText(controls.buttonLabel, kChromeFontSize) + 14.0f;
    controls.buttonX1 = right;
    controls.buttonX0 = right - buttonWidth;
    right = controls.buttonX0 - 10.0f;
  }
  controls.modeRight = right;
  return controls;
}

void
CommandLine::setFloatingMode(bool floating)
{
  isFloating = floating;
  markCompositionDirty();
  if (isFloating) {
    float width = 0.0f;
    float height = 0.0f;
    virtualWindowSize(&width, &height);
    if (floatingX < 0.0f) {
      floatingX = std::clamp(width * 0.08f, 10.0f, 100.0f);
      floatingY = 20.0f;
    }
  } else {
    isDraggingWindow = false;
    isResizingWindow = false;
  }
  logSuccess(isFloating ? "Console mode set to FLOATING"
                        : "Console mode set to MOUNTED");
}

void
CommandLine::setFloatingSize(float w, float h)
{
  floatingW = std::max(280.0f, w);
  floatingH = std::max(180.0f, h);
  markCompositionDirty();
  if (!isFloating) {
    setFloatingMode(true);
  } else {
    logSuccess("Console size set to " +
               std::to_string(static_cast<int>(floatingW)) + "x" +
               std::to_string(static_cast<int>(floatingH)));
  }
}

void
CommandLine::enrollGpuResources()
{
  gpuReady = false;
  consoleInitialized = false;
  if (!renderer) {
#if !defined(ILLUMO_SERIAL_GUEST)
    // Guest consoles are never drawn (their lines forward to the host), so a
    // missing renderer is only an error natively.
    Logger::LogError("CommandLine: no Renderer - cannot enroll GPU resources");
#endif
    return;
  }

  visual.setRenderer(renderer);
  visual.setWindow(window);
  visual.setSpace(PrimitiveSpace::Pixels);
  visual.setLayerHint(RenderLayerId::UI);
  visual.prepare(renderer);

  historyIndex = static_cast<int>(commandHistory.size());
  consoleInitialized = true;
  gpuReady = true;
  Logger::LogTrace("CommandLine enrolled (GameVisual primitives)");
}

void
CommandLine::Toggle()
{
  isOpen = !isOpen;
  markCompositionDirty();
  if (isOpen) {
    animationProgress = 0.0f;
    lastAnimTime = std::chrono::high_resolution_clock::now();
    scrollOffset = 0;
    newLinesWhileScrolled = 0;
    // Opening the console is how alerts are acknowledged.
    unseenErrors = 0;
    unseenWarnings = 0;
  } else {
    isDraggingWindow = false;
    isResizingWindow = false;
    isDraggingScrollbar = false;
  }
}

void
CommandLine::markCompositionDirty()
{
  compositionDirty = true;
}

void
CommandLine::invalidateWrapCache()
{
  wrappedHistory.clear();
  wrappedHistoryWidth = -1.0f;
  wrappedHistoryTotalLines = 0;
  wrappedVisibleEntries = 0;
}

void
CommandLine::wrapEntry(const historyBuffer& entry,
                       float width,
                       std::vector<std::string>* lines) const
{
  lines->clear();
  if (!isEntryVisible(entry)) {
    return;
  }
  // Multi-line entries (reports such as wasm_stats) become separate visual
  // lines; text primitives would otherwise break them outside the layout.
  const std::string text = CommandLineCore::DisplayText(entry);
  std::size_t start = 0;
  while (true) {
    const std::size_t end = text.find('\n', start);
    std::string segment = text.substr(
      start, end == std::string::npos ? std::string::npos : end - start);
    if (!segment.empty() && segment.back() == '\r') {
      segment.pop_back();
    }
    wrapTextToWidth(segment, width, lines);
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
}

void
CommandLine::rebuildWrapCache(float width) const
{
  wrappedHistory.clear();
  wrappedHistory.resize(history.size());
  wrappedHistoryTotalLines = 0;
  wrappedVisibleEntries = 0;
  wrappedHistoryWidth = width;
  for (std::size_t i = 0; i < history.size(); ++i) {
    wrapEntry(history[i], width, &wrappedHistory[i]);
    wrappedHistoryTotalLines += static_cast<int>(wrappedHistory[i].size());
    if (!wrappedHistory[i].empty()) {
      ++wrappedVisibleEntries;
    }
  }
}

void
CommandLine::ensureWrapCache(float width) const
{
  if (width <= 0.0f) {
    return;
  }
  if (wrappedHistoryWidth > 0.0f &&
      std::abs(wrappedHistoryWidth - width) <= 0.5f &&
      wrappedHistory.size() == history.size()) {
    return;
  }
  rebuildWrapCache(width);
}

void
CommandLine::onInputChanged()
{
  markCompositionDirty();
}

void
CommandLine::countAlert(const historyBuffer& entry)
{
  if (isOpen || detached) {
    return;
  }
  if (entry.level == ConsoleLevel::Error) {
    ++unseenErrors;
  } else if (entry.level == ConsoleLevel::Warning) {
    ++unseenWarnings;
  }
}

void
CommandLine::onHistoryAppended(const historyBuffer& item, bool erasedFront)
{
  countAlert(item);
  if (erasedFront && !wrappedHistory.empty()) {
    wrappedHistoryTotalLines -= static_cast<int>(wrappedHistory.front().size());
    if (!wrappedHistory.front().empty()) {
      --wrappedVisibleEntries;
    }
    wrappedHistory.erase(wrappedHistory.begin());
  }
  if (wrappedHistoryWidth > 0.0f &&
      wrappedHistory.size() + 1 == history.size()) {
    std::vector<std::string> lines;
    wrapEntry(history.back(), wrappedHistoryWidth, &lines);
    const int added = static_cast<int>(lines.size());
    wrappedHistoryTotalLines += added;
    if (added > 0) {
      ++wrappedVisibleEntries;
      // Keep a scrolled-back reader on the same lines while output arrives.
      if (scrollOffset > 0) {
        scrollOffset += added;
        newLinesWhileScrolled += added;
      }
    }
    wrappedHistory.push_back(std::move(lines));
  } else if (wrappedHistory.size() != history.size()) {
    wrappedHistoryWidth = -1.0f;
  }
  markCompositionDirty();
}

void
CommandLine::onHistoryBackUpdated()
{
  if (!history.empty()) {
    countAlert(history.back());
  }
  if (wrappedHistoryWidth > 0.0f && !history.empty() &&
      wrappedHistory.size() == history.size()) {
    std::vector<std::string>& back = wrappedHistory.back();
    const int before = static_cast<int>(back.size());
    wrapEntry(history.back(), wrappedHistoryWidth, &back);
    const int after = static_cast<int>(back.size());
    wrappedHistoryTotalLines += after - before;
    if (scrollOffset > 0 && after > before) {
      scrollOffset += after - before;
      newLinesWhileScrolled += after - before;
    }
  } else {
    wrappedHistoryWidth = -1.0f;
  }
  markCompositionDirty();
}

void
CommandLine::onHistoryCleared()
{
  invalidateWrapCache();
  scrollOffset = 0;
  newLinesWhileScrolled = 0;
  markCompositionDirty();
}

void
CommandLine::onViewChanged()
{
  invalidateWrapCache();
  scrollOffset = 0;
  newLinesWhileScrolled = 0;
  markCompositionDirty();
}

// Guests reach the clipboard only through their own service queue; their
// console is a log bridge and never edits text, so they report no clipboard.
bool
CommandLine::writeClipboard(const std::string& text)
{
#if defined(ILLUMO_SERIAL_GUEST)
  (void)text;
  return false;
#else
  return Clipboard::SetText(text);
#endif
}

std::string
CommandLine::readClipboard() const
{
#if defined(ILLUMO_SERIAL_GUEST)
  return "";
#else
  return Clipboard::GetText();
#endif
}

void
CommandLine::onCloseRequested()
{
  if (detached) {
    pendingWindowRequest = WindowRequest{};
    pendingWindowRequest.kind = WindowRequestKind::Close;
    return;
  }
  isOpen = false;
  markCompositionDirty();
}

void
CommandLine::onQuitRequested()
{
  if (window != nullptr) {
    window->requestClose();
  }
}

void
CommandLine::onToggleFullscreen()
{
  if (window != nullptr) {
    window->toggleFullscreen();
  }
}

void
CommandLine::queryWindowDimensions(int* width, int* height) const
{
  if (window != nullptr) {
    const std::array<int, 2> dims = window->getWindowDimensions();
    if (width != nullptr) {
      *width = dims[0];
    }
    if (height != nullptr) {
      *height = dims[1];
    }
  } else {
    if (width != nullptr) {
      *width = 0;
    }
    if (height != nullptr) {
      *height = 0;
    }
  }
}

float
CommandLine::virtualScale() const
{
  if (detached) {
    // The separate window draws 1:1 in its own pixels.
    return 1.0f;
  }
  float uiScale = 1.0f;
  if (envVars != nullptr) {
    const EnvVar& scaleVar = envVars->getVar("uiScale");
    if (!scaleVar.value.empty() && scaleVar.valueAsDouble > 0.0) {
      uiScale = static_cast<float>(scaleVar.valueAsDouble);
    }
  }
  return uiScale;
}

void
CommandLine::virtualWindowSize(float* width, float* height) const
{
  const std::array<int, 2> dims = layoutDimensions();
  const float scale = virtualScale();
  *width = std::max(1.0f, static_cast<float>(dims[0]) / scale);
  *height = std::max(1.0f, static_cast<float>(dims[1]) / scale);
}

void
CommandLine::computeTargetPanel(float* x, float* y, float* w, float* h) const
{
  float width = 0.0f;
  float height = 0.0f;
  virtualWindowSize(&width, &height);
  if (detached) {
    *x = 0.0f;
    *y = 0.0f;
    *w = width;
    *h = height;
    return;
  }
  const float mountedHeight =
    std::clamp(height * 0.52f, std::min(160.0f, height), height);
  if (!isFloating) {
    *x = 0.0f;
    *y = 0.0f;
    *w = width;
    *h = mountedHeight;
    return;
  }
  const float margin = std::clamp(width * 0.08f, 10.0f, 100.0f);
  const float defaultW =
    std::clamp(width - margin * 2.0f, std::min(280.0f, width), width);
  *w = floatingW > 0.0f ? std::clamp(floatingW, std::min(280.0f, width), width)
                        : defaultW;
  *h = floatingH > 0.0f
         ? std::clamp(floatingH, std::min(180.0f, height), height)
         : mountedHeight;
  const float requestedX = floatingX < 0.0f ? margin : floatingX;
  const float requestedY = floatingX < 0.0f ? 20.0f : floatingY;
  *x = std::clamp(requestedX, 0.0f, std::max(0.0f, width - *w));
  *y = std::clamp(requestedY, 0.0f, std::max(0.0f, height - *h));
}

CommandLine::PanelLayout
CommandLine::computePanelLayout(bool useSmoothedPanel) const
{
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  computeTargetPanel(&x, &y, &w, &h);
  if (useSmoothedPanel && currentPanelW > 0.0f) {
    x = currentPanelX;
    y = currentPanelY;
    w = currentPanelW;
    h = currentPanelH;
  }
  // The slide is a quick drop from above; hit tests and drawing share it.
  const float progress =
    detached
      ? 1.0f
      : (animationProgress > 0.0f ? animationProgress : (isOpen ? 1.0f : 0.0f));
  const float ease = 1.0f - std::pow(1.0f - progress, 3.0f);
  const float slide =
    isFloating ? (1.0f - ease) * (y + h + 40.0f) : (1.0f - ease) * h;

  PanelLayout layout{};
  layout.panelX0 = x;
  layout.panelY0 = y - slide;
  layout.panelX1 = x + w;
  layout.panelY1 = layout.panelY0 + h;
  layout.headerHeight = kHeaderHeight;
  layout.statusTop = layout.panelY1 - kStatusHeight;
  layout.inputBottom = layout.statusTop;
  layout.inputTop = layout.inputBottom - kInputHeight;
  layout.watchHeight = watches.empty() ? 0.0f : kWatchHeight;
  layout.historyTop =
    layout.panelY0 + kHeaderHeight + layout.watchHeight + 6.0f;
  layout.historyBottom = layout.inputTop - 4.0f;
  layout.gutterWidth =
    timestampsVisible ? measureFontText(FormatTimestamp(0.0) + " ") : 0.0f;
  layout.textX = layout.panelX0 + kPadX + layout.gutterWidth;
  layout.scrollbarX1 = layout.panelX1 - 6.0f;
  layout.scrollbarX0 = layout.scrollbarX1 - kScrollbarWidth;
  layout.lineSpacing = kConsoleLineSpacing;
  layout.maxHistoryLines =
    std::max(1,
             static_cast<int>((layout.historyBottom - layout.historyTop) /
                              layout.lineSpacing));
  layout.wrapWidth =
    std::max(20.0f, (layout.scrollbarX0 - 6.0f) - layout.textX);
  return layout;
}

int
CommandLine::countWrappedHistoryLines(float availableWidth) const
{
  ensureWrapCache(availableWidth);
  return wrappedHistoryTotalLines;
}

void
CommandLine::computeHistoryScrollLimits(int* maxHistoryLines,
                                        int* maxScroll,
                                        float* historyWidth) const
{
  const PanelLayout layout = computePanelLayout(false);
  const int lines = countWrappedHistoryLines(layout.wrapWidth);
  if (maxHistoryLines != nullptr) {
    *maxHistoryLines = layout.maxHistoryLines;
  }
  if (maxScroll != nullptr) {
    *maxScroll = std::max(0, lines - layout.maxHistoryLines);
  }
  if (historyWidth != nullptr) {
    *historyWidth = layout.wrapWidth;
  }
}

void
CommandLine::clampScrollOffset()
{
  int maxScroll = 0;
  computeHistoryScrollLimits(nullptr, &maxScroll, nullptr);
  scrollOffset = std::clamp(scrollOffset, 0, maxScroll);
}

void
CommandLine::scrollBy(int lines)
{
  const int previous = scrollOffset;
  scrollOffset += lines;
  clampScrollOffset();
  if (scrollOffset == 0) {
    newLinesWhileScrolled = 0;
  }
  if (scrollOffset != previous) {
    markCompositionDirty();
  }
}

void
CommandLine::ScrollUp()
{
  scrollBy(1);
}

void
CommandLine::ScrollDown()
{
  scrollBy(-1);
}

void
CommandLine::ScrollPageUp()
{
  int maxHistoryLines = 1;
  computeHistoryScrollLimits(&maxHistoryLines, nullptr, nullptr);
  scrollBy(std::max(1, maxHistoryLines - 1));
}

void
CommandLine::ScrollPageDown()
{
  int maxHistoryLines = 1;
  computeHistoryScrollLimits(&maxHistoryLines, nullptr, nullptr);
  scrollBy(-std::max(1, maxHistoryLines - 1));
}

void
CommandLine::ScrollToTop()
{
  int maxScroll = 0;
  computeHistoryScrollLimits(nullptr, &maxScroll, nullptr);
  scrollBy(maxScroll - scrollOffset);
}

void
CommandLine::ScrollToBottom()
{
  scrollBy(-scrollOffset);
}

void
CommandLine::HandleScroll(double yOffset)
{
  if (!isInteractive() || yOffset == 0.0) {
    return;
  }
  scrollBy(yOffset > 0.0 ? 3 : -3);
}

void
CommandLine::HandleMousePress(double mouseX, double mouseY, bool isDrag)
{
  if (!isInteractive()) {
    return;
  }
  const float scale = virtualScale();
  const float vx = static_cast<float>(mouseX) / scale;
  const float vy = static_cast<float>(mouseY) / scale;
  const PanelLayout layout = computePanelLayout(false);

  if (vx < layout.panelX0 || vx > layout.panelX1 || vy < layout.panelY0 ||
      vy > layout.panelY1) {
    return;
  }

  // Corner resize grip (floating only; a detached window resizes natively).
  if (!isDrag && isFloating && !detached && vx >= layout.panelX1 - kGripSize &&
      vy >= layout.panelY1 - kGripSize) {
    isResizingWindow = true;
    resizeStartW = layout.panelX1 - layout.panelX0;
    resizeStartH = layout.panelY1 - layout.panelY0;
    resizeStartMouseX = vx;
    resizeStartMouseY = vy;
    markCompositionDirty();
    return;
  }

  if (vy <= layout.panelY0 + layout.headerHeight) {
    if (isDrag) {
      return;
    }
    const HeaderControls controls = computeHeaderControls(layout);
    if (!controls.buttonLabel.empty() && vx >= controls.buttonX0 &&
        vx <= controls.buttonX1) {
      if (detached) {
        requestDock();
      } else {
        requestDetach();
      }
      return;
    }
    if (detached) {
      // The OS title bar moves a detached window; its header has no drag.
      return;
    }
    if (isFloating && vx >= controls.closeX0) {
      Toggle();
      return;
    }
    const std::chrono::high_resolution_clock::time_point now =
      std::chrono::high_resolution_clock::now();
    const long long elapsedMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                            lastHeaderClickTime)
        .count();
    if (elapsedMs > 0 && elapsedMs < 350) {
      ToggleFloatingMode();
      lastHeaderClickTime = std::chrono::high_resolution_clock::time_point{};
      return;
    }
    lastHeaderClickTime = now;
    if (isFloating) {
      isDraggingWindow = true;
      dragWindowOffsetX = vx - layout.panelX0;
      dragWindowOffsetY = vy - layout.panelY0;
      markCompositionDirty();
    }
    return;
  }

  int maxScroll = 0;
  computeHistoryScrollLimits(nullptr, &maxScroll, nullptr);
  if (maxScroll > 0 && vx >= layout.scrollbarX0 - 8.0f &&
      vy >= layout.historyTop && vy <= layout.historyBottom) {
    if (!isDrag) {
      isDraggingScrollbar = true;
      dragStartY = vy;
      dragStartScrollOffset = scrollOffset;
    }
    const float trackHeight = layout.historyBottom - layout.historyTop;
    const float clickRatio =
      std::clamp(1.0f - (vy - layout.historyTop) / trackHeight, 0.0f, 1.0f);
    scrollOffset =
      std::clamp(static_cast<int>(clickRatio * maxScroll), 0, maxScroll);
    markCompositionDirty();
    return;
  }

  // Clicking an echoed command recalls it into the input line.
  if (!isDrag && vy >= layout.historyTop && vy <= layout.historyBottom) {
    for (const DrawnCommandLine& line : drawnCommandLines) {
      if (vy >= line.y - 2.0f && vy < line.y - 2.0f + layout.lineSpacing) {
        CancelReverseSearch();
        currentInput = line.command;
        resetCursorToEnd();
        historyIndex = static_cast<int>(commandHistory.size());
        clearCompletionHint();
        completionHint = "Recalled: Enter runs it";
        onInputChanged();
        return;
      }
    }
    return;
  }

  if (vy >= layout.inputTop) {
    acceptSearchIfActive();
    const float inputTextX = layout.panelX0 + kPadX + measureFontText(kPrompt);
    const float inputAvailableWidth =
      std::max(48.0f, layout.panelX1 - inputTextX - kPadX);
    std::size_t visibleStart = 0;
    std::size_t visibleEnd = 0;
    visibleInputRange(currentInput,
                      cursorPosition,
                      inputAvailableWidth,
                      &visibleStart,
                      &visibleEnd);
    std::size_t bestIdx = 0;
    float minDiff = 1e9f;
    for (std::size_t i = 0; i <= visibleEnd - visibleStart; ++i) {
      const float charX =
        inputTextX + measureFontTextRange(
                       currentInput.data() + visibleStart, i, kConsoleFontSize);
      const float diff = std::abs(charX - vx);
      if (diff < minDiff) {
        minDiff = diff;
        bestIdx = i;
      }
    }
    cursorPosition = std::min(visibleStart + bestIdx, currentInput.size());
    if (!isDrag) {
      selectionAnchor = cursorPosition;
    }
    markCompositionDirty();
  }
}

void
CommandLine::HandleMouseDrag(double mouseX, double mouseY)
{
  if (!isInteractive()) {
    return;
  }
  const float scale = virtualScale();
  const float vx = static_cast<float>(mouseX) / scale;
  const float vy = static_cast<float>(mouseY) / scale;
  float width = 0.0f;
  float height = 0.0f;
  virtualWindowSize(&width, &height);

  if (isResizingWindow && isFloating) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    computeTargetPanel(&x, &y, &w, &h);
    floatingW = std::clamp(resizeStartW + (vx - resizeStartMouseX),
                           std::min(280.0f, width),
                           std::max(std::min(280.0f, width), width - x));
    floatingH = std::clamp(resizeStartH + (vy - resizeStartMouseY),
                           std::min(180.0f, height),
                           std::max(std::min(180.0f, height), height - y));
    markCompositionDirty();
    return;
  }
  if (isDraggingWindow && isFloating) {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    computeTargetPanel(&x, &y, &w, &h);
    // Dragging the title bar past the game window's edge pops the console
    // out, keeping the grabbed point under the cursor.
    constexpr float kTearOffMargin = 12.0f;
    if (detachAvailable &&
        (vx < -kTearOffMargin || vy < -kTearOffMargin ||
         vx > width + kTearOffMargin || vy > height + kTearOffMargin)) {
      pendingWindowRequest = makeWindowRequest(WindowRequestKind::Detach);
      pendingWindowRequest.originX =
        static_cast<int>((vx - dragWindowOffsetX) * scale);
      pendingWindowRequest.originY =
        static_cast<int>((vy - dragWindowOffsetY) * scale);
      isDraggingWindow = false;
      return;
    }
    floatingX =
      std::clamp(vx - dragWindowOffsetX, 0.0f, std::max(0.0f, width - w));
    floatingY =
      std::clamp(vy - dragWindowOffsetY, 0.0f, std::max(0.0f, height - h));
    markCompositionDirty();
    return;
  }
  if (isDraggingScrollbar) {
    const PanelLayout layout = computePanelLayout(false);
    int maxScroll = 0;
    computeHistoryScrollLimits(nullptr, &maxScroll, nullptr);
    const float trackHeight = layout.historyBottom - layout.historyTop;
    if (trackHeight > 0.0f && maxScroll > 0) {
      const float deltaScrollRatio = -(vy - dragStartY) / trackHeight;
      scrollOffset = std::clamp(
        dragStartScrollOffset + static_cast<int>(deltaScrollRatio * maxScroll),
        0,
        maxScroll);
      markCompositionDirty();
    }
    return;
  }

  HandleMousePress(mouseX, mouseY, true);
}

void
CommandLine::HandleMouseRelease()
{
  isDraggingScrollbar = false;
  isDraggingWindow = false;
  isResizingWindow = false;
}

void
CommandLine::DrawImpl()
{
  // Migrated to tokens.
}

std::string
CommandLine::buildWatchLine() const
{
  std::string line;
  for (const std::string& watched : watches) {
    line += watched + "=" + envVars->getVar(watched).value + "\n";
  }
  return line;
}

bool
CommandLine::appendAlertBadge(Renderer* r)
{
  if (!hasUnseenAlerts()) {
    return true;
  }
  // A small closed-console notice: counts only, opened with the console key.
  std::string text;
  if (unseenErrors > 0) {
    text +=
      std::to_string(unseenErrors) + (unseenErrors == 1 ? " error" : " errors");
  }
  if (unseenWarnings > 0) {
    text += (text.empty() ? "" : "  ") + std::to_string(unseenWarnings) +
            (unseenWarnings == 1 ? " warning" : " warnings");
  }
  float width = 0.0f;
  float height = 0.0f;
  virtualWindowSize(&width, &height);
  const std::string key = text + "@" + std::to_string(static_cast<int>(width)) +
                          "x" + std::to_string(static_cast<int>(height));
  visual.setRenderer(r);
  visual.setWindow(window);
  visual.setSpace(PrimitiveSpace::Pixels);
  if (!composedAsBadge || key != composedBadgeKey) {
    visual.clearPrimitives();
    ConsolePainter paint(&visual, kUiVertCap);
    const std::string hint = "  ` to view";
    const float textWidth = measureFontText(text + hint, kChromeFontSize);
    const float boxW = textWidth + 2.0f * kPadX + 4.0f;
    const float boxH = 22.0f;
    const float boxX = std::max(0.0f, width - boxW - 10.0f);
    const float boxY = 10.0f;
    const ColorRgba accent =
      unseenErrors > 0 ? ConsoleColors::error : ConsoleColors::warning;
    paint.fill(boxX, boxY, boxW, boxH, ConsoleColors::background);
    paint.outline(boxX, boxY, boxW, boxH, ConsoleColors::border);
    paint.fill(boxX + 1.0f, boxY + 1.0f, 3.0f, boxH - 2.0f, accent);
    paint.text(text, boxX + kPadX + 4.0f, boxY + 5.0f, accent, kChromeFontSize);
    paint.text(hint,
               boxX + kPadX + 4.0f + measureFontText(text, kChromeFontSize),
               boxY + 5.0f,
               ConsoleColors::faint,
               kChromeFontSize);
    composedBadgeKey = key;
    composedAsBadge = true;
    compositionDirty = true;
  }
  visual.setVisible(true);
  return visual.AppendCommands(r);
}

int
CommandLine::currentFps() const
{
  if (renderer == nullptr || renderer->getBackend() == nullptr) {
    return 0;
  }
  return renderer->getBackend()->getFPS();
}

bool
CommandLine::AppendCommands(Renderer* r)
{
  if (!isVisible()) {
    return true;
  }
  if (!gpuReady || !r) {
    return false;
  }
  if (detached) {
    // Drawn by the separate window's software canvas, never in the game.
    return true;
  }

  const std::chrono::high_resolution_clock::time_point now =
    std::chrono::high_resolution_clock::now();
  float deltaTime = std::chrono::duration<float>(now - lastAnimTime).count();
  lastAnimTime = now;
  deltaTime = std::min(deltaTime, 0.1f);
  const float animationSpeed = 16.0f;
  if (isOpen) {
    animationProgress =
      Math::lerp(animationProgress, 1.0f, animationSpeed * deltaTime);
    if (animationProgress > 0.999f) {
      animationProgress = 1.0f;
    }
  } else {
    animationProgress =
      Math::lerp(animationProgress, 0.0f, animationSpeed * deltaTime);
    if (animationProgress < 0.01f) {
      animationProgress = 0.0f;
    }
  }
  if (!isOpen && animationProgress <= 0.0f) {
    return appendAlertBadge(r);
  }

  composePanel();
  if (composedEmpty) {
    return true;
  }
  visual.setRenderer(r);
  visual.setWindow(window);
  visual.setSpace(PrimitiveSpace::Pixels);
  visual.setVisible(true);
  return visual.AppendCommands(r);
}

bool
CommandLine::composePanel()
{
  const std::chrono::high_resolution_clock::time_point now =
    std::chrono::high_resolution_clock::now();
  // The panel snaps to its target; only the open/close slide animates.
  computeTargetPanel(
    &currentPanelX, &currentPanelY, &currentPanelW, &currentPanelH);
  clampScrollOffset();

  const std::array<int, 2> windowDimensions = layoutDimensions();
  const long long caretMilliseconds =
    std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch())
      .count();
  const bool caretVisible = (caretMilliseconds % 1000) < 560;
  const int caretPhase = caretVisible ? 1 : 0;
  const int fps = currentFps();
  const bool animating = !detached && (isOpen ? (animationProgress < 1.0f)
                                              : (animationProgress > 0.0f));
  // Watched values change outside the console, so compare them each frame.
  const std::string watchLine = buildWatchLine();
  if (animating || isDraggingWindow || isResizingWindow || composedAsBadge ||
      watchLine != composedWatchLine ||
      windowDimensions[0] != composedWindowW ||
      windowDimensions[1] != composedWindowH ||
      std::abs(currentPanelX - composedPanelX) > 0.5f ||
      std::abs(currentPanelY - composedPanelY) > 0.5f ||
      std::abs(currentPanelW - composedPanelW) > 0.5f ||
      std::abs(currentPanelH - composedPanelH) > 0.5f ||
      caretPhase != composedCaretPhase ||
      scrollOffset != composedScrollOffset || fps != composedFps) {
    compositionDirty = true;
  }
  if (!compositionDirty) {
    return false;
  }

  visual.clearPrimitives();
  ConsolePainter paint(&visual, kUiVertCap);
  const PanelLayout layout = computePanelLayout(true);
  const float x0 = layout.panelX0;
  const float y0 = layout.panelY0;
  const float x1 = layout.panelX1;
  const float y1 = layout.panelY1;
  const float panelW = x1 - x0;

  // Body, border, and the three rule lines that split the panel into
  // header / output / input / status bands.
  paint.fill(x0, y0, panelW, y1 - y0, ConsoleColors::background);
  paint.fill(x0, y0, panelW, layout.headerHeight, ConsoleColors::bar);
  paint.fill(
    x0, layout.statusTop, panelW, y1 - layout.statusTop, ConsoleColors::bar);
  paint.line(x0,
             y0 + layout.headerHeight,
             x1,
             y0 + layout.headerHeight,
             ConsoleColors::rule);
  paint.line(x0, layout.inputTop, x1, layout.inputTop, ConsoleColors::rule);
  paint.line(x0, layout.statusTop, x1, layout.statusTop, ConsoleColors::rule);
  paint.outline(x0, y0, panelW, y1 - y0, ConsoleColors::border);

  // Header: name on the left; mode, the pop-out/dock button, and (floating)
  // the close box on the right.
  const float headerTextY = y0 + 5.0f;
  const std::string modeText = detached     ? "detached"
                               : isFloating ? "floating"
                                            : "mounted";
  const HeaderControls controls = computeHeaderControls(layout);
  if (isFloating && !detached) {
    paint.fill(controls.closeX0,
               y0 + 4.0f,
               kCloseButtonWidth - 10.0f,
               layout.headerHeight - 8.0f,
               ConsoleColors::closeButton);
    paint.text("x",
               controls.closeX0 + 6.0f,
               headerTextY - 1.0f,
               ConsoleColors::text,
               kChromeFontSize);
  }
  if (!controls.buttonLabel.empty()) {
    paint.fill(controls.buttonX0,
               y0 + 4.0f,
               controls.buttonX1 - controls.buttonX0,
               layout.headerHeight - 8.0f,
               ConsoleColors::button);
    paint.outline(controls.buttonX0,
                  y0 + 4.0f,
                  controls.buttonX1 - controls.buttonX0,
                  layout.headerHeight - 8.0f,
                  ConsoleColors::border);
    paint.text(controls.buttonLabel,
               controls.buttonX0 + 7.0f,
               headerTextY,
               ConsoleColors::text,
               kChromeFontSize);
  }
  const float headerRight = controls.modeRight;
  const float modeWidth = measureFontText(modeText, kChromeFontSize);
  paint.text(modeText,
             headerRight - modeWidth,
             headerTextY,
             ConsoleColors::faint,
             kChromeFontSize);
  const std::string title = truncateTextToWidth(
    applicationName + " console",
    std::max(0.0f, headerRight - modeWidth - 16.0f - (x0 + kPadX)),
    kChromeFontSize);
  paint.text(
    title, x0 + kPadX, headerTextY, ConsoleColors::dim, kChromeFontSize);

  // Watch strip: pinned variables as dim names and bright values.
  if (layout.watchHeight > 0.0f) {
    const float stripY = y0 + layout.headerHeight;
    paint.fill(
      x0, stripY, panelW, layout.watchHeight, ConsoleColors::watchStrip);
    paint.line(x0,
               stripY + layout.watchHeight,
               x1,
               stripY + layout.watchHeight,
               ConsoleColors::rule);
    float watchX = x0 + kPadX;
    const float watchTextY = stripY + 4.0f;
    const float watchLimit = x1 - kPadX;
    for (const std::string& watched : watches) {
      const std::string name = watched + " ";
      const std::string value = envVars->getVar(watched).value;
      const float nameWidth = measureFontText(name, kChromeFontSize);
      const float valueWidth = measureFontText(value, kChromeFontSize);
      if (watchX + nameWidth + valueWidth > watchLimit) {
        paint.text(
          "...", watchX, watchTextY, ConsoleColors::faint, kChromeFontSize);
        break;
      }
      paint.text(
        name, watchX, watchTextY, ConsoleColors::faint, kChromeFontSize);
      paint.text(value.empty() ? "-" : value,
                 watchX + nameWidth,
                 watchTextY,
                 ConsoleColors::text,
                 kChromeFontSize);
      watchX += nameWidth + std::max(valueWidth, 8.0f) + 18.0f;
    }
  }

  // Output. Off-screen visual lines stay in the wrap cache for scroll totals
  // and are not tessellated; entries hidden by filters wrap to zero lines.
  const int totalLines = countWrappedHistoryLines(layout.wrapWidth);
  const int maxHistoryLines = layout.maxHistoryLines;
  float currentY = layout.historyTop;
  drawnCommandLines.clear();
  const int endIdx = wrappedHistoryTotalLines - 1 - scrollOffset;
  if (endIdx >= 0 && wrappedHistory.size() == history.size()) {
    const int startIdx = std::max(0, endIdx - (maxHistoryLines - 1));
    // Short output sits at the bottom of the output band, like a terminal.
    const int shown = endIdx - startIdx + 1;
    currentY +=
      static_cast<float>(maxHistoryLines - shown) * layout.lineSpacing;
    int visualCursor = 0;
    std::size_t entryIndex = 0;
    while (entryIndex < history.size()) {
      const int entryLines =
        static_cast<int>(wrappedHistory[entryIndex].size());
      if (visualCursor + entryLines > startIdx) {
        break;
      }
      visualCursor += entryLines;
      ++entryIndex;
    }
    for (; entryIndex < history.size() && visualCursor <= endIdx;
         ++entryIndex) {
      const historyBuffer& item = history[entryIndex];
      const std::vector<std::string>& wrapped = wrappedHistory[entryIndex];
      if (wrapped.empty()) {
        continue;
      }
      const ColorRgba color = entryColor(item);
      const bool severe = item.level == ConsoleLevel::Warning ||
                          item.level == ConsoleLevel::Error;
      // Echoed commands ("> cmd", or ">> cmd" from scripts) can be recalled.
      std::string recall;
      if (item.level == ConsoleLevel::Command) {
        const std::size_t start = item.content.rfind(">> ", 0) == 0  ? 3
                                  : item.content.rfind("> ", 0) == 0 ? 2
                                                                     : 0;
        recall = start > 0 ? item.content.substr(start) : "";
      }
      std::size_t lineIndex = 0;
      if (visualCursor < startIdx) {
        lineIndex = static_cast<std::size_t>(startIdx - visualCursor);
        visualCursor = startIdx;
      }
      for (; lineIndex < wrapped.size() && visualCursor <= endIdx;
           ++lineIndex) {
        const std::string& lineText = wrapped[lineIndex];
        if (severe) {
          // A thin severity mark in the left margin, easy to scan for.
          paint.fill(
            x0 + 3.0f, currentY - 1.0f, 2.0f, layout.lineSpacing - 2.0f, color);
        }
        if (timestampsVisible && lineIndex == 0) {
          paint.text(FormatTimestamp(item.timeSeconds),
                     x0 + kPadX,
                     currentY,
                     ConsoleColors::faint);
        }
        if (!viewFilterLower.empty()) {
          const std::string lowered = lowerCopy(lineText);
          std::size_t found = lowered.find(viewFilterLower);
          while (found != std::string::npos) {
            const float matchX =
              layout.textX +
              measureFontTextRange(lineText.data(), found, kConsoleFontSize);
            paint.fill(matchX,
                       currentY - 1.0f,
                       measureFontTextRange(lineText.data() + found,
                                            viewFilterLower.size(),
                                            kConsoleFontSize),
                       layout.lineSpacing - 2.0f,
                       ConsoleColors::match);
            found =
              lowered.find(viewFilterLower, found + viewFilterLower.size());
          }
        }
        if (!recall.empty()) {
          drawnCommandLines.push_back({ currentY, recall });
        }
        paint.text(lineText, layout.textX, currentY, color);
        currentY += layout.lineSpacing;
        ++visualCursor;
      }
    }
  }

  if (totalLines > maxHistoryLines) {
    const float trackTop = layout.historyTop;
    const float trackHeight = layout.historyBottom - trackTop;
    paint.fill(layout.scrollbarX0,
               trackTop,
               kScrollbarWidth,
               trackHeight,
               ConsoleColors::scrollTrack);
    const float thumbHeight =
      std::max(16.0f,
               trackHeight * (static_cast<float>(maxHistoryLines) /
                              static_cast<float>(totalLines)));
    const int maxScroll = totalLines - maxHistoryLines;
    const float scrollPercent =
      maxScroll > 0
        ? static_cast<float>(scrollOffset) / static_cast<float>(maxScroll)
        : 0.0f;
    const float thumbTop = (trackTop + trackHeight - thumbHeight) -
                           scrollPercent * (trackHeight - thumbHeight);
    paint.fill(layout.scrollbarX0,
               thumbTop,
               kScrollbarWidth,
               thumbHeight,
               ConsoleColors::scrollThumb);
  }

  // Input line with a horizontally scrolling window around the caret.
  const float inputY = layout.inputTop + 5.0f;
  const float promptX = x0 + kPadX;
  // Reverse search swaps the prompt for the query, like a shell.
  const bool searching = isReverseSearchActive();
  const std::string promptText =
    searching
      ? std::string(isReverseSearchFailing() ? "failed search '" : "search '") +
          getReverseSearchQuery() + "': "
      : std::string(kPrompt);
  const float inputTextX = promptX + measureFontText(promptText);
  const float inputAvailableWidth = std::max(48.0f, x1 - inputTextX - kPadX);
  std::size_t visibleStart = 0;
  std::size_t visibleEnd = 0;
  visibleInputRange(currentInput,
                    cursorPosition,
                    inputAvailableWidth,
                    &visibleStart,
                    &visibleEnd);
  const std::string visibleInput =
    currentInput.substr(visibleStart, visibleEnd - visibleStart);
  paint.text(promptText,
             promptX,
             inputY,
             searching && isReverseSearchFailing() ? ConsoleColors::warning
                                                   : ConsoleColors::dim);
  if (searching && !getReverseSearchQuery().empty()) {
    const std::string loweredQuery = lowerCopy(getReverseSearchQuery());
    const std::size_t found = lowerCopy(visibleInput).find(loweredQuery);
    if (found != std::string::npos) {
      paint.fill(inputTextX + measureFontTextRange(
                                visibleInput.data(), found, kConsoleFontSize),
                 inputY - 2.0f,
                 measureFontTextRange(visibleInput.data() + found,
                                      loweredQuery.size(),
                                      kConsoleFontSize),
                 17.0f,
                 ConsoleColors::match);
    }
  }
  if (hasSelection()) {
    const std::size_t selectionStart =
      std::max(std::min(cursorPosition, selectionAnchor), visibleStart);
    const std::size_t selectionEnd =
      std::min(std::max(cursorPosition, selectionAnchor), visibleEnd);
    if (selectionStart < selectionEnd) {
      const float highlightX0 =
        inputTextX + measureFontTextRange(visibleInput.data(),
                                          selectionStart - visibleStart,
                                          kConsoleFontSize);
      const float highlightX1 =
        inputTextX + measureFontTextRange(visibleInput.data(),
                                          selectionEnd - visibleStart,
                                          kConsoleFontSize);
      paint.fill(highlightX0,
                 inputY - 2.0f,
                 highlightX1 - highlightX0,
                 17.0f,
                 ConsoleColors::selection);
    }
  }
  paint.text(visibleInput, inputTextX, inputY, ConsoleColors::command);
  if (!searching && cursorPosition == currentInput.size() &&
      visibleEnd == currentInput.size()) {
    const std::string ghostText = getGhostSuggestion();
    const float ghostX = inputTextX + measureFontText(visibleInput);
    if (!ghostText.empty() && ghostX < inputTextX + inputAvailableWidth) {
      paint.text(truncateTextToWidth(ghostText,
                                     inputTextX + inputAvailableWidth - ghostX),
                 ghostX,
                 inputY,
                 ConsoleColors::faint);
    }
  }
  if (caretVisible && cursorPosition >= visibleStart &&
      cursorPosition <= visibleEnd) {
    const float caretX =
      inputTextX + measureFontTextRange(visibleInput.data(),
                                        cursorPosition - visibleStart,
                                        kConsoleFontSize);
    paint.fill(caretX, inputY - 2.0f, 2.0f, 17.0f, ConsoleColors::caret);
  }

  // Status bar: live numbers on the left, contextual hint on the right.
  const float statusY = layout.statusTop + 4.0f;
  std::string statusLeft;
  if (fps > 0) {
    char frame[48];
    std::snprintf(frame,
                  sizeof(frame),
                  "fps %d  %.1f ms",
                  fps,
                  1000.0 / static_cast<double>(fps));
    statusLeft = frame;
  } else {
    statusLeft = "fps --";
  }
  statusLeft += "  |  " + std::to_string(history.size()) + " lines";
  if (isViewFiltered()) {
    statusLeft += " (" + std::to_string(wrappedVisibleEntries) + " shown)";
  }
  if (!getViewFilter().empty()) {
    statusLeft += "  |  filter \"" + getViewFilter() + "\"";
  }
  if (getMinimumSeverity() > kSeverityTrace) {
    static const char* const kLevelNames[] = {
      "trace", "info", "warning", "error"
    };
    statusLeft +=
      std::string("  |  level ") + kLevelNames[getMinimumSeverity()] + "+";
  }
  if (scrollOffset > 0) {
    statusLeft += "  |  scrolled " + std::to_string(scrollOffset);
    if (newLinesWhileScrolled > 0) {
      statusLeft += " (+" + std::to_string(newLinesWhileScrolled) + " new)";
    }
  }
  std::string statusRight;
  ColorRgba statusRightColor = ConsoleColors::faint;
  const std::string paramHint = getParameterHint(currentInput);
  if (searching) {
    statusRight = "Ctrl+R older  Enter run  Right edit  Esc cancel";
    statusRightColor = ConsoleColors::dim;
  } else if (!completionHint.empty()) {
    statusRight = completionHint;
    statusRightColor = ConsoleColors::dim;
  } else if (!paramHint.empty()) {
    statusRight = paramHint;
    statusRightColor = ConsoleColors::dim;
  } else {
    statusRight = "Tab complete  Ctrl+R search  PgUp/PgDn scroll";
  }
  const std::string visibleLeft =
    truncateTextToWidth(statusLeft, panelW - 2.0f * kPadX, kChromeFontSize);
  paint.text(
    visibleLeft, x0 + kPadX, statusY, ConsoleColors::dim, kChromeFontSize);
  const bool showGrip = isFloating && !detached;
  const float gripRoom = showGrip ? kGripSize : 0.0f;
  const float rightAvailable = panelW - 3.0f * kPadX -
                               measureFontText(visibleLeft, kChromeFontSize) -
                               gripRoom;
  if (rightAvailable > 60.0f) {
    const std::string visibleRight =
      truncateTextToWidth(statusRight, rightAvailable, kChromeFontSize);
    paint.text(visibleRight,
               x1 - kPadX - gripRoom -
                 measureFontText(visibleRight, kChromeFontSize),
               statusY,
               statusRightColor,
               kChromeFontSize);
  }
  if (showGrip) {
    for (int grip = 0; grip < 2; ++grip) {
      const float offset = static_cast<float>(grip) * 4.0f;
      paint.line(x1 - 10.0f + offset,
                 y1 - 3.0f,
                 x1 - 3.0f,
                 y1 - 10.0f + offset,
                 ConsoleColors::dim);
    }
  }

  // Completion list for an ambiguous Tab, drawn last so it overlays output.
  const std::vector<std::string>& matches = getCompletionMatches();
  if (matches.size() > 1) {
    const float rowHeight = 17.0f;
    const float availableHeight =
      layout.inputTop - 2.0f - (layout.historyTop - 2.0f) - 6.0f;
    std::size_t rows = std::min(matches.size(), kMaxCompletionRows);
    const bool overflow = matches.size() > rows;
    while (rows > 1 &&
           static_cast<float>(rows + (overflow ? 1 : 0)) * rowHeight >
             availableHeight) {
      --rows;
    }
    const std::size_t hidden = matches.size() - rows;
    const std::string moreText =
      hidden > 0 ? "... " + std::to_string(hidden) + " more" : "";
    float listWidth = measureFontText(moreText);
    for (std::size_t i = 0; i < rows; ++i) {
      listWidth = std::max(listWidth, measureFontText(matches[i]));
    }
    const std::size_t lineCount = rows + (hidden > 0 ? 1 : 0);
    const float boxW = std::min(listWidth + 16.0f, x1 - inputTextX - kPadX);
    const float boxH = static_cast<float>(lineCount) * rowHeight + 6.0f;
    const float boxX = inputTextX - 8.0f;
    const float boxY = layout.inputTop - 2.0f - boxH;
    paint.fill(boxX, boxY, boxW, boxH, ConsoleColors::popup);
    paint.outline(boxX, boxY, boxW, boxH, ConsoleColors::border);
    float rowY = boxY + 3.0f;
    for (std::size_t i = 0; i < rows; ++i) {
      paint.text(truncateTextToWidth(matches[i], boxW - 16.0f),
                 boxX + 8.0f,
                 rowY,
                 ConsoleColors::text);
      rowY += rowHeight;
    }
    if (hidden > 0) {
      paint.text(moreText, boxX + 8.0f, rowY, ConsoleColors::faint);
    }
  }

  composedEmpty = paint.emitted() < 4;
  if (composedEmpty) {
    return true;
  }

  composedCaretPhase = caretPhase;
  composedScrollOffset = scrollOffset;
  composedWindowW = windowDimensions[0];
  composedWindowH = windowDimensions[1];
  composedFps = fps;
  composedWatchLine = watchLine;
  composedAsBadge = false;
  composedPanelX = currentPanelX;
  composedPanelY = currentPanelY;
  composedPanelW = currentPanelW;
  composedPanelH = currentPanelH;
  compositionDirty = false;
  return true;
}
