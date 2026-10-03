#pragma once

#include <Illumo/Foundation/Sha256.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

// Compiled guest code shared by every store of a launch and, given a
// directory, kept between launches. An artifact is reused only for a
// byte-identical module (SHA-256) under the same engine options and Wasmtime
// version; Wasmtime itself also refuses an artifact from another build or
// configuration. Concurrent requests for one module share a single compile,
// so simulation lanes that load the same worker compile it once.
//
// Disk entries carry a header with the module digest and an artifact
// checksum, are written through AtomicFile, and are pruned to the newest
// maximumFiles. A damaged, stale or unreadable entry is a miss, never a
// failure. The directory is trusted like the runtime beside it: anyone who
// can write there can already replace the runtime. Thread-safe.
class WasmModuleCache
{
public:
  using Artifact = std::shared_ptr<const std::vector<std::byte>>;
  // Compiles a module that is in neither memory nor the directory.
  using Compiler =
    std::function<bool(std::vector<std::byte>& artifact, std::string& error)>;
  enum class Source
  {
    Memory,
    Disk,
    Compiled
  };
  struct Counters
  {
    std::uint64_t compiles = 0u;
    std::uint64_t memoryHits = 0u;
    std::uint64_t diskHits = 0u;
    std::uint64_t diskWrites = 0u;
    std::uint64_t diskRejects = 0u;
  };

  // An empty directory keeps artifacts in memory only. A directory that
  // cannot be created does the same.
  explicit WasmModuleCache(std::filesystem::path directory = {},
                           std::size_t maximumFiles = 16u);
  ~WasmModuleCache();
  WasmModuleCache(const WasmModuleCache&) = delete;
  WasmModuleCache& operator=(const WasmModuleCache&) = delete;
  WasmModuleCache(WasmModuleCache&&) = delete;
  WasmModuleCache& operator=(WasmModuleCache&&) = delete;

  // The artifact for module under engineOptions, from memory, the directory
  // or compile (called at most once at a time per module). False with error
  // when compiling fails; a failure is not remembered.
  bool acquire(std::span<const std::byte> module,
               std::uint32_t engineOptions,
               const Compiler& compile,
               Artifact& artifact,
               Source& source,
               std::string& error);
  // Forgets an artifact the engine refused, in memory and on disk, so the
  // next acquire compiles.
  void discard(std::span<const std::byte> module, std::uint32_t engineOptions);
  Counters counters() const;
  const std::filesystem::path& directory() const { return m_directory; }

private:
  struct Key
  {
    Sha256::Digest module{};
    std::uint32_t options = 0u;
    bool operator<(const Key& other) const;
  };
  // One module's artifact, or the compile in flight for it.
  struct Entry
  {
    bool ready = false;
    bool failed = false;
    Artifact artifact;
    std::string error;
  };

  enum class DiskResult
  {
    Missing,
    Rejected,
    Found
  };

  std::filesystem::path entryPath(const Key& key) const;
  DiskResult readEntry(const Key& key, std::vector<std::byte>& artifact) const;
  bool writeEntry(const Key& key, const std::vector<std::byte>& artifact) const;
  void prune() const;

  std::filesystem::path m_directory;
  std::size_t m_maximumFiles;
  mutable std::mutex m_mutex;
  std::condition_variable m_settled;
  std::map<Key, std::shared_ptr<Entry>> m_entries;
  Counters m_counters;
};
