#pragma once
#include <IllumoGuest/FileProtocol.h>
#include <IllumoGuest/Services.h>

struct GuestDialogRequest
{
  bool save = false;
  std::string description;
  std::string defaultName;
  std::string pattern;
  void write(GuestWireWriter& writer) const
  {
    writer.u32(1);
    writer.u32(save ? 1 : 0);
    writer.text(description);
    writer.text(defaultName);
    writer.text(pattern);
  }
  static bool read(std::span<const std::byte> bytes, GuestDialogRequest& output)
  {
    GuestWireReader reader(bytes);
    const std::uint32_t version = reader.u32();
    const std::uint32_t save = reader.u32();
    GuestDialogRequest candidate;
    candidate.save = save != 0;
    candidate.description = reader.text(128);
    candidate.defaultName = reader.text(128);
    candidate.pattern = reader.text(64);
    if (!reader.finished() || version != 1 || save > 1 ||
        candidate.description.empty() || candidate.defaultName.empty() ||
        candidate.pattern.empty() || !guestUtf8(candidate.description) ||
        !guestUtf8(candidate.defaultName) || !guestUtf8(candidate.pattern) ||
        candidate.defaultName.find_first_of("\\/:*?\"<>|") !=
          std::string::npos) {
      return false;
    }
    output = std::move(candidate);
    return true;
  }
};

struct GuestDialogResult
{
  GuestFileOutcome outcome = GuestFileOutcome::IoError;
  bool writing = false;
  std::uint64_t size = 0;
  std::string name;
  void write(GuestWireWriter& writer) const
  {
    writer.u32(static_cast<std::uint32_t>(outcome));
    writer.u32(writing ? 1 : 0);
    writer.u64(size);
    writer.text(name);
  }
  static bool read(std::span<const std::byte> bytes, GuestDialogResult& output)
  {
    GuestWireReader reader(bytes);
    GuestDialogResult candidate;
    const std::uint32_t outcome = reader.u32();
    const std::uint32_t writing = reader.u32();
    candidate.outcome = static_cast<GuestFileOutcome>(outcome);
    candidate.writing = writing != 0;
    candidate.size = reader.u64();
    candidate.name = reader.text(64);
    if (!reader.finished() ||
        outcome > static_cast<std::uint32_t>(GuestFileOutcome::Cancelled) ||
        writing > 1) {
      return false;
    }
    if (candidate.outcome == GuestFileOutcome::Success) {
      if (candidate.name.empty() ||
          candidate.name.find('/') != std::string::npos) {
        return false;
      }
    } else if (!candidate.name.empty() || candidate.size != 0 ||
               candidate.writing) {
      return false;
    }
    output = std::move(candidate);
    return true;
  }
};

// Native dialogs run outside guest execution. The completion grants a selected
// file name that File services can open; it never returns a host path.
class GuestDialog
{
public:
  explicit GuestDialog(GuestServiceQueue& services)
    : m_services(services)
  {
  }
  GuestDialog(const GuestDialog&) = delete;
  GuestDialog& operator=(const GuestDialog&) = delete;
  GuestDialog(GuestDialog&&) = delete;
  GuestDialog& operator=(GuestDialog&&) = delete;
  void load(std::string description,
            std::string defaultName,
            std::string pattern)
  {
    apply(false,
          std::move(description),
          std::move(defaultName),
          std::move(pattern));
  }
  void save(std::string description,
            std::string defaultName,
            std::string pattern)
  {
    apply(
      true, std::move(description), std::move(defaultName), std::move(pattern));
  }
  void pump();
  bool idle() const { return !m_pending && m_request == 0; }
  const GuestDialogResult& result() const { return m_result; }
  const std::string& error() const { return m_error; }

private:
  void apply(bool save,
             std::string description,
             std::string defaultName,
             std::string pattern);
  GuestServiceQueue& m_services;
  GuestDialogRequest m_next;
  GuestDialogResult m_result;
  std::uint64_t m_request = 0;
  bool m_pending = false;
  std::string m_error;
};
