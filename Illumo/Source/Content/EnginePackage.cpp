#include <Illumo/Content/EnginePackage.h>

#include <Illumo/Content/PackageArchive.h>
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Services/Logger.h>

#include <fstream>
#include <system_error>
#include <utility>

static constexpr const char kCanonicalPrefix[] = "engine:";
static constexpr std::size_t kCanonicalPrefixLength =
  sizeof(kCanonicalPrefix) - 1u;
// The engine package is read whole into memory; it is a few MiB today.
static constexpr std::uintmax_t kMaximumEngineArchiveBytes =
  256u * 1024u * 1024u;

static bool
startsWith(const std::string& text, std::string_view prefix)
{
  return text.size() >= prefix.size() &&
         text.compare(0, prefix.size(), prefix) == 0;
}

EngineArchiveBackend::EngineArchiveBackend(std::filesystem::path path,
                                           PackageCeilings ceilings)
  : m_path(std::move(path))
  , m_ceilings(ceilings)
{
}

std::shared_ptr<EngineArchiveBackend>
EngineArchiveBackend::open(const std::filesystem::path& path,
                           const PackageCeilings& ceilings,
                           std::string& error)
{
  ILLUMO_PROFILE_ZONE("EngineArchiveBackend.open");
  std::shared_ptr<EngineArchiveBackend> backend(
    new EngineArchiveBackend(path, ceilings));
  FileState state;
  std::shared_ptr<ArchiveVfsBackend> archive = backend->load(&state, error);
  if (!archive) {
    return nullptr;
  }
  backend->m_archive = std::move(archive);
  backend->m_generation = 1u;
  backend->m_taken = state;
  return backend;
}

std::shared_ptr<ArchiveVfsBackend>
EngineArchiveBackend::load(FileState* state, std::string& error) const
{
  ILLUMO_PROFILE_ZONE("EngineArchiveBackend.load");
  const std::string name = EnginePackage::kFileName;
  std::error_code code;
  FileState found;
  found.writeTime = std::filesystem::last_write_time(m_path, code);
  if (!code) {
    found.size = std::filesystem::file_size(m_path, code);
  }
  if (code) {
    error = "Cannot read " + name + ": " + code.message();
    return nullptr;
  }
  if (found.size > kMaximumEngineArchiveBytes) {
    error = name + " is larger than 256 MiB";
    return nullptr;
  }
  // The state is taken before the bytes, so a file replaced during the read
  // differs on the next refresh and is read again.
  std::vector<uint8_t> bytes(static_cast<std::size_t>(found.size));
  std::ifstream stream(m_path, std::ios::binary);
  if (!stream || !stream.read(reinterpret_cast<char*>(bytes.data()),
                              static_cast<std::streamsize>(bytes.size()))) {
    error = "Cannot read " + name;
    return nullptr;
  }
  stream.close();
  std::unique_ptr<PackageArchive> archive = PackageArchive::open(
    std::make_shared<MemoryPackageSource>(std::move(bytes)), error);
  if (!archive) {
    return nullptr;
  }
  std::shared_ptr<ArchiveVfsBackend> backend =
    std::make_shared<ArchiveVfsBackend>(
      std::shared_ptr<const PackageArchive>(std::move(archive)));
  std::vector<uint8_t> manifestBytes;
  PackageManifest manifest;
  if (!backend->read(PackageManifest::kFileName, manifestBytes, error)) {
    error = name + ": " + error;
    return nullptr;
  }
  if (!decodePackageManifest(
        std::string(manifestBytes.begin(), manifestBytes.end()),
        m_ceilings,
        manifest,
        error)) {
    error = name + ": " + error;
    return nullptr;
  }
  if (manifest.id != EnginePackage::kPackageId ||
      manifest.kind != PackageKind::Content) {
    error =
      name + " is not the " + EnginePackage::kPackageId + " content package";
    return nullptr;
  }
  *state = found;
  return backend;
}

bool
EngineArchiveBackend::refresh(std::string* error)
{
  std::lock_guard<std::mutex> refreshing(m_refreshMutex);
  std::error_code code;
  FileState now;
  now.writeTime = std::filesystem::last_write_time(m_path, code);
  if (!code) {
    now.size = std::filesystem::file_size(m_path, code);
  }
  // A package briefly missing mid-replacement keeps its current content.
  if (code || now == m_taken || now == m_rejected) {
    return false;
  }
  ILLUMO_PROFILE_ZONE("EngineArchiveBackend.refresh");
  FileState loaded;
  std::string reason;
  std::shared_ptr<ArchiveVfsBackend> next = load(&loaded, reason);
  if (!next) {
    m_rejected = now;
    if (error != nullptr) {
      *error = reason;
    }
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_archive = std::move(next);
    ++m_generation;
  }
  m_taken = loaded;
  return true;
}

std::shared_ptr<const ArchiveVfsBackend>
EngineArchiveBackend::current() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_archive;
}

std::uint64_t
EngineArchiveBackend::generation() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_generation;
}

std::int64_t
EngineArchiveBackend::memberStamp(std::string_view member) const
{
  const std::shared_ptr<const ArchiveVfsBackend> archive = current();
  const PackageArchiveEntry* entry =
    archive ? archive->archive().find(member) : nullptr;
  // Bit 32 keeps a zero CRC from reading as "unknown".
  return entry == nullptr ? 0
                          : (static_cast<std::int64_t>(entry->crc32) |
                             (std::int64_t{ 1 } << 32));
}

