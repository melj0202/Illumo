#include <Illumo/Content/PackageArchive.h>
#include <Illumo/Content/PackageManifest.h>
#include <Illumo/Content/PackageMounts.h>
#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Content/VirtualPath.h>
#include <Illumo/Platform/AtomicFile.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Wasm/WasmFileServices.h>
#include <IllumoGuest/FileProtocol.h>
#include <IllumoGuest/Protocol.h>
#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <list>
#include <map>
#include <mutex>
#include <thread>

class WasmFileServices::State
{
public:
  struct File
  {
    std::filesystem::path destination;
    std::filesystem::path staging;
    std::fstream stream;
    // Package files are read through the virtual file tree.
    std::shared_ptr<VfsFile> mounted;
    // Project writes stage in memory and commit through the tree.
    std::string virtualDestination;
    std::vector<std::byte> buffer;
    std::uint32_t block = GuestFileRequest::MaximumBlock;
    std::uint64_t size = 0;
    std::uint64_t written = 0;
    bool writing = false;
    bool selected = false;
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File()
    {
      if (stream.is_open()) {
        stream.close();
      }
      if (!staging.empty()) {
        std::error_code ignored;
        std::filesystem::remove(staging, ignored);
      }
    }
  };
  struct Slot
  {
    std::uint32_t generation = 0;
    std::unique_ptr<File> file;
  };
  struct Request
  {
    std::uint64_t id;
    GuestFileRequest value;
  };

