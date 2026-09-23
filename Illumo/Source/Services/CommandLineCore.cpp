#include <Illumo/Services/CommandLineCore.h>
#include <Illumo/Services/Logger.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
// WASM guests compile this file too but have no file-system paths; their
// console bridge forwards to the host, whose console runs exec/savelog.
#if !defined(ILLUMO_SERIAL_GUEST)
#include <filesystem>
#include <fstream>
#include <system_error>
#endif

namespace {

struct BuiltInCommandHelp
{
  const char* name;
  const char* usage;
  const char* description;
};

const BuiltInCommandHelp kBuiltInCommands[] = {
  { "add",
    "add <variable> <amount>",
    "Add a number to a numeric environment variable" },
  { "alerts",
    "alerts [on|off|toggle]",
    "Show error/warning counts while the console is closed" },
  { "alias", "alias [<name> <command>]", "Create or list command aliases" },
  { "bind",
    "bind [<key> [command]]",
    "Bind F1-F12 to a command, or list bindings" },
  { "clear", "clear", "Clear console output" },
  { "close", "close", "Close the console" },
  { "copy",
    "copy [<lines>|all]",
    "Copy recent console output to the clipboard" },
  { "cycle",
    "cycle <variable> <value> <value> [...]",
    "Step a variable to the next value in a list" },
  { "echo", "echo <text>", "Print text to the console" },
  { "exec", "exec <file>", "Run console commands from a text file" },
  { "filter", "filter [text|off]", "Show only output lines containing text" },
  { "fps", "fps [on|off|toggle]", "Show or change the FPS overlay" },
  { "fullscreen",
    "fullscreen [on|off|toggle]",
    "Show or change fullscreen mode" },
  { "get", "get <variable>", "Read an environment variable" },
  { "help", "help [command]", "Show commands or detailed help" },
  { "history", "history [filter|clear]", "Search or clear command history" },
  { "loglevel",
    "loglevel [trace|info|warning|error]",
    "Hide console output below a severity" },
  { "memory", "memory [on|off|toggle]", "Show or change the memory overlay" },
  { "quit", "quit", "Exit the application" },
  { "repeat", "repeat <count> <command>", "Execute command multiple times" },
  { "savelog", "savelog <file>", "Write the console output to a text file" },
  { "set",
    "set <variable> <value>",
    "Create or update an environment variable" },
  { "sysinfo", "sysinfo", "Display system telemetry and statistics" },
  { "timestamps",
    "timestamps [on|off|toggle]",
    "Show or hide output timestamps" },
  { "toggle", "toggle <variable>", "Toggle a boolean environment variable" },
  { "unalias", "unalias <name>", "Remove a command alias" },
  { "unbind", "unbind <key|all>", "Remove a function-key binding" },
  { "unwatch",
    "unwatch <variable|all>",
    "Remove variables from the watch strip" },
  { "vars",
    "vars [filter]",
    "List environment variables, optionally filtered" },
  { "watch", "watch [variable]", "Pin a variable's live value in the console" },
  { "writeconfig",
    "writeconfig <file>",
    "Save aliases, bindings, and watches as an exec script" }
};

const char* const kFunctionKeys[] = { "F1", "F2", "F3", "F4",  "F5",  "F6",
                                      "F7", "F8", "F9", "F10", "F11", "F12" };

// Keys the host or DebugModule consume before bindings run: F3 (FPS), F5
// (asset reload), F6 (profiler), F11 (fullscreen).
const char* const kReservedKeys[] = { "F3", "F5", "F6", "F11" };

constexpr int kMaxExpansionDepth = 8;

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

std::string
trimCopy(const std::string& text)
{
  const std::size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  const std::size_t last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

// Double-quoted with doubled inner quotes, the parser's literal-quote form.
std::string
quoted(const std::string& text)
{
  std::string result = "\"";
  for (const char character : text) {
    if (character == '"') {
      result += '"';
    }
    result += character;
  }
  return result + "\"";
}

std::string
entryText(const CommandLineCore::historyBuffer& entry)
{
  std::string text = entry.content;
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  if (entry.repeatCount > 1) {
    text += "  (x" + std::to_string(entry.repeatCount) + ")";
  }
  return text;
}

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

} // namespace

CommandLineCore::CommandLineCore(IEnvVars* vars,
                                 CommandRegistry* commandRegistry,
                                 const std::string& applicationNameIn)
  : envVars(vars)
  , commandRegistry(commandRegistry)
  , applicationName(applicationNameIn.empty() ? "Illumo" : applicationNameIn)
  , minimumSeverity(kSeverityTrace)
  , timestampsVisible(false)
  , alertsEnabled(true)
  , searchActive(false)
  , searchFailed(false)
  , searchMatchIndex(-1)
  , startTime(std::chrono::steady_clock::now())
  , cursorPosition(0)
  , selectionAnchor(0)
  , historyIndex(0)
  , parseArena(16 * 1024)
  , aliasExpandStack(4 * 1024)
{
  currentInput = "";
  tempInput = "";
  completionHint = "";
  history = {
    { 240, 240, 240, 255, applicationName + " Developer Console" },
    { 240, 240, 240, 255, "Press ` to toggle, type 'help' for commands" }
  };
  historyIndex = 0;
}

std::string
CommandLineCore::DisplayText(const historyBuffer& entry)
{
  return entryText(entry);
}

int
CommandLineCore::severityOf(ConsoleLevel level)
{
  switch (level) {
    case ConsoleLevel::Trace:
      return kSeverityTrace;
    case ConsoleLevel::Warning:
      return kSeverityWarning;
    case ConsoleLevel::Error:
      return kSeverityError;
    default:
      return kSeverityInfo;
  }
}

double
CommandLineCore::getUptimeSeconds() const
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                       startTime)
    .count();
}

std::string
CommandLineCore::FormatTimestamp(double seconds) const
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "[%8.3f]", seconds);
  return buffer;
}

void
CommandLineCore::clearCompletionHint()
{
  completionHint.clear();
  completionMatches.clear();
}

bool
CommandLineCore::isEntryVisible(const historyBuffer& entry) const
{
  if (severityOf(entry.level) < minimumSeverity) {
    return false;
  }
  if (viewFilterLower.empty()) {
    return true;
  }
  return lowerCopy(entry.content).find(viewFilterLower) != std::string::npos;
}

std::size_t
CommandLineCore::countVisibleEntries() const
{
  std::size_t count = 0;
  for (const historyBuffer& entry : history) {
    if (isEntryVisible(entry)) {
      ++count;
    }
  }
  return count;
}

