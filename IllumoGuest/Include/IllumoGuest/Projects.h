#pragma once

#include <IllumoGuest/Services.h>
#include <IllumoGuest/Wire.h>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// The Project service (Project capability): the host's list of recent
// project folders for this application, the OS folder dialogs that add one
// (an existing project, or a new one the host scaffolds), and a restart of
// the runtime on a listed project. Host paths never cross: the guest sees
// an entry as an id, valid until the next listing, plus the folder's name.
// docs/illed-editing-wave4-plan.md, 5.5.

enum class GuestProjectAction : std::uint32_t
{
  // The recent list (newest first).
  List = 1,
  // A folder dialog for an existing project; added to the list.
  Choose = 2,
  // A folder dialog for a new project; scaffolded and added to the list.
  Create = 3,
  // Prepares the runtime's restart on the listed project; the guest then
  // asks to restart (GuestUpdateFlags::RequestRestart).
  Open = 4,
  // Empties the recent list.
  Clear = 5
};

struct GuestProjectRequest
{
  static constexpr std::uint32_t Version = 1;

  GuestProjectAction action = GuestProjectAction::List;
  // Open only: a listed entry's id.
  std::uint32_t id = 0;

  void write(GuestWireWriter& output) const
  {
    output.u32(Version);
    output.u32(static_cast<std::uint32_t>(action));
    output.u32(id);
  }
  static bool read(std::span<const std::byte> bytes,
                   GuestProjectRequest& output)
  {
    GuestWireReader reader(bytes);
    const std::uint32_t version = reader.u32();
    const std::uint32_t action = reader.u32();
    const std::uint32_t id = reader.u32();
    if (!reader.finished() || version != Version || action < 1 || action > 5) {
      return false;
    }
    const bool open =
      action == static_cast<std::uint32_t>(GuestProjectAction::Open);
    if (open ? id == 0 : id != 0) {
      return false;
    }
    output.action = static_cast<GuestProjectAction>(action);
    output.id = id;
    return true;
  }
};

struct GuestProjectEntry
{
  static constexpr std::uint32_t MaximumLabelBytes = 128;

  std::uint32_t id = 0;
  // The folder's name.
  std::string label;

  static bool validLabel(const std::string& text)
  {
    if (text.empty() || text.size() > MaximumLabelBytes || !guestUtf8(text)) {
      return false;
    }
    for (char character : text) {
      if (static_cast<unsigned char>(character) < 0x20u ||
          character == '\x7f') {
        return false;
      }
    }
    return true;
  }
};

// Completion payload of every accepted project request. List carries every
// entry; Choose and Create carry the one entry (none when cancelled); Open
// and Clear carry none.
struct GuestProjectResult
{
  static constexpr std::uint32_t Version = 1;
  static constexpr std::uint32_t MaximumEntries = 16;

  bool cancelled = false;
  std::vector<GuestProjectEntry> entries;

  void write(GuestWireWriter& output) const
  {
    output.u32(Version);
    output.u32(cancelled ? 1u : 0u);
    output.u32(static_cast<std::uint32_t>(entries.size()));
    for (const GuestProjectEntry& entry : entries) {
      output.u32(entry.id);
      output.text(entry.label);
    }
  }
  static bool read(std::span<const std::byte> bytes, GuestProjectResult& output)
  {
    GuestWireReader reader(bytes);
    GuestProjectResult candidate;
    const std::uint32_t version = reader.u32();
    const std::uint32_t cancelled = reader.u32();
    const std::uint32_t count = reader.u32();
    if (!reader.valid() || version != Version || cancelled > 1u ||
        count > MaximumEntries) {
      return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
      GuestProjectEntry entry;
      entry.id = reader.u32();
      entry.label = reader.text(GuestProjectEntry::MaximumLabelBytes);
      if (!reader.valid() || entry.id == 0 ||
          !GuestProjectEntry::validLabel(entry.label)) {
        return false;
      }
      for (const GuestProjectEntry& earlier : candidate.entries) {
        if (earlier.id == entry.id) {
          return false;
        }
      }
      candidate.entries.push_back(std::move(entry));
    }
    if (!reader.finished()) {
      return false;
    }
    candidate.cancelled = cancelled == 1u;
    output = std::move(candidate);
    return true;
  }
};

