// Application launches (Launch capability): the LaunchApp wire records, the
// host service's grant and launcher checks, the guest's GuestLauncher, and
// the runtime's launcher and child processes (this test program relaunched
// as a sleeping child).
#include <Illumo/Platform/ChildProcess.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/AppLauncher.h>
#include <Illumo/Wasm/RuntimeAppLauncher.h>
#include <Illumo/Wasm/WasmFrameRenderer.h>
#include <Illumo/Wasm/WasmGameServices.h>
#include <IllumoGuest/Launcher.h>
#include <IllumoGuest/Protocol.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

static std::vector<std::byte>
bytesOf(const std::string& text)
{
  std::vector<std::byte> bytes;
  for (char character : text) {
    bytes.push_back(static_cast<std::byte>(character));
  }
  return bytes;
}

static GuestLaunchRequest
startRequest(const std::string& application, const std::string& name)
{
  GuestLaunchRequest request;
  request.action = GuestLaunchAction::Start;
  request.application = application;
  request.name = name;
  request.document = bytesOf("{\"format\":\"ilsc\"}");
  return request;
}

static GuestLaunchRequest
plainRequest(GuestLaunchAction action)
{
  GuestLaunchRequest request;
  request.action = action;
  return request;
}

static std::vector<std::byte>
encode(const GuestLaunchRequest& request)
{
  GuestWireWriter writer;
  request.write(writer);
  return writer.take();
}

static bool
decodes(const std::vector<std::byte>& bytes)
{
  GuestLaunchRequest decoded;
  return GuestLaunchRequest::read(bytes, decoded);
}

static int
testLaunchServiceDecoder()
{
  TestCounters counters;
  GuestLaunchRequest decoded;
  testTrue(counters,
           GuestLaunchRequest::read(
             encode(startRequest("playground", "scene.ilsc")), decoded) &&
             decoded.action == GuestLaunchAction::Start &&
             decoded.application == "playground" &&
             decoded.name == "scene.ilsc" &&
             decoded.document == bytesOf("{\"format\":\"ilsc\"}"),
           "a start request round-trips");
  testTrue(counters,
           decodes(encode(plainRequest(GuestLaunchAction::Stop))) &&
             decodes(encode(plainRequest(GuestLaunchAction::Status))),
           "stop and status requests round-trip");
  const char* badApplications[] = { "",    ".",   "..",     "Playground",
                                    "a/b", "x y", "../apps" };
  bool applicationsRefused = true;
  for (const char* application : badApplications) {
    applicationsRefused =
      applicationsRefused &&
      !decodes(encode(startRequest(application, "scene.ilsc")));
  }
  testTrue(
    counters, applicationsRefused, "invalid application ids are refused");
  const char* badNames[] = { "",    ".hidden", "a/b.ilsc", "..\\x.ilsc",
                             "c:x", "a b",     "../x" };
  bool namesRefused = true;
  for (const char* name : badNames) {
    namesRefused =
      namesRefused && !decodes(encode(startRequest("playground", name)));
  }
  testTrue(
    counters, namesRefused, "names that are not plain files are refused");
  GuestLaunchRequest empty = startRequest("playground", "scene.ilsc");
  empty.document.clear();
  GuestLaunchRequest stopWithDocument = plainRequest(GuestLaunchAction::Stop);
  stopWithDocument.document = bytesOf("x");
  GuestLaunchRequest statusWithName = plainRequest(GuestLaunchAction::Status);
  statusWithName.name = "scene.ilsc";
  GuestLaunchRequest stopWithApplication =
    plainRequest(GuestLaunchAction::Stop);
  stopWithApplication.application = "playground";
  testTrue(counters,
           !decodes(encode(empty)) && !decodes(encode(stopWithDocument)) &&
             !decodes(encode(statusWithName)) &&
             !decodes(encode(stopWithApplication)),
           "each action has exactly one encoding");
  std::vector<std::byte> trailing =
    encode(startRequest("playground", "scene.ilsc"));
  trailing.push_back(std::byte{ 0 });
  std::vector<std::byte> version =
    encode(plainRequest(GuestLaunchAction::Stop));
  version[0] = std::byte{ 2 };
  std::vector<std::byte> zero = encode(plainRequest(GuestLaunchAction::Stop));
  zero[4] = std::byte{ 0 };
  std::vector<std::byte> four = encode(plainRequest(GuestLaunchAction::Stop));
  four[4] = std::byte{ 4 };
  testTrue(counters,
           !decodes(trailing) && !decodes(version) && !decodes(zero) &&
             !decodes(four),
           "trailing bytes, other versions and unknown actions are refused");
  GuestWireWriter oversized;
  oversized.u32(GuestLaunchRequest::Version);
  oversized.u32(1);
  oversized.text("playground");
  oversized.text("scene.ilsc");
  oversized.u32(GuestLaunchRequest::MaximumDocumentBytes + 1u);
  testTrue(counters,
           !decodes(oversized.take()),
           "a document over the limit is refused before it is read");
  GuestLaunchStatus status;
  GuestWireWriter running;
  GuestLaunchStatus{ true }.write(running);
  GuestWireWriter invalid;
  invalid.u32(2);
  testTrue(counters,
           GuestLaunchStatus::read(running.data(), status) && status.running &&
             !GuestLaunchStatus::read(invalid.data(), status),
           "the status completion carries one flag");
  GuestServices services;
  services.records.push_back({ 1,
                               GuestService::LaunchApp,
                               GuestServiceStatus::Request,
                               encode(plainRequest(GuestLaunchAction::Stop)) });
  GuestWireWriter known;
  services.write(known);
  services.records[0].operation = static_cast<GuestService>(20);
  GuestWireWriter unknown;
  services.write(unknown);
  GuestServices read;
  testTrue(counters,
           GuestServices::read(known.data(), read, true) &&
             !GuestServices::read(unknown.data(), read, true),
           "LaunchApp is the newest service operation");
  testTrue(counters,
           (GuestEnvelope::KnownCapabilities &
            static_cast<std::uint32_t>(GuestCapability::Launch)) != 0 &&
             (GuestEnvelope::KnownCapabilities >> 14u) == 0,
           "Launch is the newest known capability");
  return counters.failures;
}

