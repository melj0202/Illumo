#include <Illumo/Content/PackageArchive.h>
#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/WasmFileServices.h>
#include <IllumoGuest/FileProtocol.h>
#include <IllumoGuest/FileTree.h>
#include <IllumoGuest/Protocol.h>
#include <IllumoGuest/VfsAssets.h>
#include <chrono>
#include <fstream>
#include <thread>

struct FileResult
{
  GuestFileOutcome outcome = GuestFileOutcome::IoError;
  std::vector<std::byte> payload;
};
class FileFixture
{
public:
  std::filesystem::path root;
  std::unique_ptr<WasmFileServices> service;
  std::uint64_t next = 1;
  static WasmFileLimits smallLimits()
  {
    WasmFileLimits limits;
    limits.fileBytes = 16;
    limits.stagedBytes = 64;
    limits.storageBytes = 32;
    limits.openFiles = 2;
    return limits;
  }
  static std::uint32_t defaultGrants()
  {
    return static_cast<std::uint32_t>(GuestCapability::Assets) |
           static_cast<std::uint32_t>(GuestCapability::Storage);
  }
  explicit FileFixture(std::shared_ptr<const VirtualFileSystem> packages = {},
                       std::uint32_t grants = defaultGrants(),
                       WasmFileLimits limits = smallLimits())
  {
    root = std::filesystem::absolute(
      "file-service-" +
      std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(root)) {
      throw std::runtime_error("Test directory collision");
    }
    std::filesystem::create_directory(root / "package");
    std::filesystem::create_directory(root / "storage");
    std::ofstream(root / "package" / "asset.txt", std::ios::binary) << "abc";
    service = packages
                ? std::make_unique<WasmFileServices>(
                    71, grants, packages, root / "storage", limits)
                : std::make_unique<WasmFileServices>(
                    71, grants, root / "package", root / "storage", limits);
  }
  ~FileFixture()
  {
    service.reset();
    // This unique directory was created by this fixture, below its CTest cwd.
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
  FileFixture(const FileFixture&) = delete;
  FileFixture& operator=(const FileFixture&) = delete;
  GuestServiceRecord record(const GuestFileRequest& request)
  {
    GuestWireWriter writer;
    request.write(writer);
    return {
      next++, GuestService::File, GuestServiceStatus::Request, writer.take()
    };
  }
  FileResult send(const GuestFileRequest& request)
  {
    GuestServices batch;
    batch.records.push_back(record(request));
    const std::uint64_t id = batch.records[0].request;
    if (!service->submit(batch)) {
      throw std::runtime_error(service->error());
    }
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
      GuestServices ready = service->poll();
      if (!ready.records.empty()) {
        if (ready.records.size() != 1 || ready.records[0].request != id) {
          throw std::runtime_error("File completion ownership mismatch");
        }
        GuestWireReader reader(ready.records[0].payload);
        FileResult result;
        result.outcome = static_cast<GuestFileOutcome>(reader.u32());
        const std::span<const std::byte> bytes =
          reader.bytes(reader.remaining());
        result.payload.assign(bytes.begin(), bytes.end());
        return result;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("File completion timed out");
  }
};

static std::shared_ptr<IVfsBackend>
archiveOf(const std::vector<std::pair<std::string, std::string>>& files)
{
  PackageArchiveWriter writer;
  std::string error;
  for (const std::pair<std::string, std::string>& file : files) {
    writer.add(file.first,
               std::vector<uint8_t>(file.second.begin(), file.second.end()),
               error);
  }
  std::vector<uint8_t> bytes;
  writer.finish(bytes, error);
  std::shared_ptr<const PackageArchive> archive = PackageArchive::open(
    std::make_shared<MemoryPackageSource>(std::move(bytes)), error);
  return std::make_shared<ArchiveVfsBackend>(archive);
}

static std::string
textOf(const std::vector<std::byte>& bytes)
{
  std::string text;
  for (std::byte value : bytes) {
    text.push_back(static_cast<char>(value));
  }
  return text;
}

// The Package area served from an .ilpk-backed /app with an overlay on top.
static int
packageFromArchive()
{
  TestCounters counters;
  std::shared_ptr<VirtualFileSystem> vfs =
    std::make_shared<VirtualFileSystem>();
  VfsMount app;
  app.point = "/app";
  app.layers.push_back({ archiveOf({ { "a.txt", "mod" } }), "mod" });
  app.layers.push_back({ archiveOf({ { "a.txt", "base" },
                                     { "deep/b.txt", "bee" },
                                     { "big.bin", std::string(40, 'x') } }),
                         "game" });
  std::string error;
  if (!vfs->mount(std::move(app), error)) {
    throw std::runtime_error(error);
  }
  FileFixture fixture(vfs);
  GuestFileRequest request;
  request.path = "a.txt";
  FileResult opened = fixture.send(request);
  GuestWireReader reader(opened.payload);
  const GuestResourceId overlaid = GuestResourceId::read(reader);
  testTrue(counters,
           opened.outcome == GuestFileOutcome::Success && reader.u64() == 3,
           "an overlaid package file opens with the overlay's size");
  request = {};
  request.action = GuestFileAction::Read;
  request.file = overlaid;
  request.size = 3;
  testTrue(counters,
           textOf(fixture.send(request).payload) == "mod",
           "reads come from the top layer");
  request = {};
  request.action = GuestFileAction::Close;
  request.file = overlaid;
  fixture.send(request);
  request = {};
  request.path = "deep/b.txt";
  opened = fixture.send(request);
  GuestWireReader nested(opened.payload);
  request = {};
  request.action = GuestFileAction::Read;
  request.file = GuestResourceId::read(nested);
  request.offset = 1;
  request.size = 2;
  testTrue(counters,
           textOf(fixture.send(request).payload) == "ee",
           "ranged reads work inside archives");
  const GuestResourceId nestedFile = request.file;
  request = {};
  request.action = GuestFileAction::Close;
  request.file = nestedFile;
  fixture.send(request);
  for (const char* denied : { "../a.txt", "deep", "big.bin", "/a.txt" }) {
    request = {};
    request.path = denied;
    testTrue(counters,
             fixture.send(request).outcome == GuestFileOutcome::Denied,
             "escapes, directories and oversized files are denied");
  }
  request = {};
  request.path = "missing.txt";
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::NotFound,
           "a missing package file is not found");
  return counters.failures == 0 ? 0 : 1;
}

static std::uint32_t
allGrants()
{
  return static_cast<std::uint32_t>(GuestCapability::Assets) |
         static_cast<std::uint32_t>(GuestCapability::Storage) |
         static_cast<std::uint32_t>(GuestCapability::SelectedFiles) |
         static_cast<std::uint32_t>(GuestCapability::ProjectFiles);
}

static bool
decodes(const GuestFileRequest& request)
{
  GuestWireWriter writer;
  request.write(writer);
  GuestFileRequest decoded;
  return GuestFileRequest::read(writer.data(), decoded) &&
         decoded.action == request.action && decoded.area == request.area &&
         decoded.path == request.path && decoded.target == request.target &&
         decoded.offset == request.offset && decoded.size == request.size;
}

static int
decoderV2()
{
  TestCounters counters;
  GuestFileRequest request;
  request.area = GuestFileArea::Mounted;
  request.path = "/packages/forest/a.png";
  testTrue(counters, decodes(request), "a mounted open round-trips");
  request = {};
  request.action = GuestFileAction::List;
  request.area = GuestFileArea::Mounted;
  request.path = "/packages";
  request.offset = 256;
  request.size = 256;
  testTrue(counters, decodes(request), "a list page round-trips");
  request.size = 0;
  testTrue(counters, !decodes(request), "an empty list page is refused");
  request.size = 257;
  testTrue(counters, !decodes(request), "an oversized list page is refused");
  request.size = 10;
  request.area = GuestFileArea::Package;
  testTrue(counters, !decodes(request), "lists address mounted paths only");
  request = {};
  request.action = GuestFileAction::Stat;
  request.area = GuestFileArea::Mounted;
  request.path = "/app/a.txt";
  testTrue(counters, decodes(request), "a stat round-trips");
  request.offset = 1;
  testTrue(counters, !decodes(request), "a stat carries no offset");
  request = {};
  request.action = GuestFileAction::Import;
  request.area = GuestFileArea::Selected;
  request.path = "sel-1";
  request.target = "/project/a.png";
  testTrue(counters, decodes(request), "an import round-trips");
  request.target = "project/a.png";
  testTrue(counters, !decodes(request), "an import target is absolute");
  request.action = GuestFileAction::Pack;
  request.target = "/project";
  testTrue(counters, decodes(request), "a pack round-trips");
  request.area = GuestFileArea::Mounted;
  testTrue(counters, !decodes(request), "a pack names a selected grant");
  request = {};
  request.area = GuestFileArea::Mounted;
  request.path = "relative.txt";
  testTrue(counters, !decodes(request), "mounted opens need absolute paths");
  request.path = "a.txt";
  request.target = "/project/x";
  testTrue(counters, !decodes(request), "an open carries no target");
  request = {};
  request.action = GuestFileAction::Read;
  request.file = GuestResourceId{ 1, GuestResourceKind::File, 1, 1 };
  request.size = GuestFileRequest::MaximumMountedBlock;
  testTrue(counters, decodes(request), "a 1 MiB read block is accepted");
  request.size = GuestFileRequest::MaximumMountedBlock + 1;
  testTrue(counters, !decodes(request), "larger read blocks are refused");
  // A version 1 payload (no target field) is refused outright.
  GuestWireWriter old;
  old.u32(1);
  old.u32(0);
  old.u32(0);
  old.u32(0);
  GuestResourceId{}.write(old);
  old.text("a.txt");
  old.u64(0);
  old.u64(0);
  old.u32(0);
  GuestFileRequest decoded;
  testTrue(counters,
           !GuestFileRequest::read(old.data(), decoded),
           "version 1 requests are refused");
  GuestFileListing listing;
  listing.total = 3;
  listing.entries = { { "a", true, 0 }, { "b.txt", false, 12 } };
  GuestWireWriter listed;
  listing.write(listed);
  GuestFileListing back;
  testTrue(counters,
           GuestFileListing::read(listed.data(), back) && back.total == 3 &&
             back.entries.size() == 2 && back.entries[0].directory &&
             back.entries[1].size == 12,
           "listings round-trip");
  GuestFileStatus status{ false, 99, "forest" };
  GuestWireWriter stated;
  status.write(stated);
  GuestFileStatus statusBack;
  testTrue(counters,
           GuestFileStatus::read(stated.data(), statusBack) &&
             statusBack.size == 99 && statusBack.packageId == "forest",
           "stats round-trip");
  return counters.failures == 0 ? 0 : 1;
}

// A tree of mounts on disk: /app (archive), /packages/forest (directory with
// a 1.5 MiB file and 300 small ones) and a writable /project.
class MountedTree
{
public:
  std::filesystem::path root;
  std::shared_ptr<VirtualFileSystem> vfs =
    std::make_shared<VirtualFileSystem>();
  std::string big;
  explicit MountedTree(bool project = true)
  {
    root = std::filesystem::absolute(
      "mounted-" +
      std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "forest" / "many");
    std::filesystem::create_directories(root / "project");
    for (std::size_t index = 0; index < 1536u * 1024u; ++index) {
      big.push_back(static_cast<char>('a' + index % 23));
    }
    std::ofstream(root / "forest" / "big.bin", std::ios::binary) << big;
    for (int index = 0; index < 300; ++index) {
      char name[32];
      std::snprintf(name, sizeof(name), "f%03d.txt", index);
      std::ofstream(root / "forest" / "many" / name, std::ios::binary) << index;
    }
    std::ofstream(root / "project" / "illumo.json", std::ios::binary)
      << R"({"format":"ilpk","format_version":1,"id":"my-scene","kind":"content"})";
    std::ofstream(root / "import-source.txt", std::ios::binary) << "imported";
    std::string error;
    VfsMount app;
    app.point = "/app";
    app.layers.push_back({ archiveOf({ { "a.txt", "app file" } }), "demo" });
    VfsMount forest;
    forest.point = "/packages/forest";
    forest.layers.push_back(
      { DirectoryVfsBackend::open(root / "forest", false, error), "forest" });
    if (!vfs->mount(app, error) || !vfs->mount(forest, error)) {
      throw std::runtime_error(error);
    }
    if (project) {
      VfsMount writable;
      writable.point = "/project";
      writable.layers.push_back(
        { DirectoryVfsBackend::open(root / "project", true, error),
          "my-scene" });
      if (!vfs->mount(writable, error)) {
        throw std::runtime_error(error);
      }
    }
  }
  ~MountedTree()
  {
    vfs.reset();
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};

static WasmFileLimits
mountedLimits()
{
  WasmFileLimits limits;
  limits.openFiles = 8;
  return limits;
}

static GuestResourceId
openFile(FileFixture& fixture,
         GuestFileArea area,
         const std::string& path,
         GuestFileOutcome* outcome,
         std::uint64_t* size,
         bool writing = false,
         std::uint64_t length = 0)
{
  GuestFileRequest request;
  request.area = area;
  request.path = path;
  request.writing = writing;
  request.size = writing ? length : 0;
  FileResult opened = fixture.send(request);
  *outcome = opened.outcome;
  if (opened.outcome != GuestFileOutcome::Success) {
    return {};
  }
  GuestWireReader reader(opened.payload);
  const GuestResourceId file = GuestResourceId::read(reader);
  *size = reader.u64();
  return file;
}

static void
closeFile(FileFixture& fixture, const GuestResourceId& file)
{
  GuestFileRequest request;
  request.action = GuestFileAction::Close;
  request.file = file;
  fixture.send(request);
}

static GuestFileOutcome
writeProject(FileFixture& fixture,
             const std::string& path,
             const std::string& text)
{
  GuestFileOutcome outcome = GuestFileOutcome::IoError;
  std::uint64_t size = 0;
  const GuestResourceId file = openFile(
    fixture, GuestFileArea::Mounted, path, &outcome, &size, true, text.size());
  if (outcome != GuestFileOutcome::Success) {
    return outcome;
  }
  GuestFileRequest request;
  request.action = GuestFileAction::Write;
  request.file = file;
  for (char value : text) {
    request.data.push_back(static_cast<std::byte>(value));
  }
  if (fixture.send(request).outcome != GuestFileOutcome::Success) {
    closeFile(fixture, file);
    return GuestFileOutcome::IoError;
  }
  request = {};
  request.action = GuestFileAction::Commit;
  request.file = file;
  const GuestFileOutcome committed = fixture.send(request).outcome;
  if (committed != GuestFileOutcome::Success) {
    closeFile(fixture, file);
  }
  return committed;
}

static int
mountedFiles()
{
  TestCounters counters;
  MountedTree tree;
  FileFixture fixture(tree.vfs, allGrants(), mountedLimits());
  GuestFileRequest request;
  request.action = GuestFileAction::Stat;
  request.area = GuestFileArea::Mounted;
  request.path = "/packages/forest/big.bin";
  FileResult stated = fixture.send(request);
  GuestFileStatus status;
  testTrue(counters,
           stated.outcome == GuestFileOutcome::Success &&
             GuestFileStatus::read(stated.payload, status) &&
             status.size == tree.big.size() && status.packageId == "forest",
           "stat reports size and package");

  request = {};
  request.action = GuestFileAction::List;
  request.area = GuestFileArea::Mounted;
  request.path = "/packages/forest/many";
  request.size = GuestFileRequest::MaximumListPage;
  FileResult first = fixture.send(request);
  GuestFileListing page;
  testTrue(counters,
           first.outcome == GuestFileOutcome::Success &&
             GuestFileListing::read(first.payload, page) && page.total == 300 &&
             page.entries.size() == 256 &&
             page.entries.front().name == "f000.txt",
           "a listing pages at 256 entries");
  request.offset = 256;
  FileResult second = fixture.send(request);
  testTrue(counters,
           GuestFileListing::read(second.payload, page) &&
             page.entries.size() == 44 &&
             page.entries.back().name == "f299.txt",
           "the cursor continues the listing");

  GuestFileOutcome outcome = GuestFileOutcome::IoError;
  std::uint64_t size = 0;
  const GuestResourceId big = openFile(fixture,
                                       GuestFileArea::Mounted,
                                       "/packages/forest/big.bin",
                                       &outcome,
                                       &size);
  request = {};
  request.action = GuestFileAction::Read;
  request.file = big;
  request.size = GuestFileRequest::MaximumMountedBlock;
  FileResult block = fixture.send(request);
  testTrue(counters,
           outcome == GuestFileOutcome::Success && size == tree.big.size() &&
             block.outcome == GuestFileOutcome::Success &&
             block.payload.size() == GuestFileRequest::MaximumMountedBlock &&
             textOf(block.payload) ==
               tree.big.substr(0, GuestFileRequest::MaximumMountedBlock),
           "mounted files read in 1 MiB blocks");
  closeFile(fixture, big);

  testTrue(counters,
           writeProject(fixture, "/project/scenes/main.ilsc", "{}") ==
             GuestFileOutcome::Success,
           "a project file is written and committed");
  std::vector<uint8_t> bytes;
  std::string error;
  testTrue(counters,
           tree.vfs->read("/project/scenes/main.ilsc", bytes, error) &&
             std::string(bytes.begin(), bytes.end()) == "{}",
           "the commit lands in the project");

  std::string grant;
  testTrue(counters,
           fixture.service->grantSelected(
             tree.root / "import-source.txt", false, grant, size),
           "a selection is granted");
  request = {};
  request.action = GuestFileAction::Import;
  request.area = GuestFileArea::Selected;
  request.path = grant;
  request.target = "/project/imports/source.txt";
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::Success &&
             tree.vfs->read("/project/imports/source.txt", bytes, error) &&
             std::string(bytes.begin(), bytes.end()) == "imported",
           "a selected file imports into the project");

