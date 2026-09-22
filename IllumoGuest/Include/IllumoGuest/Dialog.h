#pragma once
#include <IllumoGuest/FileProtocol.h>
#include <IllumoGuest/Services.h>

struct GuestDialogRequest
{
  bool save = false;
  // Version 2: an open whose selection stays writable, so an editor can save
  // the document in place. Invalid together with save.
  bool edit = false;
  std::string description;
  std::string defaultName;
  std::string pattern;
  void write(GuestWireWriter& writer) const
  {
    writer.u32(edit ? 2 : 1);
    writer.u32(save ? 1 : 0);
    writer.text(description);
    writer.text(defaultName);
    writer.text(pattern);
    if (edit) {
      writer.u32(1);
    }
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
    if (version == 2) {
      candidate.edit = reader.u32() == 1;
      if (!candidate.edit || candidate.save) {
        return false;
      }
    }
    if (!reader.finished() || (version != 1 && version != 2) || save > 1 ||
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
  // Selection name for File services ("sel-N"); never a host path.
  std::string name;
  // The chosen file's base name, for display only.
  std::string label;
  void write(GuestWireWriter& writer) const
  {
    writer.u32(static_cast<std::uint32_t>(outcome));
    writer.u32(writing ? 1 : 0);
    writer.u64(size);
    writer.text(name);
    writer.text(label);
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
    candidate.label = reader.text(128);
    if (!reader.finished() ||
        outcome > static_cast<std::uint32_t>(GuestFileOutcome::Cancelled) ||
        writing > 1 || !guestUtf8(candidate.label) ||
        candidate.label.find_first_of("\\/:") != std::string::npos) {
      return false;
    }
    if (candidate.outcome == GuestFileOutcome::Success) {
      if (candidate.name.empty() ||
          candidate.name.find('/') != std::string::npos) {
        return false;
      }
    } else if (!candidate.name.empty() || candidate.size != 0 ||
               candidate.writing || !candidate.label.empty()) {
      return false;
    }
    output = std::move(candidate);
    return true;
  }
};

// Startup bytes naming the document the user passed to the runtime (--open).
// The host has already granted it as the selection "launch"; only its base
// name is disclosed, as a label for the product's UI.
struct GuestLaunch
{
  static constexpr std::uint32_t Magic = 0x31534c49u; // ILS1
  static constexpr const char* Selection = "launch";
  std::string label;
  bool editable = false;
  std::uint64_t size = 0;
  void write(GuestWireWriter& writer) const
  {
    writer.u32(Magic);
    writer.u32(1);
    writer.text(label);
    writer.u32(editable ? 1 : 0);
    writer.u64(size);
  }
  static bool read(std::span<const std::byte> bytes, GuestLaunch& output)
  {
    GuestWireReader reader(bytes);
    const std::uint32_t magic = reader.u32();
    const std::uint32_t version = reader.u32();
    GuestLaunch candidate;
    candidate.label = reader.text(128);
    const std::uint32_t editable = reader.u32();
    candidate.size = reader.u64();
    if (!reader.finished() || magic != Magic || version != 1 || editable > 1 ||
        candidate.label.empty() || !guestUtf8(candidate.label) ||
        candidate.label.find_first_of("\\/:") != std::string::npos) {
      return false;
    }
    candidate.editable = editable != 0;
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
  // An open for editing: the selection may later be written in place.
  void edit(std::string description,
            std::string defaultName,
            std::string pattern)
  {
    m_edit = true;
    apply(false,
          std::move(description),
          std::move(defaultName),
          std::move(pattern));
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
  bool m_edit = false;
  std::string m_error;
};
