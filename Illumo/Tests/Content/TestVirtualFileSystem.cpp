#include <Illumo/Content/PackageArchive.h>
#include <Illumo/Content/VfsAssetSource.h>
#include <Illumo/Content/VfsConsole.h>
#include <Illumo/Content/VfsTreeSource.h>
#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/MeshLoader.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>

static std::vector<uint8_t>
bytesOf(const std::string& text)
{
  return std::vector<uint8_t>(text.begin(), text.end());
}

static std::string
textOf(const std::vector<uint8_t>& bytes)
{
  return std::string(bytes.begin(), bytes.end());
}

// A scratch directory removed when the test ends.
class ScratchDirectory
{
public:
  explicit ScratchDirectory(const char* name)
  {
    m_path = std::filesystem::temp_directory_path() /
             (std::string("illumo-vfs-") + name);
    std::error_code code;
    std::filesystem::remove_all(m_path, code);
    std::filesystem::create_directories(m_path, code);
  }
  ~ScratchDirectory()
  {
    std::error_code code;
    std::filesystem::remove_all(m_path, code);
  }
  const std::filesystem::path& path() const { return m_path; }
  void file(const std::string& relative, const std::string& text) const
  {
    const std::filesystem::path target = m_path / relative;
    std::error_code code;
    std::filesystem::create_directories(target.parent_path(), code);
    std::ofstream stream(target, std::ios::binary);
    stream << text;
  }

private:
  std::filesystem::path m_path;
};

static std::shared_ptr<IVfsBackend>
archiveOf(const std::vector<std::pair<std::string, std::string>>& files)
{
  PackageArchiveWriter writer;
  std::string error;
  for (const std::pair<std::string, std::string>& file : files) {
    writer.add(file.first, bytesOf(file.second), error);
  }
  std::vector<uint8_t> bytes;
  writer.finish(bytes, error);
  std::shared_ptr<const PackageArchive> archive = PackageArchive::open(
    std::make_shared<MemoryPackageSource>(std::move(bytes)), error);
  return std::make_shared<ArchiveVfsBackend>(archive);
}

static VfsMount
single(const std::string& point,
       std::shared_ptr<IVfsBackend> backend,
       const std::string& packageId)
{
  VfsMount mount;
  mount.point = point;
  mount.layers.push_back({ std::move(backend), packageId });
  return mount;
}

static std::vector<std::string>
names(const std::vector<VfsEntry>& entries)
{
  std::vector<std::string> result;
  for (const VfsEntry& entry : entries) {
    result.push_back(entry.name);
  }
  return result;
}

