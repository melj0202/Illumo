#include <Illumo/Foundation/BuildInfo.h>
#include <Illumo/Foundation/MacroDefs.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/Logger.h>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <iostream>
#include <mutex>
#include <thread>

std::unique_ptr<Logger> Logger::instance;

// Background threads (asset decode, file and compute workers) may log. The
// file, backlog and terminal are serialized here. The console and settings
// are main-thread affine: only the owner thread (the one that initializes and
// configures the logger) forwards to the console and reads the level, which
// other threads take from the cached copy. Recursive: a console sink may
// itself log.
static std::recursive_mutex s_loggerMutex;
static std::thread::id s_ownerThread;
static long s_cachedLogLevel = 2;

// Local wall-clock time as "YYYY-MM-DD HH:MM:SS" (withDate) or
// "HH:MM:SS.mmm".
static std::string
localTimestamp(bool withDate)
{
  const std::chrono::system_clock::time_point now =
    std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  const long long milliseconds =
    std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch())
      .count() %
    1000;
  std::tm local{};
#if defined(_WIN32)
  if (localtime_s(&local, &seconds) != 0) {
    return {};
  }
#else
  if (localtime_r(&seconds, &local) == nullptr) {
    return {};
  }
#endif
  char text[32] = {};
  if (withDate) {
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local);
    return text;
  }
  std::strftime(text, sizeof(text), "%H:%M:%S", &local);
  char withMilliseconds[40] = {};
  std::snprintf(withMilliseconds,
                sizeof(withMilliseconds),
                "%s.%03lld",
                text,
                milliseconds);
  return withMilliseconds;
}

long
Logger::getSafeLogLevel()
{
  if (std::this_thread::get_id() != s_ownerThread) {
    return s_cachedLogLevel;
  }
  s_cachedLogLevel = instance && instance->envVars
                       ? instance->envVars->getVar("logLevel").valueAsLong
                       : 2; // safe default level: Error and Warning
  return s_cachedLogLevel;
}

Logger::Logger(IEnvVars* ev,
               CommandLine* cl,
               const std::filesystem::path& filePath)
  : envVars(ev)
  , commandLine(nullptr)
  , logFilePath(filePath.empty()
                  ? EnvVars::ApplicationConfigPath().parent_path() / "log.txt"
                  : filePath)
{
  logFileStream.open(logFilePath, std::ios::app);
  if (!logFileStream.is_open()) {
    std::fputs("Logger: could not open log file\n", stderr);
  }
  // Session header: when this run started and which build produced it.
  logFileStream << "\n"
                << "========================" << '\n'
                << "Session " << localTimestamp(true) << '\n'
                << "Illumo " << BuildInfo::VersionNumber << ", built "
                << __DATE__ << "  " << __TIME__ << '\n'
                << "========================" << std::endl;
  attachConsole(cl);
}

Logger::~Logger()
{
  if (logFileStream.is_open()) {
    logFileStream.close();
  }
}

bool
Logger::initLogger(IEnvVars* ev,
                   CommandLine* cl,
                   const std::filesystem::path& filePath)
{
  const std::lock_guard<std::recursive_mutex> lock(s_loggerMutex);
  if (!instance) {
    s_ownerThread = std::this_thread::get_id();
    instance = std::make_unique<Logger>(ev, cl, filePath);
  }
  return instance->logFileStream.is_open();
}

void
Logger::setContext(IEnvVars* ev, CommandLine* cl)
{
  const std::lock_guard<std::recursive_mutex> lock(s_loggerMutex);
  s_ownerThread = std::this_thread::get_id();
  if (instance) {
    instance->envVars = ev;
    instance->attachConsole(cl);
  }
}

void
Logger::shutdownLogger()
{
  const std::lock_guard<std::recursive_mutex> lock(s_loggerMutex);
  instance.reset();
}

