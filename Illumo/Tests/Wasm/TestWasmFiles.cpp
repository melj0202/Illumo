#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/WasmFileServices.h>
#include <IllumoGuest/FileProtocol.h>
#include <IllumoGuest/Protocol.h>
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
  FileFixture()
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
    WasmFileLimits limits;
    limits.fileBytes = 16;
    limits.stagedBytes = 64;
    limits.storageBytes = 32;
    limits.openFiles = 2;
    service = std::make_unique<WasmFileServices>(
      71,
      static_cast<std::uint32_t>(GuestCapability::Assets) |
        static_cast<std::uint32_t>(GuestCapability::Storage),
      root / "package",
      root / "storage",
      limits);
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

int
main(int argc, char** argv)
try {
  if (argc == 2 && std::string(argv[1]) == "--list") {
    std::puts("Illumo.Wasm.FileServices");
    return 0;
  }
  if (argc != 3 || std::string(argv[1]) != "--run" ||
      std::string(argv[2]) != "Illumo.Wasm.FileServices") {
    return 2;
  }
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
} catch (const std::exception& exception) {
  std::fprintf(stderr, "%s\n", exception.what());
  return 1;
}
