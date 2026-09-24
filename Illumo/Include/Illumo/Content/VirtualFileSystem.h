#pragma once

#include <Illumo/Content/PackageArchive.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// The host's virtual file tree (native only). Packages are mounted at fixed
// points (/engine, /app, /packages/<id>, /project); every path is a
// VirtualPath and nothing here ever returns a host path. See
// docs/content-packages-and-scenes-design.md section 6.

enum class VfsKind
{
  File,
  Directory
};

struct VfsStat
{
  VfsKind kind = VfsKind::File;
  uint64_t size = 0;
  // Change stamp for hot reload; 0 when immutable or unknown.
  int64_t stamp = 0;
  // The package that supplies the entry; empty for /engine and synthesized
  // directories.
  std::string packageId;
};

struct VfsEntry
{
  std::string name;
  VfsKind kind = VfsKind::File;
  uint64_t size = 0;
};

// One package's content tree, addressed by relative path ("" is its root).
// Implementations are thread-safe.
class IVfsBackend
{
public:
  virtual ~IVfsBackend() = default;
  virtual bool stat(std::string_view relative, VfsStat& output) const = 0;
  // Children sorted by name; false when relative is not a directory.
  virtual bool list(std::string_view relative,
                    std::vector<VfsEntry>& entries) const = 0;
  virtual bool read(std::string_view relative,
                    std::vector<uint8_t>& bytes,
                    std::string& error) const = 0;
  // Bytes [offset, offset + bytes) clipped to the file. The default reads the
  // whole file and slices it.
  virtual bool readRange(std::string_view relative,
                         uint64_t offset,
                         std::size_t bytes,
                         std::vector<uint8_t>& output,
                         std::string& error) const;
  virtual bool writable() const { return false; }
  // Replaces a file atomically, creating parent directories.
  virtual bool write(std::string_view relative,
                     const std::vector<uint8_t>& bytes,
                     std::string& error);
  // "directory" or "archive", for the console.
  virtual const char* kindName() const = 0;
};

// A loose directory. Lookups are case-sensitive even on case-insensitive
// file systems, and a path that reaches outside the root through a symlink
// or junction does not exist.
class DirectoryVfsBackend : public IVfsBackend
{
public:
  static std::shared_ptr<DirectoryVfsBackend>
  open(const std::filesystem::path& root, bool writable, std::string& error);

  bool stat(std::string_view relative, VfsStat& output) const override;
  bool list(std::string_view relative,
            std::vector<VfsEntry>& entries) const override;
  bool read(std::string_view relative,
            std::vector<uint8_t>& bytes,
            std::string& error) const override;
  bool readRange(std::string_view relative,
                 uint64_t offset,
                 std::size_t bytes,
                 std::vector<uint8_t>& output,
                 std::string& error) const override;
  bool writable() const override { return m_writable; }
  bool write(std::string_view relative,
             const std::vector<uint8_t>& bytes,
             std::string& error) override;
  const char* kindName() const override { return "directory"; }

private:
  std::filesystem::path m_root;
  bool m_writable = false;
  mutable std::mutex m_writeMutex;

  // The host path of an existing entry, or false when it is absent, differs
  // in case, or resolves outside the root.
  bool resolve(std::string_view relative, std::filesystem::path* path) const;
};

class ArchiveVfsBackend : public IVfsBackend
{
public:
  explicit ArchiveVfsBackend(std::shared_ptr<const PackageArchive> archive);
  static std::shared_ptr<ArchiveVfsBackend> openFile(
    const std::filesystem::path& path,
    std::string& error,
    const PackageArchiveLimits& limits = {});

  bool stat(std::string_view relative, VfsStat& output) const override;
  bool list(std::string_view relative,
            std::vector<VfsEntry>& entries) const override;
  bool read(std::string_view relative,
            std::vector<uint8_t>& bytes,
            std::string& error) const override;
  bool readRange(std::string_view relative,
                 uint64_t offset,
                 std::size_t bytes,
                 std::vector<uint8_t>& output,
                 std::string& error) const override;
  const char* kindName() const override { return "archive"; }
  const PackageArchive& archive() const { return *m_archive; }

private:
  std::shared_ptr<const PackageArchive> m_archive;
};

struct VfsLayer
{
  std::shared_ptr<IVfsBackend> backend;
  std::string packageId;
};

struct VfsMount
{
  // A normalized absolute path such as "/app" or "/packages/forest".
  std::string point;
  // Top layer first. Only an overlay target has more than one; the base
  // package is the last layer.
  std::vector<VfsLayer> layers;
  // File/directory collisions between layers, found at mount time.
  std::vector<std::string> conflicts;
};

// An immutable snapshot of the mounts, sorted by point.
class VfsMountTable
{
public:
  const std::vector<VfsMount>& mounts() const { return m_mounts; }
  const VfsMount* find(std::string_view point) const;
  // The mount holding a normalized path and the path below it.
  const VfsMount* resolve(std::string_view normalized,
                          std::string_view* relative) const;
  // Names below a directory that exists only because mounts lie beneath it
  // ("/" or "/packages"); false when the path is not such a directory.
  bool synthesized(std::string_view normalized,
                   std::vector<std::string>* names) const;

private:
  friend class VirtualFileSystem;
  std::vector<VfsMount> m_mounts;
};

// An open file. It keeps its backend alive, so it stays readable after its
// mount is removed.
class VfsFile
{
public:
  const std::string& path() const { return m_path; }
  uint64_t size() const { return m_size; }
  const std::string& packageId() const { return m_packageId; }
  bool read(uint64_t offset,
            std::size_t bytes,
            std::vector<uint8_t>& output,
            std::string& error) const;
  bool readAll(std::vector<uint8_t>& output, std::string& error) const;

private:
  friend class VirtualFileSystem;
  std::shared_ptr<IVfsBackend> m_backend;
  std::string m_path;
  std::string m_relative;
  std::string m_packageId;
  uint64_t m_size = 0;
};

class VirtualFileSystem
{
public:
  // Largest listing page.
  static constexpr std::size_t kMaximumListPage = 256;

  // Adds a mount. The point must be normalized, not "/", and neither contain
  // nor lie inside another mount. Multi-layer mounts record conflicts.
  bool mount(VfsMount mount, std::string& error);
  bool unmount(std::string_view point);
  std::shared_ptr<const VfsMountTable> table() const;

  bool stat(std::string_view path, VfsStat& output, std::string& error) const;
  // A merged, name-sorted directory listing. offset and limit page it; total
  // receives the full count.
  bool list(std::string_view path,
            std::vector<VfsEntry>& entries,
            std::string& error,
            std::size_t offset = 0,
            std::size_t limit = SIZE_MAX,
            std::size_t* total = nullptr) const;
  bool read(std::string_view path,
            std::vector<uint8_t>& bytes,
            std::string& error) const;
  bool readRange(std::string_view path,
                 uint64_t offset,
                 std::size_t bytes,
                 std::vector<uint8_t>& output,
                 std::string& error) const;
  std::shared_ptr<VfsFile> open(std::string_view path,
                                std::string& error) const;
  // Writes only into a single-layer writable mount (/project). *.wasm and
  // illumo.json files are refused so content cannot plant code or manifests.
  bool write(std::string_view path,
             const std::vector<uint8_t>& bytes,
             std::string& error) const;

private:
  mutable std::mutex m_mutex;
  std::shared_ptr<const VfsMountTable> m_table =
    std::make_shared<const VfsMountTable>();
};