  std::string saveGrant;
  testTrue(counters,
           fixture.service->grantSelected(
             tree.root / "out.ilpk", true, saveGrant, size),
           "a save location is granted");
  request = {};
  request.action = GuestFileAction::Pack;
  request.area = GuestFileArea::Selected;
  request.path = saveGrant;
  request.target = "/project";
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::Success,
           "the project packs on the pack worker");
  std::unique_ptr<PackageArchive> packed =
    PackageArchive::openFile(tree.root / "out.ilpk", error);
  testTrue(counters,
           packed != nullptr && packed->find("illumo.json") != nullptr &&
             packed->find("scenes/main.ilsc") != nullptr &&
             packed->find("imports/source.txt") != nullptr,
           "the archive holds the whole project");
  return counters.failures == 0 ? 0 : 1;
}

static int
mountedDeny()
{
  TestCounters counters;
  MountedTree tree;
  {
    const std::uint32_t readOnly =
      static_cast<std::uint32_t>(GuestCapability::Assets) |
      static_cast<std::uint32_t>(GuestCapability::Storage) |
      static_cast<std::uint32_t>(GuestCapability::SelectedFiles);
    FileFixture fixture(tree.vfs, readOnly, mountedLimits());
    testTrue(counters,
             writeProject(fixture, "/project/a.txt", "x") ==
               GuestFileOutcome::Denied,
             "project writes need ProjectFiles");
    std::string grant;
    std::uint64_t size = 0;
    fixture.service->grantSelected(
      tree.root / "import-source.txt", false, grant, size);
    GuestFileRequest request;
    request.action = GuestFileAction::Import;
    request.area = GuestFileArea::Selected;
    request.path = grant;
    request.target = "/project/a.txt";
    testTrue(counters,
             fixture.send(request).outcome == GuestFileOutcome::Denied,
             "imports need ProjectFiles");
  }
  {
    FileFixture fixture(
      tree.vfs,
      static_cast<std::uint32_t>(GuestCapability::Storage) |
        static_cast<std::uint32_t>(GuestCapability::ProjectFiles),
      mountedLimits());
    GuestFileRequest request;
    request.action = GuestFileAction::List;
    request.area = GuestFileArea::Mounted;
    request.path = "/packages";
    request.size = 10;
    testTrue(counters,
             fixture.send(request).outcome == GuestFileOutcome::Denied,
             "listing needs Assets");
  }
  WasmFileLimits tight = mountedLimits();
  tight.projectBytes = 100;
  FileFixture fixture(tree.vfs, allGrants(), tight);
  for (const char* path : { "/app/a.txt",
                            "/packages/forest/new.txt",
                            "/project/mod.wasm",
                            "/project/Plugin.WASM",
                            "/project/illumo.json",
                            "/project",
                            "/project/../app/x.txt" }) {
    testTrue(counters,
             writeProject(fixture, path, "x") == GuestFileOutcome::Denied,
             (std::string("refuses writing ") + path).c_str());
  }
  testTrue(counters,
           writeProject(fixture, "/project/huge.txt", std::string(120, 'z')) ==
             GuestFileOutcome::Denied,
           "the project quota is enforced at commit");
  testTrue(counters,
           writeProject(fixture, "/project/small.txt", "ok") ==
             GuestFileOutcome::Success,
           "writes inside the quota succeed");
  GuestFileOutcome outcome = GuestFileOutcome::IoError;
  std::uint64_t size = 0;
  const GuestResourceId packageFile =
    openFile(fixture, GuestFileArea::Package, "a.txt", &outcome, &size);
  GuestFileRequest request;
  request.action = GuestFileAction::Read;
  request.file = packageFile;
  request.size = GuestFileRequest::MaximumBlock + 1;
  testTrue(counters,
           outcome == GuestFileOutcome::Success &&
             fixture.send(request).outcome == GuestFileOutcome::Denied,
           "package-area reads keep 64 KiB blocks");
  closeFile(fixture, packageFile);
  request = {};
  request.action = GuestFileAction::Stat;
  request.area = GuestFileArea::Mounted;
  request.path = "/packages/forest/missing.txt";
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::NotFound,
           "missing mounted paths are not found");
  // Packing a project without a valid manifest is refused.
  std::filesystem::remove(tree.root / "project" / "illumo.json");
  std::string saveGrant;
  fixture.service->grantSelected(tree.root / "out.ilpk", true, saveGrant, size);
  request = {};
  request.action = GuestFileAction::Pack;
  request.area = GuestFileArea::Selected;
  request.path = saveGrant;
  request.target = "/project";
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::Denied &&
             !std::filesystem::exists(tree.root / "out.ilpk"),
           "a project without a manifest does not pack");
  return counters.failures == 0 ? 0 : 1;
}

