#include "thirdparty/stb/stb_easy_font.h"
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/IMesh.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

namespace {
struct BuiltInCommandHelp
{
  const char* name;
  const char* usage;
  const char* description;
};

const BuiltInCommandHelp kBuiltInCommands[] = {
  { "alias", "alias [<name> <command>]", "Create or list command aliases" },
  { "clear", "clear", "Clear console output" },
  { "close", "close", "Close the console" },
  { "echo", "echo <text>", "Print text to the console" },
  { "fps", "fps [on|off|toggle]", "Show or change the FPS overlay" },
  { "fullscreen",
    "fullscreen [on|off|toggle]",
    "Show or change fullscreen mode" },
  { "get", "get <variable>", "Read an environment variable" },
  { "help", "help [command]", "Show commands or detailed help" },
  { "history", "history [filter|clear]", "Search or clear command history" },
  { "quit", "quit", "Exit the application" },
  { "repeat", "repeat <count> <command>", "Execute command multiple times" },
  { "set",
    "set <variable> <value>",
    "Create or update an environment variable" },
  { "sysinfo", "sysinfo", "Display system telemetry and statistics" },
  { "toggle", "toggle <variable>", "Toggle a boolean environment variable" },
  { "unalias", "unalias <name>", "Remove a command alias" },
  { "vars", "vars [filter]", "List environment variables, optionally filtered" }
};

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

std::string
upperCopy(const std::string& text)
{
  std::string upper = text;
  for (std::size_t i = 0; i < upper.size(); ++i) {
    upper[i] =
      static_cast<char>(std::toupper(static_cast<unsigned char>(upper[i])));
  }
  return upper;
}

static bool
isRestartRequiredVariable(const std::string& key)
{
  const std::string lower = lowerCopy(key);
  return lower == "msaa" || lower == "winx" || lower == "winy" ||
         lower == "graphicsapi";
}

std::string
joinArguments(const std::vector<std::string>& args, std::size_t first)
{
  std::string result;
  for (std::size_t i = first; i < args.size(); ++i) {
    if (!result.empty()) {
      result += " ";
    }
    result += args[i];
  }
  return result;
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

bool
parseBoolValue(const std::string& text, bool* value)
{
  if (value == nullptr) {
    return false;
  }
  const std::string lowered = lowerCopy(text);
  if (lowered == "on" || lowered == "true" || lowered == "yes" ||
      lowered == "1") {
    *value = true;
    return true;
  }
  if (lowered == "off" || lowered == "false" || lowered == "no" ||
      lowered == "0") {
    *value = false;
    return true;
  }
  return false;
}

const BuiltInCommandHelp*
findBuiltInCommand(const std::string& name)
{
  for (const BuiltInCommandHelp& command : kBuiltInCommands) {
    if (name == command.name) {
      return &command;
    }
  }
  return nullptr;
}

std::string
findEnvironmentKey(IEnvVars* envVars, const std::string& requested)
{
  if (envVars == nullptr) {
    return "";
  }
  const std::string loweredRequested = lowerCopy(requested);
  const std::unordered_map<std::string, EnvVar>& variables = envVars->getVars();
  for (const std::pair<const std::string, EnvVar>& variable : variables) {
    if (lowerCopy(variable.first) == loweredRequested) {
      return variable.first;
    }
  }
  return "";
}

static float
easyFontAdvance(unsigned char ch)
{
  if (ch < 32u || ch >= 128u) {
    return 0.0f;
  }
  return static_cast<float>(stb_easy_font_charinfo[ch - 32].advance & 15) +
         stb_easy_font_spacing_val;
}

static float
measureFontTextRange(const char* text, std::size_t length)
{
  float len = 0.0f;
  float maxLen = 0.0f;
  for (std::size_t i = 0; i < length; ++i) {
    if (text[i] == '\n') {
      if (len > maxLen) {
        maxLen = len;
      }
      len = 0.0f;
      continue;
    }
    len += easyFontAdvance(static_cast<unsigned char>(text[i]));
  }
  if (len > maxLen) {
    maxLen = len;
  }
  return static_cast<float>(static_cast<int>(std::ceil(maxLen)) * 2);
}

static float
measureFontText(const std::string& text)
{
  return measureFontTextRange(text.data(), text.size());
}

static std::string
truncateTextToWidth(const std::string& text, float maxWidth)
{
  if (maxWidth <= 8.0f || text.empty()) {
    return "";
  }
  if (measureFontTextRange(text.data(), text.size()) <= maxWidth) {
    return text;
  }
  std::size_t end = 0;
  float raw = 0.0f;
  while (end < text.size()) {
    float next = raw + easyFontAdvance(static_cast<unsigned char>(text[end]));
    float measured = static_cast<float>(static_cast<int>(std::ceil(next)) * 2);
    if (measured > maxWidth) {
      break;
    }
    raw = next;
    ++end;
  }
  return text.substr(0, end);
}

static void
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
  std::size_t start = 0;
  while (start < text.size()) {
    if (measureFontTextRange(text.data() + start, text.size() - start) <=
        maxWidth) {
      lines->push_back(text.substr(start));
      return;
    }
    std::size_t end = start;
    float raw = 0.0f;
    while (end < text.size()) {
      float next = raw + easyFontAdvance(static_cast<unsigned char>(text[end]));
      float measured =
        static_cast<float>(static_cast<int>(std::ceil(next)) * 2);
      if (measured > maxWidth && end > start) {
        break;
      }
      raw = next;
      ++end;
    }
    if (end == start) {
      end = start + 1;
    }
    lines->push_back(text.substr(start, end - start));
    start = end;
  }
}
}