static int
testVfsMountsAndStat()
{
  TestCounters counters;
  ScratchDirectory engine("mounts");
  engine.file("Shaders/basic.vert", "void main() {}");
  engine.file("readme.txt", "engine");
  std::string error;
  VirtualFileSystem vfs;
  testTrue(
    counters,
    vfs.mount(single("/engine",
                     DirectoryVfsBackend::open(engine.path(), false, error),
                     ""),
              error),
    "a directory mounts at /engine");
  testTrue(counters,
           vfs.mount(single("/packages/forest",
                            archiveOf({ { "illumo.json", "{}" },
                                        { "meshes/tree.obj", "v 0 0 0" } }),
                            "forest"),
                     error),
           "an archive mounts at /packages/forest");
  testTrue(
    counters,
    !vfs.mount(single("/packages/forest/meshes", archiveOf({}), "x"), error) &&
      !vfs.mount(single("/packages", archiveOf({}), "x"), error) &&
      !vfs.mount(single("/", archiveOf({}), "x"), error) &&
      !vfs.mount(single("/engine/", archiveOf({}), "x"), error),
    "overlapping, root and unnormalized points are refused");

  std::vector<VfsEntry> entries;
  testTrue(counters,
           vfs.list("/", entries, error) &&
             names(entries) == std::vector<std::string>{ "engine", "packages" },
           "the root lists mount names");
  testTrue(counters,
           vfs.list("/packages", entries, error) &&
             names(entries) == std::vector<std::string>{ "forest" },
           "/packages lists package ids");
  VfsStat stat;
  testTrue(counters,
           vfs.stat("/packages/forest/meshes/tree.obj", stat, error) &&
             stat.kind == VfsKind::File && stat.size == 7 &&
             stat.packageId == "forest",
           "stat names the supplying package");
  testTrue(counters,
           vfs.stat("/packages", stat, error) &&
             stat.kind == VfsKind::Directory && stat.packageId.empty(),
           "synthesized directories stat as directories");
  testTrue(counters,
           vfs.list("/engine", entries, error) &&
             names(entries) ==
               std::vector<std::string>{ "Shaders", "readme.txt" },
           "a directory mount lists its files");
  std::vector<uint8_t> bytes;
  testTrue(counters,
           vfs.read("/engine/Shaders/basic.vert", bytes, error) &&
             textOf(bytes) == "void main() {}",
           "directory files read");
  testTrue(
    counters,
    vfs.readRange("/packages/forest/meshes/tree.obj", 2, 3, bytes, error) &&
      textOf(bytes) == "0 0",
    "ranges read from archives");
  testTrue(counters,
           !vfs.read("/packages/forest/missing", bytes, error) &&
             !vfs.read("/nowhere/file", bytes, error) &&
             !vfs.read("/packages", bytes, error),
           "missing paths and directories do not read");
  std::size_t total = 0;
  testTrue(counters,
           vfs.list("/engine", entries, error, 1, 5, &total) &&
             entries.size() == 1 && entries[0].name == "readme.txt" &&
             total == 2,
           "listings page");
  testTrue(counters, vfs.unmount("/engine"), "a mount unmounts");
  testTrue(counters,
           !vfs.stat("/engine/readme.txt", stat, error) &&
             !vfs.unmount("/engine"),
           "an unmounted path is gone");
  return counters.failures;
}

static int
testVfsOverlayOrder()
{
  TestCounters counters;
  std::string error;
  VirtualFileSystem vfs;
  VfsMount app;
  app.point = "/app";
  // Top first: a mod replacing a.txt and turning "dir" into a file, then a
  // middle layer that makes "notes" a directory over the base's file.
  app.layers.push_back(
    { archiveOf({ { "a.txt", "mod" }, { "dir", "file wins" } }), "mod" });
  app.layers.push_back(
    { archiveOf({ { "notes/extra.txt", "extra" }, { "c.txt", "mid" } }),
      "middle" });
  app.layers.push_back({ archiveOf({ { "a.txt", "base" },
                                     { "b.txt", "base" },
                                     { "dir/x.txt", "hidden" },
                                     { "notes", "base file" } }),
                         "game" });
  testTrue(counters, vfs.mount(app, error), "a layered /app mounts");
  std::vector<uint8_t> bytes;
  testTrue(counters,
           vfs.read("/app/a.txt", bytes, error) && textOf(bytes) == "mod",
           "the top layer's file wins");
  testTrue(counters,
           vfs.read("/app/b.txt", bytes, error) && textOf(bytes) == "base",
           "lower files show through");
  std::vector<VfsEntry> entries;
  testTrue(
    counters,
    vfs.list("/app", entries, error) &&
      names(entries) ==
        std::vector<std::string>{ "a.txt", "b.txt", "c.txt", "dir", "notes" },
    "directories union across layers");
  VfsStat stat;
  testTrue(counters,
           vfs.stat("/app/dir", stat, error) && stat.kind == VfsKind::File &&
             stat.packageId == "mod",
           "a top file replaces a lower directory");
  testTrue(counters,
           !vfs.read("/app/dir/x.txt", bytes, error),
           "files under a replaced directory are hidden");
  testTrue(counters,
           vfs.stat("/app/notes", stat, error) &&
             stat.kind == VfsKind::Directory &&
             vfs.read("/app/notes/extra.txt", bytes, error) &&
             textOf(bytes) == "extra",
           "a higher directory hides a lower file");
  std::shared_ptr<const VfsMountTable> table = vfs.table();
  testTrue(counters,
           table->find("/app") != nullptr &&
             table->find("/app")->conflicts.size() == 2,
           "both collisions are recorded at mount time");
  return counters.failures;
}