void
CommandLineCore::SetViewFilter(const std::string& text)
{
  viewFilter = text;
  viewFilterLower = lowerCopy(text);
  onViewChanged();
}

void
CommandLineCore::SetMinimumSeverity(int severity)
{
  minimumSeverity = std::clamp(severity, kSeverityTrace, kSeverityError);
  onViewChanged();
}

void
CommandLineCore::setTimestampsVisible(bool visible)
{
  timestampsVisible = visible;
  onViewChanged();
}

bool
CommandLineCore::isBindableKey(const std::string& keyName)
{
  const std::string upper = upperCopy(keyName);
  for (const char* key : kFunctionKeys) {
    if (upper == key) {
      return true;
    }
  }
  return false;
}

bool
CommandLineCore::isReservedKey(const std::string& keyName)
{
  const std::string upper = upperCopy(keyName);
  for (const char* key : kReservedKeys) {
    if (upper == key) {
      return true;
    }
  }
  return false;
}

bool
CommandLineCore::BindKey(const std::string& keyName, const std::string& command)
{
  if (!isBindableKey(keyName) || isReservedKey(keyName) ||
      trimCopy(command).empty()) {
    return false;
  }
  keyBindings[upperCopy(keyName)] = command;
  return true;
}

bool
CommandLineCore::UnbindKey(const std::string& keyName)
{
  return keyBindings.erase(upperCopy(keyName)) > 0;
}

std::string
CommandLineCore::GetKeyBinding(const std::string& keyName) const
{
  std::map<std::string, std::string>::const_iterator it =
    keyBindings.find(upperCopy(keyName));
  return it != keyBindings.end() ? it->second : "";
}

bool
CommandLineCore::HasKeyBinding(const std::string& keyName) const
{
  return keyBindings.find(upperCopy(keyName)) != keyBindings.end();
}

void
CommandLineCore::runCommandChain(const std::string& text)
{
  parseArena.Clear();
  aliasExpandStack.Clear();
  std::vector<std::string> subCommands;
  splitChainInto(text, subCommands);
  for (const std::string& singleCmd : subCommands) {
    ExecuteSingleCommand(singleCmd, 0);
  }
  parseArena.Clear();
  aliasExpandStack.Clear();
}

bool
CommandLineCore::RunKeyBinding(const std::string& keyName)
{
  const std::string command = GetKeyBinding(keyName);
  if (command.empty()) {
    return false;
  }
  runCommandChain(command);
  return true;
}

bool
CommandLineCore::findReverseMatch(int startIndex)
{
  if (searchQuery.empty()) {
    searchMatchIndex = -1;
    searchFailed = false;
    currentInput = searchSavedInput;
    resetCursorToEnd();
    return false;
  }
  const std::string loweredQuery = lowerCopy(searchQuery);
  const int newest = static_cast<int>(commandHistory.size()) - 1;
  for (int i = std::min(startIndex, newest); i >= 0; --i) {
    const std::string& candidate = commandHistory[static_cast<std::size_t>(i)];
    if (lowerCopy(candidate).find(loweredQuery) != std::string::npos) {
      searchMatchIndex = i;
      searchFailed = false;
      currentInput = candidate;
      resetCursorToEnd();
      return true;
    }
  }
  // Keep showing the previous match, like a terminal's failing search.
  searchFailed = true;
  return false;
}

void
CommandLineCore::BeginReverseSearch()
{
  if (!searchActive) {
    searchActive = true;
    searchFailed = false;
    searchMatchIndex = -1;
    searchQuery.clear();
    searchSavedInput = currentInput;
    clearCompletionHint();
    onInputChanged();
    return;
  }
  // Repeated Ctrl+R steps to the next older match.
  if (searchMatchIndex > 0) {
    const int previous = searchMatchIndex;
    if (!findReverseMatch(searchMatchIndex - 1)) {
      searchMatchIndex = previous;
    }
  } else if (!searchQuery.empty()) {
    searchFailed = true;
  }
  onInputChanged();
}

void
CommandLineCore::AcceptReverseSearch()
{
  if (!searchActive) {
    return;
  }
  searchActive = false;
  searchFailed = false;
  searchQuery.clear();
  resetCursorToEnd();
  historyIndex = static_cast<int>(commandHistory.size());
  onInputChanged();
}

void
CommandLineCore::CancelReverseSearch()
{
  if (!searchActive) {
    return;
  }
  searchActive = false;
  searchFailed = false;
  searchQuery.clear();
  currentInput = searchSavedInput;
  resetCursorToEnd();
  onInputChanged();
}

void
CommandLineCore::acceptSearchIfActive()
{
  if (searchActive) {
    AcceptReverseSearch();
  }
}

bool
CommandLineCore::AddWatch(const std::string& name)
{
  const std::string key = findEnvironmentKey(envVars, name);
  if (key.empty()) {
    return false;
  }
  if (std::find(watches.begin(), watches.end(), key) != watches.end()) {
    return true;
  }
  if (watches.size() >= kMaxWatches) {
    return false;
  }
  watches.push_back(key);
  onViewChanged();
  return true;
}

bool
CommandLineCore::RemoveWatch(const std::string& name)
{
  const std::string lowered = lowerCopy(name);
  for (std::vector<std::string>::iterator it = watches.begin();
       it != watches.end();
       ++it) {
    if (lowerCopy(*it) == lowered) {
      watches.erase(it);
      onViewChanged();
      return true;
    }
  }
  return false;
}

void
CommandLineCore::setAlertsEnabled(bool enabled)
{
  alertsEnabled = enabled;
  onViewChanged();
}

std::string
CommandLineCore::BuildConfigText() const
{
  std::string text = "# " + applicationName +
                     " console config. Restore it with: exec <this file>\n";
  std::vector<std::pair<std::string, std::string>> sortedAliases(
    aliases.begin(), aliases.end());
  std::sort(sortedAliases.begin(), sortedAliases.end());
  for (const std::pair<std::string, std::string>& aliasItem : sortedAliases) {
    text += "alias " + aliasItem.first + " " + quoted(aliasItem.second) + "\n";
  }
  for (const std::pair<const std::string, std::string>& binding : keyBindings) {
    text += "bind " + binding.first + " " + quoted(binding.second) + "\n";
  }
  for (const std::string& watched : watches) {
    text += "watch " + watched + "\n";
  }
  if (timestampsVisible) {
    text += "timestamps on\n";
  }
  if (!alertsEnabled) {
    text += "alerts off\n";
  }
  return text;
}