class FakeLauncher final : public IAppLauncher
{
public:
  bool available() const override { return supported; }
  bool launch(const std::string& application,
              const std::string& name,
              std::span<const std::byte> document,
              std::string& error) override
  {
    ++launches;
    if (application != "playground") {
      error = "unknown";
      return false;
    }
    lastApplication = application;
    lastName = name;
    lastDocument.assign(document.begin(), document.end());
    active = true;
    return true;
  }
  void stop() override
  {
    ++stops;
    active = false;
  }
  bool running() override { return active; }

  bool supported = true;
  bool active = false;
  int launches = 0;
  int stops = 0;
  std::string lastApplication;
  std::string lastName;
  std::vector<std::byte> lastDocument;
};

struct HostFixture
{
  NullRenderWindow window{ 640, 480 };
  EnvVars env;
  Camera camera{ glm::vec2(0, 0), 1, &env };
  MockBackend mock;
  Renderer renderer{ &window, &env, &camera, &mock, false };
  HostFixture()
  {
    mock.Initialize();
    renderer.ensureBuiltinStyles();
  }
};

static bool
exchange(WasmGameServices& services,
         std::uint64_t request,
         const GuestLaunchRequest& value,
         GuestServiceRecord& result)
{
  GuestServices requests;
  requests.records.push_back({ request,
                               GuestService::LaunchApp,
                               GuestServiceStatus::Request,
                               encode(value) });
  GuestWireWriter bytes;
  requests.write(bytes);
  std::vector<std::byte> completion;
  GuestServices results;
  if (!services.process(bytes.data(), completion) ||
      !GuestServices::read(completion, results, false) ||
      results.records.size() != 1) {
    return false;
  }
  result = results.records[0];
  return result.request == request &&
         result.operation == GuestService::LaunchApp;
}

static bool
reportsRunning(const GuestServiceRecord& record, bool running)
{
  GuestLaunchStatus status;
  return record.status == GuestServiceStatus::Complete &&
         GuestLaunchStatus::read(record.payload, status) &&
         status.running == running;
}

