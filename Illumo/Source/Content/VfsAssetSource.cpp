#include <Illumo/Content/VfsAssetSource.h>

#include <Illumo/Content/VirtualPath.h>

VfsAssetSource::VfsAssetSource(std::shared_ptr<const VirtualFileSystem> vfs,
                               std::string baseDirectory)
  : m_vfs(std::move(vfs))
  , m_base(std::move(baseDirectory))
{
}

std::string
VfsAssetSource::canonical(const std::string& path) const
{
  std::string resolved;
  // An invalid reference resolves to nothing, so reads fail cleanly.
  if (!VirtualPath::join(m_base, path, resolved)) {
    return std::string();
  }
  return resolved;
}

bool
VfsAssetSource::read(const std::string& canonical,
                     std::vector<unsigned char>& bytes) const
{
  if (canonical.empty() || !m_vfs) {
    return false;
  }
  std::vector<uint8_t> data;
  std::string error;
  if (!m_vfs->read(canonical, data, error)) {
    return false;
  }
  bytes.assign(data.begin(), data.end());
  return true;
}

std::int64_t
VfsAssetSource::stamp(const std::string& canonical) const
{
  VfsStat stat;
  std::string error;
  if (canonical.empty() || !m_vfs || !m_vfs->stat(canonical, stat, error)) {
    return 0;
  }
  return stat.stamp;
}
