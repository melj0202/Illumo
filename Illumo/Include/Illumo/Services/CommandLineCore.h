#pragma once

#include <Illumo/Services/ArenaAlloc.h>
#include <Illumo/Services/ChainedStackAlloc.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/IEnvVars.h>

#include <chrono>
#include <cstddef>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#define MAX_CHARS_PER_LINE 1024
#define MAX_CMD_HISTORY 256
#define MAX_CONSOLE_LINES 2048

// Output severity. The view filter compares severityOf() values; Plain,
// Command, Info, and Success share the informational tier.
enum class ConsoleLevel : unsigned char
{
  Plain = 0,
  Command = 1,
  Info = 2,
  Success = 3,
  Warning = 4,
  Error = 5,
  Trace = 6
};

class CommandLineCore
{
public:
  struct historyBuffer
  {
    unsigned char r, g, b, a;
    std::string content;
    ConsoleLevel level = ConsoleLevel::Plain;
    // Seconds since the console was constructed (last repeat for collapsed
    // entries).
    double timeSeconds = 0.0;
    // Consecutive identical lines collapse into one entry with a count.
    unsigned int repeatCount = 1;
  };

  static constexpr int kSeverityTrace = 0;
  static constexpr int kSeverityInfo = 1;
  static constexpr int kSeverityWarning = 2;
  static constexpr int kSeverityError = 3;
  static int severityOf(ConsoleLevel level);
  // Entry text as displayed: trailing line breaks removed, repeat count added.
  static std::string DisplayText(const historyBuffer& entry);

  CommandLineCore(IEnvVars* vars,
                  CommandRegistry* commandRegistry,
                  const std::string& applicationName = "Illumo");
  virtual ~CommandLineCore() = default;

  // Text input editing
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

  // Clipboard editing. Copy/Cut act on the selection, or the whole input when
  // nothing is selected. Pasted line breaks become command separators.
  bool CopySelection();
  bool CutSelection();
  bool Paste();
  void InsertText(const std::string& text);

  // Execution & history
  void ExecuteCommand();
  void ExecuteSingleCommand(const std::string& singleCmd,
                            int expansionDepth = 0);
  void HistoryUp();
  void HistoryDown();
  void AddToHistory(std::string command);
  void ClearHistory();
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
  void AppendEntry(ConsoleLevel level,
                   unsigned char r,
                   unsigned char g,
                   unsigned char b,
                   unsigned char a,
                   std::string str);

  // Logging shortcuts
  void logNormal(const std::string& str);
  void logError(const std::string& str);
  void logWarning(const std::string& str);
  void logSuccess(const std::string& str);
  void logTrace(const std::string& str);

  // Parsing & chaining
  std::vector<std::string> ParseCommandArgs(const std::string& text,
                                            const std::string& delim) const;
  std::vector<std::string> SplitCommandChain(const std::string& text) const;

  // Aliases
  void SetAlias(const std::string& name, const std::string& expansion);
  void RemoveAlias(const std::string& name);
  bool HasAlias(const std::string& name) const;
  std::string GetAlias(const std::string& name) const;
  const std::unordered_map<std::string, std::string>& GetAliases() const
  {
    return aliases;
  }

  // Output view filters. Hidden entries stay in the buffer; only the view
  // changes, so clearing a filter restores them.
  void SetViewFilter(const std::string& text);
  void SetMinimumSeverity(int severity);
  const std::string& getViewFilter() const { return viewFilter; }
  int getMinimumSeverity() const { return minimumSeverity; }
  bool isViewFiltered() const
  {
    return !viewFilter.empty() || minimumSeverity > kSeverityTrace;
  }
  bool isEntryVisible(const historyBuffer& entry) const;
  std::size_t countVisibleEntries() const;
  void setTimestampsVisible(bool visible);
  bool getTimestampsVisible() const { return timestampsVisible; }

  // Function-key bindings (F1-F12). Host-reserved keys are rejected.
  bool BindKey(const std::string& keyName, const std::string& command);
  bool UnbindKey(const std::string& keyName);
  std::string GetKeyBinding(const std::string& keyName) const;
  bool HasKeyBinding(const std::string& keyName) const;
  // Runs the bound command as if typed; false when the key is unbound.
  bool RunKeyBinding(const std::string& keyName);
  const std::map<std::string, std::string>& GetKeyBindings() const
  {
    return keyBindings;
  }
  static bool isBindableKey(const std::string& keyName);
  static bool isReservedKey(const std::string& keyName);

  // Reverse incremental history search (Ctrl+R). While active, typed
  // characters edit the query and the input mirrors the newest match.
  void BeginReverseSearch();
  void AcceptReverseSearch();
  void CancelReverseSearch();
  bool isReverseSearchActive() const { return searchActive; }
  bool isReverseSearchFailing() const { return searchFailed; }
  const std::string& getReverseSearchQuery() const { return searchQuery; }