bool
CommandLineCore::WriteConfigFile(const std::string& path,
                                 std::string* resolvedPath) const
{
  if (trimCopy(path).empty()) {
    return false;
  }
#if defined(ILLUMO_SERIAL_GUEST)
  if (resolvedPath != nullptr) {
    *resolvedPath = path;
  }
  return false;
#else
  std::error_code error;
  const std::filesystem::path target =
    std::filesystem::absolute(std::filesystem::path(path), error);
  if (resolvedPath != nullptr) {
    *resolvedPath = error ? path : target.string();
  }
  std::ofstream file(error ? std::filesystem::path(path) : target,
                     std::ios::out | std::ios::trunc);
  if (!file) {
    return false;
  }
  file << BuildConfigText();
  file.flush();
  return static_cast<bool>(file);
#endif
}

std::string
CommandLineCore::BuildLogText(std::size_t lineCount, bool visibleOnly) const
{
  std::vector<const historyBuffer*> selected;
  for (std::size_t i = history.size(); i > 0; --i) {
    const historyBuffer& entry = history[i - 1];
    if (visibleOnly && !isEntryVisible(entry)) {
      continue;
    }
    selected.push_back(&entry);
    if (lineCount > 0 && selected.size() >= lineCount) {
      break;
    }
  }
  std::string text;
  for (std::size_t i = selected.size(); i > 0; --i) {
    if (!text.empty()) {
      text += "\n";
    }
    if (timestampsVisible) {
      text += FormatTimestamp(selected[i - 1]->timeSeconds) + " ";
    }
    text += entryText(*selected[i - 1]);
  }
  return text;
}

bool
CommandLineCore::SaveLogFile(const std::string& path,
                             std::string* resolvedPath) const
{
  if (trimCopy(path).empty()) {
    return false;
  }
#if defined(ILLUMO_SERIAL_GUEST)
  if (resolvedPath != nullptr) {
    *resolvedPath = path;
  }
  return false;
#else
  std::error_code error;
  const std::filesystem::path target =
    std::filesystem::absolute(std::filesystem::path(path), error);
  if (resolvedPath != nullptr) {
    *resolvedPath = error ? path : target.string();
  }
  std::ofstream file(error ? std::filesystem::path(path) : target,
                     std::ios::out | std::ios::trunc);
  if (!file) {
    return false;
  }
  file << applicationName << " console log (" << history.size()
       << " entries)\n";
  for (const historyBuffer& entry : history) {
    file << FormatTimestamp(entry.timeSeconds) << " " << entryText(entry)
         << "\n";
  }
  file.flush();
  return static_cast<bool>(file);
#endif
}

bool
CommandLineCore::ExecuteScriptFile(const std::string& path, int expansionDepth)
{
  if (expansionDepth > kMaxExpansionDepth) {
    logError("exec nesting limit exceeded");
    return false;
  }
#if defined(ILLUMO_SERIAL_GUEST)
  logError("exec is unavailable inside a WASM guest: " + path);
  return false;
#else
  // Scripts are debugging aids, not programs; bound them like `repeat`.
  constexpr std::size_t kMaxScriptLines = 1000;
  std::ifstream file{ std::filesystem::path(path) };
  if (!file) {
    logError("Cannot open script: " + path);
    return false;
  }
  std::vector<std::string> lines;
  std::string line;
  bool truncated = false;
  while (std::getline(file, line)) {
    const std::string trimmed = trimCopy(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }
    if (lines.size() >= kMaxScriptLines) {
      truncated = true;
      break;
    }
    lines.push_back(trimmed);
  }
  if (truncated) {
    logWarning("Script stops after " + std::to_string(kMaxScriptLines) +
               " commands: " + path);
  }
  for (const std::string& scriptLine : lines) {
    AppendEntry(ConsoleLevel::Command, 150, 150, 150, 255, ">> " + scriptLine);
    std::vector<std::string> chained;
    splitChainInto(scriptLine, chained);
    for (const std::string& subCmd : chained) {
      ExecuteSingleCommand(subCmd, expansionDepth + 1);
    }
  }
  logNormal("exec " + path + ": " + std::to_string(lines.size()) +
            " command line(s)");
  return true;
#endif
}

bool
CommandLineCore::CopySelection()
{
  std::string text;
  if (hasSelection()) {
    const std::size_t start = std::min(cursorPosition, selectionAnchor);
    const std::size_t end = std::max(cursorPosition, selectionAnchor);
    text = currentInput.substr(start, end - start);
  } else {
    text = currentInput;
  }
  if (text.empty()) {
    return false;
  }
  return writeClipboard(text);
}

bool
CommandLineCore::CutSelection()
{
  if (!CopySelection()) {
    return false;
  }
  if (hasSelection()) {
    eraseSelection();
    clearCompletionHint();
    onInputChanged();
  } else {
    ClearInput();
  }
  return true;
}

bool
CommandLineCore::Paste()
{
  const std::string text = readClipboard();
  if (text.empty()) {
    return false;
  }
  InsertText(text);
  return true;
}

void
CommandLineCore::InsertText(const std::string& text)
{
  // Line breaks become `;` so a pasted multi-line snippet runs as a chain.
  std::string sanitized;
  bool pendingBreak = false;
  for (const char character : text) {
    if (character == '\r' || character == '\n') {
      pendingBreak = true;
      continue;
    }
    const unsigned char code = static_cast<unsigned char>(character);
    const char normalized = character == '\t' ? ' ' : character;
    if (normalized != ' ' && (code < 32 || code > 126)) {
      continue;
    }
    if (pendingBreak) {
      if (!trimCopy(sanitized).empty()) {
        sanitized += "; ";
      }
      pendingBreak = false;
    }
    sanitized += normalized;
  }
  if (sanitized.empty()) {
    return;
  }
  acceptSearchIfActive();
  eraseSelection();
  const std::size_t capacity = MAX_CHARS_PER_LINE - 1;
  const std::size_t room =
    currentInput.size() < capacity ? capacity - currentInput.size() : 0;
  if (sanitized.size() > room) {
    sanitized.resize(room);
  }
  currentInput.insert(cursorPosition, sanitized);
  cursorPosition += sanitized.size();
  selectionAnchor = cursorPosition;
  clearCompletionHint();
  onInputChanged();
}

void
CommandLineCore::resetCursorToEnd()
{
  cursorPosition = currentInput.size();
  selectionAnchor = cursorPosition;
}

