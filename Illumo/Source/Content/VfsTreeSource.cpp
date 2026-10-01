#include <Illumo/Content/VfsTreeSource.h>
#include <utility>

VfsTreeSource::VfsTreeSource(std::shared_ptr<const VirtualFileSystem> vfs)
  : m_vfs(std::move(vfs))
{
}

bool
VfsTreeSource::list(const std::string& directory,
                    std::vector<FileTreeEntry>& entries) const
{
  entries.clear();
  std::vector<VfsEntry> listed;
  std::string error;
  if (!m_vfs || !m_vfs->list(directory, listed, error, 0, kMaximumEntries)) {
    return false;
  }
  entries.reserve(listed.size());
  for (const VfsEntry& entry : listed) {
    entries.push_back(
      { entry.name, entry.kind == VfsKind::Directory, entry.size });
  }
  return true;
}

bool
VfsTreeSource::stat(const std::string& path, FileTreeStatus& status) const
{
  VfsStat found;
  std::string error;
  if (!m_vfs || !m_vfs->stat(path, found, error)) {
    return false;
  }
  status.directory = found.kind == VfsKind::Directory;
  status.size = found.size;
  status.packageId = found.packageId;
  return true;
}