static int
testVfsEscapeRejected()
{
  TestCounters counters;
  ScratchDirectory root("escape");
  root.file("pkg/lower.txt", "lower");
  root.file("outside.txt", "secret");
  std::string error;
  VirtualFileSystem vfs;
  vfs.mount(single("/packages/pkg",
                   DirectoryVfsBackend::open(root.path() / "pkg", true, error),
                   "pkg"),
            error);
  std::vector<uint8_t> bytes;
  for (const char* path : { "/packages/pkg/../outside.txt",
                            "/packages/pkg/..\\outside.txt",
                            "/packages/pkg/./lower.txt",
                            "packages/pkg/lower.txt",
                            "/packages/pkg/C:/outside.txt" }) {
    testTrue(counters,
             !vfs.read(path, bytes, error),
             (std::string("refuses ") + path).c_str());
  }
  testTrue(counters,
           !vfs.read("/packages/pkg/LOWER.txt", bytes, error),
           "lookups are case-sensitive on every host");
  testTrue(counters,
           vfs.read("/packages/pkg/lower.txt", bytes, error),
           "the exact name reads");
  // A link inside the package pointing outside it must not resolve. Creating
  // links can need privileges on Windows; the check runs when it is allowed.
  std::error_code code;
  std::filesystem::create_directory_symlink(
    root.path(), root.path() / "pkg" / "link", code);
  if (!code) {
    testTrue(counters,
             !vfs.read("/packages/pkg/link/outside.txt", bytes, error),
             "a symlinked directory does not escape the root");
    std::vector<VfsEntry> entries;
    vfs.list("/packages/pkg", entries, error);
    testTrue(counters,
             names(entries) == std::vector<std::string>{ "lower.txt" },
             "symlinks are not listed");
    testTrue(counters,
             !vfs.write("/packages/pkg/link/planted.txt", bytesOf("x"), error),
             "writes do not follow links out of the root");
  }
  return counters.failures;
}

static int
testVfsConcurrentReads()
{
  TestCounters counters;
  std::string error;
  VirtualFileSystem vfs;
  std::vector<std::pair<std::string, std::string>> files;
  for (int index = 0; index < 16; ++index) {
    files.push_back(
      { "f" + std::to_string(index) + ".txt",
        std::string(200 + index * 37, static_cast<char>('a' + index)) });
  }
  vfs.mount(single("/packages/data", archiveOf(files), "data"), error);
  std::atomic<int> failures{ 0 };
  std::atomic<bool> stop{ false };
  std::vector<std::thread> readers;
  for (int thread = 0; thread < 6; ++thread) {
    readers.emplace_back([&vfs, &files, &failures, thread]() {
      std::vector<uint8_t> bytes;
      std::string readError;
      for (int round = 0; round < 300; ++round) {
        const std::pair<std::string, std::string>& file =
          files[static_cast<std::size_t>((round + thread) % 16)];
        if (!vfs.read("/packages/data/" + file.first, bytes, readError) ||
            textOf(bytes) != file.second) {
          failures.fetch_add(1);
        }
      }
    });
  }
  // Remount an unrelated package while readers run.
  std::thread churn([&vfs, &stop]() {
    std::string churnError;
    while (!stop.load()) {
      vfs.mount(single("/packages/churn", archiveOf({ { "x", "y" } }), "churn"),
                churnError);
      vfs.unmount("/packages/churn");
    }
  });
  for (std::thread& reader : readers) {
    reader.join();
  }
  stop.store(true);
  churn.join();
  testTrue(counters,
           failures.load() == 0,
           "concurrent reads during mount churn all succeed");
  return counters.failures;
}