void
CommandLineCore::eraseSelection()
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
CommandLineCore::findPreviousWordBoundary() const
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
CommandLineCore::findNextWordBoundary() const
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
CommandLineCore::AddCharacter(unsigned int codepoint)
{
  if (searchActive) {
    if (codepoint >= 32 && codepoint <= 126 && searchQuery.size() < 128) {
      searchQuery += static_cast<char>(codepoint);
      // A longer query can still match the current entry.
      findReverseMatch(searchMatchIndex >= 0
                         ? searchMatchIndex
                         : static_cast<int>(commandHistory.size()) - 1);
      onInputChanged();
    }
    return;
  }
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
      onInputChanged();
    }
  }
}

void
CommandLineCore::HandleBackspace(bool byWord)
{
  if (searchActive) {
    if (!searchQuery.empty()) {
      searchQuery.pop_back();
      findReverseMatch(static_cast<int>(commandHistory.size()) - 1);
      onInputChanged();
    }
    return;
  }
  if (hasSelection()) {
    eraseSelection();
    clearCompletionHint();
    onInputChanged();
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
  onInputChanged();
}

void
CommandLineCore::HandleDelete(bool byWord)
{
  acceptSearchIfActive();
  if (hasSelection()) {
    eraseSelection();
    clearCompletionHint();
    onInputChanged();
    return;
  }
  if (cursorPosition >= currentInput.size()) {
    return;
  }

  std::size_t eraseTo = byWord ? findNextWordBoundary() : cursorPosition + 1;
  currentInput.erase(cursorPosition, eraseTo - cursorPosition);
  selectionAnchor = cursorPosition;
  clearCompletionHint();
  onInputChanged();
}

void
CommandLineCore::MoveCursorLeft(bool byWord, bool select)
{
  acceptSearchIfActive();
  if (!select && hasSelection()) {
    cursorPosition = std::min(cursorPosition, selectionAnchor);
    selectionAnchor = cursorPosition;
    onInputChanged();
    return;
  }
  std::size_t newPosition = byWord
                              ? findPreviousWordBoundary()
                              : (cursorPosition > 0 ? cursorPosition - 1 : 0);
  if (!select) {
    selectionAnchor = newPosition;
  }
  cursorPosition = newPosition;
  onInputChanged();
}

void
CommandLineCore::MoveCursorRight(bool byWord, bool select)
{
  if (searchActive) {
    AcceptReverseSearch();
    return;
  }
  if (!select && cursorPosition == currentInput.size()) {
    std::string ghost = getGhostSuggestion();
    if (!ghost.empty()) {
      currentInput += ghost;
      cursorPosition = currentInput.size();
      selectionAnchor = cursorPosition;
      onInputChanged();
      return;
    }
  }
  if (!select && hasSelection()) {
    cursorPosition = std::max(cursorPosition, selectionAnchor);
    selectionAnchor = cursorPosition;
    onInputChanged();
    return;
  }
  std::size_t newPosition =
    byWord ? findNextWordBoundary()
           : std::min(cursorPosition + 1, currentInput.size());
  if (!select) {
    selectionAnchor = newPosition;
  }
  cursorPosition = newPosition;
  onInputChanged();
}

void
CommandLineCore::MoveCursorHome(bool select)
{
  acceptSearchIfActive();
  if (!select) {
    selectionAnchor = 0;
  }
  cursorPosition = 0;
  onInputChanged();
}

void
CommandLineCore::MoveCursorEnd(bool select)
{
  acceptSearchIfActive();
  if (!select) {
    selectionAnchor = currentInput.size();
  }
  cursorPosition = currentInput.size();
  onInputChanged();
}

void
CommandLineCore::SelectAll()
{
  acceptSearchIfActive();
  selectionAnchor = 0;
  cursorPosition = currentInput.size();
  onInputChanged();
}

void
CommandLineCore::ClearInput()
{
  searchActive = false;
  searchFailed = false;
  searchQuery.clear();
  currentInput.clear();
  resetCursorToEnd();
  clearCompletionHint();
  onInputChanged();
}

void
CommandLineCore::logNormal(const std::string& str)
{
  AppendEntry(ConsoleLevel::Info, 255, 255, 255, 255, str);
}

void
CommandLineCore::logError(const std::string& str)
{
  AppendEntry(ConsoleLevel::Error, 255, 100, 100, 255, "ERROR: " + str);
}

void
CommandLineCore::logWarning(const std::string& str)
{
  AppendEntry(ConsoleLevel::Warning, 255, 220, 100, 255, "WARNING: " + str);
}

void
CommandLineCore::logSuccess(const std::string& str)
{
  AppendEntry(ConsoleLevel::Success, 100, 255, 100, 255, "SUCCESS: " + str);
}

void
CommandLineCore::logTrace(const std::string& str)
{
  AppendEntry(ConsoleLevel::Trace, 206, 0, 252, 255, "TRACE: " + str);
}

void
CommandLineCore::AppendString(unsigned char r,
                              unsigned char g,
                              unsigned char b,
                              unsigned char a,
                              std::string str)
{
  AppendEntry(ConsoleLevel::Plain, r, g, b, a, std::move(str));
}

void
CommandLineCore::AppendEntry(ConsoleLevel level,
                             unsigned char r,
                             unsigned char g,
                             unsigned char b,
                             unsigned char a,
                             std::string str)
{
  const double now = getUptimeSeconds();
  // Collapse spam: an identical consecutive line bumps the previous entry.
  if (!history.empty() && history.back().level == level &&
      history.back().content == str && history.back().r == r &&
      history.back().g == g && history.back().b == b && history.back().a == a) {
    ++history.back().repeatCount;
    history.back().timeSeconds = now;
    onHistoryBackUpdated();
    return;
  }
  historyBuffer entry{ r, g, b, a, std::move(str) };
  entry.level = level;
  entry.timeSeconds = now;
  history.push_back(std::move(entry));
  bool erasedFront = false;
  if (history.size() > MAX_CONSOLE_LINES) {
    history.erase(history.begin());
    erasedFront = true;
  }
  onHistoryAppended(history.back(), erasedFront);
}

void
CommandLineCore::AppendStringLn(unsigned char r,
                                unsigned char g,
                                unsigned char b,
                                unsigned char a,
                                std::string str)
{
  AppendString(r, g, b, a, str + "\n");
}

void
CommandLineCore::ClearHistory()
{
  history.clear();
  historyBuffer heading{
    240, 240, 240, 255, applicationName + " Developer Console"
  };
  heading.timeSeconds = getUptimeSeconds();
  history.push_back(std::move(heading));
  onHistoryCleared();
}

bool
CommandLineCore::parseArgsInto(const std::string& text,
                               std::vector<std::string>& outArgs) const
{
  outArgs = ParseCommandArgs(text, " \t");
  return true;
}

bool
CommandLineCore::splitChainInto(const std::string& text,
                                std::vector<std::string>& outCommands) const
{
  outCommands = SplitCommandChain(text);
  return true;
}

std::vector<std::string>
CommandLineCore::ParseCommandArgs(const std::string& text,
                                  const std::string& delim) const
{
  std::vector<std::string> args;
  std::string currentArg;
  char quote = '\0';
  bool tokenStarted = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char character = text[i];
    // Backslashes are path separators unless escaping console punctuation.
    // Inside quotes, doubled matching quotes encode a literal quote so a
    // trailing Windows separator never consumes the closing quote.
    const bool windowsPath = (currentArg.size() > 1 && currentArg[1] == ':') ||
                             currentArg.starts_with("\\\\");
    if (character == '\\' && quote == '\0' && !windowsPath &&
        i + 1 < text.size() &&
        (delim.find(text[i + 1]) != std::string::npos || text[i + 1] == ';' ||
         text[i + 1] == '\'' || text[i + 1] == '"')) {
      currentArg += text[++i];
      tokenStarted = true;
      continue;
    }
    if (quote != '\0') {
      if (character == quote) {
        if (i + 1 < text.size() && text[i + 1] == quote) {
          currentArg += text[++i];
        } else {
          quote = '\0';
        }
      } else {
        currentArg += character;
      }
      tokenStarted = true;
    } else if (character == '\'' || character == '"') {
      quote = character;
      tokenStarted = true;
    } else if (delim.find(character) != std::string::npos) {
      if (tokenStarted) {
        args.push_back(currentArg);
        currentArg.clear();
        tokenStarted = false;
      }
    } else {
      currentArg += character;
      tokenStarted = true;
    }
  }
  if (tokenStarted) {
    args.push_back(currentArg);
  }
  return args;
}

