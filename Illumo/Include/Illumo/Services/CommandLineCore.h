#pragma once

#include <Illumo/Services/ArenaAlloc.h>
#include <Illumo/Services/ChainedStackAlloc.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/IEnvVars.h>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#define MAX_CHARS_PER_LINE 1024
#define MAX_CMD_HISTORY 256

class CommandLineCore
{
public:
  struct historyBuffer
  {
    unsigned char r, g, b, a;
    std::string content;
  };

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

  // Inspection
  std::string getGhostSuggestion() const;
  const std::string& getCurrentInput() const { return currentInput; }
  const std::string& getCompletionHint() const { return completionHint; }
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

  IEnvVars* envVars;
  CommandRegistry* commandRegistry;
  std::string applicationName;

  std::string currentInput;
  std::string tempInput;
  std::string completionHint;
  std::vector<historyBuffer> history;
  std::vector<std::string> commandHistory;
  std::unordered_map<std::string, std::string> aliases;
  std::size_t cursorPosition;
  std::size_t selectionAnchor;
  int historyIndex;

  mutable ArenaAlloc parseArena;
  mutable ChainedStackAlloc aliasExpandStack;
};