static int
testVfsUnmountWhileOpen()
{
  TestCounters counters;
  std::string error;
  VirtualFileSystem vfs;
  std::weak_ptr<IVfsBackend> weak;
  {
    std::shared_ptr<IVfsBackend> backend =
      archiveOf({ { "big.txt", std::string(5000, 'z') } });
    weak = backend;
    vfs.mount(single("/packages/temp", backend, "temp"), error);
  }
  std::shared_ptr<VfsFile> file = vfs.open("/packages/temp/big.txt", error);
  testTrue(counters,
           file != nullptr && file->size() == 5000 &&
             file->packageId() == "temp",
           "a file opens");
  testTrue(counters, vfs.unmount("/packages/temp"), "its mount unmounts");
  std::vector<uint8_t> bytes;
  testTrue(counters,
           file != nullptr && file->read(4990, 100, bytes, error) &&
             bytes.size() == 10,
           "the open file still reads after unmount");
  testTrue(counters, !weak.expired(), "the open file keeps its backend alive");
  file.reset();
  testTrue(counters, weak.expired(), "the backend closes with its last reader");
  return counters.failures;
}

static int
testVfsWriteOnlyWritable()
{
  TestCounters counters;
  ScratchDirectory project("write");
  ScratchDirectory engine("write-engine");
  std::string error;
  VirtualFileSystem vfs;
  vfs.mount(single("/project",
                   DirectoryVfsBackend::open(project.path(), true, error),
                   "project"),
            error);
  vfs.mount(single("/engine",
                   DirectoryVfsBackend::open(engine.path(), false, error),
                   ""),
            error);
  VfsMount layered;
  layered.point = "/app";
  layered.layers.push_back(
    { DirectoryVfsBackend::open(project.path(), true, error), "project" });
  layered.layers.push_back({ archiveOf({ { "a", "b" } }), "game" });
  vfs.mount(layered, error);

  testTrue(counters,
           vfs.write("/project/scenes/new/main.ilsc", bytesOf("{}"), error),
           "a project write creates parent directories");
  std::vector<uint8_t> bytes;
  testTrue(counters,
           vfs.read("/project/scenes/new/main.ilsc", bytes, error) &&
             textOf(bytes) == "{}",
           "and reads back");
  testTrue(
    counters,
    vfs.write("/project/scenes/new/main.ilsc", bytesOf("{\"v\":2}"), error) &&
      vfs.read("/project/scenes/new/main.ilsc", bytes, error) &&
      textOf(bytes) == "{\"v\":2}",
    "a write replaces a file");
  testTrue(counters,
           !vfs.write("/engine/x.txt", bytesOf("x"), error),
           "read-only mounts refuse writes");
  testTrue(counters,
           !vfs.write("/app/x.txt", bytesOf("x"), error),
           "merged views refuse writes");
  testTrue(counters,
           !vfs.write("/project/mod.wasm", bytesOf("x"), error) &&
             !vfs.write("/project/Mod.WASM", bytesOf("x"), error) &&
             !vfs.write("/project/illumo.json", bytesOf("{}"), error) &&
             !vfs.write("/project/sub/ILLUMO.JSON", bytesOf("{}"), error),
           "modules and manifests cannot be written");
  testTrue(counters,
           !vfs.write("/project", bytesOf("x"), error) &&
             !vfs.write("/project/scenes/new", bytesOf("x"), error),
           "a directory is not replaced by a file");
  project.file(".illumo-staging-123", "tmp");
  std::vector<VfsEntry> entries;
  vfs.list("/project", entries, error);
  testTrue(counters,
           names(entries) == std::vector<std::string>{ "scenes" },
           "host staging files are hidden");
  return counters.failures;
}

// Records the material text AssetManager hands the mesh loader.
class RecordingMeshBackend : public IMeshLoaderBackend
{
public:
  std::string materialText;
  bool loadFromFile(const std::string& filePath,
                    const MeshLoadOptions& options,
                    MeshLoadResult* outResult) override
  {
    (void)filePath;
    (void)options;
    outResult->success = false;
    return false;
  }
  bool loadFromMemory(const std::string& fileContent,
                      const MeshLoadOptions& options,
                      const std::string& baseDir,
                      MeshLoadResult* outResult) override
  {
    (void)fileContent;
    (void)baseDir;
    materialText = options.materialText;
    MeshVertex vertex;
    outResult->mesh.vertices = { vertex, vertex, vertex };
    outResult->mesh.vertices[1].position.x = 1.0f;
    outResult->mesh.vertices[2].position.y = 1.0f;
    outResult->mesh.indices = { 0, 1, 2 };
    outResult->success = true;
    return true;
  }
};

