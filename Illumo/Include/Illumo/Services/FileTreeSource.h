#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct FileTreeEntry
{
  std::string name;
  bool directory = false;
  std::uint64_t size = 0;
};

struct FileTreeStatus
{
  bool directory = false;
  std::uint64_t size = 0;
  // The package that supplies the entry; empty when none does.
  std::string packageId;
};

// A read-only view of a file tree for engine tools, addressed by normalized
// absolute '/'-separated paths. IllumoRuntime publishes its virtual file tree
// through IllumoContext::fileTree; core code never learns how the tree is
// mounted or where its files live on the host. Calls are synchronous and
// main-thread affine.
class IFileTreeSource
{
public:
  IFileTreeSource() = default;
  virtual ~IFileTreeSource() = default;
  IFileTreeSource(const IFileTreeSource&) = delete;
  IFileTreeSource& operator=(const IFileTreeSource&) = delete;
  IFileTreeSource(IFileTreeSource&&) = delete;
  IFileTreeSource& operator=(IFileTreeSource&&) = delete;

  // One directory's children sorted by name; false when the path is not a
  // directory.
  virtual bool list(const std::string& directory,
                    std::vector<FileTreeEntry>& entries) const = 0;
  virtual bool stat(const std::string& path, FileTreeStatus& status) const = 0;
};