  // Environment variables pinned to the console's live watch strip.
  static constexpr std::size_t kMaxWatches = 8;
  bool AddWatch(const std::string& name);
  bool RemoveWatch(const std::string& name);
  const std::vector<std::string>& GetWatches() const { return watches; }

  // Closed-console error/warning counter; the UI decides how to show it.
  void setAlertsEnabled(bool enabled);
  bool getAlertsEnabled() const { return alertsEnabled; }

  // Aliases, bindings, watches, and view settings as an exec-able script.
  std::string BuildConfigText() const;
  bool WriteConfigFile(const std::string& path,
                       std::string* resolvedPath) const;

  // Runs one command per line; blank lines and '#' comments are skipped.
  bool ExecuteScriptFile(const std::string& path, int expansionDepth = 0);
  // Writes the complete buffer (ignoring view filters) with timestamps.
  bool SaveLogFile(const std::string& path, std::string* resolvedPath) const;
  // Plain text of the last `lineCount` visible entries (0 = all visible).
  std::string BuildLogText(std::size_t lineCount, bool visibleOnly) const;
  std::string FormatTimestamp(double seconds) const;

  // Inspection
  std::string getGhostSuggestion() const;
  const std::string& getCurrentInput() const { return currentInput; }
  const std::string& getCompletionHint() const { return completionHint; }
  // Every match from the last ambiguous Tab, for a completion list.
  const std::vector<std::string>& getCompletionMatches() const
  {
    return completionMatches;
  }
  double getUptimeSeconds() const;
  std::size_t getCursorPosition() const { return cursorPosition; }
  std::size_t getSelectionAnchor() const { return selectionAnchor; }
  bool hasSelection() const { return cursorPosition != selectionAnchor; }
  const std::vector<historyBuffer>& getHistory() const { return history; }
  const std::vector<std::string>& getCommandHistory() const
  {
    return commandHistory;
  }
  int getHistoryIndex() const { return historyIndex; }
  const std::string& getApplicationName() const { return applicationName; }
  IEnvVars* getEnvVars() const { return envVars; }
  CommandRegistry* getCommandRegistry() const { return commandRegistry; }

  // Virtual hooks for UI / platform integration
  virtual void onInputChanged() {}
  virtual void onHistoryAppended(const historyBuffer& item, bool erasedFront)
  {
    (void)item;
    (void)erasedFront;
  }
  virtual void onHistoryCleared() {}
  // The newest entry changed in place (a repeat collapsed into it).
  virtual void onHistoryBackUpdated() {}
  virtual void onViewChanged() {}
  virtual bool writeClipboard(const std::string& text)
  {
    (void)text;
    return false;
  }
  virtual std::string readClipboard() const { return ""; }
  virtual void onCloseRequested() {}
  virtual void onQuitRequested() {}
  virtual void onToggleFullscreen() {}
  virtual void queryWindowDimensions(int* width, int* height) const
  {
    if (width != nullptr) {
      *width = 0;
    }
    if (height != nullptr) {
      *height = 0;
    }
  }

protected:
  void clearCompletionHint();
  void eraseSelection();
  void resetCursorToEnd();
  std::size_t findPreviousWordBoundary() const;
  std::size_t findNextWordBoundary() const;
  std::vector<std::string> getCompletionCandidates(
    const std::string& leadingText) const;
  std::string getParameterHint(const std::string& inputLine) const;

  bool parseArgsInto(const std::string& text,
                     std::vector<std::string>& outArgs) const;
  bool splitChainInto(const std::string& text,
                      std::vector<std::string>& outCommands) const;
  // Expands `!!`, `!<n>`, and `!<prefix>` against command history. Returns
  // false (after logging) when the reference does not resolve.
  bool expandHistoryReference(const std::string& input, std::string* expanded);
  void runCommandChain(const std::string& text);
  // Searches command history from startIndex toward older entries.
  bool findReverseMatch(int startIndex);
  void acceptSearchIfActive();
  void logMatchingCommands(const std::string& needle, bool* anyMatch);

  IEnvVars* envVars;
  CommandRegistry* commandRegistry;
  std::string applicationName;

  std::string currentInput;
  std::string tempInput;
  std::string completionHint;
  std::vector<historyBuffer> history;
  std::vector<std::string> commandHistory;
  std::unordered_map<std::string, std::string> aliases;
  std::map<std::string, std::string> keyBindings;
  std::vector<std::string> completionMatches;
  std::string viewFilter;
  std::string viewFilterLower;
  int minimumSeverity;
  bool timestampsVisible;
  bool alertsEnabled;
  std::vector<std::string> watches;
  bool searchActive;
  bool searchFailed;
  int searchMatchIndex;
  std::string searchQuery;
  std::string searchSavedInput;
  std::chrono::steady_clock::time_point startTime;
  std::size_t cursorPosition;
  std::size_t selectionAnchor;
  int historyIndex;

  mutable ArenaAlloc parseArena;
  mutable ChainedStackAlloc aliasExpandStack;
};