// Moves one round of service batches between the guest queue and the host,
// the way the runtime does at a frame boundary.
static void
exchangeOnce(FileFixture& fixture, GuestServiceQueue& queue)
{
  GuestServices completions = fixture.service->poll();
  GuestWireWriter writer;
  completions.write(writer);
  std::vector<std::byte> outgoing;
  if (!queue.exchange(writer.data(), outgoing)) {
    throw std::runtime_error("Guest queue rejected host completions");
  }
  GuestServices requests;
  if (!GuestServices::read(outgoing, requests, true)) {
    throw std::runtime_error("Guest wrote an invalid request batch");
  }
  if (!requests.records.empty() && !fixture.service->submit(requests)) {
    throw std::runtime_error(fixture.service->error());
  }
}

// The guest SDK's GuestFiles and GuestFileTree against the host service,
// bridged by hand the way the runtime exchanges service batches.
static int
guestFileTree()
{
  TestCounters counters;
  MountedTree tree;
  FileFixture fixture(tree.vfs, allGrants(), mountedLimits());
  GuestServiceQueue queue;
  GuestFiles files(queue);
  GuestFileTree browser(files);
  std::vector<GuestFileEntry> listed;
  GuestFileOutcome listOutcome = GuestFileOutcome::IoError;
  GuestFileStatus status;
  std::vector<std::byte> loaded;
  GuestFileOutcome writeOutcome = GuestFileOutcome::IoError;
  std::vector<std::byte> appFile;
  int callbacks = 0;
  browser.list(
    "/packages/forest/many",
    [&](GuestFileOutcome outcome, std::vector<GuestFileEntry> entries) {
      listOutcome = outcome;
      listed = std::move(entries);
      ++callbacks;
    });
  browser.stat("/packages/forest/big.bin",
               [&](GuestFileOutcome, GuestFileStatus value) {
                 status = std::move(value);
                 ++callbacks;
               });
  browser.read("/packages/forest/big.bin",
               [&](GuestFileOutcome, std::vector<std::byte> bytes) {
                 loaded = std::move(bytes);
                 ++callbacks;
               });
  std::vector<std::byte> note;
  for (char value : std::string("note")) {
    note.push_back(static_cast<std::byte>(value));
  }
  browser.write("/project/notes/n.txt", note, [&](GuestFileOutcome outcome) {
    writeOutcome = outcome;
    ++callbacks;
    // Callbacks may start new operations.
    browser.read("/app/a.txt",
                 [&](GuestFileOutcome, std::vector<std::byte> bytes) {
                   appFile = std::move(bytes);
                   ++callbacks;
                 });
  });
  for (int step = 0; step < 5000 && callbacks < 5; ++step) {
    exchangeOnce(fixture, queue);
    files.pump();
    browser.pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  testTrue(counters, callbacks == 5, "every operation calls back once");
  testTrue(counters,
           listOutcome == GuestFileOutcome::Success && listed.size() == 300 &&
             listed.back().name == "f299.txt",
           "a listing pages through a whole directory");
  testTrue(counters,
           status.size == tree.big.size() && status.packageId == "forest",
           "stat through the tree");
  testTrue(counters,
           textOf(loaded) == tree.big,
           "a 1.5 MiB mounted file reads intact in 1 MiB blocks");
  std::vector<uint8_t> bytes;
  std::string error;
  testTrue(counters,
           writeOutcome == GuestFileOutcome::Success &&
             tree.vfs->read("/project/notes/n.txt", bytes, error) &&
             std::string(bytes.begin(), bytes.end()) == "note",
           "a project write through the tree");
  testTrue(counters,
           textOf(appFile) == "app file",
           "an operation started from a callback completes");
  testTrue(counters, browser.idle() && files.idle(), "the tree drains");
  return counters.failures == 0 ? 0 : 1;
}

// Pumps the guest asset cache against the host until done() or a deadline.
template<typename Condition>
static bool
pumpAssets(FileFixture& fixture,
           GuestServiceQueue& queue,
           GuestFiles& files,
           GuestVfsAssets& cache,
           Condition done)
{
  for (int step = 0; step < 5000; ++step) {
    exchangeOnce(fixture, queue);
    files.pump();
    cache.pump();
    if (done()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

static std::string
textOf(const std::vector<unsigned char>& bytes)
{
  return std::string(bytes.begin(), bytes.end());
}

static int
guestPinnedPreload()
{
  TestCounters counters;
  MountedTree tree;
  FileFixture fixture(tree.vfs, allGrants(), mountedLimits());
  GuestServiceQueue queue;
  GuestFiles files(queue);
  GuestVfsAssets cache(files);
  testEqStr(counters,
            cache.canonical("Assets\\IllEd/./atlas.jpg"),
            "/app/Assets/IllEd/atlas.jpg",
            "relative names join /app");
  testEqStr(counters,
            cache.canonical("/packages/forest/../x"),
            "",
            "'..' never resolves");
  cache.preload({ "a.txt", "/packages/forest/big.bin", "missing.txt" });
  testTrue(counters, !cache.ready(), "preloads start pending");
  testTrue(
    counters,
    pumpAssets(fixture, queue, files, cache, [&]() { return cache.ready(); }),
    "every preload completes, missing ones included");
  std::vector<unsigned char> bytes;
  testTrue(counters,
           cache.read(cache.canonical("a.txt"), bytes) &&
             textOf(bytes) == "app file",
           "a package-relative preload serves AssetManager");
  testTrue(counters,
           !cache.contains("missing.txt") &&
             !cache.read(cache.canonical("missing.txt"), bytes),
           "a missing preload is absent");
  cache.setBudget(1);
  testTrue(counters,
           cache.contains("/packages/forest/big.bin") &&
             cache.contains("a.txt"),
           "pinned preloads survive any budget");
  return counters.failures == 0 ? 0 : 1;
}

static int
guestAssetFetchAndEvict()
{
  TestCounters counters;
  MountedTree tree;
  std::ofstream(tree.root / "forest" / "second.bin", std::ios::binary)
    << std::string(1536u * 1024u, 's');
  FileFixture fixture(tree.vfs, allGrants(), mountedLimits());
  GuestServiceQueue queue;
  GuestFiles files(queue);
  GuestVfsAssets cache(files);
  cache.setBudget(2u * 1024u * 1024u);
  const std::uint64_t first = cache.fetch({ "/packages/forest/big.bin",
                                            "/packages/forest/many/f001.txt",
                                            "/packages/forest/nope.bin",
                                            "/packages/forest/big.bin" });
  // The same file requested by a second set shares one load.
  const std::uint64_t again = cache.fetch({ "/packages/forest/big.bin" });
  std::vector<std::string> missing;
  testTrue(counters,
           pumpAssets(fixture,
                      queue,
                      files,
                      cache,
                      [&]() {
                        return cache.fetched(first, &missing) &&
                               cache.fetched(again);
                      }),
           "fetch sets complete");
  testTrue(counters,
           missing.size() == 1 && missing[0] == "/packages/forest/nope.bin",
           "unreadable paths are reported per set");
  std::vector<unsigned char> bytes;
  testTrue(counters,
           cache.read("/packages/forest/big.bin", bytes) &&
             textOf(bytes) == tree.big &&
             cache.read("/packages/forest/many/f001.txt", bytes) &&
             textOf(bytes) == "1",
           "fetched bytes serve synchronously");
  const std::uint64_t second = cache.fetch({ "/packages/forest/second.bin" });
  testTrue(
    counters,
    pumpAssets(
      fixture, queue, files, cache, [&]() { return cache.fetched(second); }),
    "a second set loads past the budget");
  testTrue(counters,
           cache.contains("/packages/forest/big.bin") &&
             cache.contains("/packages/forest/second.bin") &&
             cache.cachedBytes() > cache.budget(),
           "held entries are never evicted");
  cache.release(first);
  testTrue(counters,
           cache.contains("/packages/forest/big.bin"),
           "an entry another set still holds stays");
  cache.release(again);
  testTrue(counters,
           !cache.contains("/packages/forest/big.bin") &&
             cache.contains("/packages/forest/second.bin") &&
             cache.cachedBytes() <= cache.budget(),
           "released entries are evicted least recently used first");
  cache.release(second);
  const std::uint64_t cached = cache.fetch({ "/packages/forest/second.bin" });
  testTrue(counters,
           cache.fetched(cached),
           "a set of cached entries completes immediately");
  return counters.failures == 0 ? 0 : 1;
}

static int
guestLocalEntries()
{
  TestCounters counters;
  GuestServiceQueue queue;
  GuestFiles files(queue);
  GuestVfsAssets cache(files);
  std::string path;
  testTrue(counters,
           cache.putLocal("models/tri.obj", { 'v', ' ', '0' }, &path) &&
             path == "/local/models/tri.obj",
           "app bytes live under /local");
  std::vector<unsigned char> bytes;
  testTrue(counters,
           cache.read(path, bytes) && textOf(bytes) == "v 0",
           "local bytes serve AssetManager");
  testTrue(counters,
           !cache.putLocal("../escape.obj", { 'x' }) &&
             !cache.putLocal("", { 'x' }),
           "local names cannot escape /local");
  cache.setBudget(1);
  testTrue(counters, cache.contains(path), "local entries are pinned");
  cache.putLocal("models/tri.obj", { 'v', ' ', '1' });
  testTrue(counters,
           cache.read(path, bytes) && textOf(bytes) == "v 1" &&
             cache.cachedBytes() == 3,
           "replacing a local entry keeps the byte count exact");
  cache.removeLocal("models/tri.obj");
  testTrue(counters,
           !cache.contains(path) && cache.cachedBytes() == 0,
           "removing a local entry frees it");
  return counters.failures == 0 ? 0 : 1;
}

// Mounted (1 MiB blocks) versus storage (64 KiB blocks) whole-file reads
// through the guest SDK. Prints one JSON line.
static int
benchFileThroughput()
{
  MountedTree tree(false);
  const std::string payload(32u * 1024u * 1024u, 'b');
  std::ofstream(tree.root / "forest" / "huge.bin", std::ios::binary) << payload;
  WasmFileLimits limits = mountedLimits();
  FileFixture fixture(tree.vfs, allGrants(), limits);
  std::ofstream(fixture.root / "storage" / "huge.bin", std::ios::binary)
    << payload;
  GuestServiceQueue queue;
  GuestFiles files(queue);
  double seconds[2] = { 0.0, 0.0 };
  for (int pass = 0; pass < 2; ++pass) {
    const bool mounted = pass == 0;
    const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
    const std::uint64_t task =
      files.read(mounted ? GuestFileArea::Mounted : GuestFileArea::Storage,
                 mounted ? "/packages/forest/huge.bin" : "huge.bin");
    GuestFileResult result;
    for (;;) {
      exchangeOnce(fixture, queue);
      files.pump();
      if (files.take(task, result)) {
        break;
      }
    }
    seconds[pass] =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
        .count();
    if (result.outcome != GuestFileOutcome::Success ||
        result.bytes.size() != payload.size()) {
      std::fprintf(stderr, "Throughput read failed\n");
      return 1;
    }
  }
  const double megabytes = static_cast<double>(payload.size()) / 1.0e6;
  std::printf("{\"benchmark\":\"Illumo.Wasm.Bench.FileThroughput\","
              "\"mountedMBps\":%.1f,\"storageMBps\":%.1f}\n",
              megabytes / seconds[0],
              megabytes / seconds[1]);
  return 0;
}

static int
fileServices()
{
  TestCounters counters;
  FileFixture fixture;
  GuestFileRequest request;
  request.path = "asset.txt";
  FileResult opened = fixture.send(request);
  GuestWireReader reader(opened.payload);
  const GuestResourceId asset = GuestResourceId::read(reader);
  testTrue(counters,
           opened.outcome == GuestFileOutcome::Success && asset.owner == 71 &&
             reader.u64() == 3 && reader.finished(),
           "Open scoped package asset");
  request = {};
  request.action = GuestFileAction::Read;
  request.file = asset;
  request.size = 3;
  FileResult read = fixture.send(request);
  testTrue(counters,
           read.outcome == GuestFileOutcome::Success &&
             read.payload == std::vector<std::byte>{ std::byte{ 'a' },
                                                     std::byte{ 'b' },
                                                     std::byte{ 'c' } },
           "Read copied bounded bytes");
  ++request.file.owner;
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::Denied,
           "Cross-owner file rejected");
  request.file = asset;
  request.offset = 2;
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::Denied,
           "Read range cannot exceed file");
  request = {};
  request.action = GuestFileAction::Close;
  request.file = asset;
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::Success &&
             fixture.send(request).outcome == GuestFileOutcome::Denied,
           "Closed capability immediately stale");
  for (const char* path : { "../package/asset.txt",
                            "/asset.txt",
                            "C:/asset.txt",
                            "asset.txt:private",
                            "CON.txt",
                            "name.",
                            "folder/../asset.txt",
                            ".illumo-tmp-owned",
                            ".ILLUMO-stage",
                            "CONIN$",
                            "conout$.txt",
                            "bad\nname" }) {
    request = {};
    request.area = GuestFileArea::Storage;
    request.path = path;
    testTrue(counters,
             fixture.send(request).outcome == GuestFileOutcome::Denied,
             "Escaping or reserved path rejected");
  }
  request.path = "missing.txt";
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::NotFound,
           "Missing file distinguished from denial");
  request.writing = true;
  request.area = GuestFileArea::Package;
  request.size = 6;
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::Denied,
           "Package is read-only");
  request.area = GuestFileArea::Storage;
  request.path = "save.csim";
  opened = fixture.send(request);
  GuestWireReader destination(opened.payload);
  const GuestResourceId save = GuestResourceId::read(destination);
  testTrue(counters,
           opened.outcome == GuestFileOutcome::Success &&
             save.generation != asset.generation,
           "Slot reuse changes generation");
  request = {};
  request.file = save;
  request.action = GuestFileAction::Commit;
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::Denied,
           "Incomplete write cannot commit");
  request.action = GuestFileAction::Write;
  request.data = { std::byte{ 'a' }, std::byte{ 'b' }, std::byte{ 'c' } };
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::Success,
           "First streaming write");
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::Denied,
           "Overlapping write rejected");
  request.offset = 3;
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::Success,
           "Second streaming write");
  testTrue(counters,
           !std::filesystem::exists(fixture.root / "storage" / "save.csim"),
           "Uncommitted file remains private");
  request = {};
  request.file = save;
  request.action = GuestFileAction::Commit;
  testTrue(
    counters,
    fixture.send(request).outcome == GuestFileOutcome::Success &&
      std::filesystem::file_size(fixture.root / "storage" / "save.csim") == 6,
    "Commit publishes complete file");
  request = {};
  request.area = GuestFileArea::Storage;
  request.writing = true;
  request.path = "save.csim";
  request.size = 17;
  testTrue(counters,
           fixture.send(request).outcome == GuestFileOutcome::Denied,
           "Declared file quota enforced before staging");
  request.size = 8;
  opened = fixture.send(request);
  GuestWireReader aborted(opened.payload);
  request = {};
  request.file = GuestResourceId::read(aborted);
  request.action = GuestFileAction::Close;
  testTrue(
    counters,
    fixture.send(request).outcome == GuestFileOutcome::Success &&
      std::filesystem::file_size(fixture.root / "storage" / "save.csim") == 6,
    "Abort preserves prior destination");
  GuestServices malformed;
  request = {};
  request.area = GuestFileArea::Storage;
  request.path = "invalid.csim";
  request.writing = true;
  malformed.records.push_back(fixture.record(request));
  malformed.records.push_back(
    { fixture.next++, GuestService::File, GuestServiceStatus::Request, {} });
  testTrue(counters,
           !fixture.service->submit(malformed) &&
             fixture.service->pendingRequests() == 0,
           "Batch validates before any queued mutation");
  GuestServices pending;
  pending.records.push_back(fixture.record(request));
  testTrue(counters,
           fixture.service->submit(pending),
           "Cancellation fixture submitted");
  fixture.service->cancel();
  testTrue(counters,
           fixture.service->pendingRequests() == 0 &&
             fixture.service->poll().records.empty(),
           "Cancellation revokes all deliverable requests");
  return counters.failures == 0 ? 0 : 1;
}

