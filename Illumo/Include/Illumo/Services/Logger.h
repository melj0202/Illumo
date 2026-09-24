#pragma once
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

/*
    This class describes a logger class that writes messages to a file,
    and to a terminal if there is one present in the build
    (aka if it is a debug build)

    Log Levels
        0 = No logging
        1 = Error logging
        2 = Error and warning logging
        3 = Error, warning, and info logging
        4 = Error, warning, info, and trace logging

    Messages logged before the first console attaches (platform, window and
    GPU startup) are kept, bounded, and replayed into that console.
*/

class IEnvVars;
class CommandLine;

class Logger
{
public:
  Logger(IEnvVars* ev,
         CommandLine* cl,
         const std::filesystem::path& filePath = {});
  ~Logger();

  void operator=(const Logger&) = delete;
  Logger(const Logger&) = delete;

  // Logging is best-effort side-effect work; callers have no recovery action.
  static void LogError(const char* message);
  static void LogWarning(const char* message);
  static void LogInfo(const char* message);
  static void Log(const char* message);
  static void LogTrace(const char* message);

  static void LogError(char* message);
  static void LogWarning(char* message);
  static void LogInfo(char* message);
  static void Log(char* message);
  static void LogTrace(char* message);

  static void LogError(const std::string& message)
  {
    LogError(message.c_str());
  }
  static void LogWarning(const std::string& message)
  {
    LogWarning(message.c_str());
  }
  static void LogInfo(const std::string& message) { LogInfo(message.c_str()); }
  static void Log(const std::string& message) { Log(message.c_str()); }
  static void LogTrace(const std::string& message)
  {
    LogTrace(message.c_str());
  }

  // Wide character logging functions
  static void LogWError(const wchar_t* /*message*/) {};
  static void LogWWarning(const wchar_t* /*message*/) {};
  static void LogWInfo(const wchar_t* /*message*/) {};
  static void LogW(const wchar_t* /*message*/) {};
  static bool initLogger(IEnvVars* ev = nullptr,
                         CommandLine* cl = nullptr,
                         const std::filesystem::path& filePath = {});
  static void setContext(IEnvVars* ev, CommandLine* cl);
  static void shutdownLogger();
  // Console tools can reserve stdout for machine-readable results.
  static void setConsoleToStderr(bool enabled) { consoleToStderr = enabled; }
  static CommandLine* getCommandLine()
  {
    return instance ? instance->commandLine : nullptr;
  }

  // The file every message is appended to (empty before initLogger).
  static std::filesystem::path getLogFilePath()
  {
    return instance ? instance->logFilePath : std::filesystem::path();
  }

  std::ofstream logFileStream;

  static constexpr std::size_t kStartupBacklogLimit = 256;

private:
  enum class Level
  {
    Error,
    Warning,
    Info,
    Plain,
    Trace
  };
  struct BacklogEntry
  {
    Level level;
    std::string text;
  };

  static inline bool consoleToStderr = false;
  static long getSafeLogLevel();
  static void write(Level level, const char* message);
  static void sendToConsole(CommandLine* console,
                            Level level,
                            const std::string& text);
  void attachConsole(CommandLine* console);
  static std::unique_ptr<Logger> instance;
  IEnvVars* envVars;
  CommandLine* commandLine;
  std::filesystem::path logFilePath;
  bool consoleEverAttached = false;
  std::vector<BacklogEntry> startupBacklog;
  std::size_t startupBacklogDropped = 0;
};