bool
EngineArchiveBackend::stat(std::string_view relative, VfsStat& output) const
{
  const std::shared_ptr<const ArchiveVfsBackend> archive = current();
  return archive && archive->stat(relative, output);
}

bool
EngineArchiveBackend::list(std::string_view relative,
                           std::vector<VfsEntry>& entries) const
{
  const std::shared_ptr<const ArchiveVfsBackend> archive = current();
  return archive && archive->list(relative, entries);
}

bool
EngineArchiveBackend::read(std::string_view relative,
                           std::vector<uint8_t>& bytes,
                           std::string& error) const
{
  const std::shared_ptr<const ArchiveVfsBackend> archive = current();
  return archive && archive->read(relative, bytes, error);
}

bool
EngineArchiveBackend::readRange(std::string_view relative,
                                uint64_t offset,
                                std::size_t bytes,
                                std::vector<uint8_t>& output,
                                std::string& error) const
{
  const std::shared_ptr<const ArchiveVfsBackend> archive = current();
  return archive && archive->readRange(relative, offset, bytes, output, error);
}

std::shared_ptr<EngineArchiveBackend>
EnginePackage::open(const std::filesystem::path& runtimeDirectory,
                    const PackageCeilings& ceilings,
                    std::string& error)
{
  ILLUMO_PROFILE_ZONE("EnginePackage.open");
  error.clear();
  std::error_code code;
  // A development build stages loose files, which always win.
  if (std::filesystem::is_directory(runtimeDirectory / "Assets", code)) {
    return nullptr;
  }
  const std::filesystem::path path = runtimeDirectory / kFileName;
  if (!std::filesystem::is_regular_file(path, code)) {
    return nullptr;
  }
  return EngineArchiveBackend::open(path, ceilings, error);
}

EnginePackageSource::EnginePackageSource(
  std::shared_ptr<EngineArchiveBackend> package,
  std::filesystem::path runtimeDirectory,
  IAssetSource* fallback)
  : m_package(std::move(package))
  , m_runtimeDirectory(std::move(runtimeDirectory).lexically_normal())
  , m_fallback(fallback)
{
}

bool
EnginePackageSource::memberFor(const std::string& path,
                               std::string* member) const
{
  if (!m_package || path.empty()) {
    return false;
  }
  std::filesystem::path name(path);
  if (name.is_absolute()) {
    name = name.lexically_normal().lexically_relative(m_runtimeDirectory);
  }
  std::string relative = name.lexically_normal().generic_string();
  if (relative.empty() || startsWith(relative, "..")) {
    return false;
  }
  std::string candidate;
  if (startsWith(relative, "Assets/")) {
    candidate = relative.substr(7);
  } else if (startsWith(relative, "Shader/")) {
    candidate = relative;
  } else {
    return false;
  }
  VfsStat stat;
  if (candidate.empty() || !m_package->stat(candidate, stat) ||
      stat.kind != VfsKind::File) {
    return false;
  }
  if (member != nullptr) {
    *member = std::move(candidate);
  }
  return true;
}

std::string
EnginePackageSource::canonical(const std::string& path) const
{
  // Canonical names are their own canonical form (AssetManager hands them
  // back, for example to shader preprocessing).
  if (startsWith(path, kCanonicalPrefix)) {
    return path;
  }
  std::string member;
  if (memberFor(path, &member)) {
    return kCanonicalPrefix + member;
  }
  return m_fallback != nullptr ? m_fallback->canonical(path) : path;
}

// Takes a rebuilt package, logging a replacement that had to be ignored once.
static void
followPackage(EngineArchiveBackend& package)
{
  std::string error;
  if (package.refresh(&error)) {
    Logger::LogInfo(std::string("Reloaded ") + EnginePackage::kFileName);
  } else if (!error.empty()) {
    Logger::LogWarning("Keeping the loaded engine package: " + error);
  }
}

// The member a canonical name, or a name as written ("Shader/x.glsl", which
// AssetManager's polling passes for shaders), stands for.
static bool
memberOf(const EnginePackageSource& source,
         const std::string& name,
         std::string* member)
{
  if (startsWith(name, kCanonicalPrefix)) {
    *member = name.substr(kCanonicalPrefixLength);
    return true;
  }
  return source.memberFor(name, member);
}

bool
EnginePackageSource::read(const std::string& canonical,
                          std::vector<unsigned char>& bytes) const
{
  std::string member;
  if (memberOf(*this, canonical, &member)) {
    // An explicit reload (F5, asset_reload) takes a rebuilt package.
    followPackage(*m_package);
    std::string error;
    return m_package->read(member, bytes, error);
  }
  return m_fallback != nullptr && m_fallback->read(canonical, bytes);
}

std::int64_t
EnginePackageSource::stamp(const std::string& canonical) const
{
  std::string member;
  if (memberOf(*this, canonical, &member)) {
    // Hot-reload polling: a rebuilt package changes the stamps of exactly
    // the members whose content changed.
    followPackage(*m_package);
    return m_package->memberStamp(member);
  }
  return m_fallback != nullptr ? m_fallback->stamp(canonical) : 0;
}
