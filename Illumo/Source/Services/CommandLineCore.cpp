#include <Illumo/Services/CommandLineCore.h>
#include <Illumo/Services/Logger.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
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

void
CommandLineCore::clearCompletionHint()
{
  completionHint.clear();
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
  while (position > 0 &&
         !std::isspace(
           static_cast<unsigned char>(currentInput[position - 1]))) {
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
  if (!select) {
    selectionAnchor = 0;
  }
  cursorPosition = 0;
  onInputChanged();
}

void
CommandLineCore::MoveCursorEnd(bool select)
{
  if (!select) {
    selectionAnchor = currentInput.size();
  }
  cursorPosition = currentInput.size();
  onInputChanged();
}

void
CommandLineCore::SelectAll()
{
  selectionAnchor = 0;
  cursorPosition = currentInput.size();
  onInputChanged();
}

void
CommandLineCore::ClearInput()
{
  currentInput.clear();
  resetCursorToEnd();
  clearCompletionHint();
  onInputChanged();
}

void
CommandLineCore::logNormal(const std::string& str)
{
  AppendString(255, 255, 255, 255, str);
}

void
CommandLineCore::logError(const std::string& str)
{
  AppendString(255, 100, 100, 255, "ERROR: " + str);
}

void
CommandLineCore::logWarning(const std::string& str)
{
  AppendString(255, 220, 100, 255, "WARNING: " + str);
}

void
CommandLineCore::logSuccess(const std::string& str)
{
  AppendString(100, 255, 100, 255, "SUCCESS: " + str);
}

void
CommandLineCore::logTrace(const std::string& str)
{
  AppendString(206, 0, 252, 255, "TRACE: " + str);
}

void
CommandLineCore::AppendString(unsigned char r,
                              unsigned char g,
                              unsigned char b,
                              unsigned char a,
                              std::string str)
{
  history.push_back({ r, g, b, a, str });
  bool erasedFront = false;
  if (history.size() > MAX_CMD_HISTORY) {
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
  history.push_back(
    { 240, 240, 240, 255, applicationName + " Developer Console" });
  onHistoryCleared();
}

bool
CommandLineCore::parseArgsInto(const std::string& text,
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
CommandLineCore::splitChainInto(const std::string& text,
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
CommandLineCore::ParseCommandArgs(const std::string& text,
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
CommandLineCore::SplitCommandChain(const std::string& text) const
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
  while (tokenStart > 0 &&
         !std::isspace(
           static_cast<unsigned char>(currentInput[tokenStart - 1]))) {
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
    } else if (command == "get" || command == "set" || command == "toggle" ||
               command == "vars") {
      if (envVars != nullptr) {
        const std::unordered_map<std::string, EnvVar>& vars =
          envVars->getVars();
        for (const std::pair<const std::string, EnvVar>& variable : vars) {
          candidates.push_back(variable.first);
        }
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
CommandLineCore::Complete()
{
  parseArena.Clear();

  std::size_t tokenStart = cursorPosition;
  while (tokenStart > 0 &&
         !std::isspace(
           static_cast<unsigned char>(currentInput[tokenStart - 1]))) {
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
  onInputChanged();
}

void
CommandLineCore::ExecuteCommand()
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

  for (const std::string& singleCmd : subCommands) {
    ExecuteSingleCommand(singleCmd, 0);
  }

  parseArena.Clear();
  aliasExpandStack.Clear();
}

void
CommandLineCore::ExecuteSingleCommand(const std::string& singleCmd,
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
  } else if (cmd == "fps") {
    const bool currentValue =
      envVars ? envVars->getVar("showFPS").valueAsBool : false;
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
        if (envVars != nullptr) {
          envVars->setVar("showFPS", requestedValue);
        }
        logSuccess(std::string("FPS overlay: ") +
                   (requestedValue ? "on" : "off"));
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