std::vector<std::string>
CommandLineCore::SplitCommandChain(const std::string& text) const
{
  std::vector<std::string> commands;
  std::string currentCmd;
  std::string token;
  char quote = '\0';
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char character = text[i];
    const bool windowsPath =
      (token.size() > 1 && token[1] == ':') || token.starts_with("\\\\");
    if (character == '\\' && quote == '\0' && !windowsPath &&
        i + 1 < text.size() &&
        (text[i + 1] == ';' || text[i + 1] == '\'' || text[i + 1] == '"' ||
         text[i + 1] == ' ' || text[i + 1] == '\t')) {
      currentCmd += character;
      currentCmd += text[++i];
      token += text[i];
      continue;
    }
    if (quote != '\0') {
      if (character == quote) {
        if (i + 1 < text.size() && text[i + 1] == quote) {
          currentCmd += character;
          currentCmd += text[++i];
          token += character;
          continue;
        }
        quote = '\0';
      } else {
        token += character;
      }
    } else if (character == '\'' || character == '"') {
      quote = character;
    } else if (character == ';') {
      if (currentCmd.find_first_not_of(" \t\r\n") != std::string::npos) {
        commands.push_back(currentCmd);
      }
      currentCmd.clear();
      token.clear();
      continue;
    } else if (character == ' ' || character == '\t') {
      token.clear();
    } else {
      token += character;
    }
    currentCmd += character;
  }
  if (currentCmd.find_first_not_of(" \t\r\n") != std::string::npos) {
    commands.push_back(currentCmd);
  }
  return commands;
}

void
CommandLineCore::SetAlias(const std::string& name, const std::string& expansion)
{
  if (!name.empty()) {
    aliases[lowerCopy(name)] = expansion;
  }
}

void
CommandLineCore::RemoveAlias(const std::string& name)
{
  aliases.erase(lowerCopy(name));
}

bool
CommandLineCore::HasAlias(const std::string& name) const
{
  return aliases.find(lowerCopy(name)) != aliases.end();
}

std::string
CommandLineCore::GetAlias(const std::string& name) const
{
  std::unordered_map<std::string, std::string>::const_iterator it =
    aliases.find(lowerCopy(name));
  if (it != aliases.end()) {
    return it->second;
  }
  return "";
}

std::string
CommandLineCore::getGhostSuggestion() const
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
CommandLineCore::getParameterHint(const std::string& inputLine) const
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
CommandLineCore::getCompletionCandidates(const std::string& leadingText) const
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
    if (envVars != nullptr) {
      const std::unordered_map<std::string, EnvVar>& vars = envVars->getVars();
      for (const std::pair<const std::string, EnvVar>& variable : vars) {
        candidates.push_back(variable.first);
      }
    }
    for (const std::pair<const std::string, std::string>& aliasItem : aliases) {
      candidates.push_back(aliasItem.first);
    }
  } else {
    const std::string command = lowerCopy(leadingArgs[0]);
    if (commandRegistry != nullptr && commandRegistry->HasCommand(command)) {
      candidates = commandRegistry->GetCommandCompletions(command);
    } else if (command == "unwatch") {
      candidates = watches;
      candidates.push_back("all");
    } else if ((command == "watch" || command == "add" || command == "cycle") &&
               leadingArgs.size() == 1) {
      if (envVars != nullptr) {
        for (const std::pair<const std::string, EnvVar>& variable :
             envVars->getVars()) {
          candidates.push_back(variable.first);
        }
      }
    } else if (command == "get" || command == "set" || command == "toggle" ||
               command == "vars") {
      if (envVars != nullptr) {
        const std::unordered_map<std::string, EnvVar>& vars =
          envVars->getVars();
        for (const std::pair<const std::string, EnvVar>& variable : vars) {
          candidates.push_back(variable.first);
        }
      }
    } else if (command == "fps" || command == "memory" ||
               command == "fullscreen" || command == "timestamps" ||
               command == "alerts") {
      candidates = { "off", "on", "toggle" };
    } else if (command == "loglevel") {
      candidates = { "error", "info", "trace", "warning" };
    } else if (command == "filter") {
      candidates = { "off" };
    } else if (command == "copy") {
      candidates = { "all" };
    } else if ((command == "bind" || command == "unbind") &&
               leadingArgs.size() == 1) {
      for (const char* key : kFunctionKeys) {
        if (!isReservedKey(key)) {
          candidates.push_back(key);
        }
      }
      if (command == "unbind") {
        candidates.push_back("all");
      }
    } else if (command == "help" && leadingArgs.size() == 1) {
      for (const BuiltInCommandHelp& builtIn : kBuiltInCommands) {
        candidates.push_back(builtIn.name);
      }
      if (commandRegistry != nullptr) {
        std::vector<std::string> registeredCommands =
          commandRegistry->GetCommandNames();
        candidates.insert(candidates.end(),
                          registeredCommands.begin(),
                          registeredCommands.end());
      }
    }
  }

  std::sort(candidates.begin(), candidates.end());
  candidates.erase(std::unique(candidates.begin(), candidates.end()),
                   candidates.end());
  return candidates;
}

