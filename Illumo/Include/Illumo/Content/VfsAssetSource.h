#pragma once

#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Rendering/AssetSource.h>
#include <memory>
#include <string>

// AssetManager bytes from the virtual file tree (native tools and tests).
// Canonical names are normalized absolute virtual paths; relative names are
// joined to a base directory ("/app" by default). Unreadable or invalid
// paths fail reads rather than touching the host file system.
class VfsAssetSource : public IAssetSource
{
public:
  explicit VfsAssetSource(std::shared_ptr<const VirtualFileSystem> vfs,
                          std::string baseDirectory = "/app");

  std::string canonical(const std::string& path) const override;
  bool read(const std::string& canonical,
            std::vector<unsigned char>& bytes) const override;
  std::int64_t stamp(const std::string& canonical) const override;

private:
  std::shared_ptr<const VirtualFileSystem> m_vfs;
  std::string m_base;
};
