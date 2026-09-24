#pragma once

#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Services/FileTreeSource.h>
#include <memory>

// Publishes a VirtualFileSystem to engine tools (the DebugModule file tree)
// as a read-only IFileTreeSource. Listings stop at kMaximumEntries per
// directory.
class VfsTreeSource final : public IFileTreeSource
{
public:
  static constexpr std::size_t kMaximumEntries = 4096;

  explicit VfsTreeSource(std::shared_ptr<const VirtualFileSystem> vfs);

  bool list(const std::string& directory,
            std::vector<FileTreeEntry>& entries) const override;
  bool stat(const std::string& path, FileTreeStatus& status) const override;

private:
  std::shared_ptr<const VirtualFileSystem> m_vfs;
};
