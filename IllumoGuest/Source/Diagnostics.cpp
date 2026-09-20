#include <Illumo/Services/Logger.h>
#include <IllumoGuest/Diagnostics.h>
#include <stdexcept>

static GuestServiceQueue* diagnosticQueue = nullptr;
static std::uint64_t dropped = 0;
GuestDiagnostics::GuestDiagnostics(GuestServiceQueue& queue)
{
  if (diagnosticQueue != nullptr) {
    throw std::logic_error("Only one diagnostic route per store");
  }
  diagnosticQueue = &queue;
}
GuestDiagnostics::~GuestDiagnostics()
{
  diagnosticQueue = nullptr;
}
std::uint64_t
GuestDiagnostics::droppedMessages()
{
  return dropped;
}
static void
logMessage(std::uint32_t level, const char* text)
{
  if (!diagnosticQueue || !text) {
    ++dropped;
    return;
  }
  std::size_t length = 0;
  while (length < 4096 && text[length] != '\0') {
    ++length;
  }
  GuestWireWriter payload;
  payload.u32(level);
  payload.text(std::string(text, length));
  if (diagnosticQueue->enqueue(GuestService::Log, payload.take()) == 0) {
    ++dropped;
  }
}
void
Logger::LogError(const char* text)
{
  logMessage(1, text);
}
void
Logger::LogWarning(const char* text)
{
  logMessage(2, text);
}
void
Logger::LogInfo(const char* text)
{
  logMessage(3, text);
}
void
Logger::Log(const char* text)
{
  logMessage(3, text);
}
void
Logger::LogTrace(const char* text)
{
  logMessage(4, text);
}
void
Logger::LogError(char* text)
{
  LogError(static_cast<const char*>(text));
}
void
Logger::LogWarning(char* text)
{
  LogWarning(static_cast<const char*>(text));
}
void
Logger::LogInfo(char* text)
{
  LogInfo(static_cast<const char*>(text));
}
void
Logger::Log(char* text)
{
  Log(static_cast<const char*>(text));
}
void
Logger::LogTrace(char* text)
{
  LogTrace(static_cast<const char*>(text));
}
