#include "Rendering/Gpu/GpuShaderCache.h"

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Platform/AtomicFile.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

// Entry layout, little-endian: magic, format, key, blob size, checksum, blob.
static constexpr std::array<char, 4> kMagic = { 'I', 'L', 'G', 'S' };
static constexpr std::uint32_t kFormat = 1u;
static constexpr std::uint64_t kMaximumBlobBytes = 64ull * 1024u * 1024u;
static constexpr char kExtension[] = ".gpucache";

// Corruption check (identity is the key): FNV-1a over 64-bit words, then the
// tail and the length.
static std::uint64_t
blobChecksum(std::span<const unsigned char> bytes)
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
    hash = (hash ^ bytes[index]) * kPrime;
  }
  return (hash ^ static_cast<std::uint64_t>(bytes.size())) * kPrime;
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

GpuShaderCache::GpuShaderCache(std::size_t maximumFiles)
  : m_maximumFiles(std::max<std::size_t>(1u, maximumFiles))
{
}

void
GpuShaderCache::setDirectory(const std::filesystem::path& directory)
{
  m_directory = directory;
  m_created = false;
}

Sha256::Digest
GpuShaderCache::key(std::string_view tag,
                    std::span<const std::span<const unsigned char>> parts)
{
  Sha256 hash;
  const std::uint64_t tagSize = tag.size();
  hash.update(std::as_bytes(std::span(&tagSize, 1)));
  hash.update(std::as_bytes(std::span(tag.data(), tag.size())));
  for (const std::span<const unsigned char>& part : parts) {
    const std::uint64_t size = part.size();
    hash.update(std::as_bytes(std::span(&size, 1)));
    hash.update(std::as_bytes(part));
  }
  return hash.finish();
}

std::filesystem::path
GpuShaderCache::entryPath(const Sha256::Digest& key) const
{
  static constexpr char kHex[] = "0123456789abcdef";
  std::string name;
  name.reserve(key.size() * 2u + sizeof(kExtension));
  for (std::uint8_t byte : key) {
    name.push_back(kHex[byte >> 4u]);
    name.push_back(kHex[byte & 15u]);
  }
  name += kExtension;
  return m_directory / name;
}

bool
GpuShaderCache::load(const Sha256::Digest& key,
                     std::vector<unsigned char>* blob) const
{
  if (m_directory.empty() || blob == nullptr) {
    return false;
  }
  ILLUMO_PROFILE_ZONE("GpuShaderCache.load");
  std::ifstream stream(entryPath(key), std::ios::binary);
  if (!stream.is_open()) {
    return false;
  }
  std::array<char, 4> magic{};
  stream.read(magic.data(), magic.size());
  std::array<unsigned char, 4> format{};
  stream.read(reinterpret_cast<char*>(format.data()), format.size());
  Sha256::Digest stored{};
  stream.read(reinterpret_cast<char*>(stored.data()), stored.size());
  std::uint64_t size = 0u;
  std::uint64_t checksum = 0u;
  const std::uint32_t storedFormat =
    static_cast<std::uint32_t>(format[0]) |
    (static_cast<std::uint32_t>(format[1]) << 8u) |
    (static_cast<std::uint32_t>(format[2]) << 16u) |
    (static_cast<std::uint32_t>(format[3]) << 24u);
  if (!stream.good() || magic != kMagic || storedFormat != kFormat ||
      stored != key || !getU64(stream, size) || !getU64(stream, checksum) ||
      size > kMaximumBlobBytes) {
    return false;
  }
  blob->resize(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char*>(blob->data()),
              static_cast<std::streamsize>(size));
  if (!stream.good() || stream.peek() != std::ifstream::traits_type::eof() ||
      blobChecksum(*blob) != checksum) {
    blob->clear();
    return false;
  }
  return true;
}

void
GpuShaderCache::store(const Sha256::Digest& key,
                      std::span<const unsigned char> blob)
{
  if (m_directory.empty() || blob.size() > kMaximumBlobBytes) {
    return;
  }
  ILLUMO_PROFILE_ZONE("GpuShaderCache.store");
  if (!m_created) {
    std::error_code error;
    std::filesystem::create_directories(m_directory, error);
    if (error) {
      // An unwritable location disables the cache for this run.
      m_directory.clear();
      return;
    }
    m_created = true;
  }
  const std::uint64_t checksum = blobChecksum(blob);
  const bool written = AtomicFile::write(
    entryPath(key), [&key, blob, checksum](std::ostream& stream, std::string*) {
      stream.write(kMagic.data(), kMagic.size());
      const std::array<char, 4> format = {
        static_cast<char>(kFormat & 0xffu), 0, 0, 0
      };
      stream.write(format.data(), format.size());
      stream.write(reinterpret_cast<const char*>(key.data()), key.size());
      putU64(stream, blob.size());
      putU64(stream, checksum);
      stream.write(reinterpret_cast<const char*>(blob.data()),
                   static_cast<std::streamsize>(blob.size()));
      return stream.good();
    });
  if (written) {
    prune();
  }
}

void
GpuShaderCache::prune() const
{
  struct File
  {
    std::filesystem::path path;
    std::filesystem::file_time_type time;
  };
  std::vector<File> entries;
  std::error_code error;
  std::filesystem::directory_iterator it(m_directory, error);
  for (; !error && it != std::filesystem::directory_iterator();
       it.increment(error)) {
    std::error_code fileError;
    if (it->path().extension() != kExtension ||
        !it->is_regular_file(fileError) || fileError) {
      continue;
    }
    const std::filesystem::file_time_type time = it->last_write_time(fileError);
    if (!fileError) {
      entries.push_back(File{ it->path(), time });
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
