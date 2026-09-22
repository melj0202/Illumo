#pragma once

#include <Illumo/Rendering/AssetSource.h>
#include <IllumoGuest/Files.h>
#include <map>
#include <string>
#include <vector>

// Package files preloaded into guest memory, served synchronously to
// AssetManager. Names are package-relative ('/' separated); lookups normalize
// '\' and leading "./" so product paths such as "Assets/IllEd/x.jpg" match.
class GuestPackageAssets final : public IAssetSource
{
public:
  explicit GuestPackageAssets(GuestFiles& files)
    : m_files(files)
  {
  }
  // Starts reading each named package file. Missing files are reported once
  // and then behave as absent assets.
  void preload(const std::vector<std::string>& names);
  // Pumped each update; true once every requested file has completed.
  bool ready();

  std::string canonical(const std::string& path) const override;
  bool read(const std::string& canonical,
            std::vector<unsigned char>& bytes) const override;
  std::int64_t stamp(const std::string& canonical) const override;

private:
  GuestFiles& m_files;
  std::map<std::uint64_t, std::string> m_pending;
  std::map<std::string, std::vector<unsigned char>> m_assets;
};