CommandLine::CommandLine(IEnvVars* vars,
                         CommandRegistry* commandRegistry,
                         IRenderWindow* win,
                         Renderer* rendererIn,
                         const std::string& applicationNameIn)
  : envVars(vars)
  , window(win)
  , commandRegistry(commandRegistry)
  , renderer(rendererIn)
  , applicationName(applicationNameIn.empty() ? "Illumo" : applicationNameIn)
  , visual(kUiQuadCap)
  , gpuReady(false)
  , animationProgress(0.0f)
  , lastAnimTime(std::chrono::high_resolution_clock::now())
  , wrappedHistoryWidth(-1.0f)
  , wrappedHistoryTotalLines(0)
  , compositionDirty(true)
  , composedCaretPhase(-1)
  , composedPulseStep(-1)
  , composedScrollOffset(-1)
  , composedWindowW(-1)
  , composedWindowH(-1)
  , composedPanelX(-1.0f)
  , composedPanelY(-1.0f)
  , composedPanelW(-1.0f)
  , composedPanelH(-1.0f)
  , cursorPosition(0)
  , selectionAnchor(0)
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
  , parseArena(16 * 1024)
  , aliasExpandStack(4 * 1024)
{
  isOpen = false;
  currentInput = "";
  tempInput = "";
  completionHint = "";
  history = {
    { 240, 240, 240, 255, applicationName + " Developer Console" },
    { 240, 240, 240, 255, "Press ` to toggle, type 'help' for commands" }
  };

  historyIndex = 0;
  scrollOffset = 0;
  consoleInitialized = false;

  if (commandRegistry) {
    commandRegistry->RegisterCommand(
      "console_mode",
      [this](const std::vector<std::string>& args) {
        if (args.empty()) {
          logNormal("Console mode: " +
                    std::string(isFloating ? "FLOATING" : "MOUNTED"));
          logNormal("Usage: console_mode [floating|mounted|toggle]");
          return;
        }
        std::string mode = lowerCopy(args[0]);
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
      "console_mode [floating|mounted|toggle]",
      "Switch console between floating and top-mounted modes",
      { "floating", "mounted", "toggle" });

    commandRegistry->RegisterCommand(
      "console_size",
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

void
CommandLine::ToggleFloatingMode()
{
  setFloatingMode(!isFloating);
}

void
CommandLine::setFloatingMode(bool floating)
{
  isFloating = floating;
  markCompositionDirty();
  if (isFloating) {
    std::array<int, 2> windowDimensions =
      window ? window->getWindowDimensions() : std::array<int, 2>{ 1280, 720 };
    float width = static_cast<float>(windowDimensions[0]);
    float defaultMarginX = std::clamp(width * 0.08f, 20.0f, 100.0f);
    if (floatingX < 0.0f) {
      floatingX = defaultMarginX;
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
    Logger::LogError("CommandLine: no Renderer — cannot enroll GPU resources");
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
    completionHint =
      "Tab: complete  |  Ctrl+Arrows: words  |  Ctrl+A: select all";
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
}

void
CommandLine::rebuildWrapCache(float width) const
{
  wrappedHistory.clear();
  wrappedHistory.resize(history.size());
  wrappedHistoryTotalLines = 0;
  wrappedHistoryWidth = width;
  for (std::size_t i = 0; i < history.size(); ++i) {
    wrapTextToWidth(history[i].content, width, &wrappedHistory[i]);
    wrappedHistoryTotalLines += static_cast<int>(wrappedHistory[i].size());
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
CommandLine::clearCompletionHint()
{
  completionHint.clear();
}

void
CommandLine::resetCursorToEnd()
{
  cursorPosition = currentInput.size();
  selectionAnchor = cursorPosition;
}

void
CommandLine::eraseSelection()
{
  if (!hasSelection()) {
    return;
  }

  std::size_t start = std::min(cursorPosition, selectionAnchor);
  std::size_t end = std::max(cursorPosition, selectionAnchor);
  currentInput.erase(start, end - start);
  cursorPosition = start;
  selectionAnchor = start;
}

std::size_t
CommandLine::findPreviousWordBoundary() const
{
  std::size_t position = cursorPosition;
  while (position > 0 &&
         std::isspace(static_cast<unsigned char>(currentInput[position - 1]))) {
    --position;
  }
  while (position > 0 && !std::isspace(static_cast<unsigned char>(
                           currentInput[position - 1]))) {
    --position;
  }
  return position;
}

std::size_t
CommandLine::findNextWordBoundary() const
{
  std::size_t position = cursorPosition;
  while (position < currentInput.size() &&
         !std::isspace(static_cast<unsigned char>(currentInput[position]))) {
    ++position;
  }
  while (position < currentInput.size() &&
         std::isspace(static_cast<unsigned char>(currentInput[position]))) {
    ++position;
  }
  return position;
}

void
CommandLine::AddCharacter(unsigned int codepoint)
{
  std::size_t selectedCharacters =
    hasSelection() ? std::max(cursorPosition, selectionAnchor) -
                       std::min(cursorPosition, selectionAnchor)
                   : 0;
  if (currentInput.size() - selectedCharacters < MAX_CHARS_PER_LINE - 1) {
    if (codepoint >= 32 && codepoint <= 126) {
      eraseSelection();
      currentInput.insert(cursorPosition, 1, static_cast<char>(codepoint));
      ++cursorPosition;
      selectionAnchor = cursorPosition;
      clearCompletionHint();
      markCompositionDirty();
    }
  }
}

void
CommandLine::HandleBackspace(bool byWord)
{
  if (hasSelection()) {
    eraseSelection();
    clearCompletionHint();
    markCompositionDirty();
    return;
  }
  if (cursorPosition == 0) {
    return;
  }

  std::size_t eraseFrom =
    byWord ? findPreviousWordBoundary() : cursorPosition - 1;
  currentInput.erase(eraseFrom, cursorPosition - eraseFrom);
  cursorPosition = eraseFrom;
  selectionAnchor = cursorPosition;
  clearCompletionHint();
  markCompositionDirty();
}

void
CommandLine::HandleDelete(bool byWord)
{
  if (hasSelection()) {
    eraseSelection();
    clearCompletionHint();
    markCompositionDirty();
    return;
  }
  if (cursorPosition >= currentInput.size()) {
    return;
  }

  std::size_t eraseTo = byWord ? findNextWordBoundary() : cursorPosition + 1;
  currentInput.erase(cursorPosition, eraseTo - cursorPosition);
  selectionAnchor = cursorPosition;
  clearCompletionHint();
  markCompositionDirty();
}

void
CommandLine::MoveCursorLeft(bool byWord, bool select)
{
  if (!select && hasSelection()) {
    cursorPosition = std::min(cursorPosition, selectionAnchor);
    selectionAnchor = cursorPosition;
    markCompositionDirty();
    return;
  }
  std::size_t newPosition = byWord
                              ? findPreviousWordBoundary()
                              : (cursorPosition > 0 ? cursorPosition - 1 : 0);
  if (!select) {
    selectionAnchor = newPosition;
  }
  cursorPosition = newPosition;
  markCompositionDirty();
}

void
CommandLine::MoveCursorRight(bool byWord, bool select)
{
  if (!select && cursorPosition == currentInput.size()) {
    std::string ghost = getGhostSuggestion();
    if (!ghost.empty()) {
      currentInput += ghost;
      cursorPosition = currentInput.size();
      selectionAnchor = cursorPosition;
      markCompositionDirty();
      return;
    }
  }
  if (!select && hasSelection()) {
    cursorPosition = std::max(cursorPosition, selectionAnchor);
    selectionAnchor = cursorPosition;
    markCompositionDirty();
    return;
  }
  std::size_t newPosition =
    byWord ? findNextWordBoundary()
           : std::min(cursorPosition + 1, currentInput.size());
  if (!select) {
    selectionAnchor = newPosition;
  }
  cursorPosition = newPosition;
  markCompositionDirty();
}

void
CommandLine::MoveCursorHome(bool select)
{
  if (!select) {
    selectionAnchor = 0;
  }
  cursorPosition = 0;
  markCompositionDirty();
}

void
CommandLine::MoveCursorEnd(bool select)
{
  if (!select) {
    selectionAnchor = currentInput.size();
  }
  cursorPosition = currentInput.size();
  markCompositionDirty();
}

void
CommandLine::SelectAll()
{
  selectionAnchor = 0;
  cursorPosition = currentInput.size();
  markCompositionDirty();
}

void
CommandLine::ClearInput()
{
  currentInput.clear();
  resetCursorToEnd();
  clearCompletionHint();
  markCompositionDirty();
}

void
CommandLine::logNormal(const std::string& str)
{
  AppendString(255, 255, 255, 255, str);
}
void
CommandLine::logError(const std::string& str)
{
  AppendString(255, 100, 100, 255, "ERROR: " + str);
  // AppendStringLn(255, 255, 255, 255, str);
}
void
CommandLine::logWarning(const std::string& str)
{
  AppendString(255, 220, 100, 255, "WARNING: " + str);
  // AppendStringLn(255, 255, 255, 255, str);
}
void
CommandLine::logSuccess(const std::string& str)
{
  AppendString(100, 255, 100, 255, "SUCCESS: " + str);
  // AppendStringLn(255, 255, 255, 255, str);
}

void
CommandLine::logTrace(const std::string& str)
{
  AppendString(206, 0, 252, 255, "TRACE: " + str);
  // AppendStringLn(255, 255, 255, 255, str);
}

bool
CommandLine::parseArgsInto(const std::string& text,
                           std::vector<std::string>& outArgs) const
{
  outArgs.clear();
  std::string currentArg;
  char quote = '\0';
  bool escaping = false;
  bool tokenStarted = false;
  bool arenaOk = true;

  auto flushToken = [&]() {
    if (!tokenStarted && currentArg.empty()) {
      return;
    }
    char* staged = parseArena.AllocateCString(currentArg);
    if (staged != nullptr) {
      outArgs.push_back(std::string(staged));
    } else {
      arenaOk = false;
      outArgs.push_back(currentArg);
    }
    currentArg.clear();
    tokenStarted = false;
  };

  for (char character : text) {
    if (escaping) {
      currentArg += character;
      escaping = false;
      tokenStarted = true;
      continue;
    }
    if (character == '\\') {
      escaping = true;
      tokenStarted = true;
      continue;
    }
    if (quote != '\0') {
      if (character == quote) {
        quote = '\0';
      } else {
        currentArg += character;
      }
      tokenStarted = true;
      continue;
    }
    if (character == '\'' || character == '"') {
      quote = character;
      tokenStarted = true;
      continue;
    }
    if (character == ' ' || character == '\t') {
      flushToken();
      continue;
    }
    currentArg += character;
    tokenStarted = true;
  }
  flushToken();
  return arenaOk;
}

bool
CommandLine::splitChainInto(const std::string& text,
                            std::vector<std::string>& outCommands) const
{
  outCommands.clear();
  std::string currentCmd;
  char quote = '\0';
  bool escaping = false;
  bool arenaOk = true;

  auto flushCommand = [&]() {
    std::size_t firstNonSpace = currentCmd.find_first_not_of(" \t\r\n");
    if (firstNonSpace == std::string::npos) {
      currentCmd.clear();
      return;
    }
    char* staged = parseArena.AllocateCString(currentCmd);
    if (staged != nullptr) {
      outCommands.push_back(std::string(staged));
    } else {
      arenaOk = false;
      outCommands.push_back(currentCmd);
    }
    currentCmd.clear();
  };

  for (std::size_t i = 0; i < text.size(); ++i) {
    char character = text[i];
    if (escaping) {
      currentCmd += character;
      escaping = false;
      continue;
    }
    if (character == '\\') {
      escaping = true;
      currentCmd += character;
      continue;
    }
    if (quote != '\0') {
      if (character == quote) {
        quote = '\0';
      }
      currentCmd += character;
      continue;
    }
    if (character == '\'' || character == '"') {
      quote = character;
      currentCmd += character;
      continue;
    }
    if (character == ';') {
      flushCommand();
      continue;
    }
    currentCmd += character;
  }
  flushCommand();
  return arenaOk;
}

std::vector<std::string>
CommandLine::ParseCommandArgs(const std::string& text,
                              const std::string& delim) const
{
  (void)delim;
  std::vector<std::string> args;
  std::string currentArg;
  char quote = '\0';
  bool escaping = false;
  bool tokenStarted = false;

  for (char character : text) {
    if (escaping) {
      currentArg += character;
      escaping = false;
      tokenStarted = true;
      continue;
    }
    if (character == '\\') {
      escaping = true;
      tokenStarted = true;
      continue;
    }
    if (quote != '\0') {
      if (character == quote) {
        quote = '\0';
      } else {
        currentArg += character;
      }
      tokenStarted = true;
      continue;
    }
    if (character == '\'' || character == '"') {
      quote = character;
      tokenStarted = true;
      continue;
    }
    if (delim.find(character) != std::string::npos) {
      if (tokenStarted) {
        args.push_back(currentArg);
        currentArg.clear();
        tokenStarted = false;
      }
      continue;
    }
    currentArg += character;
    tokenStarted = true;
  }

  if (escaping) {
    currentArg += '\\';
  }
  if (tokenStarted) {
    args.push_back(currentArg);
  }
  return args;
}

std::vector<std::string>
CommandLine::SplitCommandChain(const std::string& text) const
{
  std::vector<std::string> commands;
  std::string currentCmd;
  char quote = '\0';
  bool escaping = false;

  for (std::size_t i = 0; i < text.size(); ++i) {
    char character = text[i];
    if (escaping) {
      currentCmd += character;
      escaping = false;
      continue;
    }
    if (character == '\\') {
      escaping = true;
      currentCmd += character;
      continue;
    }
    if (quote != '\0') {
      if (character == quote) {
        quote = '\0';
      }
      currentCmd += character;
      continue;
    }
    if (character == '\'' || character == '"') {
      quote = character;
      currentCmd += character;
      continue;
    }
    if (character == ';') {
      std::size_t firstNonSpace = currentCmd.find_first_not_of(" \t\r\n");
      if (firstNonSpace != std::string::npos) {
        commands.push_back(currentCmd);
      }
      currentCmd.clear();
      continue;
    }
    currentCmd += character;
  }

  std::size_t firstNonSpace = currentCmd.find_first_not_of(" \t\r\n");
  if (firstNonSpace != std::string::npos) {
    commands.push_back(currentCmd);
  }
  return commands;
}

void
CommandLine::SetAlias(const std::string& name, const std::string& expansion)
{
  if (!name.empty()) {
    aliases[lowerCopy(name)] = expansion;
  }
}

void
CommandLine::RemoveAlias(const std::string& name)
{
  aliases.erase(lowerCopy(name));
}

bool
CommandLine::HasAlias(const std::string& name) const
{
  return aliases.find(lowerCopy(name)) != aliases.end();
}

std::string
CommandLine::GetAlias(const std::string& name) const
{
  std::unordered_map<std::string, std::string>::const_iterator it =
    aliases.find(lowerCopy(name));
  if (it != aliases.end()) {
    return it->second;
  }
  return "";
}

std::string
CommandLine::getGhostSuggestion() const
{
  if (currentInput.empty() || cursorPosition != currentInput.size() ||
      hasSelection()) {
    return "";
  }

  std::size_t tokenStart = cursorPosition;
  while (tokenStart > 0 && !std::isspace(static_cast<unsigned char>(
                             currentInput[tokenStart - 1]))) {
    --tokenStart;
  }
  const std::string prefix =
    currentInput.substr(tokenStart, cursorPosition - tokenStart);
  if (prefix.empty()) {
    return "";
  }

  const std::string leadingText = currentInput.substr(0, tokenStart);
  std::vector<std::string> candidates = getCompletionCandidates(leadingText);
  for (const std::string& candidate : candidates) {
    if (candidate.size() > prefix.size()) {
      bool matchesPrefix = true;
      for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(candidate[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
          matchesPrefix = false;
          break;
        }
      }
      if (matchesPrefix) {
        return candidate.substr(prefix.size());
      }
    }
  }
  return "";
}

std::string
CommandLine::getParameterHint(const std::string& inputLine) const
{
  if (inputLine.empty()) {
    return "";
  }
  parseArena.Clear();
  std::vector<std::string> args;
  parseArgsInto(inputLine, args);
  if (args.empty()) {
    return "";
  }
  const std::string cmd = lowerCopy(args[0]);
  const BuiltInCommandHelp* builtIn = findBuiltInCommand(cmd);
  if (builtIn != nullptr) {
    return std::string("Usage: ") + builtIn->usage;
  }
  if (commandRegistry != nullptr && commandRegistry->HasCommand(cmd)) {
    std::string usage = commandRegistry->GetCommandUsage(cmd);
    if (!usage.empty()) {
      return std::string("Usage: ") + usage;
    }
  }
  return "";
}

std::vector<std::string>
CommandLine::getCompletionCandidates(const std::string& leadingText) const
{
  std::vector<std::string> candidates;
  std::vector<std::string> leadingArgs;
  parseArgsInto(leadingText, leadingArgs);
  if (leadingArgs.empty()) {
    for (const BuiltInCommandHelp& command : kBuiltInCommands) {
      candidates.push_back(command.name);
    }

    if (commandRegistry != nullptr) {
      std::vector<std::string> registeredCommands =
        commandRegistry->GetCommandNames();
      candidates.insert(
        candidates.end(), registeredCommands.begin(), registeredCommands.end());
    }
    const std::unordered_map<std::string, EnvVar>& vars = envVars->getVars();
    for (const std::pair<const std::string, EnvVar>& variable : vars) {
      candidates.push_back(variable.first);
    }
    for (const std::pair<const std::string, std::string>& aliasItem : aliases) {
      candidates.push_back(aliasItem.first);
    }
  } else {
    const std::string command = lowerCopy(leadingArgs[0]);
    if (commandRegistry != nullptr && commandRegistry->HasCommand(command)) {
      candidates = commandRegistry->GetCommandCompletions(command);
    } else if (command == "get" || command == "set" || command == "toggle" ||
               command == "vars") {
      const std::unordered_map<std::string, EnvVar>& vars = envVars->getVars();
      for (const std::pair<const std::string, EnvVar>& variable : vars) {
        candidates.push_back(variable.first);
      }
    } else if (command == "fps" || command == "fullscreen") {
      candidates = { "off", "on", "toggle" };
    }
  }

  std::sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()),
                   candidates.end());
  return candidates;
}

void
CommandLine::Complete()
{
  parseArena.Clear();

  std::size_t tokenStart = cursorPosition;
  while (tokenStart > 0 && !std::isspace(static_cast<unsigned char>(
                             currentInput[tokenStart - 1]))) {
    --tokenStart;
  }
  const std::string prefix =
    currentInput.substr(tokenStart, cursorPosition - tokenStart);
  const std::string leadingText = currentInput.substr(0, tokenStart);
  std::vector<std::string> candidates = getCompletionCandidates(leadingText);
  std::vector<std::string> matches;
  for (const std::string& candidate : candidates) {
    if (candidate.size() < prefix.size()) {
      continue;
    }

    bool matchesPrefix = true;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(candidate[i])) !=
          std::tolower(static_cast<unsigned char>(prefix[i]))) {
        matchesPrefix = false;
        break;
      }
    }
    if (matchesPrefix) {
      matches.push_back(candidate);
    }
  }

  if (matches.empty()) {
    completionHint = "No completion matches '" + prefix + "'";
    markCompositionDirty();
    return;
  }

  std::string replacement = matches[0];
  for (std::size_t i = 1; i < matches.size(); ++i) {
    std::size_t commonLength = 0;
    while (commonLength < replacement.size() &&
           commonLength < matches[i].size() &&
           replacement[commonLength] == matches[i][commonLength]) {
      ++commonLength;
    }
    replacement.resize(commonLength);
  }

  if (replacement.size() > prefix.size() || matches.size() == 1) {
    currentInput.replace(tokenStart, cursorPosition - tokenStart, replacement);
    cursorPosition = tokenStart + replacement.size();
    selectionAnchor = cursorPosition;
  }

  if (matches.size() == 1) {
    if (tokenStart == 0 && cursorPosition == currentInput.size()) {
      currentInput += " ";
      ++cursorPosition;
      selectionAnchor = cursorPosition;
    }
    completionHint = "Completed: " + matches[0];
    markCompositionDirty();
    return;
  }

  completionHint = "Matches: ";
  const std::size_t visibleMatches =
    std::min(matches.size(), static_cast<std::size_t>(4));
  for (std::size_t i = 0; i < visibleMatches; ++i) {
    if (i > 0) {
      completionHint += "  ";
    }
    completionHint += matches[i];
  }
  if (matches.size() > visibleMatches) {
    completionHint += "  ...";
  }
  markCompositionDirty();
}
void
CommandLine::ExecuteCommand()
{
  if (currentInput.empty()) {
    return;
  }

  AddToHistory(currentInput);

  parseArena.Clear();
  aliasExpandStack.Clear();

  std::vector<std::string> subCommands;
  splitChainInto(currentInput, subCommands);
  ClearInput();
  scrollOffset = 0;

  for (const std::string& singleCmd : subCommands) {
    ExecuteSingleCommand(singleCmd, 0);
  }

  parseArena.Clear();
  aliasExpandStack.Clear();
}

void
CommandLine::ExecuteSingleCommand(const std::string& singleCmd,
                                  int expansionDepth)
{
  if (expansionDepth > 8) {
    logError("Alias expansion depth limit exceeded");
    return;
  }

  std::vector<std::string> commandParts;
  parseArgsInto(singleCmd, commandParts);
  if (commandParts.empty()) {
    return;
  }

  if (expansionDepth == 0) {
    AppendString(100, 200, 255, 255, "> " + singleCmd);
  }

  const std::string rawCommand = commandParts[0];
  const std::string cmd = lowerCopy(rawCommand);
  std::vector<std::string> args(commandParts.begin() + 1, commandParts.end());

  if (HasAlias(cmd)) {
    std::string expanded = GetAlias(cmd);
    if (!args.empty()) {
      expanded += " " + joinArguments(args, 0);
    }
    // Nested eval: stage expanded text on the LIFO stack so each expansion
    // depth frees in reverse order after its children return.
    char* stacked = aliasExpandStack.AllocateCString(expanded);
    const char* expandedView = stacked != nullptr ? stacked : expanded.c_str();
    std::vector<std::string> chained;
    splitChainInto(std::string(expandedView), chained);
    for (const std::string& subCmd : chained) {
      ExecuteSingleCommand(subCmd, expansionDepth + 1);
    }
    if (stacked != nullptr) {
      aliasExpandStack.FreeTop(stacked);
    }
    return;
  }

  if (cmd == "alias") {
    if (args.empty()) {
      if (aliases.empty()) {
        logNormal("No aliases defined.");
      } else {
        logNormal("Defined aliases:");
        std::vector<std::pair<std::string, std::string>> sortedAliases(
          aliases.begin(), aliases.end());
        std::sort(sortedAliases.begin(), sortedAliases.end());
        for (const std::pair<std::string, std::string>& aliasItem :
             sortedAliases) {
          logNormal("  " + aliasItem.first + " = \"" + aliasItem.second + "\"");
        }
      }
    } else if (args.size() == 1) {
      if (HasAlias(args[0])) {
        logNormal(args[0] + " = \"" + GetAlias(args[0]) + "\"");
      } else {
        logError("Unknown alias: " + args[0]);
      }
    } else {
      const std::string name = args[0];
      const std::string expansion = joinArguments(args, 1);
      SetAlias(name, expansion);
      logSuccess("Alias '" + name + "' set to: " + expansion);
    }
  } else if (cmd == "unalias") {
    if (args.size() != 1) {
      logNormal("Usage: unalias <name>");
    } else if (HasAlias(args[0])) {
      RemoveAlias(args[0]);
      logSuccess("Alias '" + args[0] + "' removed");
    } else {
      logError("Unknown alias: " + args[0]);
    }
  } else if (cmd == "repeat") {
    if (args.size() < 2) {
      logNormal("Usage: repeat <count> <command>");
    } else {
      long count = 0;
      if (!parseLongStrict(args[0], &count) || count < 1 || count > 1000) {
        logError("repeat count must be an integer from 1 to 1000");
      } else {
        const std::string repeatCmd = joinArguments(args, 1);
        for (long i = 0; i < count; ++i) {
          ExecuteSingleCommand(repeatCmd, expansionDepth + 1);
        }
      }
    }
  } else if (cmd == "history") {
    if (args.size() == 1 && lowerCopy(args[0]) == "clear") {
      commandHistory.clear();
      historyIndex = 0;
      logSuccess("Command history cleared");
    } else {
      const std::string filter = args.empty() ? "" : lowerCopy(args[0]);
      logNormal("Command history:");
      int count = 0;
      for (std::size_t i = 0; i < commandHistory.size(); ++i) {
        if (filter.empty() ||
            lowerCopy(commandHistory[i]).find(filter) != std::string::npos) {
          logNormal("  " + std::to_string(i + 1) + ": " + commandHistory[i]);
          ++count;
        }
      }
      if (count == 0) {
        logWarning("No history entries match '" + filter + "'");
      }
    }
  } else if (cmd == "sysinfo") {
    logNormal("=== " + applicationName + " System Telemetry ===");
    logNormal("Registered commands: " +
              std::to_string(commandRegistry
                               ? commandRegistry->GetCommandNames().size()
                               : 0));
    logNormal("Env variables:       " +
              std::to_string(envVars ? envVars->getVars().size() : 0));
    logNormal("Defined aliases:     " + std::to_string(aliases.size()));
    std::array<int, 2> dims =
      window ? window->getWindowDimensions() : std::array<int, 2>{ 0, 0 };
    logNormal("Window resolution:   " + std::to_string(dims[0]) + "x" +
              std::to_string(dims[1]));
    logNormal("FPS overlay:         " +
              std::string(envVars && envVars->getVar("showFPS").valueAsBool
                            ? "on"
                            : "off"));
  } else if (cmd == "help") {
    if (args.empty()) {
      logNormal("Built-in commands:");
      for (const BuiltInCommandHelp& command : kBuiltInCommands) {
        logNormal("  " + std::string(command.usage) + " - " +
                  command.description);
      }

      if (commandRegistry != nullptr) {
        std::vector<std::string> registeredCommands =
          commandRegistry->GetCommandNames();
        if (!registeredCommands.empty()) {
          logNormal("Registered commands:");
        }
        for (const std::string& commandName : registeredCommands) {
          std::string usage = commandRegistry->GetCommandUsage(commandName);
          std::string description =
            commandRegistry->GetCommandDescription(commandName);
          if (usage.empty()) {
            usage = commandName;
          }
          logNormal("  " + usage +
                    (description.empty() ? "" : " - " + description));
        }
      }
      logNormal("Use 'help <command>' for one command.");
    } else {
      const std::string requested = lowerCopy(args[0]);
      const BuiltInCommandHelp* builtIn = findBuiltInCommand(requested);
      if (builtIn != nullptr) {
        logNormal(std::string(builtIn->usage) + " - " + builtIn->description);
      } else if (commandRegistry != nullptr &&
                 commandRegistry->HasCommand(requested)) {
        std::string usage = commandRegistry->GetCommandUsage(requested);
        std::string description =
          commandRegistry->GetCommandDescription(requested);
        logNormal((usage.empty() ? requested : usage) +
                  (description.empty() ? "" : " - " + description));
      } else {
        logError("No help available for '" + args[0] + "'");
      }
    }
  } else if (cmd == "clear") {
    history.clear();
    invalidateWrapCache();
    AppendString(240, 240, 240, 255, applicationName + " Developer Console");
  } else if (cmd == "echo") {
    logNormal(joinArguments(args, 0));
  } else if (cmd == "get") {
    if (args.size() != 1) {
      logNormal("Usage: get <variable>");
    } else {
      const std::string key = findEnvironmentKey(envVars, args[0]);
      if (key.empty()) {
        logError("Unknown variable: " + args[0]);
      } else {
        logNormal(key + " = " + envVars->getVar(key).value);
      }
    }
  } else if (cmd == "set") {
    if (args.size() < 2) {
      logNormal("Usage: set <variable> <value>");
    } else {
      std::string key = findEnvironmentKey(envVars, args[0]);
      if (key.empty()) {
        key = args[0];
      }
      const std::string value = joinArguments(args, 1);
      envVars->setVar(key, value);
      logSuccess(key + " = " + value);
      if (isRestartRequiredVariable(key)) {
        logWarning("Note: Changes to '" + key +
                   "' will take effect after restarting the application.");
      }
    }
  } else if (cmd == "toggle") {
    if (args.size() != 1) {
      logNormal("Usage: toggle <variable>");
    } else {
      const std::string key = findEnvironmentKey(envVars, args[0]);
      if (key.empty()) {
        logError("Unknown variable: " + args[0]);
      } else {
        const bool value = !envVars->getVar(key).valueAsBool;
        envVars->setVar(key, value);
        logSuccess(key + " = " + (value ? "true" : "false"));
        if (isRestartRequiredVariable(key)) {
          logWarning("Note: Changes to '" + key +
                     "' will take effect after restarting the application.");
        }
      }
    }
  } else if (cmd == "vars") {
    const std::string filter = args.empty() ? "" : lowerCopy(args[0]);
    std::vector<std::string> variableLines;
    const std::unordered_map<std::string, EnvVar>& variables =
      envVars->getVars();
    for (const std::pair<const std::string, EnvVar>& variable : variables) {
      if (filter.empty() ||
          lowerCopy(variable.first).find(filter) != std::string::npos) {
        variableLines.push_back(variable.first + " = " + variable.second.value);
      }
    }
    std::sort(variableLines.begin(), variableLines.end());
    if (variableLines.empty()) {
      logWarning("No variables match '" +
                 (args.empty() ? std::string("") : args[0]) + "'");
    }
    for (const std::string& line : variableLines) {
      logNormal(line);
    }
  } else if (cmd == "fps") {
    const bool currentValue = envVars->getVar("showFPS").valueAsBool;
    if (args.empty()) {
      logNormal(std::string("FPS overlay: ") + (currentValue ? "on" : "off"));
    } else {
      bool requestedValue = false;
      bool valid = false;
      if (args.size() == 1 && lowerCopy(args[0]) == "toggle") {
        requestedValue = !currentValue;
        valid = true;
      } else if (args.size() == 1) {
        valid = parseBoolValue(args[0], &requestedValue);
      }
      if (!valid) {
        logError("Usage: fps [on|off|toggle]");
      } else {
        envVars->setVar("showFPS", requestedValue);
        logSuccess(std::string("FPS overlay: ") +
                   (requestedValue ? "on" : "off"));
      }
    }
  } else if (cmd == "fullscreen") {
    const bool currentValue = envVars->getVar("fullscreen").valueAsBool;
    bool requestedValue = !currentValue;
    bool valid = args.empty();
    if (args.size() == 1 && lowerCopy(args[0]) == "toggle") {
      valid = true;
    } else if (args.size() == 1) {
      valid = parseBoolValue(args[0], &requestedValue);
    }
    if (!valid) {
      logError("Usage: fullscreen [on|off|toggle]");
    } else {
      if (requestedValue != currentValue) {
        window->toggleFullscreen();
      }
      envVars->setVar("fullscreen", requestedValue);
      logSuccess(std::string("Fullscreen: ") + (requestedValue ? "on" : "off"));
    }
  } else if (cmd == "close") {
    isOpen = false;
    markCompositionDirty();
  } else if (cmd == "quit") {
    window->requestClose();
  } else if (cmd == "vid_restart") {
    logWarning("vid_restart is unavailable: safely rebuilding the OpenGL "
               "context requires resource re-enrollment");
  } else {
    if (commandRegistry != nullptr && commandRegistry->HasCommand(cmd)) {
      commandRegistry->QueueCommand(cmd, args);
    } else {
      const std::string key = findEnvironmentKey(envVars, rawCommand);
      if (!key.empty()) {
        if (args.empty()) {
          logNormal(key + " = " + envVars->getVar(key).value);
        } else if (args.size() == 1) {
          envVars->setVar(key, args[0]);
          logSuccess(key + " = " + args[0]);
          if (isRestartRequiredVariable(key)) {
            logWarning("Note: Changes to '" + key +
                       "' will take effect after restarting the application.");
          }
        } else {
          logError("Variable assignment accepts one value; use set for text "
                   "with spaces");
        }
      } else {
        logError("Unknown command or variable: " + rawCommand);
      }
    }
  }
}

void
CommandLine::AddToHistory(std::string command)
{
  commandHistory.push_back(command);
  // imitate stack behavior by setting historyIndex to top of stack and popping
  // the first entry when full.
  if (commandHistory.size() > MAX_CMD_HISTORY) {
    commandHistory.erase(commandHistory.begin());
  }
  historyIndex = (int)commandHistory.size();
  tempInput = "";
  resetCursorToEnd();
}

void
CommandLine::HistoryDown()
{
  if (commandHistory.empty())
    return;
  if (historyIndex < (int)commandHistory.size()) {
    historyIndex++;
    if (historyIndex == (int)commandHistory.size()) {
      currentInput = tempInput;
    } else {
      currentInput = commandHistory[historyIndex];
    }
    resetCursorToEnd();
    markCompositionDirty();
  }
}

void
CommandLine::HistoryUp()
{
  if (commandHistory.empty())
    return;
  if (historyIndex > 0) {
    if (historyIndex == (int)commandHistory.size()) {
      tempInput = currentInput;
    }
    historyIndex--;
    currentInput = commandHistory[historyIndex];
    resetCursorToEnd();
    markCompositionDirty();
  }
}

CommandLine::PanelLayout
CommandLine::computePanelLayout(bool useSmoothedPanel) const
{
  std::array<int, 2> windowDimensions =
    window ? window->getWindowDimensions() : std::array<int, 2>{ 1280, 720 };
  float uiScale = 1.0f;
  if (envVars != nullptr) {
    const EnvVar& scaleVar = envVars->getVar("uiScale");
    if (!scaleVar.value.empty() && scaleVar.valueAsDouble > 0.0) {
      uiScale = static_cast<float>(scaleVar.valueAsDouble);
    }
  }
  const float width =
    static_cast<float>(windowDimensions[0]) / (uiScale > 0.0f ? uiScale : 1.0f);
  const float height =
    static_cast<float>(windowDimensions[1]) / (uiScale > 0.0f ? uiScale : 1.0f);
  float defaultMarginX = std::clamp(width * 0.08f, 10.0f, 100.0f);
  float defaultFloatW =
    std::clamp(width - defaultMarginX * 2.0f, std::min(200.0f, width), width);
  float defaultFloatH =
    std::clamp(height * 0.52f, std::min(120.0f, height), height);
  float floatW = (isFloating && floatingW > 0.0f)
                   ? std::clamp(floatingW, std::min(200.0f, width), width)
                   : defaultFloatW;
  float floatH = (isFloating && floatingH > 0.0f)
                   ? std::clamp(floatingH, std::min(120.0f, height), height)
                   : defaultFloatH;
  float targetX0 =
    isFloating ? std::clamp(floatingX, 0.0f, std::max(0.0f, width - floatW))
               : 0.0f;
  float targetY0 =
    isFloating ? std::clamp(floatingY, 0.0f, std::max(0.0f, height - floatH))
               : 0.0f;
  float targetW = isFloating ? floatW : width;
  float targetH = isFloating ? floatH : defaultFloatH;
  float panelX =
    (useSmoothedPanel && currentPanelX >= 0.0f) ? currentPanelX : targetX0;
  float panelY =
    (useSmoothedPanel && currentPanelX >= 0.0f) ? currentPanelY : targetY0;
  float panelW =
    (useSmoothedPanel && currentPanelX >= 0.0f) ? currentPanelW : targetW;
  float panelH =
    (useSmoothedPanel && currentPanelX >= 0.0f) ? currentPanelH : targetH;
  panelW = std::min(panelW, width);
  panelH = std::min(panelH, height);
  panelX = std::clamp(panelX, 0.0f, std::max(0.0f, width - panelW));
  panelY = std::clamp(panelY, 0.0f, std::max(0.0f, height - panelH));
  PanelLayout layout{};
  layout.panelX0 = panelX;
  layout.panelY0 = panelY;
  layout.panelX1 = panelX + panelW;
  layout.panelY1 = panelY + panelH;
  layout.headerHeight = std::min(34.0f, panelH * 0.25f);
  layout.inputRowHeight = std::min(40.0f, panelH * 0.30f);
  layout.historyTop = layout.panelY0 + layout.headerHeight + 8.0f;
  layout.inputTop = layout.panelY1 - layout.inputRowHeight;
  layout.historyBottom = layout.inputTop - 8.0f;
  layout.lineSpacing = 24.0f;
  layout.maxHistoryLines = static_cast<int>(
    (layout.historyBottom - layout.historyTop) / layout.lineSpacing);
  if (layout.maxHistoryLines < 1) {
    layout.maxHistoryLines = 1;
  }
  layout.historyBaseWidth =
    std::max(20.0f, (layout.panelX1 - layout.panelX0) - 32.0f);
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
  const PanelLayout layout = computePanelLayout(true);
  float baseWidth = layout.historyBaseWidth;
  float insetWidth = std::max(20.0f, baseWidth - 16.0f);
  float width = baseWidth;
  if (wrappedHistoryWidth > 0.0f &&
      std::abs(wrappedHistoryWidth - insetWidth) <= 0.5f) {
    width = insetWidth;
  }
  int lines = countWrappedHistoryLines(width);
  if (lines > layout.maxHistoryLines && width == baseWidth) {
    width = insetWidth;
    lines = countWrappedHistoryLines(width);
  }
  if (maxHistoryLines != nullptr) {
    *maxHistoryLines = layout.maxHistoryLines;
  }
  if (maxScroll != nullptr) {
    *maxScroll = std::max(0, lines - layout.maxHistoryLines);
  }
  if (historyWidth != nullptr) {
    *historyWidth = width;
  }
}

void
CommandLine::clampScrollOffset()
{
  int maxScroll = 0;
  computeHistoryScrollLimits(nullptr, &maxScroll, nullptr);
  if (scrollOffset < 0) {
    scrollOffset = 0;
  }
  if (scrollOffset > maxScroll) {
    scrollOffset = maxScroll;
  }
}

void
CommandLine::ScrollUp()
{
  clampScrollOffset();
  int maxScroll = 0;
  computeHistoryScrollLimits(nullptr, &maxScroll, nullptr);
  if (scrollOffset < maxScroll) {
    scrollOffset++;
    markCompositionDirty();
  }
}

void
CommandLine::ScrollDown()
{
  if (scrollOffset > 0) {
    scrollOffset--;
    markCompositionDirty();
  }
}

void
CommandLine::HandleScroll(double yOffset)
{
  if (!isOpen || history.empty()) {
    return;
  }
  int delta = static_cast<int>(yOffset > 0.0 ? 3 : (yOffset < 0.0 ? -3 : 0));
  int previous = scrollOffset;
  scrollOffset += delta;
  clampScrollOffset();
  if (scrollOffset != previous) {
    markCompositionDirty();
  }
}

void
CommandLine::HandleMousePress(double mouseX, double mouseY, bool isDrag)
{
  if (!isOpen) {
    return;
  }

  std::array<int, 2> windowDimensions =
    window ? window->getWindowDimensions() : std::array<int, 2>{ 1280, 720 };
  float uiScale = 1.0f;
  if (envVars != nullptr) {
    const EnvVar& scaleVar = envVars->getVar("uiScale");
    if (!scaleVar.value.empty() && scaleVar.valueAsDouble > 0.0) {
      uiScale = static_cast<float>(scaleVar.valueAsDouble);
    }
  }
  const float width =
    static_cast<float>(windowDimensions[0]) / (uiScale > 0.0f ? uiScale : 1.0f);
  const float height =
    static_cast<float>(windowDimensions[1]) / (uiScale > 0.0f ? uiScale : 1.0f);
  const float virtMouseX =
    static_cast<float>(mouseX) / (uiScale > 0.0f ? uiScale : 1.0f);
  const float virtMouseY =
    static_cast<float>(mouseY) / (uiScale > 0.0f ? uiScale : 1.0f);
  float defaultMarginX = std::clamp(width * 0.08f, 10.0f, 100.0f);
  float defaultFloatW =
    std::clamp(width - defaultMarginX * 2.0f, std::min(200.0f, width), width);
  float defaultFloatH =
    std::clamp(height * 0.52f, std::min(120.0f, height), height);

  float floatW = (isFloating && floatingW > 0.0f)
                   ? std::clamp(floatingW, std::min(200.0f, width), width)
                   : defaultFloatW;
  float floatH = (isFloating && floatingH > 0.0f)
                   ? std::clamp(floatingH, std::min(120.0f, height), height)
                   : defaultFloatH;
  float panelHeight = isFloating ? floatH : defaultFloatH;

  float effectiveAnim =
    (animationProgress > 0.0f) ? animationProgress : (isOpen ? 1.0f : 0.0f);
  float yOffset = -panelHeight * (1.0f - effectiveAnim);

  if (isFloating && floatingX < 0.0f) {
    floatingX = defaultMarginX;
    floatingY = 20.0f;
  }
  float panelX0 =
    isFloating ? std::clamp(floatingX, 0.0f, std::max(0.0f, width - floatW))
               : 0.0f;
  float panelX1 = isFloating ? (panelX0 + floatW) : width;
  float panelY0 =
    isFloating
      ? std::clamp(floatingY, 0.0f, std::max(0.0f, height - panelHeight))
      : yOffset;
  float panelY1 = panelY0 + panelHeight;

  const float headerHeight = std::min(34.0f, panelHeight * 0.25f);
  const float inputRowHeight = std::min(40.0f, panelHeight * 0.30f);
  const float historyTop = panelY0 + headerHeight + 8.0f;
  const float inputTop = panelY1 - inputRowHeight;
  const float historyBottom = inputTop - 8.0f;

  if (virtMouseX < panelX0 || virtMouseX > panelX1 || virtMouseY < panelY0 ||
      virtMouseY > panelY1) {
    return;
  }

  // Corner resize grip (bottom-right 18x18 region)
  if (isFloating && virtMouseX >= panelX1 - 18.0f && virtMouseX <= panelX1 &&
      virtMouseY >= panelY1 - 18.0f && virtMouseY <= panelY1) {
    if (!isDrag) {
      isResizingWindow = true;
      resizeStartW = panelX1 - panelX0;
      resizeStartH = panelY1 - panelY0;
      resizeStartMouseX = virtMouseX;
      resizeStartMouseY = virtMouseY;
      markCompositionDirty();
      return;
    }
  }

  if (virtMouseY >= panelY0 && virtMouseY <= panelY0 + headerHeight) {
    if (!isDrag) {
      if (isFloating && virtMouseX >= panelX1 - 28.0f &&
          virtMouseX <= panelX1 - 6.0f) {
        Toggle();
        return;
      }
      auto now = std::chrono::high_resolution_clock::now();
      auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - lastHeaderClickTime)
                         .count();
      if (elapsedMs > 0 && elapsedMs < 350) {
        ToggleFloatingMode();
        lastHeaderClickTime = std::chrono::high_resolution_clock::time_point{};
        return;
      }
      lastHeaderClickTime = now;
      if (isFloating) {
        isDraggingWindow = true;
        dragWindowOffsetX = virtMouseX - panelX0;
        dragWindowOffsetY = virtMouseY - panelY0;
        markCompositionDirty();
        return;
      }
    }
  }

  int maxHistoryLines = 0;
  int maxScroll = 0;
  computeHistoryScrollLimits(&maxHistoryLines, &maxScroll, nullptr);

  if (maxScroll > 0) {
    float scrollbarWidth = 5.0f;
    float scrollbarRightMargin = 9.0f;
    float barX1 = panelX1 - scrollbarWidth - scrollbarRightMargin;
    float barX2 = panelX1 - scrollbarRightMargin;
    if (mouseX >= barX1 - 12.0f && mouseX <= barX2 + 12.0f &&
        mouseY >= historyTop && mouseY <= historyBottom) {
      if (!isDrag) {
        isDraggingScrollbar = true;
        dragStartY = static_cast<float>(mouseY);
        dragStartScrollOffset = scrollOffset;
      }
      float trackHeight = historyBottom - historyTop;
      float clickRatio =
        1.0f - (static_cast<float>(mouseY) - historyTop) / trackHeight;
      if (clickRatio < 0.0f) {
        clickRatio = 0.0f;
      }
      if (clickRatio > 1.0f) {
        clickRatio = 1.0f;
      }
      scrollOffset = static_cast<int>(clickRatio * maxScroll);
      if (scrollOffset < 0) {
        scrollOffset = 0;
      }
      if (scrollOffset > maxScroll) {
        scrollOffset = maxScroll;
      }
      markCompositionDirty();
      return;
    }
  }

  if (mouseY >= inputTop && mouseY <= panelY1) {
    const float inputTextX = panelX0 + 40.0f;
    const float inputAvailableWidth =
      std::max(48.0f, (panelX1 - panelX0) - 40.0f - 22.0f);
    std::size_t visibleStart = 0;
    while (visibleStart < cursorPosition) {
      if (measureFontTextRange(currentInput.data() + visibleStart,
                               cursorPosition - visibleStart) <=
          inputAvailableWidth) {
        break;
      }
      ++visibleStart;
    }
    std::size_t visibleEnd = cursorPosition;
    while (visibleEnd < currentInput.size()) {
      if (measureFontTextRange(currentInput.data() + visibleStart,
                               visibleEnd + 1 - visibleStart) >
          inputAvailableWidth) {
        break;
      }
      ++visibleEnd;
    }
    std::string visibleInput =
      currentInput.substr(visibleStart, visibleEnd - visibleStart);

    float relX = static_cast<float>(mouseX);
    std::size_t bestIdx = 0;
    float minDiff = 1e9f;
    for (std::size_t i = 0; i <= visibleInput.size(); ++i) {
      float charX = inputTextX + measureFontTextRange(visibleInput.data(), i);
      float diff = std::abs(charX - relX);
      if (diff < minDiff) {
        minDiff = diff;
        bestIdx = i;
      }
    }
    std::size_t targetPos = visibleStart + bestIdx;
    if (targetPos > currentInput.size()) {
      targetPos = currentInput.size();
    }

    cursorPosition = targetPos;
    if (!isDrag) {
      selectionAnchor = cursorPosition;
    }
    markCompositionDirty();
  }
}

void
CommandLine::HandleMouseDrag(double mouseX, double mouseY)
{
  if (!isOpen) {
    return;
  }
  if (isResizingWindow && isFloating) {
    std::array<int, 2> windowDimensions =
      window ? window->getWindowDimensions() : std::array<int, 2>{ 1280, 720 };
    float width = static_cast<float>(windowDimensions[0]);
    float height = static_cast<float>(windowDimensions[1]);

    float deltaX = static_cast<float>(mouseX) - resizeStartMouseX;
    float deltaY = static_cast<float>(mouseY) - resizeStartMouseY;

    floatingW = std::clamp(resizeStartW + deltaX, 280.0f, width - floatingX);
    floatingH = std::clamp(resizeStartH + deltaY, 180.0f, height - floatingY);
    markCompositionDirty();
    return;
  }
  if (isDraggingWindow && isFloating) {
    std::array<int, 2> windowDimensions =
      window ? window->getWindowDimensions() : std::array<int, 2>{ 1280, 720 };
    float width = static_cast<float>(windowDimensions[0]);
    float height = static_cast<float>(windowDimensions[1]);
    float defaultMarginX = std::clamp(width * 0.08f, 20.0f, 100.0f);
    float defaultFloatW = std::max(280.0f, width - defaultMarginX * 2.0f);
    float defaultFloatH = height * 0.52f;
    if (defaultFloatH < 240.0f) {
      defaultFloatH = 240.0f;
    }
    if (defaultFloatH > height - 20.0f) {
      defaultFloatH = height - 20.0f;
    }
    float floatW = (floatingW > 0.0f) ? floatingW : defaultFloatW;
    float floatH = (floatingH > 0.0f) ? floatingH : defaultFloatH;

    floatingX = std::clamp(
      static_cast<float>(mouseX) - dragWindowOffsetX, 0.0f, width - floatW);
    floatingY = std::clamp(
      static_cast<float>(mouseY) - dragWindowOffsetY, 0.0f, height - floatH);
    markCompositionDirty();
    return;
  }
  if (isDraggingScrollbar) {
    std::array<int, 2> windowDimensions =
      window ? window->getWindowDimensions() : std::array<int, 2>{ 1280, 720 };
    float height = static_cast<float>(windowDimensions[1]);
    float defaultFloatH = height * 0.52f;
    if (defaultFloatH < 240.0f) {
      defaultFloatH = 240.0f;
    }
    if (defaultFloatH > height - 20.0f) {
      defaultFloatH = height - 20.0f;
    }
    float panelHeight =
      (isFloating && floatingH > 0.0f) ? floatingH : defaultFloatH;
    float effectiveAnim =
      (animationProgress > 0.0f) ? animationProgress : (isOpen ? 1.0f : 0.0f);
    float yOffset = -panelHeight * (1.0f - effectiveAnim);
    const float headerHeight = 34.0f;
    const float inputRowHeight = 40.0f;
    const float historyTop = yOffset + headerHeight + 8.0f;
    const float inputTop = yOffset + panelHeight - inputRowHeight;
    const float historyBottom = inputTop - 8.0f;

    int maxHistoryLines = 0;
    int maxScroll = 0;
    computeHistoryScrollLimits(&maxHistoryLines, &maxScroll, nullptr);

    float trackHeight = historyBottom - historyTop;
    if (trackHeight > 0.0f && maxScroll > 0) {
      float deltaY = static_cast<float>(mouseY) - dragStartY;
      float deltaScrollRatio = -deltaY / trackHeight;
      int newScroll =
        dragStartScrollOffset + static_cast<int>(deltaScrollRatio * maxScroll);
      if (newScroll < 0) {
        newScroll = 0;
      }
      if (newScroll > maxScroll) {
        newScroll = maxScroll;
      }
      scrollOffset = newScroll;
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
CommandLine::AppendString(unsigned char r,
                          unsigned char g,
                          unsigned char b,
                          unsigned char a,
                          std::string str)
{
  history.push_back({ r, g, b, a, str });
  if (history.size() > MAX_CMD_HISTORY) {
    history.erase(history.begin());
    if (!wrappedHistory.empty()) {
      wrappedHistoryTotalLines -=
        static_cast<int>(wrappedHistory.front().size());
      wrappedHistory.erase(wrappedHistory.begin());
    }
  }
  if (wrappedHistoryWidth > 0.0f &&
      wrappedHistory.size() + 1 == history.size()) {
    std::vector<std::string> lines;
    wrapTextToWidth(history.back().content, wrappedHistoryWidth, &lines);
    wrappedHistoryTotalLines += static_cast<int>(lines.size());
    wrappedHistory.push_back(std::move(lines));
  } else if (wrappedHistory.size() != history.size()) {
    wrappedHistoryWidth = -1.0f;
  }
  markCompositionDirty();
}
void
CommandLine::AppendStringLn(unsigned char r,
                            unsigned char g,
                            unsigned char b,
                            unsigned char a,
                            std::string str)
{
  AppendString(r, g, b, a, str + "\n");
}

void
CommandLine::DrawImpl()
{
  // Migrated to tokens.
}

namespace {

// Emit chrome/text as GameVisual primitives (D-R15). writeAt tracks approximate
// vertex budget for overflow (same units as the old packed UI buffer).
static unsigned int
packSolidQuad(GameVisual* visual,
              unsigned int destCap,
              unsigned int writeAt,
              float x0,
              float y0,
              float x1,
              float y1,
              unsigned char r,
              unsigned char g,
              unsigned char b,
              unsigned char a)
{
  if (writeAt + 4 > destCap || visual == nullptr) {
    return writeAt;
  }
  ColorRgba color{ r, g, b, a };
  visual->addFilledRect(x0, y0, x1 - x0, y1 - y0, color);
  return writeAt + 4;
}

static unsigned int
packSolidRect(GameVisual* visual,
              unsigned int destCap,
              unsigned int writeAt,
              float x,
              float y,
              float width,
              float height,
              ColorRgba color)
{
  return packSolidQuad(visual,
                       destCap,
                       writeAt,
                       x,
                       y,
                       x + width,
                       y + height,
                       color.r,
                       color.g,
                       color.b,
                       color.a);
}

static unsigned int
packOutlineRect(GameVisual* visual,
                unsigned int destCap,
                unsigned int writeAt,
                float x,
                float y,
                float width,
                float height,
                ColorRgba color,
                float lineWidth)
{
  if (writeAt + 16 > destCap || visual == nullptr) {
    return writeAt;
  }
  visual->addOutlineRect(x, y, width, height, color, lineWidth);
  return writeAt + 16;
}

static unsigned int
packLine(GameVisual* visual,
         unsigned int destCap,
         unsigned int writeAt,
         float x0,
         float y0,
         float x1,
         float y1,
         ColorRgba color,
         float lineWidth)
{
  if (writeAt + 4 > destCap || visual == nullptr) {
    return writeAt;
  }
  visual->addLine(x0, y0, x1, y1, color, lineWidth);
  return writeAt + 4;
}

// Historical console text used 2× easy-font (sizePt 24).
static unsigned int
packFontLine(GameVisual* visual,
             unsigned int destCap,
             unsigned int writeAt,
             float x,
             float y,
             const char* text,
             unsigned char color[4])
{
  if (writeAt >= destCap || text == nullptr || visual == nullptr) {
    return writeAt;
  }
  ColorRgba c{ color[0], color[1], color[2], color[3] };
  visual->addText(text, x, y, 24.0f, c);
  // Rough budget: ~1 quad per character (under-estimate is fine for soft cap).
  const unsigned int estimate =
    static_cast<unsigned int>(std::strlen(text) * 4u);
  if (writeAt + estimate > destCap) {
    return destCap;
  }
  return writeAt + estimate;
}

} // namespace

bool
CommandLine::AppendCommands(Renderer* r)
{
  if (!isVisible()) {
    return true;
  }
  if (!gpuReady || !r) {
    return false;
  }

  // Animation
  std::chrono::high_resolution_clock::time_point now =
    std::chrono::high_resolution_clock::now();
  float deltaTime = std::chrono::duration<float>(now - lastAnimTime).count();
  lastAnimTime = now;
  if (deltaTime > 0.1f) {
    deltaTime = 0.1f;
  }
  const float animationSpeed = 12.0f;
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
    return true;
  }
  // Cubic Ease-Out for ultra-smooth decelerating open/close animations
  float easeProgress = 1.0f - std::pow(1.0f - animationProgress, 3.0f);

  std::array<int, 2> windowDimensions = window->getWindowDimensions();
  int winWidth = windowDimensions[0];
  int winHeight = windowDimensions[1];
  float width = static_cast<float>(winWidth);
  float height = static_cast<float>(winHeight);

  float defaultMarginX = std::clamp(width * 0.08f, 20.0f, 100.0f);
  float defaultFloatW = std::max(280.0f, width - defaultMarginX * 2.0f);
  float defaultFloatH = height * 0.52f;
  if (defaultFloatH < 240.0f) {
    defaultFloatH = 240.0f;
  }
  if (defaultFloatH > height - 20.0f) {
    defaultFloatH = height - 20.0f;
  }

  float floatW = (isFloating && floatingW > 0.0f)
                   ? std::clamp(floatingW, 280.0f, width)
                   : defaultFloatW;
  float floatH = (isFloating && floatingH > 0.0f)
                   ? std::clamp(floatingH, 180.0f, height)
                   : defaultFloatH;

  float targetX0 =
    isFloating ? std::clamp(floatingX, 0.0f, width - floatW) : 0.0f;
  float targetY0 =
    isFloating ? std::clamp(floatingY, 0.0f, height - floatH) : 0.0f;
  float targetW = isFloating ? floatW : width;
  float targetH = isFloating ? floatH : defaultFloatH;

  if (isFloating && floatingX < 0.0f) {
    floatingX = defaultMarginX;
    floatingY = 20.0f;
    targetX0 = defaultMarginX;
    targetY0 = 20.0f;
  }

  if (currentPanelX < 0.0f) {
    currentPanelX = targetX0;
    currentPanelY = targetY0;
    currentPanelW = targetW;
    currentPanelH = targetH;
  } else {
    float smoothSpeed = (isDraggingWindow || isResizingWindow) ? 35.0f : 16.0f;
    currentPanelX =
      Math::lerp(currentPanelX, targetX0, smoothSpeed * deltaTime);
    currentPanelY =
      Math::lerp(currentPanelY, targetY0, smoothSpeed * deltaTime);
    currentPanelW = Math::lerp(currentPanelW, targetW, smoothSpeed * deltaTime);
    currentPanelH = Math::lerp(currentPanelH, targetH, smoothSpeed * deltaTime);
  }

  float panelHeight = currentPanelH;
  float ySlide = isFloating
                   ? (currentPanelY - (1.0f - easeProgress) *
                                        (currentPanelY + panelHeight + 40.0f))
                   : (-panelHeight * (1.0f - easeProgress));

  float panelX0 = currentPanelX;
  float panelX1 = currentPanelX + currentPanelW;
  float panelY0 = ySlide;
  float panelY1 = panelY0 + panelHeight;

  const float headerHeight = 34.0f;
  const float inputRowHeight = 40.0f;
  const float historyTop = panelY0 + headerHeight + 8.0f;
  const float inputTop = panelY1 - inputRowHeight;
  const float historyBottom = inputTop - 8.0f;
  float lineSpacing = 24.0f;
  int maxHistoryLines =
    static_cast<int>((historyBottom - historyTop) / lineSpacing);
  if (maxHistoryLines < 1) {
    maxHistoryLines = 1;
  }

  long long caretMilliseconds =
    std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch())
      .count();
  bool caretVisible = (caretMilliseconds % 1000) < 560;
  int caretPhase = caretVisible ? 1 : 0;
  int pulseStep = static_cast<int>((caretMilliseconds / 100) % 10);
  const bool animating =
    isOpen ? (animationProgress < 1.0f) : (animationProgress > 0.0f);
  const bool panelBusy = animating || isDraggingWindow || isResizingWindow;
  if (panelBusy) {
    compositionDirty = true;
  }
  if (winWidth != composedWindowW || winHeight != composedWindowH) {
    compositionDirty = true;
  }
  if (std::abs(currentPanelX - composedPanelX) > 0.5f ||
      std::abs(currentPanelY - composedPanelY) > 0.5f ||
      std::abs(currentPanelW - composedPanelW) > 0.5f ||
      std::abs(currentPanelH - composedPanelH) > 0.5f) {
    compositionDirty = true;
  }
  if (caretPhase != composedCaretPhase || pulseStep != composedPulseStep ||
      scrollOffset != composedScrollOffset) {
    compositionDirty = true;
  }
  if (!compositionDirty) {
    visual.setRenderer(r);
    visual.setWindow(window);
    visual.setSpace(PrimitiveSpace::Pixels);
    visual.setVisible(true);
    return visual.AppendCommands(r);
  }

  // Rebuild chrome + text as GameVisual primitives when dirty.
  visual.clearPrimitives();
  visual.setRenderer(r);
  visual.setWindow(window);
  visual.setSpace(PrimitiveSpace::Pixels);
  const unsigned int kCap = kUiVertCap;
  unsigned int packed = 0;

  float pulse =
    (std::sin(static_cast<float>(pulseStep) * 0.62831853f) + 1.0f) * 0.5f;

  // Primitive-composed console chrome. The panel uses a small, coherent
  // theme and real outlines/lines; text, selection, caret, and scrolling stay
  // in the same stable painter stream.
  const unsigned char revealOpacity =
    static_cast<unsigned char>(std::clamp(easeProgress, 0.0f, 1.0f) * 255.0f);
  packed =
    packSolidRect(&visual,
                  kCap,
                  packed,
                  0.0f,
                  0.0f,
                  width,
                  height,
                  UiTheme::applyOpacity(UiTheme::canvasShade(), revealOpacity));
  packed = packSolidRect(&visual,
                         kCap,
                         packed,
                         panelX0 + 5.0f,
                         panelY0 + 7.0f,
                         panelX1 - panelX0,
                         panelY1 - panelY0,
                         UiTheme::panelShadow());
  packed = packSolidRect(&visual,
                         kCap,
                         packed,
                         panelX0,
                         panelY0,
                         panelX1 - panelX0,
                         panelY1 - panelY0,
                         UiTheme::panelSurface());
  packed = packOutlineRect(&visual,
                           kCap,
                           packed,
                           panelX0,
                           panelY0,
                           panelX1 - panelX0,
                           panelY1 - panelY0,
                           UiTheme::panelBorder(),
                           1.0f);

  packed = packSolidRect(&visual,
                         kCap,
                         packed,
                         panelX0 + 1.0f,
                         panelY0 + 1.0f,
                         panelX1 - panelX0 - 2.0f,
                         headerHeight - 1.0f,
                         UiTheme::panelRaised());
  const ColorRgba animatedAccent = UiTheme::applyOpacity(
    UiTheme::accent(), static_cast<unsigned char>(205.0f + pulse * 50.0f));
  packed = packLine(&visual,
                    kCap,
                    packed,
                    panelX0 + 1.0f,
                    panelY0 + headerHeight,
                    panelX1 - 1.0f,
                    panelY0 + headerHeight,
                    animatedAccent,
                    2.0f);
  packed = packSolidRect(&visual,
                         kCap,
                         packed,
                         panelX0 + 10.0f,
                         panelY0 + 10.0f,
                         6.0f,
                         14.0f,
                         animatedAccent);

  const float historyInsetX = panelX0 + 9.0f;
  const float historyInsetWidth = std::max(1.0f, panelX1 - panelX0 - 18.0f);
  const float historyInsetHeight =
    std::max(1.0f, historyBottom - historyTop + 8.0f);
  packed = packSolidRect(&visual,
                         kCap,
                         packed,
                         historyInsetX,
                         historyTop - 4.0f,
                         historyInsetWidth,
                         historyInsetHeight,
                         UiTheme::panelInset());
  packed = packOutlineRect(&visual,
                           kCap,
                           packed,
                           historyInsetX,
                           historyTop - 4.0f,
                           historyInsetWidth,
                           historyInsetHeight,
                           UiTheme::divider(),
                           1.0f);

  packed = packSolidRect(&visual,
                         kCap,
                         packed,
                         panelX0 + 9.0f,
                         inputTop + 3.0f,
                         panelX1 - panelX0 - 18.0f,
                         inputRowHeight - 9.0f,
                         UiTheme::panelRaised());
  packed = packOutlineRect(&visual,
                           kCap,
                           packed,
                           panelX0 + 9.0f,
                           inputTop + 3.0f,
                           panelX1 - panelX0 - 18.0f,
                           inputRowHeight - 9.0f,
                           UiTheme::divider(),
                           1.0f);
  packed = packSolidRect(&visual,
                         kCap,
                         packed,
                         panelX0 + 9.0f,
                         inputTop + 3.0f,
                         3.0f,
                         inputRowHeight - 9.0f,
                         animatedAccent);

  if (isFloating) {
    packed = packOutlineRect(&visual,
                             kCap,
                             packed,
                             panelX0 + 2.0f,
                             panelY0 + 2.0f,
                             panelX1 - panelX0 - 4.0f,
                             panelY1 - panelY0 - 4.0f,
                             UiTheme::accentSoft(),
                             1.0f);
    for (int grip = 0; grip < 3; ++grip) {
      const float offset = static_cast<float>(grip) * 4.0f;
      packed = packLine(&visual,
                        kCap,
                        packed,
                        panelX1 - 15.0f + offset,
                        panelY1 - 4.0f,
                        panelX1 - 4.0f,
                        panelY1 - 15.0f + offset,
                        UiTheme::accent(),
                        1.5f);
    }
  }

  int totalLines = 0;
  float historyAvailableWidth = 0.0f;
  computeHistoryScrollLimits(&maxHistoryLines, nullptr, &historyAvailableWidth);
  totalLines = countWrappedHistoryLines(historyAvailableWidth);
  if (totalLines > maxHistoryLines) {
    const float trackTop = historyTop;
    const float trackBottom = historyBottom;
    const float trackHeight = trackBottom - trackTop;
    const float scrollbarWidth = 4.0f;
    const float scrollbarRightMargin = isFloating ? 16.0f : 12.0f;
    const float barX1 = panelX1 - scrollbarWidth - scrollbarRightMargin;
    const float barX2 = panelX1 - scrollbarRightMargin;

    packed = packSolidRect(&visual,
                           kCap,
                           packed,
                           barX1,
                           trackTop,
                           barX2 - barX1,
                           trackHeight,
                           UiTheme::divider());

    float thumbHeight = trackHeight * (static_cast<float>(maxHistoryLines) /
                                       static_cast<float>(totalLines));
    if (thumbHeight < 20.0f) {
      thumbHeight = 20.0f;
    }
    const int maxScroll = totalLines - maxHistoryLines;
    const float scrollPercent =
      (maxScroll > 0)
        ? (static_cast<float>(scrollOffset) / static_cast<float>(maxScroll))
        : 0.0f;
    float thumbTop =
      (trackBottom - thumbHeight) - scrollPercent * (trackHeight - thumbHeight);
    thumbTop =
      std::clamp(thumbTop, trackTop + 2.0f, trackBottom - thumbHeight - 2.0f);
    packed = packSolidRect(&visual,
                           kCap,
                           packed,
                           barX1,
                           thumbTop,
                           barX2 - barX1,
                           thumbHeight,
                           animatedAccent);
  }

  const ColorRgba primaryText = UiTheme::textPrimary();
  const ColorRgba mutedText = UiTheme::textMuted();
  const ColorRgba accentText = UiTheme::accent();
  unsigned char titleColor[4] = {
    primaryText.r, primaryText.g, primaryText.b, primaryText.a
  };
  unsigned char promptColor[4] = {
    accentText.r, accentText.g, accentText.b, accentText.a
  };
  unsigned char inputColor[4] = {
    primaryText.r, primaryText.g, primaryText.b, primaryText.a
  };

  std::string paramHint = getParameterHint(currentInput);
  std::string statusText;
  unsigned char statusColor[4];

  if (!completionHint.empty()) {
    statusText = completionHint;
    statusColor[0] = accentText.r;
    statusColor[1] = accentText.g;
    statusColor[2] = accentText.b;
    statusColor[3] = 255;
  } else if (!paramHint.empty()) {
    statusText = paramHint;
    const ColorRgba warningText = UiTheme::warning();
    statusColor[0] = warningText.r;
    statusColor[1] = warningText.g;
    statusColor[2] = warningText.b;
    statusColor[3] = 255;
  } else {
    statusText = "Tab complete  |  Double-click title: float";
    statusColor[0] = mutedText.r;
    statusColor[1] = mutedText.g;
    statusColor[2] = mutedText.b;
    statusColor[3] = 255;
  }

  float headerWidth = panelX1 - panelX0;
  const std::string upperApplicationName = upperCopy(applicationName);
  std::string titleStr = isFloating
                           ? upperApplicationName + "  /  FLOATING CONSOLE"
                           : upperApplicationName + "  /  CONSOLE";
  if (headerWidth < 380.0f) {
    titleStr = upperApplicationName;
  }
  std::string visibleTitle =
    truncateTextToWidth(titleStr, std::max(0.0f, headerWidth - 20.0f));
  if (!visibleTitle.empty()) {
    packed = packFontLine(&visual,
                          kCap,
                          packed,
                          panelX0 + 14.0f,
                          panelY0 + 9.0f,
                          visibleTitle.c_str(),
                          titleColor);
  }

  float closeBtnWidth = 0.0f;
  if (isFloating) {
    closeBtnWidth = 28.0f;
    float closeX1 = panelX1 - 8.0f;
    float closeX0 = panelX1 - 28.0f;
    float closeY0 = panelY0 + 6.0f;
    float closeY1 = panelY0 + 24.0f;
    packed = packSolidRect(&visual,
                           kCap,
                           packed,
                           closeX0,
                           closeY0,
                           closeX1 - closeX0,
                           closeY1 - closeY0,
                           UiTheme::error());
    unsigned char closeTextColor[4] = { 255, 255, 255, 255 };
    packed = packFontLine(&visual,
                          kCap,
                          packed,
                          closeX0 + 6.0f,
                          closeY0 + 3.0f,
                          "X",
                          closeTextColor);
  }

  float titleWidth = measureFontText(visibleTitle);
  float statusAvailableWidth =
    (panelX1 - panelX0) - titleWidth - closeBtnWidth - 44.0f;
  if (statusAvailableWidth >= 60.0f) {
    std::string truncatedStatus =
      truncateTextToWidth(statusText, statusAvailableWidth);
    if (!truncatedStatus.empty()) {
      float statusX =
        panelX1 - closeBtnWidth - measureFontText(truncatedStatus) - 16.0f;
      packed = packFontLine(&visual,
                            kCap,
                            packed,
                            statusX,
                            panelY0 + 9.0f,
                            truncatedStatus.c_str(),
                            statusColor);
    }
  }

  // History text is drawn after chrome, but before the input row, so its
  // clipping and scroll thumb agree with the available space. Off-screen
  // visual lines stay in the wrap cache for scroll totals and are not
  // tessellated.
  float currentY = historyTop;
  int endIdx = wrappedHistoryTotalLines - 1 - scrollOffset;
  if (endIdx >= 0 && wrappedHistory.size() == history.size()) {
    int startIdx = endIdx - (maxHistoryLines - 1);
    if (startIdx < 0) {
      startIdx = 0;
    }
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
      unsigned char itemColor[4] = { item.r, item.g, item.b, item.a };
      if (item.content.rfind("SUCCESS:", 0) == 0) {
        const ColorRgba successText = UiTheme::success();
        itemColor[0] = successText.r;
        itemColor[1] = successText.g;
        itemColor[2] = successText.b;
        itemColor[3] = 255;
      } else if (item.content.rfind("ERROR:", 0) == 0) {
        const ColorRgba errorText = UiTheme::error();
        itemColor[0] = errorText.r;
        itemColor[1] = errorText.g;
        itemColor[2] = errorText.b;
        itemColor[3] = 255;
      } else if (item.content.rfind("WARNING:", 0) == 0) {
        const ColorRgba warningText = UiTheme::warning();
        itemColor[0] = warningText.r;
        itemColor[1] = warningText.g;
        itemColor[2] = warningText.b;
        itemColor[3] = 255;
      }
      const std::vector<std::string>& wrapped = wrappedHistory[entryIndex];
      std::size_t lineIndex = 0;
      if (visualCursor < startIdx) {
        lineIndex = static_cast<std::size_t>(startIdx - visualCursor);
        visualCursor = startIdx;
      }
      for (; lineIndex < wrapped.size() && visualCursor <= endIdx;
           ++lineIndex) {
        if (!wrapped[lineIndex].empty()) {
          packed = packFontLine(&visual,
                                kCap,
                                packed,
                                panelX0 + 14.0f,
                                currentY,
                                wrapped[lineIndex].c_str(),
                                itemColor);
        }
        currentY += lineSpacing;
        ++visualCursor;
      }
    }
  }

  // The input row has a fixed-width text viewport. This keeps a long command
  // editable: the cursor remains on-screen, selection is visible, and the
  // caret is a real rendered bar rather than an appended underscore.
  const float inputTextX = panelX0 + 42.0f;
  const float inputAvailableWidth =
    std::max(48.0f, (panelX1 - panelX0) - 42.0f - 22.0f);
  std::size_t visibleStart = 0;
  while (visibleStart < cursorPosition) {
    if (measureFontTextRange(currentInput.data() + visibleStart,
                             cursorPosition - visibleStart) <=
        inputAvailableWidth) {
      break;
    }
    ++visibleStart;
  }
  std::size_t visibleEnd = cursorPosition;
  while (visibleEnd < currentInput.size()) {
    if (measureFontTextRange(currentInput.data() + visibleStart,
                             visibleEnd + 1 - visibleStart) >
        inputAvailableWidth) {
      break;
    }
    ++visibleEnd;
  }
  std::string visibleInput =
    currentInput.substr(visibleStart, visibleEnd - visibleStart);
  float inputY = inputTop + 12.0f;

  // Compact prompt marker keeps the input hierarchy readable without
  // competing with command text.
  packed = packSolidRect(&visual,
                         kCap,
                         packed,
                         panelX0 + 15.0f,
                         inputY - 2.0f,
                         18.0f,
                         18.0f,
                         UiTheme::panelInset());
  packed = packOutlineRect(&visual,
                           kCap,
                           packed,
                           panelX0 + 15.0f,
                           inputY - 2.0f,
                           18.0f,
                           18.0f,
                           UiTheme::accentSoft(),
                           1.0f);
  packed = packFontLine(
    &visual, kCap, packed, panelX0 + 18.0f, inputY, ">", promptColor);

  if (hasSelection()) {
    std::size_t selectionStart = std::min(cursorPosition, selectionAnchor);
    std::size_t selectionEnd = std::max(cursorPosition, selectionAnchor);
    std::size_t highlightStart = std::max(selectionStart, visibleStart);
    std::size_t highlightEnd = std::min(selectionEnd, visibleEnd);
    if (highlightStart < highlightEnd) {
      std::string beforeSelection =
        visibleInput.substr(0, highlightStart - visibleStart);
      std::string selectedText = visibleInput.substr(
        highlightStart - visibleStart, highlightEnd - highlightStart);
      float highlightX0 = inputTextX + measureFontText(beforeSelection);
      float highlightX1 = highlightX0 + measureFontText(selectedText);
      packed = packSolidRect(&visual,
                             kCap,
                             packed,
                             highlightX0,
                             inputY - 3.0f,
                             highlightX1 - highlightX0,
                             19.0f,
                             UiTheme::selection());
    }
  }
  packed = packFontLine(&visual,
                        kCap,
                        packed,
                        inputTextX,
                        inputY,
                        visibleInput.c_str(),
                        inputColor);

  // Inline completion preview.
  if (cursorPosition == currentInput.size() &&
      visibleEnd == currentInput.size()) {
    std::string ghostText = getGhostSuggestion();
    if (!ghostText.empty()) {
      float ghostX = inputTextX + measureFontTextRange(visibleInput.data(),
                                                       visibleInput.size());
      if (ghostX < inputTextX + inputAvailableWidth) {
        unsigned char ghostColor[4] = {
          mutedText.r, mutedText.g, mutedText.b, 150
        };
        packed = packFontLine(
          &visual, kCap, packed, ghostX, inputY, ghostText.c_str(), ghostColor);
      }
    }
  }

  if (caretVisible && cursorPosition >= visibleStart &&
      cursorPosition <= visibleEnd) {
    std::string textBeforeCaret =
      visibleInput.substr(0, cursorPosition - visibleStart);
    float caretX = inputTextX + measureFontTextRange(textBeforeCaret.data(),
                                                     textBeforeCaret.size());
    packed = packLine(&visual,
                      kCap,
                      packed,
                      caretX,
                      inputY - 3.0f,
                      caretX,
                      inputY + 16.0f,
                      UiTheme::accentSoft(),
                      4.0f);
    packed = packLine(&visual,
                      kCap,
                      packed,
                      caretX,
                      inputY - 3.0f,
                      caretX,
                      inputY + 16.0f,
                      UiTheme::textPrimary(),
                      1.5f);
  }

  if (packed < 4) {
    return true;
  }

  visual.setVisible(true);
  composedCaretPhase = caretPhase;
  composedPulseStep = pulseStep;
  composedScrollOffset = scrollOffset;
  composedWindowW = winWidth;
  composedWindowH = winHeight;
  composedPanelX = currentPanelX;
  composedPanelY = currentPanelY;
  composedPanelW = currentPanelW;
  composedPanelH = currentPanelH;
  compositionDirty = false;
  return visual.AppendCommands(r);
}