void
CommandLineCore::Complete()
{
  acceptSearchIfActive();
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

  completionMatches.clear();
  if (matches.empty()) {
    completionHint = "No completion matches '" + prefix + "'";
    onInputChanged();
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
    onInputChanged();
    return;
  }

  completionHint = "Matches: ";
  const std::size_t visibleMatches =
    std::min(matches.size(), static_cast<std::size_t>(6));
  for (std::size_t i = 0; i < visibleMatches; ++i) {
    if (i > 0) {
      completionHint += "  ";
    }
    completionHint += matches[i];
  }
  if (matches.size() > visibleMatches) {
    completionHint += "  ...";
  }
  completionMatches = matches;
  onInputChanged();
}

void
CommandLineCore::ExecuteCommand()
{
  acceptSearchIfActive();
  if (currentInput.empty()) {
    return;
  }

  std::string line = currentInput;
  if (trimCopy(line).rfind('!', 0) == 0) {
    std::string expanded;
    if (!expandHistoryReference(trimCopy(line), &expanded)) {
      ClearInput();
      return;
    }
    line = expanded;
  }

  AddToHistory(line);
  ClearInput();
  runCommandChain(line);
}

bool
CommandLineCore::expandHistoryReference(const std::string& input,
                                        std::string* expanded)
{
  // `!!` repeats the last command, `!<n>` the numbered entry shown by
  // `history`, `!<prefix>` the newest command starting with prefix. Text after
  // the reference is appended as extra arguments.
  const std::size_t split = input.find_first_of(" \t");
  const std::string reference = input.substr(0, split);
  const std::string rest =
    split == std::string::npos ? "" : trimCopy(input.substr(split));
  if (commandHistory.empty()) {
    logError("No command history for " + reference);
    return false;
  }
  std::string resolved;
  if (reference == "!!") {
    resolved = commandHistory.back();
  } else if (reference.size() > 1) {
    const std::string key = reference.substr(1);
    long index = 0;
    if (parseLongStrict(key, &index)) {
      if (index < 1 || index > static_cast<long>(commandHistory.size())) {
        logError("History entry out of range: " + reference);
        return false;
      }
      resolved = commandHistory[static_cast<std::size_t>(index - 1)];
    } else {
      const std::string loweredKey = lowerCopy(key);
      for (std::size_t i = commandHistory.size(); i > 0; --i) {
        if (lowerCopy(commandHistory[i - 1]).rfind(loweredKey, 0) == 0) {
          resolved = commandHistory[i - 1];
          break;
        }
      }
      if (resolved.empty()) {
        logError("No command in history starts with '" + key + "'");
        return false;
      }
    }
  } else {
    logError("Usage: !! | !<number> | !<prefix>");
    return false;
  }
  *expanded = rest.empty() ? resolved : resolved + " " + rest;
  return true;
}

void
CommandLineCore::logMatchingCommands(const std::string& needle, bool* anyMatch)
{
  const std::string loweredNeedle = lowerCopy(needle);
  std::vector<std::string> lines;
  for (const BuiltInCommandHelp& command : kBuiltInCommands) {
    if (lowerCopy(command.name).find(loweredNeedle) != std::string::npos ||
        lowerCopy(command.description).find(loweredNeedle) !=
          std::string::npos) {
      lines.push_back("  " + std::string(command.usage) + " - " +
                      command.description);
    }
  }
  if (commandRegistry != nullptr) {
    for (const std::string& name : commandRegistry->GetCommandNames()) {
      std::string usage = commandRegistry->GetCommandUsage(name);
      const std::string description =
        commandRegistry->GetCommandDescription(name);
      if (lowerCopy(name).find(loweredNeedle) == std::string::npos &&
          lowerCopy(description).find(loweredNeedle) == std::string::npos) {
        continue;
      }
      if (usage.empty()) {
        usage = name;
      }
      lines.push_back("  " + usage +
                      (description.empty() ? "" : " - " + description));
    }
  }
  if (anyMatch != nullptr) {
    *anyMatch = !lines.empty();
  }
  if (lines.empty()) {
    return;
  }
  logNormal("Commands matching '" + needle + "':");
  for (const std::string& line : lines) {
    logNormal(line);
  }
}

