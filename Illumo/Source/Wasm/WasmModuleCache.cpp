#include <Illumo/Wasm/WasmModuleCache.h>

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Platform/AtomicFile.h>

#include <wasmtime.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string_view>

// Entry layout, little-endian: magic, format, engine options, the Wasmtime
// version (length-prefixed), module SHA-256, artifact size and checksum,
// then the artifact.
static constexpr std::array<char, 4> kMagic = { 'I', 'L', 'W', 'C' };
static constexpr std::uint32_t kFormat = 1u;
static constexpr std::string_view kWasmtimeVersion = WASMTIME_VERSION;
static constexpr std::uint64_t kMaximumArtifactBytes = 512ull * 1024u * 1024u;
static constexpr char kExtension[] = ".cwasm";

// Corruption check over the artifact (identity is the module digest): FNV-1a
// over little-endian 64-bit words, then the tail and the length.
static std::uint64_t
artifactChecksum(std::span<const std::byte> bytes)
{
  constexpr std::uint64_t kPrime = 0x100000001b3ull;
  std::uint64_t hash = 0xcbf29ce484222325ull;
  const std::size_t words = bytes.size() / 8u;
  for (std::size_t index = 0u; index < words; ++index) {
    std::uint64_t word = 0u;
    std::memcpy(&word, bytes.data() + index * 8u, 8u);
    hash = (hash ^ word) * kPrime;
  }
  for (std::size_t index = words * 8u; index < bytes.size(); ++index) {
    hash = (hash ^ static_cast<std::uint64_t>(bytes[index])) * kPrime;
  }
  return (hash ^ static_cast<std::uint64_t>(bytes.size())) * kPrime;
}

static void
putU32(std::ostream& stream, std::uint32_t value)
{
  std::array<char, 4> bytes{};
  for (unsigned int index = 0u; index < 4u; ++index) {
    bytes[index] = static_cast<char>((value >> (index * 8u)) & 0xffu);
  }
  stream.write(bytes.data(), bytes.size());
}

static void
putU64(std::ostream& stream, std::uint64_t value)
{
  std::array<char, 8> bytes{};
  for (unsigned int index = 0u; index < 8u; ++index) {
    bytes[index] = static_cast<char>((value >> (index * 8u)) & 0xffu);
  }
  stream.write(bytes.data(), bytes.size());
}

static bool
getU32(std::istream& stream, std::uint32_t& value)
{
  std::array<unsigned char, 4> bytes{};
  stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  value = 0u;
  for (unsigned int index = 0u; index < 4u; ++index) {
    value |= static_cast<std::uint32_t>(bytes[index]) << (index * 8u);
  }
  return stream.good();
}

static bool
getU64(std::istream& stream, std::uint64_t& value)
{
  std::array<unsigned char, 8> bytes{};
  stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  value = 0u;
  for (unsigned int index = 0u; index < 8u; ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8u);
  }
  return stream.good();
}

bool
WasmModuleCache::Key::operator<(const Key& other) const
{
  return module != other.module ? module < other.module
                                : options < other.options;
}

WasmModuleCache::WasmModuleCache(std::filesystem::path directory,
                                 std::size_t maximumFiles)
  : m_maximumFiles(std::max<std::size_t>(1u, maximumFiles))
{
  if (directory.empty()) {
    return;
  }
  std::error_code error;
  std::filesystem::create_directories(directory, error);
  if (!error && std::filesystem::is_directory(directory, error) && !error) {
    m_directory = std::move(directory);
  }
}

WasmModuleCache::~WasmModuleCache() = default;

std::filesystem::path
WasmModuleCache::entryPath(const Key& key) const
{
  static constexpr char kHex[] = "0123456789abcdef";
  std::string name;
  name.reserve(80u);
  for (std::uint8_t byte : key.module) {
    name.push_back(kHex[byte >> 4u]);
    name.push_back(kHex[byte & 15u]);
  }
  name += "-" + std::to_string(key.options) + kExtension;
  return m_directory / name;
}