static int
testLaunchDeny()
{
  TestCounters counters;
  HostFixture fixture;
  WasmFrameRenderer frames(fixture.renderer, 81);
  const std::uint32_t launch =
    static_cast<std::uint32_t>(GuestCapability::Launch);
  FakeLauncher launcher;
  GuestServiceRecord result;

  WasmGameServices denied(frames, 0, ILLUMO_ENGINE_ASSETS);
  denied.setLauncher(&launcher);
  testTrue(counters,
           exchange(denied, 1, startRequest("playground", "a.ilsc"), result) &&
             result.status == GuestServiceStatus::Rejected &&
             launcher.launches == 0,
           "without the Launch grant nothing starts");
  denied.cancel();
  testTrue(counters,
           launcher.stops == 0,
           "a guest without the grant cannot stop a launch either");

  WasmGameServices unwired(frames, launch, ILLUMO_ENGINE_ASSETS);
  testTrue(counters,
           exchange(unwired, 1, startRequest("playground", "a.ilsc"), result) &&
             result.status == GuestServiceStatus::Rejected,
           "a host without a launcher rejects the request");

  FakeLauncher unavailable;
  unavailable.supported = false;
  WasmGameServices offline(frames, launch, ILLUMO_ENGINE_ASSETS);
  offline.setLauncher(&unavailable);
  testTrue(counters,
           exchange(offline, 1, startRequest("playground", "a.ilsc"), result) &&
             result.status == GuestServiceStatus::Rejected &&
             unavailable.launches == 0,
           "an unavailable launcher starts nothing");

  WasmGameServices malformed(frames, launch, ILLUMO_ENGINE_ASSETS);
  malformed.setLauncher(&launcher);
  GuestServices requests;
  std::vector<std::byte> payload =
    encode(startRequest("playground", "../a.ilsc"));
  requests.records.push_back(
    { 1, GuestService::LaunchApp, GuestServiceStatus::Request, payload });
  GuestWireWriter bytes;
  requests.write(bytes);
  std::vector<std::byte> completion;
  testTrue(counters,
           !malformed.process(bytes.data(), completion) &&
             launcher.launches == 0,
           "a malformed launch request fails the exchange");

  WasmGameServices granted(frames, launch, ILLUMO_ENGINE_ASSETS);
  granted.setLauncher(&launcher);
  testTrue(
    counters,
    exchange(granted, 1, startRequest("playground", "level.ilsc"), result) &&
      reportsRunning(result, true) && launcher.lastName == "level.ilsc" &&
      launcher.lastDocument == bytesOf("{\"format\":\"ilsc\"}"),
    "a granted start launches with the document and reports it running");
  testTrue(counters,
           exchange(granted, 2, startRequest("missing", "a.ilsc"), result) &&
             result.status == GuestServiceStatus::Rejected,
           "a launch the host refuses is rejected");
  testTrue(
    counters,
    exchange(granted, 3, plainRequest(GuestLaunchAction::Status), result) &&
      reportsRunning(result, true),
    "status reports the running launch");
  testTrue(
    counters,
    exchange(granted, 4, plainRequest(GuestLaunchAction::Stop), result) &&
      reportsRunning(result, false) && launcher.stops == 1,
    "stop ends the launch");
  exchange(granted, 5, startRequest("playground", "level.ilsc"), result);
  granted.cancel();
  testTrue(counters,
           !launcher.active && launcher.stops == 2,
           "a cancelled guest's launch ends with it");
  return counters.failures;
}

static int
testGuestLauncher()
{
  TestCounters counters;
  GuestServiceQueue queue;
  GuestLauncher launcher(queue);
  const std::vector<std::byte> document = bytesOf("{}");
  testTrue(counters,
           !launcher.available() &&
             !launcher.start("playground", "a.ilsc", document) &&
             !queue.hasOutgoing(),
           "without the grant nothing is queued");
  launcher.setGranted(true);
  testTrue(counters,
           !launcher.start("Playground", "a.ilsc", document) &&
             !launcher.start("playground", "../a.ilsc", document) &&
             !launcher.start("playground", "a.ilsc", {}) &&
             !queue.hasOutgoing(),
           "invalid starts are refused before they are queued");
  testTrue(counters,
           launcher.start("playground", "a.ilsc", document) &&
             launcher.pending(),
           "a valid start is queued");
  GuestServices empty;
  GuestWireWriter emptyBytes;
  empty.write(emptyBytes);
  std::vector<std::byte> outgoing;
  queue.exchange(emptyBytes.data(), outgoing);
  GuestServices sent;
  GuestServices::read(outgoing, sent, true);
  GuestLaunchRequest request;
  testTrue(counters,
           sent.records.size() == 1 &&
             sent.records[0].operation == GuestService::LaunchApp &&
             GuestLaunchRequest::read(sent.records[0].payload, request) &&
             request.application == "playground",
           "the start travels as a LaunchApp request");
  GuestServices completed;
  GuestWireWriter status;
  GuestLaunchStatus{ true }.write(status);
  completed.records.push_back({ sent.records[0].request,
                                GuestService::LaunchApp,
                                GuestServiceStatus::Complete,
                                status.take() });
  GuestWireWriter completedBytes;
  completed.write(completedBytes);
  queue.exchange(completedBytes.data(), outgoing);
  testTrue(counters,
           launcher.poll() && launcher.running() && !launcher.pending() &&
             !launcher.takeRefused(),
           "the completion reports the launch running");
  launcher.stop();
  queue.exchange(emptyBytes.data(), outgoing);
  GuestServices::read(outgoing, sent, true);
  GuestServices rejected;
  rejected.records.push_back({ sent.records[0].request,
                               GuestService::LaunchApp,
                               GuestServiceStatus::Rejected,
                               {} });
  GuestWireWriter rejectedBytes;
  rejected.write(rejectedBytes);
  queue.exchange(rejectedBytes.data(), outgoing);
  testTrue(counters,
           launcher.poll() && launcher.takeRefused() && !launcher.takeRefused(),
           "a rejection is reported once");
  return counters.failures;
}

