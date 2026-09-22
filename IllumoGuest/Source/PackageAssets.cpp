#include <Illumo/Services/Logger.h>
#include <IllumoGuest/PackageAssets.h>

// Guests have no file system: without a supplied source, AssetManager fails
// every load visibly instead of importing WASI file functions.
IAssetSource*
DefaultAssetSource()
{
  return nullptr;
}

void
GuestPackageAssets::preload(const std::vector<std::string>& names)
{
  for (const std::string& name : names) {
    const std::string key = canonical(name);
    const std::uint64_t task = m_files.read(GuestFileArea::Package, key);
    if (task == 0) {
      Logger::LogError("Package asset could not be requested: " + key);
      continue;
    }
    m_pending.emplace(task, key);
  }
}

bool
GuestPackageAssets::ready()
{
  for (std::map<std::uint64_t, std::string>::iterator it = m_pending.begin();
       it != m_pending.end();) {
    GuestFileResult result;
    if (!m_files.take(it->first, result)) {
      ++it;
      continue;
    }
    if (result.outcome == GuestFileOutcome::Success) {
      std::vector<unsigned char>& bytes = m_assets[it->second];
      bytes.resize(result.bytes.size());
      for (std::size_t index = 0; index < result.bytes.size(); ++index) {
        bytes[index] = static_cast<unsigned char>(result.bytes[index]);
      }
    } else {
      Logger::LogError("Package asset is missing or unreadable: " + it->second);
    }
    it = m_pending.erase(it);
  }
  return m_pending.empty();
}

std::string
GuestPackageAssets::canonical(const std::string& path) const
{
  std::string name = path;
  for (char& character : name) {
    if (character == '\\') {
      character = '/';
    }
  }
  while (name.starts_with("./")) {
    name.erase(0, 2);
  }
  return name;
}

bool
GuestPackageAssets::read(const std::string& canonical,
                         std::vector<unsigned char>& bytes) const
{
  const std::map<std::string, std::vector<unsigned char>>::const_iterator
    found = m_assets.find(canonical);
  if (found == m_assets.end()) {
    return false;
  }
  bytes = found->second;
  return true;
}

std::int64_t
GuestPackageAssets::stamp(const std::string& canonical) const
{
  (void)canonical;
  return 0;
}
