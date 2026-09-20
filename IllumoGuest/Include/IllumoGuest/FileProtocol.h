#pragma once
#include <IllumoGuest/ResourceId.h>

enum class GuestFileAction : std::uint32_t
{
  Open,
  Read,
  Write,
  Commit,
  Close
};
enum class GuestFileArea : std::uint32_t
{
  Package,
  Storage,
  Selected
};
enum class GuestFileOutcome : std::uint32_t
{
  Success,
  NotFound,
  Denied,
  IoError,
  Cancelled
};

// Read-only package paths and private storage paths are relative UTF-8 names.
// Native dialog selections are granted as already-open file capabilities.
struct GuestFileRequest
{
  static constexpr std::uint32_t MaximumBlock = 64u * 1024u;
  GuestFileAction action = GuestFileAction::Open;
  GuestFileArea area = GuestFileArea::Package;
  bool writing = false;
  GuestResourceId file;
  std::string path;
  std::uint64_t offset = 0;
  std::uint64_t size =
    0; // Open(write): exact final length; Read: block length.
  std::vector<std::byte> data;

  void write(GuestWireWriter& output) const
  {
    output.u32(1);
    output.u32(static_cast<std::uint32_t>(action));
    output.u32(static_cast<std::uint32_t>(area));
    output.u32(writing ? 1 : 0);
    file.write(output);
    output.text(path);
    output.u64(offset);
    output.u64(size);
    output.u32(static_cast<std::uint32_t>(data.size()));
    output.bytes(data);
  }
  static bool read(std::span<const std::byte> bytes, GuestFileRequest& output)
  {
    if (bytes.size() > MaximumBlock + 2048u) {
      return false;
    }
    GuestWireReader reader(bytes);
    const std::uint32_t version = reader.u32();
    const std::uint32_t action = reader.u32();
    const std::uint32_t area = reader.u32();
    const std::uint32_t writing = reader.u32();
    GuestFileRequest request;
    request.action = static_cast<GuestFileAction>(action);
    request.area = static_cast<GuestFileArea>(area);
    request.writing = writing != 0;
    request.file = GuestResourceId::read(reader);
    request.path = reader.text(1024);
    request.offset = reader.u64();
    request.size = reader.u64();
    const std::uint32_t count = reader.u32();
    if (count > MaximumBlock) {
      return false;
    }
    const std::span<const std::byte> data = reader.bytes(count);
    if (!reader.finished() || version != 1 || action > 4 || area > 2 ||
        writing > 1) {
      return false;
    }
    if (request.action == GuestFileAction::Open) {
      if (request.path.empty() ||
          request.path.find('\0') != std::string::npos ||
          request.file.owner != 0 || request.file.slot != 0 ||
          request.file.generation != 0 || request.offset != 0 || count != 0 ||
          (!request.writing && request.size != 0)) {
        return false;
      }
    } else {
      if (!request.path.empty() || writing != 0 || area != 0 ||
          request.file.owner == 0 ||
          request.file.kind != GuestResourceKind::File ||
          request.file.slot == 0 || request.file.generation == 0) {
        return false;
      }
      if (request.action == GuestFileAction::Read) {
        if (request.size == 0 || request.size > MaximumBlock || count != 0) {
          return false;
        }
      } else if (request.action == GuestFileAction::Write) {
        if (count == 0 || request.size != 0) {
          return false;
        }
      } else if (request.size != 0 || request.offset != 0 || count != 0) {
        return false;
      }
    }
    request.data.assign(data.begin(), data.end());
    output = std::move(request);
    return true;
  }
};