int
main(int argc, char** argv)
try {
  if (argc == 2 && std::string(argv[1]) == "--list") {
    std::puts("Illumo.Wasm.FileServices");
    std::puts("Illumo.Wasm.PackageFromArchive");
    std::puts("Illumo.Wasm.FileProtocolV2Decoder");
    std::puts("Illumo.Wasm.MountedFiles");
    std::puts("Illumo.Wasm.MountedDeny");
    std::puts("Illumo.Wasm.GuestFileTree");
    std::puts("Illumo.Wasm.Bench.FileThroughput");
    std::puts("Illumo.Wasm.GuestPinnedPreload");
    std::puts("Illumo.Wasm.GuestAssetFetchAndEvict");
    std::puts("Illumo.Wasm.GuestLocalEntries");
    return 0;
  }
  if (argc != 3 || std::string(argv[1]) != "--run") {
    return 2;
  }
  const std::string name = argv[2];
  if (name == "Illumo.Wasm.FileServices") {
    return fileServices();
  }
  if (name == "Illumo.Wasm.PackageFromArchive") {
    return packageFromArchive();
  }
  if (name == "Illumo.Wasm.FileProtocolV2Decoder") {
    return decoderV2();
  }
  if (name == "Illumo.Wasm.MountedFiles") {
    return mountedFiles();
  }
  if (name == "Illumo.Wasm.MountedDeny") {
    return mountedDeny();
  }
  if (name == "Illumo.Wasm.GuestFileTree") {
    return guestFileTree();
  }
  if (name == "Illumo.Wasm.Bench.FileThroughput") {
    return benchFileThroughput();
  }
  if (name == "Illumo.Wasm.GuestPinnedPreload") {
    return guestPinnedPreload();
  }
  if (name == "Illumo.Wasm.GuestAssetFetchAndEvict") {
    return guestAssetFetchAndEvict();
  }
  if (name == "Illumo.Wasm.GuestLocalEntries") {
    return guestLocalEntries();
  }
  return 2;
} catch (const std::exception& exception) {
  std::fprintf(stderr, "%s\n", exception.what());
  return 1;
}