void
CommandLineCore::ExecuteSingleCommand(const std::string& singleCmd,
                                      int expansionDepth)
{
  if (expansionDepth > kMaxExpansionDepth) {
    logError("Alias expansion depth limit exceeded");
    return;
  }

  std::vector<std::string> commandParts;
  parseArgsInto(singleCmd, commandParts);
  if (commandParts.empty()) {
    return;
  }

  if (expansionDepth == 0) {
    AppendEntry(ConsoleLevel::Command, 100, 200, 255, 255, "> " + singleCmd);
  }

  const std::string rawCommand = commandParts[0];
  const std::string cmd = lowerCopy(rawCommand);
  std::vector<std::string> args(commandParts.begin() + 1, commandParts.end());

  if (HasAlias(cmd)) {
    std::string expanded = GetAlias(cmd);
    if (!args.empty()) {
      expanded += " " + joinArguments(args, 0);
    }
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
    int winDims[2] = { 0, 0 };
    queryWindowDimensions(&winDims[0], &winDims[1]);
    logNormal("Window resolution:   " + std::to_string(winDims[0]) + "x" +
              std::to_string(winDims[1]));
    logNormal("FPS overlay:         " +
              std::string(envVars && envVars->getVar("showFPS").valueAsBool
                            ? "on"
                            : "off"));
    logNormal("Memory overlay:      " +
              std::string(envVars && envVars->getVar("showMemory").valueAsBool
                            ? "on"
                            : "off"));
    logNormal("Console lines:       " + std::to_string(history.size()) + "/" +
              std::to_string(MAX_CONSOLE_LINES) + " (" +
              std::to_string(countVisibleEntries()) + " visible)");
    logNormal("Key bindings:        " + std::to_string(keyBindings.size()));
    char uptime[32];
    std::snprintf(uptime, sizeof(uptime), "%.1f s", getUptimeSeconds());
    logNormal("Console uptime:      " + std::string(uptime));
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
      logNormal("Use 'help <command>' for one command, or 'help <word>' to "
                "search.");
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
        bool anyMatch = false;
        logMatchingCommands(args[0], &anyMatch);
        if (!anyMatch) {
          logError("No help available for '" + args[0] + "'");
        }
      }
    }
  } else if (cmd == "clear") {
    ClearHistory();
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
    if (envVars != nullptr) {
      const std::unordered_map<std::string, EnvVar>& variables =
        envVars->getVars();
      for (const std::pair<const std::string, EnvVar>& variable : variables) {
        if (filter.empty() ||
            lowerCopy(variable.first).find(filter) != std::string::npos) {
          variableLines.push_back(variable.first + " = " +
                                  variable.second.value);
        }
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
  } else if (cmd == "fps" || cmd == "memory") {
    const char* variable = cmd == "fps" ? "showFPS" : "showMemory";
    const char* label = cmd == "fps" ? "FPS overlay: " : "Memory overlay: ";
    const bool currentValue =
      envVars ? envVars->getVar(variable).valueAsBool : false;
    if (args.empty()) {
      logNormal(std::string(label) + (currentValue ? "on" : "off"));
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
        logError("Usage: " + cmd + " [on|off|toggle]");
      } else {
        if (envVars != nullptr) {
          envVars->setVar(variable, requestedValue);
        }
        logSuccess(std::string(label) + (requestedValue ? "on" : "off"));
      }
    }
  } else if (cmd == "fullscreen") {
    const bool currentValue =
      envVars ? envVars->getVar("fullscreen").valueAsBool : false;
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
        onToggleFullscreen();
      }
      if (envVars != nullptr) {
        envVars->setVar("fullscreen", requestedValue);
      }
      logSuccess(std::string("Fullscreen: ") + (requestedValue ? "on" : "off"));
    }
  } else if (cmd == "filter") {
    const std::string text = joinArguments(args, 0);
    if (args.empty()) {
      logNormal(viewFilter.empty() ? "Output filter: off"
                                   : "Output filter: \"" + viewFilter + "\"");
    } else if (args.size() == 1 && lowerCopy(args[0]) == "off") {
      SetViewFilter("");
      logSuccess("Output filter cleared");
    } else {
      SetViewFilter(text);
      logSuccess("Output filter: \"" + text + "\" (" +
                 std::to_string(countVisibleEntries()) + " matching lines)");
    }
  } else if (cmd == "loglevel") {
    static const char* const kSeverityNames[] = {
      "trace", "info", "warning", "error"
    };
    if (args.empty()) {
      logNormal(std::string("Log level: ") + kSeverityNames[minimumSeverity]);
    } else {
      const std::string requested = lowerCopy(args[0]);
      int severity = -1;
      if (requested == "trace" || requested == "all") {
        severity = kSeverityTrace;
      } else if (requested == "info") {
        severity = kSeverityInfo;
      } else if (requested == "warning" || requested == "warn") {
        severity = kSeverityWarning;
      } else if (requested == "error") {
        severity = kSeverityError;
      }
      if (severity < 0 || args.size() != 1) {
        logError("Usage: loglevel [trace|info|warning|error]");
      } else {
        // The status bar reports the active level, so this confirmation may
        // itself be hidden by a stricter level.
        logSuccess(std::string("Log level: ") + kSeverityNames[severity]);
        SetMinimumSeverity(severity);
      }
    }
  } else if (cmd == "timestamps") {
    bool requested = !timestampsVisible;
    const bool valid =
      args.empty() || (args.size() == 1 && lowerCopy(args[0]) == "toggle") ||
      (args.size() == 1 && parseBoolValue(args[0], &requested));
    if (!valid) {
      logError("Usage: timestamps [on|off|toggle]");
    } else {
      setTimestampsVisible(requested);
      logSuccess(std::string("Timestamps: ") + (requested ? "on" : "off"));
    }
  } else if (cmd == "copy") {
    long lineCount = 0;
    const bool all = !args.empty() && lowerCopy(args[0]) == "all";
    if (args.size() > 1 ||
        (!args.empty() && !all &&
         (!parseLongStrict(args[0], &lineCount) || lineCount < 1))) {
      logError("Usage: copy [<lines>|all]");
    } else {
      const std::string text =
        BuildLogText(static_cast<std::size_t>(lineCount), true);
      if (writeClipboard(text)) {
        const std::size_t copiedLines =
          text.empty() ? 0
                       : static_cast<std::size_t>(
                           std::count(text.begin(), text.end(), '\n') + 1);
        logSuccess("Copied " + std::to_string(copiedLines) +
                   " line(s) to the clipboard");
      } else {
        logError("Clipboard is unavailable");
      }
    }
  } else if (cmd == "savelog") {
    if (args.size() != 1) {
      logError("Usage: savelog <file>");
    } else {
      std::string resolved;
      if (SaveLogFile(args[0], &resolved)) {
        logSuccess("Console log written to " + resolved);
      } else {
        logError("Could not write console log to " +
                 (resolved.empty() ? args[0] : resolved));
      }
    }
  } else if (cmd == "exec") {
    if (args.size() != 1) {
      logError("Usage: exec <file>");
    } else {
      ExecuteScriptFile(args[0], expansionDepth);
    }
  } else if (cmd == "bind") {
    if (args.empty()) {
      if (keyBindings.empty()) {
        logNormal("No key bindings. Usage: bind <F1-F12> <command>");
      }
      for (const std::pair<const std::string, std::string>& binding :
           keyBindings) {
        logNormal("  " + binding.first + " = \"" + binding.second + "\"");
      }
    } else if (!isBindableKey(args[0])) {
      logError("Only F1-F12 can be bound: " + args[0]);
    } else if (args.size() == 1) {
      const std::string bound = GetKeyBinding(args[0]);
      logNormal(upperCopy(args[0]) + " = " +
                (bound.empty() ? "(unbound)" : "\"" + bound + "\""));
    } else if (isReservedKey(args[0])) {
      logError(upperCopy(args[0]) +
               " is reserved by the host (F3 FPS, F5 reload, F6 profiler, "
               "F11 fullscreen)");
    } else {
      const std::string command = joinArguments(args, 1);
      BindKey(args[0], command);
      logSuccess(upperCopy(args[0]) + " bound to: " + command);
    }
  } else if (cmd == "unbind") {
    if (args.size() != 1) {
      logError("Usage: unbind <key|all>");
    } else if (lowerCopy(args[0]) == "all") {
      const std::size_t removed = keyBindings.size();
      keyBindings.clear();
      logSuccess("Removed " + std::to_string(removed) + " key binding(s)");
    } else if (UnbindKey(args[0])) {
      logSuccess(upperCopy(args[0]) + " unbound");
    } else {
      logError("No binding for " + args[0]);
    }
  } else if (cmd == "watch") {
    if (args.empty()) {
      if (watches.empty()) {
        logNormal("No watches. Usage: watch <variable>");
      }
      for (const std::string& watched : watches) {
        logNormal("  " + watched + " = " + envVars->getVar(watched).value);
      }
    } else if (args.size() != 1) {
      logError("Usage: watch [variable]");
    } else if (findEnvironmentKey(envVars, args[0]).empty()) {
      logError("Unknown variable: " + args[0]);
    } else if (!AddWatch(args[0])) {
      logError("At most " + std::to_string(kMaxWatches) +
               " variables can be watched");
    } else {
      logSuccess("Watching " + findEnvironmentKey(envVars, args[0]));
    }
  } else if (cmd == "unwatch") {
    if (args.size() != 1) {
      logError("Usage: unwatch <variable|all>");
    } else if (lowerCopy(args[0]) == "all") {
      const std::size_t removed = watches.size();
      watches.clear();
      onViewChanged();
      logSuccess("Removed " + std::to_string(removed) + " watch(es)");
    } else if (RemoveWatch(args[0])) {
      logSuccess("Stopped watching " + args[0]);
    } else {
      logError("Not watching " + args[0]);
    }
  } else if (cmd == "add") {
    const std::string key =
      args.empty() ? "" : findEnvironmentKey(envVars, args[0]);
    double current = 0.0;
    double amount = 0.0;
    bool valid = args.size() == 2 && !key.empty();
    if (valid) {
      try {
        std::size_t consumed = 0;
        amount = std::stod(args[1], &consumed);
        valid = consumed == args[1].size();
        const std::string& value = envVars->getVar(key).value;
        current = value.empty() ? 0.0 : std::stod(value, &consumed);
        valid = valid && (value.empty() || consumed == value.size());
      } catch (...) {
        valid = false;
      }
    }
    if (args.size() == 2 && key.empty()) {
      logError("Unknown variable: " + args[0]);
    } else if (!valid) {
      logError("Usage: add <variable> <amount> (numeric variables only)");
    } else {
      // Whole-number variables stay whole so integer readers keep working.
      const std::string& value = envVars->getVar(key).value;
      const bool integral = value.find_first_of(".eE") == std::string::npos &&
                            args[1].find_first_of(".eE") == std::string::npos;
      const double result = current + amount;
      std::string text;
      if (integral) {
        text = std::to_string(static_cast<long long>(std::llround(result)));
      } else {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%g", result);
        text = buffer;
      }
      envVars->setVar(key, text);
      logSuccess(key + " = " + text);
      if (isRestartRequiredVariable(key)) {
        logWarning("Note: Changes to '" + key +
                   "' will take effect after restarting the application.");
      }
    }
  } else if (cmd == "cycle") {
    if (args.size() < 3) {
      logError("Usage: cycle <variable> <value> <value> [...]");
    } else {
      std::string key = findEnvironmentKey(envVars, args[0]);
      if (key.empty()) {
        key = args[0];
      }
      const std::string current = lowerCopy(envVars->getVar(key).value);
      std::size_t next = 1;
      for (std::size_t i = 1; i < args.size(); ++i) {
        if (lowerCopy(args[i]) == current) {
          next = i + 1 < args.size() ? i + 1 : 1;
          break;
        }
      }
      envVars->setVar(key, args[next]);
      logSuccess(key + " = " + args[next]);
      if (isRestartRequiredVariable(key)) {
        logWarning("Note: Changes to '" + key +
                   "' will take effect after restarting the application.");
      }
    }
  } else if (cmd == "alerts") {
    bool requested = !alertsEnabled;
    const bool valid =
      args.empty() || (args.size() == 1 && lowerCopy(args[0]) == "toggle") ||
      (args.size() == 1 && parseBoolValue(args[0], &requested));
    if (!valid) {
      logError("Usage: alerts [on|off|toggle]");
    } else {
      setAlertsEnabled(requested);
      logSuccess(std::string("Closed-console alerts: ") +
                 (requested ? "on" : "off"));
    }
  } else if (cmd == "writeconfig") {
    if (args.size() != 1) {
      logError("Usage: writeconfig <file>");
    } else {
      std::string resolved;
      if (WriteConfigFile(args[0], &resolved)) {
        logSuccess("Console config written to " + resolved +
                   " (restore with exec)");
      } else {
        logError("Could not write console config to " +
                 (resolved.empty() ? args[0] : resolved));
      }
    }
  } else if (cmd == "close") {
    onCloseRequested();
  } else if (cmd == "quit") {
    onQuitRequested();
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
CommandLineCore::AddToHistory(std::string command)
{
  commandHistory.push_back(command);
  if (commandHistory.size() > MAX_CMD_HISTORY) {
    commandHistory.erase(commandHistory.begin());
  }
  historyIndex = static_cast<int>(commandHistory.size());
  tempInput = "";
  resetCursorToEnd();
}

void
CommandLineCore::HistoryDown()
{
  acceptSearchIfActive();
  if (commandHistory.empty()) {
    return;
  }
  if (historyIndex < static_cast<int>(commandHistory.size())) {
    historyIndex++;
    if (historyIndex == static_cast<int>(commandHistory.size())) {
      currentInput = tempInput;
    } else {
      currentInput = commandHistory[historyIndex];
    }
    resetCursorToEnd();
    onInputChanged();
  }
}

void
CommandLineCore::HistoryUp()
{
  acceptSearchIfActive();
  if (commandHistory.empty()) {
    return;
  }
  if (historyIndex > 0) {
    if (historyIndex == static_cast<int>(commandHistory.size())) {
      tempInput = currentInput;
    }
    historyIndex--;
    currentInput = commandHistory[historyIndex];
    resetCursorToEnd();
    onInputChanged();
  }
}
