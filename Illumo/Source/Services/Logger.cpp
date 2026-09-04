#include <Illumo/Foundation/MacroDefs.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/IEnvVars.h>
#include <Illumo/Services/Logger.h>
#include <iostream>

Logger* Logger::instance = nullptr;

long
Logger::getSafeLogLevel()
{
  if (instance && instance->envVars) {
    return instance->envVars->getVar("logLevel").valueAsLong;
  }
  return 2; // safe default level: Error and Warning
}

Logger::Logger(IEnvVars* ev, CommandLine* cl)
  : envVars(ev)
  , commandLine(cl)
{
  logFileStream.open("log.txt", std::ios::app);
  // Log date and time
  logFileStream << "\n"
                << "========================" << '\n'
                << __DATE__ << "  " << __TIME__ << "\n"
                << "========================" << std::endl;
}

Logger::~Logger()
{
  if (logFileStream.is_open()) {
    logFileStream.close();
  }
}

bool
Logger::initLogger(IEnvVars* ev, CommandLine* cl)
{
  if (!instance) {
    instance = new Logger(ev, cl);
  }
  return true;
}

void
Logger::setContext(IEnvVars* ev, CommandLine* cl)
{
  if (instance) {
    instance->envVars = ev;
    instance->commandLine = cl;
  }
}

void
Logger::shutdownLogger()
{
  delete instance;
  instance = nullptr;
}

void
Logger::LogInfo(const char* message)
{
  if (!message || __STRLEN(message) == 0 || getSafeLogLevel() < 3)
    return;
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
  printf("\x1B[34mINFO\033[0m: %s\n", message);
#endif
  if (instance) {
    instance->logFileStream << "INFO: " << message << std::endl;
    if (instance->commandLine) {
      instance->commandLine->logNormal(message);
    }
  }
}

void
Logger::LogWarning(const char* message)
{
  if (!message || __STRLEN(message) == 0 || getSafeLogLevel() < 2)
    return;
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
  printf("\x1B[33mWARNING\033[0m: %s\n", message);
#endif
  if (instance) {
    instance->logFileStream << "WARNING: " << message << std::endl;
    if (instance->commandLine) {
      instance->commandLine->logWarning(message);
    }
  }
}

void
Logger::LogError(const char* message)
{
  if (!message || __STRLEN(message) == 0 || getSafeLogLevel() < 1)
    return;
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
  printf("\x1B[31mERROR\033[0m: %s\n", message);
#endif
  if (instance) {
    instance->logFileStream << "ERROR: " << message << std::endl;
    if (instance->commandLine) {
      instance->commandLine->logError(message);
    }
  }
}

void
Logger::Log(const char* message)
{
  if (!message || __STRLEN(message) == 0 || getSafeLogLevel() < 1)
    return;
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
  std::cout << message << std::endl;
#endif
  if (instance) {
    instance->logFileStream << message << std::endl;
    if (instance->commandLine) {
      instance->commandLine->logNormal(message);
    }
  }
}

void
Logger::LogTrace(const char* message)
{
  if (!message || __STRLEN(message) == 0 || getSafeLogLevel() < 4)
    return;
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
  printf("\x1B[35mTRACE\x1B[0m: %s\n", message);
#endif
  if (instance) {
    instance->logFileStream << "TRACE: " << message << std::endl;
    if (instance->commandLine) {
      instance->commandLine->logTrace(message);
    }
  }
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
