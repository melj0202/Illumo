#pragma once

#include <Illumo/Foundation/Sha256.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

// Compiled shader and pipeline data the explicit-API backends keep between
// runs (D-R38): blobs under a directory, one file per SHA-256 key, each with
// a header, its size and a checksum, written through AtomicFile and pruned
// to the newest maximumFiles. A missing, damaged or foreign entry is a miss;
// no directory keeps nothing. Callers key entries by everything that shapes
// the blob (source text, tool versions, device identity). Main-thread use.
class GpuShaderCache
{
public:
  explicit GpuShaderCache(std::size_t maximumFiles = 512u);

  // Empty disables the cache. The directory is created when first written.
  void setDirectory(const std::filesystem::path& directory);
  bool enabled() const { return !m_directory.empty(); }

  bool load(const Sha256::Digest& key, std::vector<unsigned char>* blob) const;
  void store(const Sha256::Digest& key, std::span<const unsigned char> blob);

  // A key from a tag and parts, each length-prefixed so no two part lists
  // collide.
  static Sha256::Digest key(
    std::string_view tag,
    std::span<const std::span<const unsigned char>> parts);

private:
  std::filesystem::path entryPath(const Sha256::Digest& key) const;
  void prune() const;

  std::filesystem::path m_directory;
  std::size_t m_maximumFiles;
  bool m_created = false;
};
