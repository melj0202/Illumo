#include <Illumo/Content/VirtualFileSystem.h>

#include <Illumo/Content/VirtualPath.h>
#include <Illumo/Platform/AtomicFile.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <deque>
#include <fstream>
#include <map>
#include <system_error>

static std::filesystem::path
fromUtf8(std::string_view text)
{
  return std::filesystem::path(
    std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

static std::string
toUtf8(const std::filesystem::path& path)
{
  const std::u8string text = path.generic_u8string();
  return std::string(text.begin(), text.end());
}

static bool
sortByName(const VfsEntry& a, const VfsEntry& b)
{
  return a.name < b.name;
}

bool
IVfsBackend::readRange(std::string_view relative,
                       uint64_t offset,
                       std::size_t bytes,
                       std::vector<uint8_t>& output,
                       std::string& error) const
{
  std::vector<uint8_t> whole;
  if (!read(relative, whole, error)) {
    return false;
  }
  const uint64_t start = std::min<uint64_t>(offset, whole.size());
  const uint64_t count = std::min<uint64_t>(bytes, whole.size() - start);
  output.assign(whole.begin() + static_cast<std::ptrdiff_t>(start),
                whole.begin() + static_cast<std::ptrdiff_t>(start + count));
  return true;
}

bool
IVfsBackend::write(std::string_view relative,
                   const std::vector<uint8_t>& bytes,
                   std::string& error)
{
  (void)relative;
  (void)bytes;
  error = "The mount is read-only";
  return false;
}

// --- Directory backend ---

std::shared_ptr<DirectoryVfsBackend>
DirectoryVfsBackend::open(const std::filesystem::path& root,
                          bool writable,
                          std::string& error)
{
  std::error_code code;
  const std::filesystem::path canonical =
    std::filesystem::canonical(root, code);
  if (code || !std::filesystem::is_directory(canonical, code)) {
    error = "Not a directory: " + toUtf8(root);
    return nullptr;
  }
  std::shared_ptr<DirectoryVfsBackend> backend =
    std::make_shared<DirectoryVfsBackend>();
  backend->m_root = canonical;
  backend->m_writable = writable;
  return backend;
}

bool
DirectoryVfsBackend::resolve(std::string_view relative,
                             std::filesystem::path* path) const
{
  if (relative.empty()) {
    *path = m_root;
    return true;
  }
  if (!VirtualPath::validRelative(relative)) {
    return false;
  }
  const std::filesystem::path candidate = m_root / fromUtf8(relative);
  std::error_code code;
  const std::filesystem::file_status status =
    std::filesystem::symlink_status(candidate, code);
  if (code || !std::filesystem::exists(status) ||
      std::filesystem::is_symlink(status)) {
    return false;
  }
  // The canonical form carries the on-disk case and resolves links, so it
  // matches only for an exact-case path that stays inside the root.
  const std::filesystem::path canonical =
    std::filesystem::canonical(candidate, code);
  if (code || toUtf8(canonical) != toUtf8(candidate)) {
    return false;
  }
  *path = candidate;
  return true;
}

bool
DirectoryVfsBackend::stat(std::string_view relative, VfsStat& output) const
{
  std::filesystem::path path;
  if (!resolve(relative, &path)) {
    return false;
  }
  std::error_code code;
  const std::filesystem::file_status status =
    std::filesystem::status(path, code);
  if (code) {
    return false;
  }
  VfsStat result;
  if (std::filesystem::is_directory(status)) {
    result.kind = VfsKind::Directory;
  } else if (std::filesystem::is_regular_file(status)) {
    result.kind = VfsKind::File;
    result.size = std::filesystem::file_size(path, code);
    if (code) {
      return false;
    }
  } else {
    return false;
  }
  const std::filesystem::file_time_type written =
    std::filesystem::last_write_time(path, code);
  result.stamp = code ? 0 : written.time_since_epoch().count();
  output = result;
  return true;
}

bool
DirectoryVfsBackend::list(std::string_view relative,
                          std::vector<VfsEntry>& entries) const
{
  std::filesystem::path path;
  std::error_code code;
  if (!resolve(relative, &path) || !std::filesystem::is_directory(path, code)) {
    return false;
  }
  std::vector<VfsEntry> result;
  std::filesystem::directory_iterator walk(path, code);
  const std::filesystem::directory_iterator done;
  for (; !code && walk != done; walk.increment(code)) {
    const std::filesystem::directory_entry& item = *walk;
    const std::string name = toUtf8(item.path().filename());
    // Invalid names include host staging files (".illumo-*").
    if (!VirtualPath::validComponent(name) || item.is_symlink(code)) {
      continue;
    }
    VfsEntry entry;
    entry.name = name;
    if (item.is_directory(code)) {
      entry.kind = VfsKind::Directory;
    } else if (item.is_regular_file(code)) {
      entry.kind = VfsKind::File;
      entry.size = item.file_size(code);
    } else {
      continue;
    }
    result.push_back(std::move(entry));
  }
  std::sort(result.begin(), result.end(), sortByName);
  entries = std::move(result);
  return true;
}

bool
DirectoryVfsBackend::read(std::string_view relative,
                          std::vector<uint8_t>& bytes,
                          std::string& error) const
{
  return readRange(relative, 0, SIZE_MAX, bytes, error);
}

bool
DirectoryVfsBackend::readRange(std::string_view relative,
                               uint64_t offset,
                               std::size_t bytes,
                               std::vector<uint8_t>& output,
                               std::string& error) const
{
  std::filesystem::path path;
  std::error_code code;
  if (!resolve(relative, &path) ||
      !std::filesystem::is_regular_file(path, code)) {
    error = "No such file";
    return false;
  }
  const uint64_t size = std::filesystem::file_size(path, code);
  if (code) {
    error = "Cannot read file";
    return false;
  }
  const uint64_t start = std::min(offset, size);
  const uint64_t count = std::min<uint64_t>(bytes, size - start);
  std::ifstream stream(path, std::ios::binary);
  std::vector<uint8_t> result(static_cast<std::size_t>(count));
  stream.seekg(static_cast<std::streamoff>(start));
  if (count > 0) {
    stream.read(reinterpret_cast<char*>(result.data()),
                static_cast<std::streamsize>(count));
  }
  if (!stream || static_cast<uint64_t>(stream.gcount()) != count) {
    if (count > 0) {
      error = "Cannot read file";
      return false;
    }
  }
  output = std::move(result);
  return true;
}

bool
DirectoryVfsBackend::write(std::string_view relative,
                           const std::vector<uint8_t>& bytes,
                           std::string& error)
{
  if (!m_writable) {
    error = "The mount is read-only";
    return false;
  }
  if (!VirtualPath::validRelative(relative)) {
    error = "Invalid file name";
    return false;
  }
  const std::lock_guard<std::mutex> lock(m_writeMutex);
  // Create missing parents one component at a time; an existing component
  // must be a real directory inside the root.
  std::size_t slash = relative.find('/');
  while (slash != std::string_view::npos) {
    const std::string_view parent = relative.substr(0, slash);
    std::filesystem::path existing;
    std::error_code code;
    if (resolve(parent, &existing)) {
      if (!std::filesystem::is_directory(existing, code)) {
        error = "A file is in the way of " + std::string(parent);
        return false;
      }
    } else {
      const std::filesystem::path created = m_root / fromUtf8(parent);
      if (std::filesystem::exists(
            std::filesystem::symlink_status(created, code)) ||
          !std::filesystem::create_directory(created, code)) {
        error = "Cannot create directory " + std::string(parent);
        return false;
      }
    }
    slash = relative.find('/', slash + 1);
  }
  const std::filesystem::path target = m_root / fromUtf8(relative);
  std::filesystem::path existing;
  std::error_code code;
  if (std::filesystem::exists(std::filesystem::symlink_status(target, code))) {
    if (!resolve(relative, &existing) ||
        !std::filesystem::is_regular_file(existing, code)) {
      error = "Cannot replace " + std::string(relative);
      return false;
    }
  }
  return AtomicFile::write(
    target,
    [&bytes](std::ostream& stream, std::string* writeError) {
      stream.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
      if (!stream && writeError != nullptr) {
        *writeError = "Cannot write the file";
      }
      return static_cast<bool>(stream);
    },
    &error);
}

// --- Archive backend ---

ArchiveVfsBackend::ArchiveVfsBackend(
  std::shared_ptr<const PackageArchive> archive)
  : m_archive(std::move(archive))
{
}

std::shared_ptr<ArchiveVfsBackend>
ArchiveVfsBackend::openFile(const std::filesystem::path& path,
                            std::string& error,
                            const PackageArchiveLimits& limits)
{
  std::shared_ptr<const PackageArchive> archive =
    PackageArchive::openFile(path, error, limits);
  if (!archive) {
    return nullptr;
  }
  return std::make_shared<ArchiveVfsBackend>(archive);
}

bool
ArchiveVfsBackend::stat(std::string_view relative, VfsStat& output) const
{
  VfsStat result;
  if (m_archive->isDirectory(relative)) {
    result.kind = VfsKind::Directory;
  } else {
    const PackageArchiveEntry* entry = m_archive->find(relative);
    if (entry == nullptr) {
      return false;
    }
    result.kind = VfsKind::File;
    result.size = entry->size;
  }
  output = result;
  return true;
}

bool
ArchiveVfsBackend::list(std::string_view relative,
                        std::vector<VfsEntry>& entries) const
{
  if (!m_archive->isDirectory(relative)) {
    return false;
  }
  std::vector<VfsEntry> result;
  const std::string prefix =
    relative.empty() ? std::string() : std::string(relative) + "/";
  for (const PackageArchiveChild& child : m_archive->list(relative)) {
    VfsEntry entry;
    entry.name = child.name;
    entry.kind = child.directory ? VfsKind::Directory : VfsKind::File;
    if (!child.directory) {
      const PackageArchiveEntry* file = m_archive->find(prefix + child.name);
      entry.size = file == nullptr ? 0 : file->size;
    }
    result.push_back(std::move(entry));
  }
  std::sort(result.begin(), result.end(), sortByName);
  entries = std::move(result);
  return true;
}

bool
ArchiveVfsBackend::read(std::string_view relative,
                        std::vector<uint8_t>& bytes,
                        std::string& error) const
{
  const PackageArchiveEntry* entry = m_archive->find(relative);
  if (entry == nullptr) {
    error = "No such file";
    return false;
  }
  return m_archive->read(*entry, bytes, error);
}

bool
ArchiveVfsBackend::readRange(std::string_view relative,
                             uint64_t offset,
                             std::size_t bytes,
                             std::vector<uint8_t>& output,
                             std::string& error) const
{
  const PackageArchiveEntry* entry = m_archive->find(relative);
  if (entry == nullptr) {
    error = "No such file";
    return false;
  }
  return m_archive->readRange(*entry, offset, bytes, output, error);
}

// --- Mount table ---

const VfsMount*
VfsMountTable::find(std::string_view point) const
{
  for (const VfsMount& mount : m_mounts) {
    if (mount.point == point) {
      return &mount;
    }
  }
  return nullptr;
}

const VfsMount*
VfsMountTable::resolve(std::string_view normalized,
                       std::string_view* relative) const
{
  for (const VfsMount& mount : m_mounts) {
    if (VirtualPath::isWithin(normalized, mount.point)) {
      *relative = VirtualPath::relativeTo(normalized, mount.point);
      return &mount;
    }
  }
  return nullptr;
}

bool
VfsMountTable::synthesized(std::string_view normalized,
                           std::vector<std::string>* names) const
{
  std::vector<std::string> found;
  for (const VfsMount& mount : m_mounts) {
    if (mount.point == normalized ||
        !VirtualPath::isWithin(mount.point, normalized)) {
      continue;
    }
    std::string_view below = VirtualPath::relativeTo(mount.point, normalized);
    const std::size_t slash = below.find('/');
    std::string name(slash == std::string_view::npos ? below
                                                     : below.substr(0, slash));
    if (std::find(found.begin(), found.end(), name) == found.end()) {
      found.push_back(std::move(name));
    }
  }
  if (found.empty() && normalized != "/") {
    return false;
  }
  std::sort(found.begin(), found.end());
  if (names != nullptr) {
    *names = std::move(found);
  }
  return true;
}

// --- Merged lookup ---

struct VfsResolved
{
  const VfsLayer* layer = nullptr;
  VfsStat stat;
  // Layers that contribute children when the result is a directory.
  std::vector<const VfsLayer*> directories;
};

// Walks a relative path component by component. At each level the topmost
// layer holding the name decides its kind; a file there hides the name in
// every lower layer, and only layers holding it as a directory continue.
static bool
resolveInMount(const VfsMount& mount,
               std::string_view relative,
               VfsResolved& output)
{
  std::vector<const VfsLayer*> eligible;
  for (const VfsLayer& layer : mount.layers) {
    eligible.push_back(&layer);
  }
  std::size_t cursor = 0;
  for (;;) {
    const std::size_t slash = relative.find('/', cursor);
    const bool last = slash == std::string_view::npos;
    const std::string_view prefix = last ? relative : relative.substr(0, slash);
    const VfsLayer* top = nullptr;
    VfsStat topStat;
    std::vector<const VfsLayer*> directories;
    for (const VfsLayer* layer : eligible) {
      VfsStat stat;
      if (!layer->backend->stat(prefix, stat)) {
        continue;
      }
      if (top == nullptr) {
        top = layer;
        topStat = stat;
      }
      if (stat.kind == VfsKind::Directory) {
        directories.push_back(layer);
      }
    }
    if (top == nullptr) {
      return false;
    }
    if (last) {
      output.layer = top;
      output.stat = topStat;
      output.stat.packageId = top->packageId;
      if (topStat.kind == VfsKind::Directory) {
        output.directories = std::move(directories);
      }
      return true;
    }
    if (topStat.kind != VfsKind::Directory) {
      return false;
    }
    eligible = std::move(directories);
    cursor = slash + 1;
  }
}

static bool
mergedList(const VfsResolved& resolved,
           std::string_view relative,
           std::vector<VfsEntry>& entries)
{
  if (resolved.directories.size() == 1) {
    return resolved.directories.front()->backend->list(relative, entries);
  }
  std::map<std::string, VfsEntry> merged;
  for (const VfsLayer* layer : resolved.directories) {
    std::vector<VfsEntry> children;
    if (!layer->backend->list(relative, children)) {
      continue;
    }
    for (VfsEntry& child : children) {
      merged.emplace(child.name, std::move(child));
    }
  }
  entries.clear();
  for (std::pair<const std::string, VfsEntry>& item : merged) {
    entries.push_back(std::move(item.second));
  }
  return true;
}

static std::string
joinRelative(std::string_view directory, std::string_view name)
{
  return directory.empty() ? std::string(name)
                           : std::string(directory) + "/" + std::string(name);
}

// Records every name that is a file in one layer and a directory in another.
static void
findConflicts(VfsMount& mount)
{
  static constexpr std::size_t kMaximumDirectories = 20000;
  static constexpr std::size_t kMaximumConflicts = 64;
  std::deque<std::string> pending;
  pending.push_back(std::string());
  std::size_t visited = 0;
  while (!pending.empty() && visited < kMaximumDirectories &&
         mount.conflicts.size() < kMaximumConflicts) {
    const std::string directory = pending.front();
    pending.pop_front();
    ++visited;
    VfsResolved resolved;
    if (!resolveInMount(mount, directory, resolved)) {
      continue;
    }
    std::map<std::string, std::pair<VfsKind, const VfsLayer*>> first;
    for (const VfsLayer* layer : resolved.directories) {
      std::vector<VfsEntry> children;
      layer->backend->list(directory, children);
      for (const VfsEntry& child : children) {
        std::map<std::string, std::pair<VfsKind, const VfsLayer*>>::iterator
          seen = first.find(child.name);
        if (seen == first.end()) {
          first.emplace(child.name, std::make_pair(child.kind, layer));
          if (child.kind == VfsKind::Directory) {
            pending.push_back(joinRelative(directory, child.name));
          }
        } else if (seen->second.first != child.kind &&
                   mount.conflicts.size() < kMaximumConflicts) {
          const bool topIsFile = seen->second.first == VfsKind::File;
          mount.conflicts.push_back(
            mount.point + "/" + joinRelative(directory, child.name) + ": " +
            (topIsFile ? "file" : "directory") + " from " +
            seen->second.second->packageId + " hides " +
            (topIsFile ? "directory" : "file") + " from " + layer->packageId);
        }
      }
    }
  }
}

// --- Open files ---

bool
VfsFile::read(uint64_t offset,
              std::size_t bytes,
              std::vector<uint8_t>& output,
              std::string& error) const
{
  return m_backend->readRange(m_relative, offset, bytes, output, error);
}

bool
VfsFile::readAll(std::vector<uint8_t>& output, std::string& error) const
{
  return m_backend->read(m_relative, output, error);
}

// --- The file system ---

bool
VirtualFileSystem::mount(VfsMount mount, std::string& error)
{
  std::string point;
  if (!VirtualPath::normalize(mount.point, point) || point != mount.point ||
      point == "/") {
    error = "Invalid mount point: " + mount.point;
    return false;
  }
  if (mount.layers.empty()) {
    error = "A mount needs at least one layer";
    return false;
  }
  for (const VfsLayer& layer : mount.layers) {
    if (!layer.backend) {
      error = "A mount layer has no backend";
      return false;
    }
  }
  mount.conflicts.clear();
  if (mount.layers.size() > 1) {
    findConflicts(mount);
  }
  std::string layers;
  for (const VfsLayer& layer : mount.layers) {
    layers +=
      (layers.empty() ? "" : ", ") +
      (layer.packageId.empty() ? std::string("(unnamed)") : layer.packageId);
  }
  const bool writable =
    mount.layers.size() == 1 && mount.layers.front().backend->writable();
  {
    const std::lock_guard<std::mutex> lock(m_mutex);
    for (const VfsMount& existing : m_table->mounts()) {
      if (VirtualPath::isWithin(point, existing.point) ||
          VirtualPath::isWithin(existing.point, point)) {
        error = "Mount point " + point + " overlaps " + existing.point;
        return false;
      }
    }
    std::shared_ptr<VfsMountTable> next =
      std::make_shared<VfsMountTable>(*m_table);
    next->m_mounts.push_back(std::move(mount));
    std::sort(
      next->m_mounts.begin(),
      next->m_mounts.end(),
      [](const VfsMount& a, const VfsMount& b) { return a.point < b.point; });
    m_table = next;
  }
  Logger::LogTrace("Virtual file tree mounted " + point +
                   (writable ? " (writable)" : "") + " from " + layers);
  return true;
}

bool
VirtualFileSystem::unmount(std::string_view point)
{
  {
    const std::lock_guard<std::mutex> lock(m_mutex);
    if (m_table->find(point) == nullptr) {
      return false;
    }
    std::shared_ptr<VfsMountTable> next = std::make_shared<VfsMountTable>();
    for (const VfsMount& mount : m_table->mounts()) {
      if (mount.point != point) {
        next->m_mounts.push_back(mount);
      }
    }
    m_table = next;
  }
  Logger::LogTrace("Virtual file tree unmounted " + std::string(point));
  return true;
}

std::shared_ptr<const VfsMountTable>
VirtualFileSystem::table() const
{
  const std::lock_guard<std::mutex> lock(m_mutex);
  return m_table;
}

static bool
normalizeRequest(std::string_view path, std::string& output, std::string& error)
{
  if (!VirtualPath::normalize(path, output)) {
    error = "Invalid virtual path: " + std::string(path);
    return false;
  }
  return true;
}

bool
VirtualFileSystem::stat(std::string_view path,
                        VfsStat& output,
                        std::string& error) const
{
  std::string normalized;
  if (!normalizeRequest(path, normalized, error)) {
    return false;
  }
  const std::shared_ptr<const VfsMountTable> snapshot = table();
  std::string_view relative;
  const VfsMount* mount = snapshot->resolve(normalized, &relative);
  if (mount != nullptr) {
    VfsResolved resolved;
    if (resolveInMount(*mount, relative, resolved)) {
      output = resolved.stat;
      return true;
    }
  } else if (snapshot->synthesized(normalized, nullptr)) {
    output = VfsStat{};
    output.kind = VfsKind::Directory;
    return true;
  }
  error = "No such path: " + normalized;
  return false;
}

bool
VirtualFileSystem::list(std::string_view path,
                        std::vector<VfsEntry>& entries,
                        std::string& error,
                        std::size_t offset,
                        std::size_t limit,
                        std::size_t* total) const
{
  std::string normalized;
  if (!normalizeRequest(path, normalized, error)) {
    return false;
  }
  const std::shared_ptr<const VfsMountTable> snapshot = table();
  std::string_view relative;
  const VfsMount* mount = snapshot->resolve(normalized, &relative);
  std::vector<VfsEntry> all;
  std::vector<std::string> names;
  if (mount != nullptr) {
    VfsResolved resolved;
    if (!resolveInMount(*mount, relative, resolved) ||
        resolved.stat.kind != VfsKind::Directory ||
        !mergedList(resolved, relative, all)) {
      error = "Not a directory: " + normalized;
      return false;
    }
  } else if (snapshot->synthesized(normalized, &names)) {
    for (const std::string& name : names) {
      VfsEntry entry;
      entry.name = name;
      entry.kind = VfsKind::Directory;
      all.push_back(entry);
    }
  } else {
    error = "No such path: " + normalized;
    return false;
  }
  if (total != nullptr) {
    *total = all.size();
  }
  entries.clear();
  for (std::size_t index = offset; index < all.size() && entries.size() < limit;
       ++index) {
    entries.push_back(std::move(all[index]));
  }
  return true;
}

bool
VirtualFileSystem::read(std::string_view path,
                        std::vector<uint8_t>& bytes,
                        std::string& error) const
{
  std::shared_ptr<VfsFile> file = open(path, error);
  return file && file->readAll(bytes, error);
}

bool
VirtualFileSystem::readRange(std::string_view path,
                             uint64_t offset,
                             std::size_t bytes,
                             std::vector<uint8_t>& output,
                             std::string& error) const
{
  std::shared_ptr<VfsFile> file = open(path, error);
  return file && file->read(offset, bytes, output, error);
}

std::shared_ptr<VfsFile>
VirtualFileSystem::open(std::string_view path, std::string& error) const
{
  std::string normalized;
  if (!normalizeRequest(path, normalized, error)) {
    return nullptr;
  }
  const std::shared_ptr<const VfsMountTable> snapshot = table();
  std::string_view relative;
  const VfsMount* mount = snapshot->resolve(normalized, &relative);
  VfsResolved resolved;
  if (mount == nullptr || !resolveInMount(*mount, relative, resolved)) {
    error = "No such file: " + normalized;
    return nullptr;
  }
  if (resolved.stat.kind != VfsKind::File) {
    error = "Not a file: " + normalized;
    return nullptr;
  }
  std::shared_ptr<VfsFile> file = std::make_shared<VfsFile>();
  file->m_backend = resolved.layer->backend;
  file->m_path = normalized;
  file->m_relative = std::string(relative);
  file->m_packageId = resolved.layer->packageId;
  file->m_size = resolved.stat.size;
  return file;
}

static bool
endsWithInsensitive(std::string_view text, std::string_view suffix)
{
  if (text.size() < suffix.size()) {
    return false;
  }
  for (std::size_t index = 0; index < suffix.size(); ++index) {
    char c = text[text.size() - suffix.size() + index];
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    if (c != suffix[index]) {
      return false;
    }
  }
  return true;
}

bool
VirtualFileSystem::write(std::string_view path,
                         const std::vector<uint8_t>& bytes,
                         std::string& error) const
{
  std::string normalized;
  if (!normalizeRequest(path, normalized, error)) {
    return false;
  }
  const std::shared_ptr<const VfsMountTable> snapshot = table();
  std::string_view relative;
  const VfsMount* mount = snapshot->resolve(normalized, &relative);
  if (mount == nullptr || relative.empty() || mount->layers.size() != 1 ||
      !mount->layers.front().backend->writable()) {
    error = "Not writable: " + normalized;
    return false;
  }
  const std::string_view name = VirtualPath::fileName(normalized);
  const bool module = endsWithInsensitive(name, ".wasm");
  const bool manifest =
    name.size() == 11 && endsWithInsensitive(name, "illumo.json");
  if (module || manifest) {
    error = "Refusing to write executable or manifest content: " + normalized;
    return false;
  }
  return mount->layers.front().backend->write(relative, bytes, error);
}
