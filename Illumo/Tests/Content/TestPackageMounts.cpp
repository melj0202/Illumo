#include <Illumo/Content/PackageArchive.h>
#include <Illumo/Content/PackageMounts.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <filesystem>
#include <fstream>

static void
writeText(const std::filesystem::path& path, const std::string& text)
{
  std::error_code code;
  std::filesystem::create_directories(path.parent_path(), code);
  std::ofstream stream(path, std::ios::binary);
  stream << text;
}

static std::string
contentManifest(const std::string& id, const std::string& extra)
{
  return R"({"format":"ilpk","format_version":1,"id":")" + id +
         R"(","kind":"content")" + extra + "}";
}

static bool
contains(const std::vector<std::string>& lines, const std::string& text)
{
  for (const std::string& line : lines) {
    if (line.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

static int
testPackageDiscovery()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "illumo-package-mounts";
  std::error_code code;
  std::filesystem::remove_all(root, code);
  const std::filesystem::path packages = root / "packages";

  // "a-dup" sorts first, so it owns the id "forest" and the directory named
  // forest is skipped as a duplicate.
  writeText(packages / "a-dup" / "illumo.json",
            contentManifest("forest",
                            R"(,"targets":["demo"],)"
                            R"("overlays":[{"target":"/app","priority":5}])"));
  writeText(packages / "a-dup" / "x.txt", "forest");
  writeText(packages / "a-dup" / "only-forest.txt", "f");
  writeText(packages / "forest" / "illumo.json", contentManifest("forest", ""));
  writeText(packages / "broken" / "illumo.json", "{not json");
  writeText(packages / "notes.txt", "not a package");
  writeText(
    packages / "cycle-a" / "illumo.json",
    contentManifest("cycle-a",
                    R"(,"dependencies":[{"id":"cycle-b","version":"1"}])"));
  writeText(
    packages / "cycle-b" / "illumo.json",
    contentManifest("cycle-b",
                    R"(,"dependencies":[{"id":"cycle-a","version":"1"}])"));
  writeText(packages / "other" / "illumo.json",
            contentManifest("other",
                            R"(,"targets":["someone-else"],)"
                            R"("overlays":[{"target":"/app","priority":99}])"));
  // An archive that depends on forest at the same priority: dependents sit
  // above what they depend on.
  PackageArchiveWriter writer;
  std::string error;
  const std::string modManifest = contentManifest(
    "tweaks",
    R"(,"targets":["*"],"dependencies":[{"id":"forest","version":"1"}],)"
    R"("overlays":[{"target":"/app","priority":5}])");
  writer.add("illumo.json",
             std::vector<uint8_t>(modManifest.begin(), modManifest.end()),
             error);
  writer.add("x.txt", { 't', 'w', 'e', 'a', 'k' }, error);
  writer.write(packages / "tweaks.ilpk", error);

  writeText(root / "app" / "illumo.json",
            R"({"format":"ilpk","format_version":1,"id":"demo","kind":"app",)"
            R"("app":{"module":"demo.wasm"}})");
  writeText(root / "app" / "x.txt", "base");
  writeText(root / "app" / "demo.wasm", std::string("\0asm", 4));
  writeText(root / "project" / "illumo.json", contentManifest("my-scene", ""));

  LoadedPackage application;
  testTrue(
    counters,
    PackageMounts::open(root / "app", PackageCeilings{}, application, error) &&
      application.manifest.kind == PackageKind::App,
    "a package directory opens with its manifest");
  testTrue(counters,
           !PackageMounts::open(
             root / "missing", PackageCeilings{}, application, error) &&
             !PackageMounts::open(
               packages / "notes.txt", PackageCeilings{}, application, error),
           "missing paths and plain files are not packages");
  PackageMounts::open(root / "app", PackageCeilings{}, application, error);

  std::vector<std::string> taken{ "demo" };
  std::vector<std::string> warnings;
  const std::vector<LoadedPackage> found =
    PackageMounts::discover(packages, PackageCeilings{}, taken, warnings);
  std::vector<std::string> ids;
  for (const LoadedPackage& package : found) {
    ids.push_back(package.manifest.id);
  }
  testTrue(counters,
           ids ==
             std::vector<std::string>{
               "forest", "cycle-a", "cycle-b", "other", "tweaks" },
           "directories and archives are discovered in file-name order");
  testTrue(counters,
           contains(warnings, "broken") && contains(warnings, "already in use"),
           "broken and duplicate packages are skipped with warnings");

  VirtualFileSystem vfs;
  warnings.clear();
  testTrue(counters,
           PackageMounts::mountAll(
             vfs, application, found, {}, root / "project", warnings, error),
           "everything mounts");
  testTrue(counters,
           contains(warnings, "cycle-a") && contains(warnings, "cycle-b"),
           "packages on a dependency cycle are left out");
  const std::shared_ptr<const VfsMountTable> table = vfs.table();
  const VfsMount* app = table->find("/app");
  std::vector<std::string> layers;
  for (const VfsLayer& layer : app->layers) {
    layers.push_back(layer.packageId);
  }
  testTrue(counters,
           layers == std::vector<std::string>{ "tweaks", "forest", "demo" },
           "overlays stack by priority, then dependency order, over the base");
  std::vector<uint8_t> bytes;
  testTrue(counters,
           vfs.read("/app/x.txt", bytes, error) &&
             std::string(bytes.begin(), bytes.end()) == "tweak" &&
             vfs.read("/app/only-forest.txt", bytes, error),
           "the merged /app reads through every layer");
  std::vector<VfsEntry> entries;
  vfs.list("/packages", entries, error);
  std::vector<std::string> mounted;
  for (const VfsEntry& entry : entries) {
    mounted.push_back(entry.name);
  }
  testTrue(counters,
           mounted == std::vector<std::string>{ "forest", "other", "tweaks" },
           "every mountable package also appears under /packages");
  testTrue(counters,
           table->find("/engine") == nullptr,
           "an empty engine path mounts nothing");
  const VfsMount* project = table->find("/project");
  testTrue(counters,
           project != nullptr &&
             project->layers.front().packageId == "my-scene" &&
             vfs.write("/project/scenes/a.ilsc", { '{', '}' }, error),
           "/project is writable and named by its manifest");
  testTrue(counters,
           !contains(layers, "other"),
           "overlays for another application are not applied");
  std::filesystem::remove_all(root, code);
  return counters.failures;
}

static int
testPackMounted()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() / "illumo-pack-mounted";
  std::error_code code;
  std::filesystem::remove_all(root, code);
  writeText(root / "project" / "illumo.json",
            contentManifest("scene-pack", ""));
  writeText(root / "project" / "scenes" / "main.ilsc", "{}");
  writeText(root / "project" / "textures" / "a.png", "png");
  VirtualFileSystem vfs;
  std::string error;
  VfsMount project;
  project.point = "/project";
  project.layers.push_back(
    { DirectoryVfsBackend::open(root / "project", true, error), "scene-pack" });
  vfs.mount(project, error);
  testTrue(counters,
           PackageMounts::packMounted(
             vfs, "/project", root / "out.ilpk", 1u << 20, error),
           "a project with a manifest packs");
  LoadedPackage packed;
  testTrue(
    counters,
    PackageMounts::open(root / "out.ilpk", PackageCeilings{}, packed, error) &&
      packed.manifest.id == "scene-pack",
    "the archive opens as a package");
  VfsStat stat;
  testTrue(counters,
           packed.backend->stat("scenes/main.ilsc", stat) &&
             packed.backend->stat("textures/a.png", stat),
           "every project file is inside");
  testTrue(
    counters,
    !PackageMounts::packMounted(vfs, "/project", root / "small.ilpk", 4, error),
    "the size bound is enforced");
  std::filesystem::remove(root / "project" / "illumo.json", code);
  testTrue(counters,
           !PackageMounts::packMounted(
             vfs, "/project", root / "none.ilpk", 1u << 20, error) &&
             error.find("illumo.json") != std::string::npos,
           "a directory without a manifest does not pack");
  packed = LoadedPackage{};
  std::filesystem::remove_all(root, code);
  return counters.failures;
}

void
registerPackageMountsTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Content.PackMounted",
               []() { return testPackMounted(); });
  registry.add("Illumo.Content.PackageDiscovery",
               []() { return testPackageDiscovery(); });
}