  State(std::uint64_t identity,
        std::uint32_t capabilities,
        std::shared_ptr<const VirtualFileSystem> mounts,
        std::filesystem::path storage,
        WasmFileLimits policy)
    : owner(identity)
    , grants(capabilities)
    , limits(policy)
    , packages(std::move(mounts))
  {
    if (owner == 0 || !packages || !storage.is_absolute() ||
        limits.openFiles == 0 || limits.openFiles > 64 ||
        limits.fileBytes > 1024ull * 1024ull * 1024ull) {
      throw std::invalid_argument("Invalid file service policy");
    }
    storageRoot = std::filesystem::canonical(storage);
    if (!std::filesystem::is_directory(storageRoot)) {
      throw std::invalid_argument("File service roots must be directories");
    }
    files.resize(limits.openFiles);
    worker = std::thread([this]() { work(); });
    packer = std::thread([this]() { packWork(); });
  }
  ~State()
  {
    cancel();
    if (worker.joinable()) {
      worker.join();
    }
    if (packer.joinable()) {
      packer.join();
    }
  }
  void cancel()
  {
    std::lock_guard<std::mutex> lock(mutex);
    stopping = true;
    requests.clear();
    packRequests.clear();
    completions.records.clear();
    outstanding = 0;
    condition.notify_all();
    packCondition.notify_all();
  }
  // Package and storage names use the shared virtual-path component rules,
  // so a name valid here is valid in the virtual file tree and in archives.
  static bool relativeName(const std::string& path)
  {
    return VirtualPath::validRelative(path);
  }
  std::filesystem::path resolve(const GuestFileRequest& request) const
  {
    if (!relativeName(request.path)) {
      return {};
    }
    const std::filesystem::path& root = storageRoot;
    const std::u8string name(
      reinterpret_cast<const char8_t*>(request.path.data()),
      request.path.size());
    const std::filesystem::path path =
      std::filesystem::weakly_canonical(root / std::filesystem::path(name));
    std::filesystem::path::const_iterator candidate = path.begin();
    for (const std::filesystem::path& component : root) {
      if (candidate == path.end() || *candidate != component) {
        return {};
      }
      ++candidate;
    }
    return candidate != path.end() ? path : std::filesystem::path{};
  }
  File* find(const GuestResourceId& id)
  {
    if (id.owner != owner || id.kind != GuestResourceKind::File ||
        id.slot == 0 || id.slot > files.size()) {
      return nullptr;
    }
    Slot& slot = files[id.slot - 1];
    return slot.generation == id.generation ? slot.file.get() : nullptr;
  }
  void close(const GuestResourceId& id)
  {
    File* file = find(id);
    if (file && file->writing) {
      reserved -= file->size * 2;
    }
    if (file) {
      files[id.slot - 1].file.reset();
    }
  }
  bool storageFits(const File& replacing)
  {
    std::uint64_t bytes = replacing.size;
    std::size_t count = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator(storageRoot)) {
      if (++count > 4096 || entry.is_symlink()) {
        return false;
      }
      if (!entry.is_regular_file() || entry.path() == replacing.destination) {
        continue;
      }
      bool staging = false;
      for (const Slot& slot : files) {
        staging = staging || (slot.file && slot.file->staging == entry.path());
      }
      if (staging) {
        continue;
      }
      const std::uint64_t size = entry.file_size();
      if (bytes > limits.storageBytes || size > limits.storageBytes - bytes) {
        return false;
      }
      bytes += size;
    }
    return bytes <= limits.storageBytes;
  }
  // Grants are made on the main thread (dialogs and launch), so they may log.
  // Logging is best-effort and never changes a grant's outcome.
  static void logGrant(bool warning, const std::string& text)
  {
    if (warning) {
      Logger::LogWarning(text);
    } else {
      Logger::LogTrace(text);
    }
  }
  static std::string displayPath(const std::filesystem::path& path)
  {
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
  }
  static bool refuseGrant(const std::filesystem::path& path, const char* reason)
  {
    try {
      logGrant(true,
               "File grant for the guest refused (" + std::string(reason) +
                 "): " + displayPath(path));
    } catch (...) {
    }
    return false;
  }
  // Readable grants need an existing bounded file; writable grants need its
  // directory. An empty fixed name allocates the next "sel-N".
  bool grant(const std::filesystem::path& path,
             bool readable,
             bool writable,
             const std::string& fixedName,
             std::string& name,
             std::uint64_t& size)
  {
    std::error_code status;
    if (!path.is_absolute()) {
      return refuseGrant(path, "not an absolute path");
    }
    const std::filesystem::path absolute =
      std::filesystem::weakly_canonical(path, status);
    if (status || absolute.empty()) {
      return refuseGrant(path, "unresolvable path");
    }
    std::uint64_t bytes = 0;
    if (readable) {
      if (!std::filesystem::is_regular_file(absolute)) {
        return refuseGrant(absolute, "not a regular file");
      }
      bytes = std::filesystem::file_size(absolute, status);
      if (status || bytes > limits.fileBytes) {
        return refuseGrant(absolute, "unreadable size or over the file limit");
      }
    }
    if (writable && !std::filesystem::is_directory(absolute.parent_path())) {
      return refuseGrant(absolute, "its directory does not exist");
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (stopping || selected.size() >= 16 ||
        (!fixedName.empty() && selected.contains(fixedName))) {
      return refuseGrant(absolute,
                         "services stopping, 16 grants held, or name taken");
    }
    name =
      fixedName.empty() ? "sel-" + std::to_string(++nextSelected) : fixedName;
    selected[name] = SelectedGrant{ absolute, writable };
    size = bytes;
    try {
      logGrant(
        false,
        "Granted the guest " +
          std::string(writable ? (readable ? "read-write" : "write") : "read") +
          " access to " + displayPath(absolute) + " as '" + name + "'");
    } catch (...) {
    }
    return true;
  }
  bool has(GuestCapability capability) const
  {
    return (grants & static_cast<std::uint32_t>(capability)) != 0;
  }
  // A path the guest may write: inside the single-layer writable /project
  // mount, never a module or a manifest.
  bool projectTarget(const std::string& normalized) const
  {
    std::string_view relative;
    const std::shared_ptr<const VfsMountTable> table = packages->table();
    const VfsMount* mount = table->resolve(normalized, &relative);
    if (mount == nullptr || mount->point != "/project" || relative.empty() ||
        mount->layers.size() != 1 ||
        !mount->layers.front().backend->writable()) {
      return false;
    }
    std::string name(VirtualPath::fileName(normalized));
    std::transform(name.begin(), name.end(), name.begin(), [](char c) {
      return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    return name != "illumo.json" &&
           (name.size() < 5 || name.compare(name.size() - 5, 5, ".wasm") != 0);
  }
  // Every file below a mounted directory as (relative name, size), walked
  // with an explicit stack; false past 100000 entries or on a listing error.
  bool walk(const std::string& root,
            std::vector<std::pair<std::string, std::uint64_t>>& found) const
  {
    std::vector<std::string> pending{ std::string() };
    std::size_t visited = 0;
    while (!pending.empty()) {
      const std::string relative = pending.back();
      pending.pop_back();
      std::vector<VfsEntry> entries;
      std::string listError;
      if (!packages->list(relative.empty() ? root : root + "/" + relative,
                          entries,
                          listError)) {
        return false;
      }
      for (const VfsEntry& entry : entries) {
        if (++visited > 100000u) {
          return false;
        }
        const std::string child =
          relative.empty() ? entry.name : relative + "/" + entry.name;
        if (entry.kind == VfsKind::Directory) {
          pending.push_back(child);
        } else {
          found.push_back({ child, entry.size });
        }
      }
    }
    return true;
  }
  // Whether replacing target with size bytes stays inside the project quota,
  // and the project total afterwards. The total is measured once and then
  // maintained per write (only the IO worker writes the project).
  bool projectFits(const std::string& target,
                   std::uint64_t size,
                   std::uint64_t* after)
  {
    if (!projectMeasured) {
      std::vector<std::pair<std::string, std::uint64_t>> found;
      if (!walk("/project", found)) {
        return false;
      }
      projectBytes = 0;
      for (const std::pair<std::string, std::uint64_t>& file : found) {
        projectBytes += file.second;
      }
      projectMeasured = true;
    }
    std::uint64_t previous = 0;
    VfsStat stat;
    std::string statError;
    if (packages->stat(target, stat, statError) && stat.kind == VfsKind::File) {
      previous = stat.size;
    }
    const std::uint64_t without =
      projectBytes - std::min(previous, projectBytes);
    if (size > limits.projectBytes || without > limits.projectBytes - size) {
      return false;
    }
    *after = without + size;
    return true;
  }
  GuestFileOutcome query(const GuestFileRequest& request,
                         GuestWireWriter& response) const
  {
    std::string normalized;
    if (!has(GuestCapability::Assets) ||
        !VirtualPath::normalize(request.path, normalized)) {
      return GuestFileOutcome::Denied;
    }
    VfsStat stat;
    std::string lookupError;
    if (!packages->stat(normalized, stat, lookupError)) {
      return GuestFileOutcome::NotFound;
    }
    if (request.action == GuestFileAction::Stat) {
      GuestFileStatus{ stat.kind == VfsKind::Directory,
                       stat.size,
                       stat.packageId }
        .write(response);
      return GuestFileOutcome::Success;
    }
    if (stat.kind != VfsKind::Directory) {
      return GuestFileOutcome::Denied;
    }
    std::vector<VfsEntry> entries;
    std::size_t total = 0;
    if (!packages->list(normalized,
                        entries,
                        lookupError,
                        static_cast<std::size_t>(request.offset),
                        static_cast<std::size_t>(request.size),
                        &total)) {
      return GuestFileOutcome::IoError;
    }
    GuestFileListing listing;
    listing.total =
      static_cast<std::uint32_t>(std::min<std::size_t>(total, UINT32_MAX));
    for (const VfsEntry& entry : entries) {
      listing.entries.push_back(
        { entry.name, entry.kind == VfsKind::Directory, entry.size });
    }
    listing.write(response);
    return GuestFileOutcome::Success;
  }
  bool selectedPath(const std::string& name,
                    bool writable,
                    std::filesystem::path* path)
  {
    std::lock_guard<std::mutex> lock(mutex);
    const std::map<std::string, SelectedGrant>::const_iterator found =
      selected.find(name);
    if (found == selected.end() || (writable && !found->second.writable)) {
      return false;
    }
    *path = found->second.path;
    return true;
  }
  GuestFileOutcome importGrant(const GuestFileRequest& request)
  {
    std::filesystem::path source;
    std::string target;
    if (!has(GuestCapability::ProjectFiles) ||
        !has(GuestCapability::SelectedFiles) ||
        !selectedPath(request.path, false, &source) ||
        !VirtualPath::normalize(request.target, target) ||
        !projectTarget(target)) {
      return GuestFileOutcome::Denied;
    }
    std::error_code status;
    if (!std::filesystem::is_regular_file(source, status)) {
      return GuestFileOutcome::NotFound;
    }
    const std::uint64_t size = std::filesystem::file_size(source, status);
    std::uint64_t after = 0;
    if (status || size > limits.fileBytes ||
        !projectFits(target, size, &after)) {
      return GuestFileOutcome::Denied;
    }
    std::ifstream input(source, std::ios::binary);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (size > 0) {
      input.read(reinterpret_cast<char*>(bytes.data()),
                 static_cast<std::streamsize>(size));
    }
    std::string writeError;
    if ((size > 0 && input.gcount() != static_cast<std::streamsize>(size)) ||
        !packages->write(target, bytes, writeError)) {
      return GuestFileOutcome::IoError;
    }
    projectBytes = after;
    return GuestFileOutcome::Success;
  }
  // Runs on the pack worker: packs a project directory holding a valid
  // illumo.json into the writable Selected grant.
  GuestFileOutcome packProject(const GuestFileRequest& request)
  {
    std::filesystem::path destination;
    std::string source;
    if (!has(GuestCapability::ProjectFiles) ||
        !has(GuestCapability::SelectedFiles) ||
        !selectedPath(request.path, true, &destination) ||
        !VirtualPath::normalize(request.target, source) ||
        !VirtualPath::isWithin(source, "/project") ||
        packages->table()->find("/project") == nullptr) {
      return GuestFileOutcome::Denied;
    }
    // A missing or invalid manifest is a refusal; anything later is I/O.
    std::vector<std::uint8_t> manifestBytes;
    std::string packError;
    PackageManifest manifest;
    if (!packages->read(source + "/" + PackageManifest::kFileName,
                        manifestBytes,
                        packError) ||
        manifestBytes.size() > PackageMounts::kMaximumManifestBytes ||
        !decodePackageManifest(
          std::string(manifestBytes.begin(), manifestBytes.end()),
          PackageCeilings{},
          manifest,
          packError)) {
      return GuestFileOutcome::Denied;
    }
    return PackageMounts::packMounted(
             *packages, source, destination, limits.projectBytes, packError)
             ? GuestFileOutcome::Success
             : GuestFileOutcome::IoError;
  }
  GuestFileOutcome execute(const GuestFileRequest& request,
                           GuestWireWriter& response)
  {
    if (request.action == GuestFileAction::List ||
        request.action == GuestFileAction::Stat) {
      return query(request, response);
    }
    if (request.action == GuestFileAction::Import) {
      return importGrant(request);
    }
    if (request.action == GuestFileAction::Pack) {
      return packProject(request);
    }
    if (request.action == GuestFileAction::Open) {
      const bool mountedArea = request.area == GuestFileArea::Mounted;
      const GuestCapability capability =
        request.area == GuestFileArea::Package ||
            (mountedArea && !request.writing)
          ? GuestCapability::Assets
          : (mountedArea ? GuestCapability::ProjectFiles
                         : (request.area == GuestFileArea::Selected
                              ? GuestCapability::SelectedFiles
                              : GuestCapability::Storage));
      if (!has(capability) ||
          (request.writing && request.area == GuestFileArea::Package)) {
        return GuestFileOutcome::Denied;
      }
      std::filesystem::path path;
      bool selectedGrant = false;
      std::shared_ptr<VfsFile> mounted;
      std::string virtualTarget;
      if (request.area == GuestFileArea::Package || mountedArea) {
        // The Package area is the launched package's /app view: a directory
        // or an .ilpk, with any overlays merged in. Mounted paths address the
        // whole tree.
        std::string virtualPath;
        if (mountedArea ? !VirtualPath::normalize(request.path, virtualPath)
                        : !relativeName(request.path)) {
          return GuestFileOutcome::Denied;
        }
        if (!mountedArea) {
          virtualPath = "/app/" + request.path;
        }
        if (request.writing) {
          if (!projectTarget(virtualPath)) {
            return GuestFileOutcome::Denied;
          }
          virtualTarget = virtualPath;
        } else {
          VfsStat stat;
          std::string lookupError;
          if (!packages->stat(virtualPath, stat, lookupError)) {
            return GuestFileOutcome::NotFound;
          }
          if (stat.kind != VfsKind::File || stat.size > limits.fileBytes) {
            return GuestFileOutcome::Denied;
          }
          mounted = packages->open(virtualPath, lookupError);
          if (!mounted) {
            return GuestFileOutcome::IoError;
          }
        }
      } else if (request.area == GuestFileArea::Selected) {
        std::lock_guard<std::mutex> lock(mutex);
        const std::map<std::string, SelectedGrant>::const_iterator found =
          selected.find(request.path);
        if (found == selected.end() ||
            (request.writing && !found->second.writable)) {
          return GuestFileOutcome::Denied;
        }
        path = found->second.path;
        selectedGrant = true;
      } else {
        path = resolve(request);
      }
      if (path.empty() && !mounted && virtualTarget.empty()) {
        return GuestFileOutcome::Denied;
      }
      std::size_t index = 0;
      while (index < files.size() &&
             (files[index].file || files[index].generation == UINT32_MAX)) {
        ++index;
      }
      if (index == files.size()) {
        return GuestFileOutcome::Denied;
      }
      std::unique_ptr<File> file = std::make_unique<File>();
      file->writing = request.writing;
      file->selected = selectedGrant;
      file->destination = path;
      file->block = GuestFileRequest::blockFor(request.area);
      if (request.writing) {
        if (request.size > limits.fileBytes || reserved > limits.stagedBytes ||
            request.size > (limits.stagedBytes - reserved) / 2) {
          return GuestFileOutcome::Denied;
        }
        file->size = request.size;
        file->virtualDestination = virtualTarget;
        if (!virtualTarget.empty()) {
          file->buffer.reserve(static_cast<std::size_t>(request.size));
        }
        for (unsigned int attempt = 0; virtualTarget.empty() && attempt < 32;
             ++attempt) {
          file->staging =
            path.parent_path() / (".illumo-wasm-" + std::to_string(owner) +
                                  "-" + std::to_string(++nextStage));
          file->stream.clear();
          file->stream.open(file->staging,
                            std::ios::binary | std::ios::out |
                              std::ios::noreplace);
          if (file->stream.is_open()) {
            break;
          }
          // A collision is not ours to delete during cleanup.
          file->staging.clear();
        }
        if (virtualTarget.empty() && !file->stream.is_open()) {
          return GuestFileOutcome::IoError;
        }
      } else if (mounted) {
        file->size = mounted->size();
        file->mounted = std::move(mounted);
      } else {
        if (!std::filesystem::exists(path)) {
          return GuestFileOutcome::NotFound;
        }
        if (!std::filesystem::is_regular_file(path)) {
          return GuestFileOutcome::Denied;
        }
        file->size = std::filesystem::file_size(path);
        if (file->size > limits.fileBytes) {
          return GuestFileOutcome::Denied;
        }
        file->stream.open(path, std::ios::binary | std::ios::in);
        if (!file->stream.is_open()) {
          return GuestFileOutcome::IoError;
        }
      }
      Slot& slot = files[index];
      GuestResourceId{ owner,
                       GuestResourceKind::File,
                       static_cast<std::uint32_t>(index + 1),
                       slot.generation + 1 }
        .write(response);
      response.u64(file->size);
      // AtomicFile's commit writer needs a second sibling staging file.
      if (file->writing) {
        reserved += file->size * 2;
      }
      slot.file = std::move(file);
      ++slot.generation;
      return GuestFileOutcome::Success;
    }
    File* file = find(request.file);
    if (!file) {
      return GuestFileOutcome::Denied;
    }
    if (request.action == GuestFileAction::Close) {
      close(request.file);
      return GuestFileOutcome::Success;
    }
    if (request.action == GuestFileAction::Read) {
      if (file->writing || request.size > file->block ||
          request.offset > file->size ||
          request.size > file->size - request.offset) {
        return GuestFileOutcome::Denied;
      }
      std::vector<std::byte> bytes(static_cast<std::size_t>(request.size));
      if (file->mounted) {
        std::vector<uint8_t> data;
        std::string readError;
        if (!file->mounted->read(
              request.offset, bytes.size(), data, readError) ||
            data.size() != bytes.size()) {
          return GuestFileOutcome::IoError;
        }
        if (!data.empty()) {
          std::memcpy(bytes.data(), data.data(), data.size());
        }
        response.bytes(bytes);
        return GuestFileOutcome::Success;
      }
      file->stream.clear();
      file->stream.seekg(static_cast<std::streamoff>(request.offset));
      file->stream.read(reinterpret_cast<char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size()));
      if (!file->stream ||
          file->stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return GuestFileOutcome::IoError;
      }
      response.bytes(bytes);
      return GuestFileOutcome::Success;
    }
    if (!file->writing) {
      return GuestFileOutcome::Denied;
    }
    if (request.action == GuestFileAction::Write) {
      if (request.offset != file->written ||
          request.data.size() > file->block ||
          request.data.size() > file->size - file->written) {
        return GuestFileOutcome::Denied;
      }
      if (!file->virtualDestination.empty()) {
        file->buffer.insert(
          file->buffer.end(), request.data.begin(), request.data.end());
        file->written += request.data.size();
        response.u64(file->written);
        return GuestFileOutcome::Success;
      }
      file->stream.write(reinterpret_cast<const char*>(request.data.data()),
                         static_cast<std::streamsize>(request.data.size()));
      if (!file->stream) {
        return GuestFileOutcome::IoError;
      }
      file->written += request.data.size();
      response.u64(file->written);
      return GuestFileOutcome::Success;
    }
    if (request.action == GuestFileAction::Commit &&
        !file->virtualDestination.empty()) {
      std::uint64_t after = 0;
      if (file->written != file->size ||
          !projectFits(file->virtualDestination, file->size, &after)) {
        return GuestFileOutcome::Denied;
      }
      std::vector<std::uint8_t> bytes(file->buffer.size());
      if (!bytes.empty()) {
        std::memcpy(bytes.data(), file->buffer.data(), bytes.size());
      }
      std::string writeError;
      const bool written =
        packages->write(file->virtualDestination, bytes, writeError);
      close(request.file);
      if (!written) {
        return GuestFileOutcome::IoError;
      }
      projectBytes = after;
      return GuestFileOutcome::Success;
    }
    if (request.action != GuestFileAction::Commit ||
        file->written != file->size ||
        (!file->selected && !storageFits(*file))) {
      return GuestFileOutcome::Denied;
    }
    file->stream.flush();
    if (!file->stream) {
      return GuestFileOutcome::IoError;
    }
    file->stream.close();
    if (file->stream.fail()) {
      return GuestFileOutcome::IoError;
    }
    const bool committed = AtomicFile::write(
      file->destination, [&file](std::ostream& destination, std::string*) {
        std::ifstream source(file->staging, std::ios::binary);
        std::array<char, GuestFileRequest::MaximumBlock> block{};
        std::uint64_t copied = 0;
        while (copied < file->size && source && destination) {
          const std::streamsize size = static_cast<std::streamsize>(
            std::min<std::uint64_t>(block.size(), file->size - copied));
          source.read(block.data(), size);
          if (source.gcount() != size) {
            return false;
          }
          destination.write(block.data(), size);
          copied += static_cast<std::uint64_t>(size);
        }
        return copied == file->size && destination.good();
      });
    close(request.file);
    return committed ? GuestFileOutcome::Success : GuestFileOutcome::IoError;
  }
  void work()
  try {
    for (;;) {
      Request request;
      {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock,
                       [this]() { return stopping || !requests.empty(); });
        if (stopping) {
          break;
        }
        request = std::move(requests.front());
        requests.pop_front();
        // Packing can take seconds, so it runs on its own worker and never
        // holds up asset reads.
        if (request.value.action == GuestFileAction::Pack) {
          packRequests.push_back(std::move(request));
          packCondition.notify_one();
          continue;
        }
      }
      complete(request);
    }
    files.clear();
  } catch (...) {
    std::lock_guard<std::mutex> lock(mutex);
    failed = true;
    stopping = true;
  }
  void complete(const Request& request)
  {
    GuestServiceRecord completion{
      request.id, GuestService::File, GuestServiceStatus::Complete, {}
    };
    GuestWireWriter payload;
    GuestFileOutcome outcome = GuestFileOutcome::IoError;
    try {
      outcome = execute(request.value, payload);
    } catch (const std::exception&) {
    }
    GuestWireWriter response;
    response.u32(static_cast<std::uint32_t>(outcome));
    if (outcome == GuestFileOutcome::Success) {
      response.bytes(payload.data());
    }
    completion.payload = response.take();
    std::lock_guard<std::mutex> lock(mutex);
    if (!stopping) {
      completions.records.push_back(std::move(completion));
    }
  }
  void packWork()
  try {
    for (;;) {
      Request request;
      {
        std::unique_lock<std::mutex> lock(mutex);
        packCondition.wait(
          lock, [this]() { return stopping || !packRequests.empty(); });
        if (stopping) {
          break;
        }
        request = std::move(packRequests.front());
        packRequests.pop_front();
      }
      complete(request);
    }
  } catch (...) {
    std::lock_guard<std::mutex> lock(mutex);
    failed = true;
    stopping = true;
  }
  static const char* outcomeName(std::uint32_t outcome)
  {
    switch (static_cast<GuestFileOutcome>(outcome)) {
      case GuestFileOutcome::Success:
        return "success";
      case GuestFileOutcome::NotFound:
        return "not found";
      case GuestFileOutcome::Denied:
        return "denied";
      case GuestFileOutcome::IoError:
        return "I/O error";
      case GuestFileOutcome::Cancelled:
        return "cancelled";
    }
    return "unknown";
  }
  // Main thread only (submit and poll): logs imports, packs and I/O errors
  // as their completions are handed back. The workers never log.
  void report(const GuestServices& results)
  {
    for (const GuestServiceRecord& record : results.records) {
      GuestWireReader reader(record.payload);
      const std::uint32_t outcome = reader.u32();
      const std::map<std::uint64_t, std::string>::iterator found =
        tracked.find(record.request);
      if (found != tracked.end()) {
        if (outcome == static_cast<std::uint32_t>(GuestFileOutcome::Success)) {
          Logger::LogTrace(found->second + " completed");
        } else {
          Logger::LogWarning(found->second + " ended: " + outcomeName(outcome));
        }
        tracked.erase(found);
      } else if (outcome ==
                 static_cast<std::uint32_t>(GuestFileOutcome::IoError)) {
        Logger::LogWarning("Guest file request " +
                           std::to_string(record.request) +
                           " ended with an I/O error");
      }
    }
  }
  struct SelectedGrant
  {
    std::filesystem::path path;
    bool writable = false;
  };
  // Descriptions of submitted imports and packs, by request id.
  std::map<std::uint64_t, std::string> tracked;
  const std::uint64_t owner;
  const std::uint32_t grants;
  const WasmFileLimits limits;
  std::shared_ptr<const VirtualFileSystem> packages;
  std::filesystem::path storageRoot;
  std::vector<Slot> files;
  std::map<std::string, SelectedGrant> selected;
  std::uint64_t reserved = 0, nextStage = 0, lastRequest = 0, nextSelected = 0;
  mutable std::mutex mutex;
  std::condition_variable condition;
  std::list<Request> requests;
  std::list<Request> packRequests;
  std::condition_variable packCondition;
  // Project bytes, measured on first use and then maintained per write.
  std::uint64_t projectBytes = 0;
  bool projectMeasured = false;
  GuestServices completions;
  std::size_t outstanding = 0;
  bool stopping = false;
  bool failed = false;
  std::thread worker;
  std::thread packer;
  std::string error;
};