static std::string
solidTga()
{
  std::string bytes(18, '\0');
  bytes[2] = 2;
  bytes[12] = 4;
  bytes[14] = 4;
  bytes[16] = 32;
  bytes[17] = 0x28;
  for (int pixel = 0; pixel < 16; ++pixel) {
    bytes += std::string("\x28\x50\xC8\xFF", 4);
  }
  return bytes;
}

static int
testVfsAssetSourceTexture()
{
  TestCounters counters;
  std::string error;
  std::shared_ptr<VirtualFileSystem> vfs =
    std::make_shared<VirtualFileSystem>();
  vfs->mount(single("/app",
                    archiveOf({ { "textures/leaf.tga", solidTga() },
                                { "meshes/tree.obj",
                                  "mtllib tree.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\n"
                                  "usemtl leaf\nf 1 2 3\n" },
                                { "meshes/tree.mtl",
                                  "newmtl leaf\nKd 0.1 0.8 0.2\n" } }),
                    "game"),
             error);
  VfsAssetSource source(vfs);
  testEqStr(counters,
            source.canonical("textures/leaf.tga"),
            "/app/textures/leaf.tga",
            "relative names join the base directory");
  testEqStr(counters,
            source.canonical("/app/meshes/../x"),
            "",
            "invalid references canonicalize to nothing");

  HeadlessRenderFixture fixture(320, 240);
  fixture.mock.Initialize();
  AssetManager assets(&fixture.renderer, false, &source);
  const TextureHandle texture = assets.acquireTexture(
    "textures/leaf.tga", TextureOptions{}, AssetLoadMode::Synchronous);
  testTrue(counters,
           assets.getState(texture).state == AssetState::Ready,
           "a texture loads through the virtual file tree");

  // The real loader resolves materials from supplied text.
  MeshLoadOptions withText;
  withText.materialText = "newmtl leaf\nKd 0.1 0.8 0.2\n";
  const MeshLoadResult parsed = MeshLoader::loadFromMemory(
    "mtllib tree.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl leaf\nf 1 2 3\n",
    withText);
  testTrue(counters,
           parsed.success && !parsed.mesh.materials.empty() &&
             std::abs(parsed.mesh.materials.front().diffuse.y - 0.8f) < 1e-4f,
           "material text supplies the OBJ's materials");

  std::shared_ptr<RecordingMeshBackend> recorder =
    std::make_shared<RecordingMeshBackend>();
  MeshLoader::setCustomBackend(recorder);
  const MeshHandle mesh = assets.acquireMesh("meshes/tree.obj");
  MeshLoader::resetBackend();
  testTrue(counters, mesh.isValid(), "a mesh loads through the file tree");
  testTrue(counters,
           recorder->materialText.find("Kd 0.1 0.8 0.2") != std::string::npos,
           "the MTL beside the OBJ is read through the same source");
  return counters.failures;
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
testVfsConsoleListing()
{
  TestCounters counters;
  std::string error;
  VirtualFileSystem vfs;
  testTrue(counters,
           contains(VfsConsole::run(vfs, { "mounts" }), "(no mounts)"),
           "an empty table says so");
  vfs.mount(
    single("/packages/forest",
           archiveOf({ { "meshes/tree.obj", "v 0 0 0\n" },
                       { "meshes/deep/a.txt", "a" },
                       { "blob.bin", std::string("\x01\x02\x03", 3) } }),
           "forest"),
    error);
  testTrue(counters,
           contains(VfsConsole::run(vfs, { "mounts" }),
                    "/packages/forest  forest (archive)"),
           "mounts shows points, packages and kinds");
  const std::vector<std::string> listing =
    VfsConsole::run(vfs, { "ls", "/packages/forest" });
  testTrue(counters,
           listing.size() == 2 && listing[0] == "blob.bin  3 B" &&
             listing[1] == "meshes/",
           "ls lists one directory with sizes");
  const std::vector<std::string> tree =
    VfsConsole::run(vfs, { "tree", "/", "5" });
  testTrue(counters,
           contains(tree, "packages/") && contains(tree, "      meshes/") &&
             contains(tree, "          a.txt"),
           "tree indents each level");
  testTrue(counters,
           VfsConsole::run(vfs, { "tree", "/", "1" }).size() == 2,
           "tree stops at the requested depth");
  testTrue(
    counters,
    contains(VfsConsole::run(vfs, { "stat", "/packages/forest/blob.bin" }),
             "package: forest"),
    "stat names the package");
  testTrue(counters,
           contains(VfsConsole::run(
                      vfs, { "cat", "/packages/forest/meshes/tree.obj" }),
                    "v 0 0 0"),
           "cat previews text");
  testTrue(
    counters,
    contains(VfsConsole::run(vfs, { "cat", "/packages/forest/blob.bin" }),
             "01 02 03"),
    "cat dumps binary as hex");
  testTrue(counters,
           contains(VfsConsole::run(vfs, { "cat", "/nope" }), "No such file"),
           "errors are reported, not thrown");
  testTrue(counters,
           contains(VfsConsole::run(vfs, { "frobnicate" }), "usage"),
           "unknown verbs print usage");
  return counters.failures;
}

static int
testVfsTreeSource()
{
  TestCounters counters;
  std::shared_ptr<VirtualFileSystem> vfs =
    std::make_shared<VirtualFileSystem>();
  std::string error;
  vfs->mount(single("/packages/forest",
                    archiveOf({ { "csim/rules.json", "{}" },
                                { "scenes/grove.ilsc", "0123456789" } }),
                    "forest"),
             error);
  const VfsTreeSource source(vfs);
  std::vector<FileTreeEntry> entries;
  testTrue(counters,
           source.list("/", entries) && entries.size() == 1 &&
             entries[0].name == "packages" && entries[0].directory,
           "the synthesized root lists /packages");
  testTrue(counters,
           source.list("/packages/forest", entries) && entries.size() == 2 &&
             entries[0].name == "csim" && entries[0].directory &&
             entries[1].name == "scenes",
           "a mount lists its directories");
  testTrue(counters,
           !source.list("/packages/forest/csim/rules.json", entries) &&
             entries.empty(),
           "a file is not a directory");
  FileTreeStatus status;
  testTrue(counters,
           source.stat("/packages/forest/scenes/grove.ilsc", status) &&
             !status.directory && status.size == 10 &&
             status.packageId == "forest",
           "stat reports size and the supplying package");
  testTrue(counters,
           !source.stat("/packages/forest/../../etc", status),
           "an unnormalized path does not resolve");
  const VfsTreeSource empty(nullptr);
  testTrue(counters,
           !empty.list("/", entries) && !empty.stat("/", status),
           "no tree lists nothing");
  return counters.failures;
}

void
registerVirtualFileSystemTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Content.VfsTreeSource",
               []() { return testVfsTreeSource(); });
  registry.add("Illumo.Content.VfsMountsAndStat",
               []() { return testVfsMountsAndStat(); });
  registry.add("Illumo.Content.VfsOverlayOrder",
               []() { return testVfsOverlayOrder(); });
  registry.add("Illumo.Content.VfsEscapeRejected",
               []() { return testVfsEscapeRejected(); });
  registry.add("Illumo.Content.VfsConcurrentReads",
               []() { return testVfsConcurrentReads(); });
  registry.add("Illumo.Content.VfsUnmountWhileOpen",
               []() { return testVfsUnmountWhileOpen(); });
  registry.add("Illumo.Content.VfsWriteOnlyWritable",
               []() { return testVfsWriteOnlyWritable(); });
  registry.add("Illumo.Content.VfsAssetSourceTexture",
               []() { return testVfsAssetSourceTexture(); });
  registry.add("Illumo.Content.VfsConsoleListing",
               []() { return testVfsConsoleListing(); });
}
