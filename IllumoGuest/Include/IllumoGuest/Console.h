#pragma once
#include <IllumoGuest/Services.h>

enum class GuestConsoleAction : std::uint32_t
{
  Register = 0,
  Unregister = 1,
  Listen = 2,
  Log = 3
};

struct GuestConsoleRequest
{
  GuestConsoleAction action = GuestConsoleAction::Listen;
  std::uint32_t level = 0;
  std::string name;
  std::string usage;
  std::string description;
  std::vector<std::string> completions;
  std::vector<std::string> arguments;
  std::string text;
  void write(GuestWireWriter& writer) const
  {
    writer.u32(1);
    writer.u32(static_cast<std::uint32_t>(action));
    writer.u32(level);
    writer.text(name);
    writer.text(usage);
    writer.text(description);
    writer.u32(static_cast<std::uint32_t>(completions.size()));
    for (const std::string& completion : completions) {
      writer.text(completion);
    }
    writer.u32(static_cast<std::uint32_t>(arguments.size()));
    for (const std::string& argument : arguments) {
      writer.text(argument);
    }
    writer.text(text);
  }
  static bool validName(std::string_view name)
  {
    if (name.empty() || name.size() > 48) {
      return false;
    }
    const unsigned char first = static_cast<unsigned char>(name.front());
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z'))) {
      return false;
    }
    for (const char character : name) {
      if (!((character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '_' ||
            character == '-')) {
        return false;
      }
    }
    return true;
  }
  static bool read(std::span<const std::byte> bytes,
                   GuestConsoleRequest& output)
  {
    GuestWireReader reader(bytes);
    const std::uint32_t version = reader.u32();
    const std::uint32_t action = reader.u32();
    GuestConsoleRequest candidate;
    candidate.action = static_cast<GuestConsoleAction>(action);
    candidate.level = reader.u32();
    candidate.name = reader.text(48);
    candidate.usage = reader.text(256);
    candidate.description = reader.text(512);
    const std::uint32_t completionCount = reader.u32();
    if (completionCount > 64) {
      return false;
    }
    for (std::uint32_t index = 0; index < completionCount; ++index) {
      candidate.completions.push_back(reader.text(64));
    }
    const std::uint32_t argumentCount = reader.u32();
    if (argumentCount > 32) {
      return false;
    }
    for (std::uint32_t index = 0; index < argumentCount; ++index) {
      candidate.arguments.push_back(reader.text(1024));
    }
    candidate.text = reader.text(4096);
    if (!reader.finished() || version != 1 || action > 3) {
      return false;
    }
    if (candidate.action == GuestConsoleAction::Register) {
      if (!validName(candidate.name) || candidate.level != 0 ||
          !candidate.arguments.empty() || !candidate.text.empty() ||
          !guestUtf8(candidate.usage) || !guestUtf8(candidate.description)) {
        return false;
      }
      for (const std::string& completion : candidate.completions) {
        if (!guestUtf8(completion)) {
          return false;
        }
      }
    } else if (candidate.action == GuestConsoleAction::Unregister) {
      if (!validName(candidate.name) || candidate.level != 0 ||
          !candidate.usage.empty() || !candidate.description.empty() ||
          !candidate.completions.empty() || !candidate.arguments.empty() ||
          !candidate.text.empty()) {
        return false;
      }
    } else if (candidate.action == GuestConsoleAction::Listen) {
      if (candidate.level != 0 || !candidate.usage.empty() ||
          !candidate.description.empty() || !candidate.completions.empty() ||
          !candidate.text.empty()) {
        return false;
      }
      if (!candidate.name.empty() && !validName(candidate.name)) {
        return false;
      }
      for (const std::string& argument : candidate.arguments) {
        if (!guestUtf8(argument) || argument.size() > 1024) {
          return false;
        }
      }
    } else if (candidate.level < 1 || candidate.level > 4 ||
               !candidate.name.empty() || !candidate.usage.empty() ||
               !candidate.description.empty() ||
               !candidate.completions.empty() || !candidate.arguments.empty() ||
               !guestUtf8(candidate.text)) {
      return false;
    }
    output = std::move(candidate);
    return true;
  }
};

// Product command implementations stay in the guest. Native registration is
// metadata plus a host trampoline that completes a standing listen request.
class GuestConsole
{
public:
  explicit GuestConsole(GuestServiceQueue& services)
    : m_services(services)
  {
  }
  GuestConsole(const GuestConsole&) = delete;
  GuestConsole& operator=(const GuestConsole&) = delete;
  GuestConsole(GuestConsole&&) = delete;
  GuestConsole& operator=(GuestConsole&&) = delete;
  void add(std::string name,
           std::string usage,
           std::string description,
           std::vector<std::string> completions = {});
  void remove(std::string name);
  void log(std::uint32_t level, std::string text);
  void pump();
  bool take(GuestConsoleRequest& invocation);
  const std::string& error() const { return m_error; }

private:
  bool enqueue(GuestConsoleRequest request);
  GuestServiceQueue& m_services;
  std::deque<GuestConsoleRequest> m_outgoing;
  std::deque<GuestConsoleRequest> m_invocations;
  std::uint64_t m_request = 0;
  std::uint64_t m_listen = 0;
  bool m_listening = false;
  std::string m_error;
};