WasmFileServices::WasmFileServices(std::uint64_t owner,
                                   std::uint32_t grants,
                                   std::filesystem::path packageRoot,
                                   std::filesystem::path storageRoot,
                                   WasmFileLimits limits)
  : WasmFileServices(owner,
                     grants,
                     packageDirectory(packageRoot),
                     std::move(storageRoot),
                     limits)
{
}
WasmFileServices::WasmFileServices(
  std::uint64_t owner,
  std::uint32_t grants,
  std::shared_ptr<const VirtualFileSystem> packages,
  std::filesystem::path storageRoot,
  WasmFileLimits limits)
  : m_state(std::make_unique<State>(owner,
                                    grants,
                                    std::move(packages),
                                    std::move(storageRoot),
                                    limits))
{
  Logger::LogTrace(
    "Guest file services started: " + std::to_string(limits.openFiles) +
    " file slots, " + std::to_string(limits.storageBytes / (1024u * 1024u)) +
    " MiB storage quota, " +
    std::to_string(limits.projectBytes / (1024u * 1024u)) +
    " MiB project quota");
}
std::shared_ptr<const VirtualFileSystem>
WasmFileServices::packageDirectory(const std::filesystem::path& root)
{
  if (!root.is_absolute()) {
    throw std::invalid_argument("Invalid file service policy");
  }
  std::string error;
  std::shared_ptr<DirectoryVfsBackend> backend =
    DirectoryVfsBackend::open(root, false, error);
  if (!backend) {
    throw std::invalid_argument("File service roots must be directories");
  }
  std::shared_ptr<VirtualFileSystem> vfs =
    std::make_shared<VirtualFileSystem>();
  VfsMount mount;
  mount.point = "/app";
  mount.layers.push_back({ backend, "app" });
  if (!vfs->mount(std::move(mount), error)) {
    throw std::invalid_argument(error);
  }
  return vfs;
}
WasmFileServices::~WasmFileServices() = default;
bool
WasmFileServices::grantSelected(const std::filesystem::path& path,
                                bool writable,
                                std::string& name,
                                std::uint64_t& size)
{
  return m_state->grant(path, !writable, writable, {}, name, size);
}
bool
WasmFileServices::grantEditable(const std::filesystem::path& path,
                                std::string& name,
                                std::uint64_t& size)
{
  return m_state->grant(path, true, true, {}, name, size);
}
bool
WasmFileServices::grantLaunch(const std::filesystem::path& path,
                              bool editable,
                              std::uint64_t& size)
{
  std::string name;
  return m_state->grant(path, true, editable, "launch", name, size);
}
bool
WasmFileServices::submit(const GuestServices& incoming)
{
  State& state = *m_state;
  if (incoming.records.size() > GuestServices::MaximumRecords) {
    state.error = "File batch exceeds record quota";
    return false;
  }
  std::list<State::Request> validated;
  std::uint64_t last = state.lastRequest;
  for (const GuestServiceRecord& record : incoming.records) {
    GuestFileRequest request;
    if (record.operation != GuestService::File ||
        record.status != GuestServiceStatus::Request ||
        record.request <= last ||
        !GuestFileRequest::read(record.payload, request)) {
      state.error = "Invalid file request batch";
      return false;
    }
    validated.push_back({ record.request, std::move(request) });
    last = record.request;
  }
  std::lock_guard<std::mutex> lock(state.mutex);
  if (state.stopping ||
      validated.size() > GuestServices::MaximumRecords - state.outstanding) {
    state.error = "File request queue exhausted";
    return false;
  }
  for (const State::Request& request : validated) {
    if (request.value.action == GuestFileAction::Import) {
      state.tracked[request.id] = "Project import into " + request.value.target;
    } else if (request.value.action == GuestFileAction::Pack) {
      state.tracked[request.id] = "Pack of " + request.value.target;
    }
  }
  state.outstanding += validated.size();
  state.requests.splice(state.requests.end(), validated);
  state.lastRequest = last;
  state.condition.notify_one();
  return true;
}
GuestServices
WasmFileServices::poll(std::size_t byteBudget)
{
  State& state = *m_state;
  GuestServices result;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.failed) {
      throw std::runtime_error("Native file worker failed");
    }
    result.records.reserve(state.completions.records.size());
    std::size_t bytes = 12;
    while (!state.completions.records.empty()) {
      const std::size_t size =
        state.completions.records.front().payload.size() + 20;
      if (bytes > byteBudget || size > byteBudget - bytes) {
        break;
      }
      result.records.push_back(std::move(state.completions.records.front()));
      state.completions.records.erase(state.completions.records.begin());
      bytes += size;
    }
    state.outstanding -= result.records.size();
  }
  // Outside the lock: the workers keep running while completions are logged.
  // Logging is best-effort; the completions are delivered regardless.
  try {
    state.report(result);
  } catch (...) {
  }
  return result;
}
std::size_t
WasmFileServices::pendingRequests() const
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  return m_state->outstanding;
}
void
WasmFileServices::cancel()
{
  m_state->cancel();
}
const std::string&
WasmFileServices::error() const
{
  return m_state->error;
}