WasmModuleCache::DiskResult
WasmModuleCache::readEntry(const Key& key,
                           std::vector<std::byte>& artifact) const
{
  ILLUMO_PROFILE_ZONE("WasmModuleCache.readEntry");
  if (m_directory.empty()) {
    return DiskResult::Missing;
  }
  const std::filesystem::path path = entryPath(key);
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return DiskResult::Missing;
  }
  std::array<char, 4> magic{};
  stream.read(magic.data(), magic.size());
  std::uint32_t format = 0u;
  std::uint32_t options = 0u;
  std::uint32_t versionLength = 0u;
  if (!stream.good() || magic != kMagic || !getU32(stream, format) ||
      format != kFormat || !getU32(stream, options) || options != key.options ||
      !getU32(stream, versionLength) ||
      versionLength != kWasmtimeVersion.size()) {
    return DiskResult::Rejected;
  }
  std::string version(versionLength, '\0');
  stream.read(version.data(), static_cast<std::streamsize>(versionLength));
  Sha256::Digest module{};
  stream.read(reinterpret_cast<char*>(module.data()), module.size());
  std::uint64_t size = 0u;
  std::uint64_t checksum = 0u;
  if (!stream.good() || version != kWasmtimeVersion || module != key.module ||
      !getU64(stream, size) || !getU64(stream, checksum) || size == 0u ||
      size > kMaximumArtifactBytes) {
    return DiskResult::Rejected;
  }
  artifact.resize(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char*>(artifact.data()),
              static_cast<std::streamsize>(size));
  // The entry must end exactly after the artifact.
  if (!stream.good() || stream.peek() != std::ifstream::traits_type::eof() ||
      artifactChecksum(artifact) != checksum) {
    artifact.clear();
    return DiskResult::Rejected;
  }
  // Recently used entries survive pruning.
  std::error_code touched;
  std::filesystem::last_write_time(
    path, std::filesystem::file_time_type::clock::now(), touched);
  return DiskResult::Found;
}

bool
WasmModuleCache::writeEntry(const Key& key,
                            const std::vector<std::byte>& artifact) const
{
  ILLUMO_PROFILE_ZONE("WasmModuleCache.writeEntry");
  if (m_directory.empty() || artifact.empty() ||
      artifact.size() > kMaximumArtifactBytes) {
    return false;
  }
  const std::uint64_t checksum = artifactChecksum(artifact);
  const bool written = AtomicFile::write(
    entryPath(key),
    [&key, &artifact, checksum](std::ostream& stream, std::string*) {
      stream.write(kMagic.data(), kMagic.size());
      putU32(stream, kFormat);
      putU32(stream, key.options);
      putU32(stream, static_cast<std::uint32_t>(kWasmtimeVersion.size()));
      stream.write(kWasmtimeVersion.data(),
                   static_cast<std::streamsize>(kWasmtimeVersion.size()));
      stream.write(reinterpret_cast<const char*>(key.module.data()),
                   key.module.size());
      putU64(stream, artifact.size());
      putU64(stream, checksum);
      stream.write(reinterpret_cast<const char*>(artifact.data()),
                   static_cast<std::streamsize>(artifact.size()));
      return stream.good();
    });
  if (written) {
    prune();
  }
  return written;
}