static bool
waitFor(ChildProcess& child, bool running)
{
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    if (child.running() == running) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

static int
testChildProcess()
{
  TestCounters counters;
  ChildProcess child;
  testTrue(counters, !child.running(), "nothing runs before start");
  std::string error;
  testTrue(counters,
           child.start({ "--run", "Illumo.Wasm.ChildSleeper" }, &error) &&
             child.running(),
           "a child starts and runs");
  child.stop();
  testTrue(counters, !child.running(), "stop ends the child");
  testTrue(counters,
           child.start({ "--run", "Illumo.Wasm.ChildSleeper" }, &error) &&
             child.start({ "--run", "Illumo.Wasm.ChildSleeper" }, &error) &&
             child.running(),
           "starting again replaces the child");
  child.stop();
  testTrue(counters,
           child.start({ "--run", "no-such-test" }, &error) &&
             waitFor(child, false),
           "a child that exits is no longer running");
  return counters.failures;
}

static int
testRuntimeAppLauncher()
{
  TestCounters counters;
  const std::filesystem::path play =
    std::filesystem::temp_directory_path() /
    ("illumo-launch-test-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  {
    // The child is this test program, which refuses the runtime's options.
    RuntimeAppLauncher launcher({ "playground" }, play, { "--project", "p" });
    const std::vector<std::byte> document = bytesOf("{\"nodes\":[]}");
    std::string error;
    testTrue(counters,
             launcher.available() && !launcher.running(),
             "the runtime launcher is available and idle");
    testTrue(counters,
             !launcher.launch("game", "a.ilsc", document, error) &&
               !error.empty() && !std::filesystem::exists(play),
             "an application it was not given is refused");
    testTrue(counters,
             !launcher.launch("playground", "../a.ilsc", document, error) &&
               !launcher.launch("playground", "a.ilsc", {}, error),
             "invalid names and empty documents are refused");
    testTrue(counters,
             launcher.launch("playground", "level.ilsc", document, error) &&
               std::filesystem::file_size(play / "level.ilsc") ==
                 document.size(),
             "a launch saves the document in the play directory");
    launcher.stop();
    testTrue(counters, !launcher.running(), "stop ends the launch");
  }
  testTrue(counters,
           !std::filesystem::exists(play),
           "the play directory goes with the launcher");
  return counters.failures;
}

int
main(int argc, char** argv)
{
  if (argc == 2 && std::string(argv[1]) == "--list") {
    std::puts("Illumo.Wasm.LaunchServiceDecoder\nIllumo.Wasm.LaunchDeny\n"
              "Illumo.Wasm.GuestLauncher\nIllumo.Wasm.ChildProcess\n"
              "Illumo.Wasm.RuntimeAppLauncher");
    return 0;
  }
  if (argc != 3 || std::string(argv[1]) != "--run") {
    return 2;
  }
  const std::string name(argv[2]);
  if (name == "Illumo.Wasm.ChildSleeper") {
    // The child testChildProcess starts; it is always ended from outside.
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
  }
  int failures = -1;
  if (name == "Illumo.Wasm.LaunchServiceDecoder") {
    failures = testLaunchServiceDecoder();
  } else if (name == "Illumo.Wasm.LaunchDeny") {
    failures = testLaunchDeny();
  } else if (name == "Illumo.Wasm.GuestLauncher") {
    failures = testGuestLauncher();
  } else if (name == "Illumo.Wasm.ChildProcess") {
    failures = testChildProcess();
  } else if (name == "Illumo.Wasm.RuntimeAppLauncher") {
    failures = testRuntimeAppLauncher();
  }
  if (failures < 0) {
    return 2;
  }
  std::printf("%s: %d failure(s)\n", name.c_str(), failures);
  return failures == 0 ? 0 : 1;
}
