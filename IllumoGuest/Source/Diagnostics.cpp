#include <Illumo/Foundation/Fatal.h>
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Services/Logger.h>
#include <IllumoGuest/Diagnostics.h>
#include <string>

static GuestServiceQueue* diagnosticQueue = nullptr;
static std::uint64_t dropped = 0;
// Encoded Log payloads the full queue refused, oldest first, and the lines
// overflow pushed out since the last dropped-count report.
static std::deque<std::vector<std::byte>> backlog;
static std::uint64_t unreported = 0;

static std::vector<std::byte>
encodeLine(std::uint32_t level, const std::string& text)
{
  GuestWireWriter payload;
  payload.u32(level);
  payload.text(text);
  return payload.take();
}
static void
flushBacklog()
{
  if (!diagnosticQueue) {
    return;
  }
  // The report goes first: the lost lines were older than any still waiting.
  if (unreported != 0) {
    std::vector<std::byte> report =
      encodeLine(2,
                 "Guest log dropped " + std::to_string(unreported) +
                   " line(s) while the service queue was full");
    if (diagnosticQueue->tryEnqueue(GuestService::Log, report) == 0) {
      return;
    }
    unreported = 0;
  }
  while (!backlog.empty() &&
         diagnosticQueue->tryEnqueue(GuestService::Log, backlog.front()) != 0) {
    backlog.pop_front();
  }
}
GuestDiagnostics::GuestDiagnostics(GuestServiceQueue& queue)
{
  if (diagnosticQueue != nullptr) {
    illumoFatal("GuestDiagnostics: only one diagnostic route per store");
  }
  diagnosticQueue = &queue;
}
GuestDiagnostics::~GuestDiagnostics()
{
  diagnosticQueue = nullptr;
  dropped += backlog.size();
  backlog.clear();
  unreported = 0;
}
std::uint64_t
GuestDiagnostics::droppedMessages()
{
  return dropped;
}
void
GuestDiagnostics::pump()
{
  ILLUMO_PROFILE_ZONE("Diagnostics.pump");
  ILLUMO_PROFILE_PLOT("Guest log backlog", backlog.size());
  flushBacklog();
}
static void
logMessage(std::uint32_t level, const char* text)
{
  ILLUMO_PROFILE_ZONE("Diagnostics.logMessage");
  if (!diagnosticQueue || !text) {
    ++dropped;
    return;
  }
  std::size_t length = 0;
  while (length < 4096 && text[length] != '\0') {
    ++length;
  }
  std::vector<std::byte> line = encodeLine(level, std::string(text, length));
  // Waiting lines go first so the host sees them in order.
  flushBacklog();
  if (backlog.empty() && unreported == 0 &&
      diagnosticQueue->tryEnqueue(GuestService::Log, line) != 0) {
    return;
  }
  if (backlog.size() >= GuestDiagnostics::MaximumBacklog) {
    backlog.pop_front();
    ++dropped;
    ++unreported;
  }
  backlog.push_back(std::move(line));
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
