#pragma once
#include <IllumoGuest/ResourceId.h>

enum class GuestFileAction : std::uint32_t
{
  Open,
  Read,
  Write,
  Commit,
  Close,
  // Version 2: one directory page of a mounted path (offset is the cursor,
  // size the page limit).
  List,
  // Version 2: kind, size and supplying package of a mounted path.
  Stat,
  // Version 2: copy a Selected grant (path) into the project (target).
  Import,
  // Version 2: pack a project directory (target) into a writable Selected
  // grant (path) as an .ilpk.
  Pack
};
enum class GuestFileArea : std::uint32_t
{
  // The launched package: an alias of Mounted paths below /app.
  Package,
  Storage,
  Selected,
  // Version 2: absolute virtual paths ("/packages/forest/a.png").
  Mounted
};
enum class GuestFileOutcome : std::uint32_t
{
  Success,
  NotFound,
  Denied,
  IoError,
  Cancelled
};

// Package and storage paths are relative UTF-8 names; mounted paths are
// absolute virtual paths. Native dialog selections are granted as named,
// already-open file capabilities.
struct GuestFileRequest
{
  static constexpr std::uint32_t Version = 2;
  // Package, Storage and Selected transfers.
  static constexpr std::uint32_t MaximumBlock = 64u * 1024u;
  // Mounted transfers.
  static constexpr std::uint32_t MaximumMountedBlock = 1024u * 1024u;
  static constexpr std::uint32_t MaximumListPage = 256;
  GuestFileAction action = GuestFileAction::Open;
  GuestFileArea area = GuestFileArea::Package;
  bool writing = false;
  GuestResourceId file;
  std::string path;
  // Import and Pack: the project path the action writes or reads.
  std::string target;
  std::uint64_t offset = 0;
  std::uint64_t size =
    0; // Open(write): exact final length; Read: block length; List: limit.
  std::vector<std::byte> data;

  static std::uint32_t blockFor(GuestFileArea area)
  {
    return area == GuestFileArea::Mounted ? MaximumMountedBlock : MaximumBlock;
  }

  void write(GuestWireWriter& output) const
  {
    output.u32(Version);
    output.u32(static_cast<std::uint32_t>(action));
    output.u32(static_cast<std::uint32_t>(area));
    output.u32(writing ? 1 : 0);
    file.write(output);
    output.text(path);
    output.text(target);
    output.u64(offset);
    output.u64(size);
    output.u32(static_cast<std::uint32_t>(data.size()));
    output.bytes(data);
  }
  static bool read(std::span<const std::byte> bytes, GuestFileRequest& output)
  {
    if (bytes.size() > MaximumMountedBlock + 4096u) {
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
    request.target = reader.text(1024);
    request.offset = reader.u64();
    request.size = reader.u64();
    const std::uint32_t count = reader.u32();
    if (count > MaximumMountedBlock) {
      return false;
    }
    const std::span<const std::byte> data = reader.bytes(count);
    if (!reader.finished() || version != Version || action > 8 || area > 3 ||
        writing > 1) {
      return false;
    }
    const bool noFile = request.file.owner == 0 && request.file.slot == 0 &&
                        request.file.generation == 0;
    const bool mounted = request.area == GuestFileArea::Mounted;
    const bool absolute = !request.path.empty() && request.path[0] == '/';
    const bool clean = request.path.find('\0') == std::string::npos &&
                       request.target.find('\0') == std::string::npos;
    if (!clean) {
      return false;
    }
    switch (request.action) {
      case GuestFileAction::Open:
        if (request.path.empty() || !request.target.empty() || !noFile ||
            request.offset != 0 || count != 0 || (mounted && !absolute) ||
            (!request.writing && request.size != 0)) {
          return false;
        }
        break;
      case GuestFileAction::List:
      case GuestFileAction::Stat:
        if (!mounted || !absolute || !request.target.empty() || writing != 0 ||
            !noFile || count != 0) {
          return false;
        }
        if (request.action == GuestFileAction::List
              ? (request.size == 0 || request.size > MaximumListPage ||
                 request.offset > 0xFFFFFFFFu)
              : (request.size != 0 || request.offset != 0)) {
          return false;
        }
        break;
      case GuestFileAction::Import:
      case GuestFileAction::Pack:
        if (request.area != GuestFileArea::Selected || request.path.empty() ||
            absolute || request.target.empty() || request.target[0] != '/' ||
            writing != 0 || !noFile || request.offset != 0 ||
            request.size != 0 || count != 0) {
          return false;
        }
        break;
      default:
        if (!request.path.empty() || !request.target.empty() || writing != 0 ||
            area != 0 || request.file.owner == 0 ||
            request.file.kind != GuestResourceKind::File ||
            request.file.slot == 0 || request.file.generation == 0) {
          return false;
        }
        if (request.action == GuestFileAction::Read) {
          if (request.size == 0 || request.size > MaximumMountedBlock ||
              count != 0) {
            return false;
          }
        } else if (request.action == GuestFileAction::Write) {
          if (count == 0 || request.size != 0) {
            return false;
          }
        } else if (request.size != 0 || request.offset != 0 || count != 0) {
          return false;
        }
        break;
    }
    request.data.assign(data.begin(), data.end());
    output = std::move(request);
    return true;
  }
};

struct GuestFileEntry
{
  std::string name;
  bool directory = false;
  std::uint64_t size = 0;
};

// A List completion: the directory's full entry count and one page.
struct GuestFileListing
{
  std::uint32_t total = 0;
  std::vector<GuestFileEntry> entries;

  void write(GuestWireWriter& output) const
  {
    output.u32(total);
    output.u32(static_cast<std::uint32_t>(entries.size()));
    for (const GuestFileEntry& entry : entries) {
      output.text(entry.name);
      output.u32(entry.directory ? 1 : 0);
      output.u64(entry.size);
    }
  }
  static bool read(std::span<const std::byte> bytes, GuestFileListing& output)
  {
    GuestWireReader reader(bytes);
    GuestFileListing listing;
    listing.total = reader.u32();
    const std::uint32_t count = reader.u32();
    if (count > GuestFileRequest::MaximumListPage || count > listing.total) {
      return false;
    }
    for (std::uint32_t index = 0; index < count && reader.valid(); ++index) {
      GuestFileEntry entry;
      entry.name = reader.text(255);
      const std::uint32_t directory = reader.u32();
      entry.size = reader.u64();
      if (entry.name.empty() || directory > 1) {
        return false;
      }
      entry.directory = directory != 0;
      listing.entries.push_back(std::move(entry));
    }
    if (!reader.finished()) {
      return false;
    }
    output = std::move(listing);
    return true;
  }
};

// A Stat completion.
struct GuestFileStatus
{
  bool directory = false;
  std::uint64_t size = 0;
  std::string packageId;

  void write(GuestWireWriter& output) const
  {
    output.u32(directory ? 1 : 0);
    output.u64(size);
    output.text(packageId);
  }
  static bool read(std::span<const std::byte> bytes, GuestFileStatus& output)
  {
    GuestWireReader reader(bytes);
    GuestFileStatus status;
    const std::uint32_t directory = reader.u32();
    status.size = reader.u64();
    status.packageId = reader.text(64);
    if (!reader.finished() || directory > 1) {
      return false;
    }
    status.directory = directory != 0;
    output = std::move(status);
    return true;
  }
};
