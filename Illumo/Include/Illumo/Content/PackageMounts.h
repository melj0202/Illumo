#pragma once

#include <Illumo/Content/PackageManifest.h>
#include <Illumo/Content/VirtualFileSystem.h>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Opens packages and assembles the runtime's mount table (native only):
// /engine, /app (the launched application under its targeted overlays),
// /packages/<id> for every other package and the writable /project.
struct LoadedPackage
{
  PackageManifest manifest;
  std::shared_ptr<IVfsBackend> backend;
  // The file or directory name it came from, for logs only.
  std::string origin;
};

class PackageMounts
{
public:
  static constexpr std::size_t kMaximumManifestBytes = 64u * 1024u;

  // Opens a package directory (holding illumo.json) or an .ilpk archive and
  // decodes its manifest against the ceilings.
  static bool open(const std::filesystem::path& path,
                   const PackageCeilings& ceilings,
                   LoadedPackage& output,
                   std::string& error);

  // Every package directly inside directory (subdirectories with an
  // illumo.json, and *.ilpk files), in file-name order. Unreadable packages
  // and ids already in takenIds or seen earlier are skipped with a warning.
  // A missing directory holds no packages.
  static std::vector<LoadedPackage> discover(
    const std::filesystem::path& directory,
    const PackageCeilings& ceilings,
    std::vector<std::string>& takenIds,
    std::vector<std::string>& warnings);

  // Mounts everything. Packages whose dependencies are missing or cyclic
  // are left out with a warning. Overlays apply to /app when the package
  // targets the application; they stack by priority (highest on top), then
  // dependency order (dependents above what they depend on), then id. An
  // empty engineAssets or project path skips that mount.
  static bool mountAll(VirtualFileSystem& vfs,
                       const LoadedPackage& application,
                       const std::vector<LoadedPackage>& packages,
                       const std::filesystem::path& engineAssets,
                       const std::filesystem::path& project,
                       std::vector<std::string>& warnings,
                       std::string& error);

  // Packs every file below a mounted directory into an .ilpk at destination
  // (atomically). The directory must hold a valid illumo.json; the walk is
  // iterative and stops past maximumBytes of content or 100000 entries.
  static bool packMounted(const VirtualFileSystem& vfs,
                          const std::string& source,
                          const std::filesystem::path& destination,
                          std::uint64_t maximumBytes,
                          std::string& error);
};