void
WasmModuleCache::prune() const
{
  // Keep the newest entries; staging files AtomicFile left behind by a
  // terminated process go once they are an hour old.
  struct File
  {
    std::filesystem::path path;
    std::filesystem::file_time_type time;
  };
  std::vector<File> entries;
  const std::filesystem::file_time_type staleBefore =
    std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
  std::error_code error;
  std::filesystem::directory_iterator it(m_directory, error);
  const std::filesystem::directory_iterator end;
  for (; !error && it != end; it.increment(error)) {
    std::error_code fileError;
    if (!it->is_regular_file(fileError) || fileError) {
      continue;
    }
    const std::filesystem::file_time_type time = it->last_write_time(fileError);
    if (fileError) {
      continue;
    }
    const std::filesystem::path& path = it->path();
    if (path.extension() == kExtension) {
      entries.push_back(File{ path, time });
    } else if (path.filename().string().starts_with(".illumo-tmp-") &&
               time < staleBefore) {
      std::filesystem::remove(path, fileError);
    }
  }
  if (entries.size() <= m_maximumFiles) {
    return;
  }
  std::sort(entries.begin(), entries.end(), [](const File& a, const File& b) {
    return a.time > b.time;
  });
  for (std::size_t index = m_maximumFiles; index < entries.size(); ++index) {
    std::error_code removeError;
    std::filesystem::remove(entries[index].path, removeError);
  }
}

bool
WasmModuleCache::acquire(std::span<const std::byte> module,
                         std::uint32_t engineOptions,
                         const Compiler& compile,
                         Artifact& artifact,
                         Source& source,
                         std::string& error)
{
  ILLUMO_PROFILE_ZONE("WasmModuleCache.acquire");
  Key key;
  {
    ILLUMO_PROFILE_ZONE("WasmModuleCache.digest");
    key.module = Sha256::digest(module);
  }
  key.options = engineOptions;
  std::shared_ptr<Entry> entry;
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    const std::map<Key, std::shared_ptr<Entry>>::iterator found =
      m_entries.find(key);
    if (found != m_entries.end()) {
      entry = found->second;
      m_settled.wait(lock,
                     [&entry]() { return entry->ready || entry->failed; });
      if (entry->failed) {
        error = entry->error;
        return false;
      }
      m_counters.memoryHits += 1u;
      artifact = entry->artifact;
      source = Source::Memory;
      return true;
    }
    entry = std::make_shared<Entry>();
    m_entries.emplace(key, entry);
  }

  // This caller owns the lookup: the directory, then the compiler, outside
  // the lock so other modules proceed.
  std::vector<std::byte> bytes;
  const DiskResult disk = readEntry(key, bytes);
  bool succeeded = disk == DiskResult::Found;
  bool written = false;
  std::string compileError;
  if (!succeeded) {
    succeeded = compile && compile(bytes, compileError);
    written = succeeded && writeEntry(key, bytes);
  }
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (disk == DiskResult::Rejected) {
      m_counters.diskRejects += 1u;
    }
    if (succeeded) {
      if (disk == DiskResult::Found) {
        m_counters.diskHits += 1u;
        source = Source::Disk;
      } else {
        m_counters.compiles += 1u;
        source = Source::Compiled;
      }
      if (written) {
        m_counters.diskWrites += 1u;
      }
      entry->artifact =
        std::make_shared<const std::vector<std::byte>>(std::move(bytes));
      entry->ready = true;
      artifact = entry->artifact;
    } else {
      entry->failed = true;
      entry->error =
        compileError.empty() ? "WASM module failed to compile" : compileError;
      error = entry->error;
      m_entries.erase(key);
    }
  }
  m_settled.notify_all();
  return succeeded;
}

void
WasmModuleCache::discard(std::span<const std::byte> module,
                         std::uint32_t engineOptions)
{
  Key key;
  key.module = Sha256::digest(module);
  key.options = engineOptions;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::map<Key, std::shared_ptr<Entry>>::iterator found =
      m_entries.find(key);
    // A lookup in flight keeps its entry; it was not the refused artifact.
    if (found != m_entries.end() && found->second->ready) {
      m_entries.erase(found);
    }
  }
  if (!m_directory.empty()) {
    std::error_code error;
    std::filesystem::remove(entryPath(key), error);
  }
}

WasmModuleCache::Counters
WasmModuleCache::counters() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_counters;
}
