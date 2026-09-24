#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// .ilpk package archives: a strict ZIP subset (native host only).
//
// The reader accepts stored (0) and deflate (8) entries, data descriptors and
// UTF-8 names. It refuses zip64, multi-disk archives, encryption, symlinks,
// names that are not VirtualPath::validRelative (absolute, drive, backslash,
// "..", device and ".illumo-" names), duplicate names, file/directory
// collisions, local headers that disagree with the central directory,
// overlapping entries, and anything past the configured bounds. Every full
// read inflates into an exactly sized buffer and checks the CRC-32.
//
// The writer is deterministic (sorted names, fixed 1980-01-01 timestamps) and
// deflates through the engine's stb_image_write compressor unless the data is
// already compressed or deflate would not shrink it.

// CRC-32 as used by ZIP (reflected polynomial 0xEDB88320). Pass the previous
// result as running to continue a checksum across buffers.
uint32_t
packageCrc32(const void* data, std::size_t bytes, uint32_t running = 0);

// Positional, thread-safe access to archive bytes.
class IPackageByteSource
{
public:
  virtual ~IPackageByteSource() = default;
  virtual uint64_t size() const = 0;
  // Reads exactly bytes at offset; false when the range is out of bounds or
  // the read fails.
  virtual bool readAt(uint64_t offset,
                      void* output,
                      std::size_t bytes) const = 0;
};

class MemoryPackageSource : public IPackageByteSource
{
public:
  explicit MemoryPackageSource(std::vector<uint8_t> bytes);
  uint64_t size() const override { return m_bytes.size(); }
  bool readAt(uint64_t offset, void* output, std::size_t bytes) const override;

private:
  std::vector<uint8_t> m_bytes;
};

// A file opened once; reads are serialized by a mutex.
class FilePackageSource : public IPackageByteSource
{
public:
  static std::shared_ptr<FilePackageSource> open(
    const std::filesystem::path& path,
    std::string& error);
  uint64_t size() const override { return m_size; }
  bool readAt(uint64_t offset, void* output, std::size_t bytes) const override;

private:
  mutable std::mutex m_mutex;
  mutable std::ifstream m_stream;
  uint64_t m_size = 0;
};

enum class PackageArchiveMethod
{
  Stored,
  Deflate
};

struct PackageArchiveEntry
{
  // Package-relative name ("textures/a.png").
  std::string name;
  PackageArchiveMethod method = PackageArchiveMethod::Stored;
  uint32_t crc32 = 0;
  uint64_t compressedSize = 0;
  uint64_t size = 0;
  uint64_t dataOffset = 0;
};

struct PackageArchiveChild
{
  std::string name;
  bool directory = false;
};

struct PackageArchiveLimits
{
  std::size_t maximumEntries = 65535;
  uint64_t maximumDirectoryBytes = 16u * 1024u * 1024u;
  // At most 1 GiB; larger values are clamped (inflate buffers are int sized).
  uint64_t maximumEntryBytes = 256u * 1024u * 1024u;
  uint64_t maximumTotalBytes = 1024ull * 1024ull * 1024ull;
  // Largest accepted inflated/compressed size ratio of a deflate entry.
  uint64_t maximumRatio = 1024;
};

class PackageArchive
{
public:
  static std::unique_ptr<PackageArchive> open(
    std::shared_ptr<const IPackageByteSource> source,
    std::string& error,
    const PackageArchiveLimits& limits = {});
  static std::unique_ptr<PackageArchive> openFile(
    const std::filesystem::path& path,
    std::string& error,
    const PackageArchiveLimits& limits = {});

  // File entries sorted by name.
  const std::vector<PackageArchiveEntry>& entries() const { return m_entries; }
  const PackageArchiveEntry* find(std::string_view name) const;
  // True for "" (the root) and every explicit or implied directory.
  bool isDirectory(std::string_view name) const;
  // The immediate children of a directory ("" for the root), directories
  // first, each group sorted by name. Empty for an unknown directory.
  std::vector<PackageArchiveChild> list(std::string_view directory) const;
  uint64_t totalBytes() const { return m_totalBytes; }

  // Inflates one entry and checks its CRC-32. Thread-safe.
  bool read(const PackageArchiveEntry& entry,
            std::vector<uint8_t>& output,
            std::string& error) const;
  // Bytes [offset, offset + bytes) of an entry, clipped to its size. Stored
  // entries read in place (their CRC is checked only by a full read);
  // deflated entries inflate whole first.
  bool readRange(const PackageArchiveEntry& entry,
                 uint64_t offset,
                 std::size_t bytes,
                 std::vector<uint8_t>& output,
                 std::string& error) const;

private:
  std::shared_ptr<const IPackageByteSource> m_source;
  std::vector<PackageArchiveEntry> m_entries;
  // Sorted, without trailing '/'.
  std::vector<std::string> m_directories;
  uint64_t m_totalBytes = 0;
};

class PackageArchiveWriter
{
public:
  static constexpr std::size_t kMaximumEntries = 65535;
  static constexpr uint64_t kMaximumEntryBytes = 1024ull * 1024ull * 1024ull;

  // Adds a file. Fails for an invalid, duplicate or colliding name, or data
  // that the archive cannot hold without zip64.
  bool add(std::string_view name,
           std::vector<uint8_t> bytes,
           std::string& error);
  // Adds every regular file below root under its relative path. Symlinks and
  // names that are not valid package names fail the whole call.
  bool addDirectory(const std::filesystem::path& root, std::string& error);
  std::size_t entryCount() const { return m_files.size(); }

  // The archive bytes; false when the result would need zip64.
  bool finish(std::vector<uint8_t>& output, std::string& error) const;
  // finish() then an atomic replace of destination.
  bool write(const std::filesystem::path& destination,
             std::string& error) const;

  // Already-compressed formats are stored rather than deflated.
  static bool storesUncompressed(std::string_view name);

private:
  struct File
  {
    std::string name;
    std::vector<uint8_t> bytes;
  };
  std::vector<File> m_files;
};
