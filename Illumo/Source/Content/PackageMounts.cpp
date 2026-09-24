#include <Illumo/Content/PackageMounts.h>

#include <Illumo/Content/PackageArchive.h>

#include <algorithm>
#include <system_error>

static std::string
fileNameOf(const std::filesystem::path& path)
{
  const std::u8string name = path.filename().u8string();
  return std::string(name.begin(), name.end());
}

static bool
isArchive(const std::filesystem::path& path)
{
  std::string extension = fileNameOf(path.extension());
  std::transform(
    extension.begin(), extension.end(), extension.begin(), [](char c) {
      return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
  return extension == ".ilpk";
}

bool
PackageMounts::open(const std::filesystem::path& path,
                    const PackageCeilings& ceilings,
                    LoadedPackage& output,
                    std::string& error)
{
  std::error_code code;
  std::shared_ptr<IVfsBackend> backend;
  if (std::filesystem::is_directory(path, code)) {
    backend = DirectoryVfsBackend::open(path, false, error);
  } else if (std::filesystem::is_regular_file(path, code) && isArchive(path)) {
    backend = ArchiveVfsBackend::openFile(path, error);
  } else {
    error = "Not a package directory or .ilpk file: " + fileNameOf(path);
    return false;
  }
  if (!backend) {
    return false;
  }
  VfsStat stat;
  if (!backend->stat(PackageManifest::kFileName, stat) ||
      stat.kind != VfsKind::File || stat.size == 0 ||
      stat.size > kMaximumManifestBytes) {
    error = "Missing or oversized " + std::string(PackageManifest::kFileName) +
            " in " + fileNameOf(path);
    return false;
  }
  std::vector<uint8_t> bytes;
  if (!backend->read(PackageManifest::kFileName, bytes, error)) {
    return false;
  }
  LoadedPackage loaded;
  if (!decodePackageManifest(std::string(bytes.begin(), bytes.end()),
                             ceilings,
                             loaded.manifest,
                             error)) {
    error = fileNameOf(path) + ": " + error;
    return false;
  }
  loaded.backend = std::move(backend);
  loaded.origin = fileNameOf(path);
  output = std::move(loaded);
  return true;
}

std::vector<LoadedPackage>
PackageMounts::discover(const std::filesystem::path& directory,
                        const PackageCeilings& ceilings,
                        std::vector<std::string>& takenIds,
                        std::vector<std::string>& warnings)
{
  std::vector<LoadedPackage> packages;
  std::error_code code;
  if (!std::filesystem::is_directory(directory, code)) {
    return packages;
  }
  std::vector<std::filesystem::path> candidates;
  std::filesystem::directory_iterator walk(directory, code);
  const std::filesystem::directory_iterator done;
  for (; !code && walk != done; walk.increment(code)) {
    const std::filesystem::directory_entry& item = *walk;
    if (item.is_symlink(code)) {
      continue;
    }
    const bool packageDirectory =
      item.is_directory(code) &&
      std::filesystem::is_regular_file(item.path() / PackageManifest::kFileName,
                                       code);
    if (packageDirectory ||
        (item.is_regular_file(code) && isArchive(item.path()))) {
      candidates.push_back(item.path());
    }
  }
  std::sort(candidates.begin(),
            candidates.end(),
            [](const std::filesystem::path& a, const std::filesystem::path& b) {
              return fileNameOf(a) < fileNameOf(b);
            });
  for (const std::filesystem::path& candidate : candidates) {
    LoadedPackage loaded;
    std::string error;
    if (!open(candidate, ceilings, loaded, error)) {
      warnings.push_back("Skipping package " + fileNameOf(candidate) + ": " +
                         error);
      continue;
    }
    if (std::find(takenIds.begin(), takenIds.end(), loaded.manifest.id) !=
        takenIds.end()) {
      warnings.push_back("Skipping package " + fileNameOf(candidate) +
                         ": the id '" + loaded.manifest.id +
                         "' is already in use");
      continue;
    }
    takenIds.push_back(loaded.manifest.id);
    packages.push_back(std::move(loaded));
  }
  return packages;
}

bool
PackageMounts::mountAll(VirtualFileSystem& vfs,
                        const LoadedPackage& application,
                        const std::vector<LoadedPackage>& packages,
                        const std::filesystem::path& engineAssets,
                        const std::filesystem::path& project,
                        std::vector<std::string>& warnings,
                        std::string& error)
{
  if (!engineAssets.empty()) {
    std::string engineError;
    std::shared_ptr<DirectoryVfsBackend> engine =
      DirectoryVfsBackend::open(engineAssets, false, engineError);
    if (engine) {
      VfsMount mount;
      mount.point = "/engine";
      mount.layers.push_back({ engine, std::string() });
      if (!vfs.mount(std::move(mount), error)) {
        return false;
      }
    } else {
      warnings.push_back("No engine assets to mount: " + engineError);
    }
  }

  // Dependency order over the application and every package; the launched
  // application always mounts, so only other packages can be left out.
  std::vector<PackageManifest> manifests;
  manifests.push_back(application.manifest);
  for (const LoadedPackage& package : packages) {
    manifests.push_back(package.manifest);
  }
  const PackageOrderResult order = orderPackages(manifests);
  for (std::size_t index = 0; index < order.rejected.size(); ++index) {
    if (order.rejected[index] != 0) {
      warnings.push_back("Not mounting " + manifests[order.rejected[index]].id +
                         ": " + order.reasons[index]);
    }
  }
  struct Placed
  {
    const LoadedPackage* package;
    std::size_t rank;
  };
  std::vector<Placed> mounted;
  for (std::size_t rank = 0; rank < order.order.size(); ++rank) {
    const std::size_t index = order.order[rank];
    if (index != 0) {
      mounted.push_back({ &packages[index - 1], rank });
    }
  }

  // /app: overlays that target the application, top first, over the base.
  std::vector<Placed> overlays;
  std::vector<int32_t> priorities;
  for (const Placed& placed : mounted) {
    const PackageManifest& manifest = placed.package->manifest;
    for (const PackageOverlay& overlay : manifest.overlays) {
      if (overlay.target == "/app" &&
          manifest.targetsApplication(application.manifest.id)) {
        overlays.push_back(placed);
        priorities.push_back(overlay.priority);
        break;
      }
    }
  }
  std::vector<std::size_t> stack(overlays.size());
  for (std::size_t index = 0; index < stack.size(); ++index) {
    stack[index] = index;
  }
  std::sort(stack.begin(),
            stack.end(),
            [&overlays, &priorities](std::size_t a, std::size_t b) {
              if (priorities[a] != priorities[b]) {
                return priorities[a] > priorities[b];
              }
              if (overlays[a].rank != overlays[b].rank) {
                return overlays[a].rank > overlays[b].rank;
              }
              return overlays[a].package->manifest.id <
                     overlays[b].package->manifest.id;
            });
  VfsMount app;
  app.point = "/app";
  for (std::size_t index : stack) {
    app.layers.push_back({ overlays[index].package->backend,
                           overlays[index].package->manifest.id });
  }
  app.layers.push_back({ application.backend, application.manifest.id });
  const bool layered = app.layers.size() > 1;
  if (!vfs.mount(std::move(app), error)) {
    return false;
  }
  if (layered) {
    const VfsMount* merged = vfs.table()->find("/app");
    for (const std::string& conflict : merged->conflicts) {
      warnings.push_back("Overlay conflict " + conflict);
    }
  }

  for (const Placed& placed : mounted) {
    VfsMount mount;
    mount.point = "/packages/" + placed.package->manifest.id;
    mount.layers.push_back(
      { placed.package->backend, placed.package->manifest.id });
    if (!vfs.mount(std::move(mount), error)) {
      return false;
    }
  }

  if (!project.empty()) {
    std::shared_ptr<DirectoryVfsBackend> backend =
      DirectoryVfsBackend::open(project, true, error);
    if (!backend) {
      return false;
    }
    std::string id = "project";
    VfsStat stat;
    if (backend->stat(PackageManifest::kFileName, stat)) {
      LoadedPackage loaded;
      std::string manifestError;
      if (open(project, PackageCeilings{}, loaded, manifestError)) {
        id = loaded.manifest.id;
      } else {
        warnings.push_back("The project manifest is invalid: " + manifestError);
      }
    }
    VfsMount mount;
    mount.point = "/project";
    mount.layers.push_back({ backend, id });
    if (!vfs.mount(std::move(mount), error)) {
      return false;
    }
  }
  return true;
}

bool
PackageMounts::packMounted(const VirtualFileSystem& vfs,
                           const std::string& source,
                           const std::filesystem::path& destination,
                           std::uint64_t maximumBytes,
                           std::string& error)
{
  std::vector<uint8_t> manifestBytes;
  PackageManifest manifest;
  if (!vfs.read(
        source + "/" + PackageManifest::kFileName, manifestBytes, error) ||
      manifestBytes.size() > kMaximumManifestBytes ||
      !decodePackageManifest(
        std::string(manifestBytes.begin(), manifestBytes.end()),
        PackageCeilings{},
        manifest,
        error)) {
    error = "The directory has no valid " +
            std::string(PackageManifest::kFileName) +
            (error.empty() ? std::string() : ": " + error);
    return false;
  }
  PackageArchiveWriter writer;
  std::vector<std::string> pending{ std::string() };
  std::size_t visited = 0;
  std::uint64_t total = 0;
  while (!pending.empty()) {
    const std::string relative = pending.back();
    pending.pop_back();
    std::vector<VfsEntry> entries;
    if (!vfs.list(relative.empty() ? source : source + "/" + relative,
                  entries,
                  error)) {
      return false;
    }
    for (const VfsEntry& entry : entries) {
      if (++visited > 100000u) {
        error = "The directory holds too many entries to pack";
        return false;
      }
      const std::string child =
        relative.empty() ? entry.name : relative + "/" + entry.name;
      if (entry.kind == VfsKind::Directory) {
        pending.push_back(child);
        continue;
      }
      total += entry.size;
      std::vector<uint8_t> bytes;
      if (total > maximumBytes) {
        error = "The directory is too large to pack";
        return false;
      }
      if (!vfs.read(source + "/" + child, bytes, error) ||
          !writer.add(child, std::move(bytes), error)) {
        return false;
      }
    }
  }
  return writer.write(destination, error);
}