// Project folders inside a guest (Project capability). Requests go out with
// the next exchange; pump() takes their completions and runs the callbacks
// (never inside the requesting call). Without the grant it is unavailable
// and every request is refused.
class GuestProjects
{
public:
  using EntryList = std::vector<GuestProjectEntry>;
  // ok is false when the host rejected the request.
  using ListCallback = std::function<void(bool ok, EntryList entries)>;
  using PickCallback =
    std::function<void(bool ok, bool cancelled, GuestProjectEntry entry)>;
  using DoneCallback = std::function<void(bool ok)>;

  explicit GuestProjects(GuestServiceQueue& services)
    : m_services(services)
  {
  }
  void setGranted(bool granted) { m_granted = granted; }
  bool available() const { return m_granted; }

  // Each is false when not granted or the service queue is full; the
  // callback then never runs.
  bool list(ListCallback done)
  {
    Pending pending;
    pending.action = GuestProjectAction::List;
    pending.list = std::move(done);
    return send(std::move(pending), 0);
  }
  bool choose(PickCallback done)
  {
    Pending pending;
    pending.action = GuestProjectAction::Choose;
    pending.pick = std::move(done);
    return send(std::move(pending), 0);
  }
  bool create(PickCallback done)
  {
    Pending pending;
    pending.action = GuestProjectAction::Create;
    pending.pick = std::move(done);
    return send(std::move(pending), 0);
  }
  bool open(std::uint32_t id, DoneCallback done)
  {
    if (id == 0) {
      return false;
    }
    Pending pending;
    pending.action = GuestProjectAction::Open;
    pending.done = std::move(done);
    return send(std::move(pending), id);
  }
  bool clear(DoneCallback done)
  {
    Pending pending;
    pending.action = GuestProjectAction::Clear;
    pending.done = std::move(done);
    return send(std::move(pending), 0);
  }
  bool pending() const { return !m_requests.empty(); }

  // Takes finished requests and runs their callbacks; call once per update.
  void pump()
  {
    // Collected first: a callback may queue another request.
    std::vector<std::function<void()>> completions;
    std::size_t index = 0;
    while (index < m_requests.size()) {
      GuestServiceRecord record;
      if (!m_services.take(m_requests[index].id, record)) {
        ++index;
        continue;
      }
      Pending finished = std::move(m_requests[index]);
      m_requests.erase(m_requests.begin() + static_cast<std::ptrdiff_t>(index));
      GuestProjectResult result;
      const bool ok = record.status == GuestServiceStatus::Complete &&
                      GuestProjectResult::read(record.payload, result);
      if (finished.list) {
        completions.push_back([done = std::move(finished.list), ok, result]() {
          done(ok, result.entries);
        });
      } else if (finished.pick) {
        completions.push_back([done = std::move(finished.pick), ok, result]() {
          done(ok,
               ok && result.cancelled,
               result.entries.empty() ? GuestProjectEntry{}
                                      : result.entries.front());
        });
      } else if (finished.done) {
        completions.push_back([done = std::move(finished.done), ok, result]() {
          done(ok && !result.cancelled);
        });
      }
    }
    for (std::function<void()>& completion : completions) {
      completion();
    }
  }

private:
  struct Pending
  {
    std::uint64_t id = 0;
    GuestProjectAction action = GuestProjectAction::List;
    ListCallback list;
    PickCallback pick;
    DoneCallback done;
  };

  bool send(Pending pending, std::uint32_t id)
  {
    if (!m_granted) {
      return false;
    }
    GuestProjectRequest request;
    request.action = pending.action;
    request.id = id;
    GuestWireWriter writer;
    request.write(writer);
    pending.id = m_services.enqueue(GuestService::Project, writer.take());
    if (pending.id == 0) {
      return false;
    }
    m_requests.push_back(std::move(pending));
    return true;
  }

  GuestServiceQueue& m_services;
  bool m_granted = false;
  std::vector<Pending> m_requests;
};
