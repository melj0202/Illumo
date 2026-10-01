#pragma once

#include <IllumoGuest/Services.h>
#include <IllumoGuest/Wire.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// App launch requests (Launch capability). A guest the host lets launch
// applications (an editor's Play) starts one installed application in its own
// process with a document it hands over, as that application's launch
// document. One launched application runs at a time: Start replaces it, Stop
// ends it, Status reports whether it still runs. Every accepted request
// completes with a GuestLaunchStatus.
enum class GuestLaunchAction : std::uint32_t
{
  Start = 1,
  Stop = 2,
  Status = 3
};

struct GuestLaunchRequest
{
  static constexpr std::uint32_t Version = 1;
  static constexpr std::uint32_t MaximumApplicationBytes = 64;
  static constexpr std::uint32_t MaximumNameBytes = 64;
  static constexpr std::uint32_t MaximumDocumentBytes = 8u * 1024u * 1024u;

  GuestLaunchAction action = GuestLaunchAction::Start;
  // Start only: the installed application id, the document's file name as
  // the launched application sees it, and its bytes.
  std::string application;
  std::string name;
  std::vector<std::byte> document;

  // Package ids: [a-z0-9._-], at most 64, never "." or "..".
  static bool validApplication(const std::string& id)
  {
    if (id.empty() || id.size() > MaximumApplicationBytes || id == "." ||
        id == "..") {
      return false;
    }
    for (char character : id) {
      if (!((character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '.' ||
            character == '-' || character == '_')) {
        return false;
      }
    }
    return true;
  }
  // A plain file name: [A-Za-z0-9._-], at most 64, not starting with '.'.
  static bool validName(const std::string& name)
  {
    if (name.empty() || name.size() > MaximumNameBytes || name[0] == '.') {
      return false;
    }
    for (char character : name) {
      if (!((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '.' ||
            character == '-' || character == '_')) {
        return false;
      }
    }
    return true;
  }
  void write(GuestWireWriter& output) const
  {
    output.u32(Version);
    output.u32(static_cast<std::uint32_t>(action));
    output.text(application);
    output.text(name);
    output.u32(static_cast<std::uint32_t>(document.size()));
    output.bytes(document);
  }
  static bool read(std::span<const std::byte> bytes, GuestLaunchRequest& output)
  {
    GuestWireReader reader(bytes);
    GuestLaunchRequest candidate;
    const std::uint32_t version = reader.u32();
    const std::uint32_t action = reader.u32();
    candidate.application = reader.text(MaximumApplicationBytes);
    candidate.name = reader.text(MaximumNameBytes);
    const std::uint32_t size = reader.u32();
    if (!reader.valid() || version != Version || action < 1 || action > 3 ||
        size > MaximumDocumentBytes || reader.remaining() != size) {
      return false;
    }
    const std::span<const std::byte> document = reader.bytes(size);
    if (!reader.finished()) {
      return false;
    }
    candidate.action = static_cast<GuestLaunchAction>(action);
    if (candidate.action == GuestLaunchAction::Start) {
      if (!validApplication(candidate.application) ||
          !validName(candidate.name) || size == 0) {
        return false;
      }
    } else if (!candidate.application.empty() || !candidate.name.empty() ||
               size != 0) {
      return false;
    }
    candidate.document.assign(document.begin(), document.end());
    output = std::move(candidate);
    return true;
  }
};

// Completion payload of every accepted launch request.
struct GuestLaunchStatus
{
  bool running = false;
  void write(GuestWireWriter& output) const { output.u32(running ? 1u : 0u); }
  static bool read(std::span<const std::byte> bytes, GuestLaunchStatus& output)
  {
    GuestWireReader reader(bytes);
    const std::uint32_t running = reader.u32();
    if (!reader.finished() || running > 1u) {
      return false;
    }
    output.running = running == 1u;
    return true;
  }
};

// Launching applications inside a guest (Launch capability). Requests go out
// with the next exchange; poll() takes their completions. Without the grant
// it is unavailable and every request is refused.
class GuestLauncher
{
public:
  explicit GuestLauncher(GuestServiceQueue& services)
    : m_services(services)
  {
  }
  void setGranted(bool granted) { m_granted = granted; }
  bool available() const { return m_granted; }
  // False when not granted, invalid or the service queue is full.
  bool start(const std::string& application,
             const std::string& name,
             std::span<const std::byte> document)
  {
    GuestLaunchRequest request;
    request.action = GuestLaunchAction::Start;
    request.application = application;
    request.name = name;
    request.document.assign(document.begin(), document.end());
    if (!GuestLaunchRequest::validApplication(application) ||
        !GuestLaunchRequest::validName(name) || document.empty() ||
        document.size() > GuestLaunchRequest::MaximumDocumentBytes) {
      return false;
    }
    return send(request);
  }
  bool stop()
  {
    GuestLaunchRequest request;
    request.action = GuestLaunchAction::Stop;
    return send(request);
  }
  bool refresh()
  {
    GuestLaunchRequest request;
    request.action = GuestLaunchAction::Status;
    return send(request);
  }
  // Takes finished requests; true when any completed. running() then holds
  // the newest status, and refused() whether a request was rejected since the
  // last call.
  bool poll()
  {
    bool changed = false;
    std::size_t index = 0;
    while (index < m_requests.size()) {
      GuestServiceRecord record;
      if (!m_services.take(m_requests[index], record)) {
        ++index;
        continue;
      }
      GuestLaunchStatus status;
      if (record.status == GuestServiceStatus::Complete &&
          GuestLaunchStatus::read(record.payload, status)) {
        m_running = status.running;
      } else {
        m_refused = true;
      }
      m_requests.erase(m_requests.begin() + static_cast<std::ptrdiff_t>(index));
      changed = true;
    }
    return changed;
  }
  bool running() const { return m_running; }
  bool pending() const { return !m_requests.empty(); }
  bool takeRefused()
  {
    const bool refused = m_refused;
    m_refused = false;
    return refused;
  }

private:
  bool send(const GuestLaunchRequest& request)
  {
    if (!m_granted) {
      return false;
    }
    GuestWireWriter writer;
    request.write(writer);
    const std::uint64_t id =
      m_services.enqueue(GuestService::LaunchApp, writer.take());
    if (id == 0) {
      return false;
    }
    m_requests.push_back(id);
    return true;
  }

  GuestServiceQueue& m_services;
  bool m_granted = false;
  bool m_running = false;
  bool m_refused = false;
  std::vector<std::uint64_t> m_requests;
};