void
Logger::attachConsole(CommandLine* console)
{
  commandLine = console;
  if (console == nullptr || consoleEverAttached) {
    return;
  }
  // The first console receives the startup backlog; later detach/attach
  // cycles (shutdown, tests) never replay it.
  consoleEverAttached = true;
  std::vector<BacklogEntry> backlog;
  backlog.swap(startupBacklog);
  for (const BacklogEntry& entry : backlog) {
    sendToConsole(console, entry.level, entry.text);
  }
  if (startupBacklogDropped > 0) {
    sendToConsole(console,
                  Level::Warning,
                  std::to_string(startupBacklogDropped) +
                    " earlier startup messages are only in " +
                    logFilePath.string());
    startupBacklogDropped = 0;
  }
}

void
Logger::sendToConsole(CommandLine* console,
                      Level level,
                      const std::string& text)
{
  switch (level) {
    case Level::Error:
      console->logError(text);
      break;
    case Level::Warning:
      console->logWarning(text);
      break;
    case Level::Trace:
      console->logTrace(text);
      break;
    case Level::Info:
    case Level::Plain:
      console->logNormal(text);
      break;
  }
}

void
Logger::write(Level level, const char* message)
{
  long minimum = 1;
  const char* prefix = "";
  const char* terminalPrefix = "";
  switch (level) {
    case Level::Error:
      minimum = 1;
      prefix = "ERROR: ";
      terminalPrefix = "\x1B[31mERROR\x1B[0m: ";
      break;
    case Level::Warning:
      minimum = 2;
      prefix = "WARNING: ";
      terminalPrefix = "\x1B[33mWARNING\x1B[0m: ";
      break;
    case Level::Info:
      minimum = 3;
      prefix = "INFO: ";
      terminalPrefix = "\x1B[34mINFO\x1B[0m: ";
      break;
    case Level::Plain:
      minimum = 1;
      break;
    case Level::Trace:
      minimum = 4;
      prefix = "TRACE: ";
      terminalPrefix = "\x1B[35mTRACE\x1B[0m: ";
      break;
  }
  const std::lock_guard<std::recursive_mutex> lock(s_loggerMutex);
  if (!message || __STRLEN(message) == 0 || getSafeLogLevel() < minimum) {
    return;
  }
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
  std::fprintf(
    consoleToStderr ? stderr : stdout, "%s%s\n", terminalPrefix, message);
  std::fflush(consoleToStderr ? stderr : stdout);
#else
  (void)terminalPrefix;
#endif
  if (!instance) {
    return;
  }
  instance->logFileStream << '[' << localTimestamp(false) << "] " << prefix
                          << message << std::endl;
  if (instance->commandLine) {
    if (std::this_thread::get_id() == s_ownerThread) {
      sendToConsole(instance->commandLine, level, message);
    }
  } else if (!instance->consoleEverAttached) {
    if (instance->startupBacklog.size() < kStartupBacklogLimit) {
      instance->startupBacklog.push_back({ level, message });
    } else {
      ++instance->startupBacklogDropped;
    }
  }
}

void
Logger::LogInfo(const char* message)
{
  write(Level::Info, message);
}

void
Logger::LogWarning(const char* message)
{
  write(Level::Warning, message);
}

void
Logger::LogError(const char* message)
{
  write(Level::Error, message);
}

void
Logger::Log(const char* message)
{
  write(Level::Plain, message);
}

void
Logger::LogTrace(const char* message)
{
  write(Level::Trace, message);
}

void
Logger::LogInfo(char* message)
{
  LogInfo(static_cast<const char*>(message));
}

void
Logger::LogWarning(char* message)
{
  LogWarning(static_cast<const char*>(message));
}

void
Logger::LogError(char* message)
{
  LogError(static_cast<const char*>(message));
}

void
Logger::Log(char* message)
{
  Log(static_cast<const char*>(message));
}

void
Logger::LogTrace(char* message)
{
  LogTrace(static_cast<const char*>(message));
}
