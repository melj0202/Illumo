// Project folders through the generic host (D-E42): the Project service's
// wire records, the host service's grant and host checks, the guest's
// GuestProjects, and the runtime's project host (recent list in the
// settings, folder dialogs, restart arguments).
#include <Illumo/Platform/ProcessRelaunch.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/ProjectHost.h>
#include <Illumo/Wasm/RuntimeProjectHost.h>
#include <Illumo/Wasm/WasmFrameRenderer.h>
#include <Illumo/Wasm/WasmGameServices.h>
#include <IllumoGuest/Projects.h>
#include <IllumoGuest/Protocol.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

static GuestProjectRequest
request(GuestProjectAction action, std::uint32_t id = 0)
{
  GuestProjectRequest value;
  value.action = action;
  value.id = id;
  return value;
}

static std::vector<std::byte>
encode(const GuestProjectRequest& value)
{
  GuestWireWriter writer;
  value.write(writer);
  return writer.take();
}

static bool
decodes(const GuestProjectRequest& value)
{
  GuestProjectRequest decoded;
  return GuestProjectRequest::read(encode(value), decoded);
}

static int
testProjectServiceDecoder()
{
  TestCounters counters;
  GuestProjectRequest decoded;
  testTrue(counters,
           GuestProjectRequest::read(
             encode(request(GuestProjectAction::Open, 3)), decoded) &&
             decoded.action == GuestProjectAction::Open && decoded.id == 3,
           "an open request round-trips");
  testTrue(counters,
           decodes(request(GuestProjectAction::List)) &&
             decodes(request(GuestProjectAction::Choose)) &&
             decodes(request(GuestProjectAction::Create)) &&
             decodes(request(GuestProjectAction::Clear)),
           "the other actions decode without an id");
  testTrue(counters,
           !decodes(request(GuestProjectAction::Open, 0)) &&
             !decodes(request(GuestProjectAction::List, 1)) &&
             !decodes(request(static_cast<GuestProjectAction>(6))) &&
             !decodes(request(static_cast<GuestProjectAction>(0))),
           "open without an id, an id on another action and unknown actions "
           "are refused");
  std::vector<std::byte> bytes = encode(request(GuestProjectAction::List));
  std::vector<std::byte> wrongVersion = bytes;
  wrongVersion[0] = std::byte{ 2 };
  std::vector<std::byte> trailing = bytes;
  trailing.push_back(std::byte{ 0 });
  std::vector<std::byte> truncated = bytes;
  truncated.pop_back();
  testTrue(counters,
           !GuestProjectRequest::read(wrongVersion, decoded) &&
             !GuestProjectRequest::read(trailing, decoded) &&
             !GuestProjectRequest::read(truncated, decoded),
           "versions, trailing and missing bytes are refused");

  GuestProjectResult result;
  result.entries.push_back({ 1, "Forest" });
  result.entries.push_back({ 2, "Town square" });
  GuestWireWriter writer;
  result.write(writer);
  GuestProjectResult read;
  testTrue(counters,
           GuestProjectResult::read(writer.data(), read) && !read.cancelled &&
             read.entries.size() == 2 && read.entries[1].label == "Town square",
           "a listing round-trips");
  GuestProjectResult cancelled;
  cancelled.cancelled = true;
  GuestWireWriter cancelledBytes;
  cancelled.write(cancelledBytes);
  testTrue(counters,
           GuestProjectResult::read(cancelledBytes.data(), read) &&
             read.cancelled && read.entries.empty(),
           "a cancelled dialog round-trips");
  GuestProjectResult duplicate;
  duplicate.entries.push_back({ 1, "a" });
  duplicate.entries.push_back({ 1, "b" });
  GuestProjectResult zeroId;
  zeroId.entries.push_back({ 0, "a" });
  GuestProjectResult badLabel;
  badLabel.entries.push_back({ 1, "bad\nlabel" });
  GuestProjectResult longLabel;
  longLabel.entries.push_back(
    { 1, std::string(GuestProjectEntry::MaximumLabelBytes + 1, 'a') });
  GuestProjectResult tooMany;
  for (std::uint32_t index = 0; index <= GuestProjectResult::MaximumEntries;
       ++index) {
    tooMany.entries.push_back({ index + 1, "p" });
  }
  bool refused = true;
  for (const GuestProjectResult* bad :
       { &duplicate, &zeroId, &badLabel, &longLabel, &tooMany }) {
    GuestWireWriter badBytes;
    bad->write(badBytes);
    refused = refused && !GuestProjectResult::read(badBytes.data(), read);
  }
  testTrue(counters,
           refused,
           "duplicate or zero ids, control characters, long labels and too "
           "many entries are refused");
  testTrue(counters,
           (GuestEnvelope::KnownCapabilities &
            static_cast<std::uint32_t>(GuestCapability::Project)) != 0,
           "Project is a known capability");
  return counters.failures;
}

class FakeProjectHost final : public IProjectHost
{
public:
  bool available() const override { return supported; }
  std::vector<ProjectHostEntry> list() override
  {
    ++lists;
    return entries;
  }
  bool choose(ProjectHostEntry* chosen,
              bool* cancelled,
              std::string& error) override
  {
    ++chooses;
    *cancelled = cancelNext;
    if (cancelNext) {
      cancelNext = false;
      return false;
    }
    if (failNext) {
      failNext = false;
      error = "no folder";
      return false;
    }
    *chosen = { 1, "Chosen" };
    return true;
  }
  bool create(ProjectHostEntry* created,
              bool* cancelled,
              std::string& error) override
  {
    ++creates;
    (void)error;
    *cancelled = false;
    *created = { 1, "Created" };
    return true;
  }
  bool open(std::uint32_t id, std::string& error) override
  {
    ++opens;
    if (id > entries.size()) {
      error = "no such project";
      return false;
    }
    opened = id;
    return true;
  }
  void clear() override
  {
    ++clears;
    entries.clear();
  }

  bool supported = true;
  bool cancelNext = false;
  bool failNext = false;
  std::vector<ProjectHostEntry> entries{ { 1, "Forest" }, { 2, "Town" } };
  int lists = 0;
  int chooses = 0;
  int creates = 0;
  int opens = 0;
  int clears = 0;
  std::uint32_t opened = 0;
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
         std::uint64_t id,
         const GuestProjectRequest& value,
         GuestServiceRecord& result)
{
  GuestServices requests;
  requests.records.push_back(
    { id, GuestService::Project, GuestServiceStatus::Request, encode(value) });
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
  return result.request == id && result.operation == GuestService::Project;
}

static bool
completes(const GuestServiceRecord& record, GuestProjectResult* result)
{
  return record.status == GuestServiceStatus::Complete &&
         GuestProjectResult::read(record.payload, *result);
}

static int
testProjectDeny()
{
  TestCounters counters;
  HostFixture fixture;
  WasmFrameRenderer frames(fixture.renderer, 82);
  const std::uint32_t project =
    static_cast<std::uint32_t>(GuestCapability::Project);
  FakeProjectHost host;
  GuestServiceRecord result;
  GuestProjectResult payload;

  WasmGameServices denied(frames, 0, ILLUMO_ENGINE_ASSETS);
  denied.setProjectHost(&host);
  testTrue(counters,
           exchange(denied, 1, request(GuestProjectAction::List), result) &&
             result.status == GuestServiceStatus::Rejected && host.lists == 0,
           "without the Project grant nothing is listed");

  WasmGameServices unwired(frames, project, ILLUMO_ENGINE_ASSETS);
  testTrue(counters,
           exchange(unwired, 1, request(GuestProjectAction::List), result) &&
             result.status == GuestServiceStatus::Rejected,
           "a host without projects rejects the request");

  FakeProjectHost unavailable;
  unavailable.supported = false;
  WasmGameServices offline(frames, project, ILLUMO_ENGINE_ASSETS);
  offline.setProjectHost(&unavailable);
  testTrue(counters,
           exchange(offline, 1, request(GuestProjectAction::Choose), result) &&
             result.status == GuestServiceStatus::Rejected &&
             unavailable.chooses == 0,
           "an unavailable host opens no dialog");

  WasmGameServices malformed(frames, project, ILLUMO_ENGINE_ASSETS);
  malformed.setProjectHost(&host);
  GuestServices requests;
  requests.records.push_back({ 1,
                               GuestService::Project,
                               GuestServiceStatus::Request,
                               encode(request(GuestProjectAction::Open, 0)) });
  GuestWireWriter bytes;
  requests.write(bytes);
  std::vector<std::byte> completion;
  testTrue(counters,
           !malformed.process(bytes.data(), completion) && host.opens == 0,
           "a malformed project request fails the exchange");

  WasmGameServices granted(frames, project, ILLUMO_ENGINE_ASSETS);
  granted.setProjectHost(&host);
  testTrue(counters,
           exchange(granted, 1, request(GuestProjectAction::List), result) &&
             completes(result, &payload) && payload.entries.size() == 2 &&
             payload.entries[0].id == 1 && payload.entries[0].label == "Forest",
           "a granted listing returns the host's entries");
  testTrue(counters,
           exchange(granted, 2, request(GuestProjectAction::Choose), result) &&
             completes(result, &payload) && !payload.cancelled &&
             payload.entries.size() == 1 &&
             payload.entries[0].label == "Chosen",
           "a chosen folder comes back as one entry");
  host.cancelNext = true;
  testTrue(counters,
           exchange(granted, 3, request(GuestProjectAction::Choose), result) &&
             completes(result, &payload) && payload.cancelled &&
             payload.entries.empty(),
           "a cancelled dialog completes as cancelled");
  host.failNext = true;
  testTrue(counters,
           exchange(granted, 4, request(GuestProjectAction::Choose), result) &&
             result.status == GuestServiceStatus::Rejected,
           "a dialog the host could not use is rejected");
  testTrue(counters,
           exchange(granted, 5, request(GuestProjectAction::Create), result) &&
             completes(result, &payload) && payload.entries.size() == 1 &&
             payload.entries[0].label == "Created" && host.creates == 1,
           "a created project comes back as one entry");
  testTrue(counters,
           exchange(granted, 6, request(GuestProjectAction::Open, 2), result) &&
             completes(result, &payload) && host.opened == 2,
           "open hands the host the listed id");
  testTrue(counters,
           exchange(granted, 7, request(GuestProjectAction::Open, 9), result) &&
             result.status == GuestServiceStatus::Rejected,
           "an id the host does not know is rejected");
  testTrue(counters,
           exchange(granted, 8, request(GuestProjectAction::Clear), result) &&
             completes(result, &payload) && host.clears == 1 &&
             host.entries.empty(),
           "clear empties the host's list");
  return counters.failures;
}

// Runs one guest exchange: the queued requests go out as bytes, and the
// given completions (already matched to request ids by the caller) come in.
static GuestServices
roundTrip(GuestServiceQueue& queue, const GuestServices& completions)
{
  GuestWireWriter incoming;
  completions.write(incoming);
  std::vector<std::byte> outgoing;
  queue.exchange(incoming.data(), outgoing);
  GuestServices sent;
  GuestServices::read(outgoing, sent, true);
  return sent;
}

static int
testGuestProjects()
{
  TestCounters counters;
  GuestServiceQueue queue;
  GuestProjects projects(queue);
  bool listed = false;
  testTrue(counters,
           !projects.list([&](bool, GuestProjects::EntryList) { listed = true; }) &&
             !queue.hasOutgoing(),
           "without the grant a listing is refused at once");
  projects.setGranted(true);
  std::vector<GuestProjectEntry> entries;
  bool ok = false;
  testTrue(counters,
           projects.list([&](bool success, GuestProjects::EntryList list) {
             ok = success;
             entries = std::move(list);
             listed = true;
           }) && projects.pending(),
           "a granted listing is queued");
  GuestServices sent = roundTrip(queue, GuestServices{});
  GuestProjectRequest request;
  testTrue(counters,
           sent.records.size() == 1 &&
             sent.records[0].operation == GuestService::Project &&
             GuestProjectRequest::read(sent.records[0].payload, request) &&
             request.action == GuestProjectAction::List,
           "the request travels as a Project record");
  GuestProjectResult result;
  result.entries.push_back({ 1, "Forest" });
  GuestWireWriter payload;
  result.write(payload);
  GuestServices completed;
  completed.records.push_back({ sent.records[0].request,
                                GuestService::Project,
                                GuestServiceStatus::Complete,
                                payload.take() });
  roundTrip(queue, completed);
  projects.pump();
  testTrue(counters,
           listed && ok && entries.size() == 1 && entries[0].label == "Forest" &&
             !projects.pending(),
           "pump runs the listing callback with the entries");

  bool opened = false;
  bool openOk = true;
  projects.open(1, [&](bool success) {
    opened = true;
    openOk = success;
  });
  sent = roundTrip(queue, GuestServices{});
  GuestServices rejected;
  rejected.records.push_back({ sent.records[0].request,
                               GuestService::Project,
                               GuestServiceStatus::Rejected,
                               {} });
  roundTrip(queue, rejected);
  projects.pump();
  testTrue(counters,
           opened && !openOk,
           "a rejected open reports failure to its callback");
  testTrue(counters, !projects.open(0, {}), "open needs an id");

  bool picked = false;
  bool pickCancelled = false;
  projects.choose([&](bool success, bool cancelled, GuestProjectEntry) {
    picked = success;
    pickCancelled = cancelled;
  });
  sent = roundTrip(queue, GuestServices{});
  GuestProjectResult cancelledResult;
  cancelledResult.cancelled = true;
  GuestWireWriter cancelledPayload;
  cancelledResult.write(cancelledPayload);
  GuestServices cancelledIncoming;
  cancelledIncoming.records.push_back({ sent.records[0].request,
                                        GuestService::Project,
                                        GuestServiceStatus::Complete,
                                        cancelledPayload.take() });
  roundTrip(queue, cancelledIncoming);
  projects.pump();
  testTrue(counters,
           picked && pickCancelled,
           "a cancelled dialog reports success with cancelled set");
  return counters.failures;
}
static int
testRuntimeProjectHost()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-projects-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "alpha");
  std::filesystem::create_directories(root / "beta");
  std::filesystem::create_directories(root / "gone");
  const std::filesystem::path settingsPath = root / "envvars.json";
  std::string nextPick;
  {
    EnvVars settings(settingsPath);
    RuntimeProjectHost host(&settings,
                            "illed",
                            { "--app", "illed", "--storage", "s" },
                            [&](const std::string&) { return nextPick; });
    testTrue(counters, host.available(), "the host is available");
    testTrue(counters, host.list().empty(), "the list starts empty");
    ProjectHostEntry entry;
    bool cancelled = false;
    std::string error;
    testTrue(counters,
             !host.choose(&entry, &cancelled, error) && cancelled,
             "an empty pick is a cancellation");
    nextPick = (root / "alpha").string();
    testTrue(counters,
             host.choose(&entry, &cancelled, error) && !cancelled &&
               entry.id == 1 && entry.label == "alpha",
             "a chosen folder is listed first");
    nextPick = (root / "gone").string();
    testTrue(counters,
             host.choose(&entry, &cancelled, error) && entry.label == "gone",
             "a second folder is listed first");
    nextPick = (root / "beta").string();
    testTrue(counters,
             host.create(&entry, &cancelled, error) && entry.label == "beta" &&
               std::filesystem::exists(root / "beta" / "illumo.json") &&
               std::filesystem::is_directory(root / "beta" / "scenes") &&
               std::filesystem::is_directory(root / "beta" / "assets"),
             "a created project is scaffolded");
    std::ifstream manifest(root / "beta" / "illumo.json");
    const std::string text((std::istreambuf_iterator<char>(manifest)), {});
    testTrue(counters,
             text.find("\"id\": \"beta\"") != std::string::npos &&
               text.find("\"kind\": \"content\"") != std::string::npos,
             "the manifest takes the folder's name as its id");
    std::filesystem::remove_all(root / "gone");
    const std::vector<ProjectHostEntry> listed = host.list();
    testTrue(counters,
             listed.size() == 2 && listed[0].label == "beta" &&
               listed[1].label == "alpha",
             "a listing drops folders that are gone and keeps the order");
    testTrue(counters,
             !host.open(3, error) && !host.open(0, error),
             "an id outside the listing is refused");
    SetRelaunchArguments({});
    testTrue(counters, host.open(2, error), "a listed project opens");
    const std::vector<std::string>& arguments = RelaunchArguments();
    testTrue(counters,
             arguments.size() == 6 && arguments[0] == "--app" &&
               arguments[3] == "s" && arguments[4] == "--project" &&
               arguments[5] == (root / "alpha").string(),
             "the restart keeps the base arguments and adds the project");
    SetRelaunchArguments({});
    testTrue(counters,
             host.list()[0].label == "alpha",
             "an opened project moves to the front");
    nextPick = (root / "alpha").string();
    host.choose(&entry, &cancelled, error);
    testTrue(counters,
             host.list().size() == 2,
             "choosing a listed folder again does not duplicate it");
    testTrue(counters,
             settings.getVar(RuntimeProjectHost::settingsKey("illed"))
                 .value.find("alpha") != std::string::npos,
             "the list lives in the settings");
    host.clear();
    testTrue(counters, host.list().empty(), "clear empties the list");
  }
  {
    EnvVars settings(settingsPath);
    settings.setVar(RuntimeProjectHost::settingsKey("illed"),
                    (root / "beta").string() + "\n" +
                      (root / "alpha").string());
    RuntimeProjectHost host(&settings, "illed", { "--app", "illed" });
    testTrue(counters,
             host.list().size() == 2 && host.list()[0].label == "beta",
             "a stored list is read back newest first");
    RuntimeProjectHost none(nullptr, "illed", {});
    testTrue(counters, !none.available(), "no settings, no projects");
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return counters.failures;
}

int
main(int argc, char** argv)
{
  if (argc == 2 && std::string(argv[1]) == "--list") {
    std::puts("Illumo.Wasm.ProjectServiceDecoder\nIllumo.Wasm.ProjectDeny\n"
              "Illumo.Wasm.GuestProjects\nIllumo.Wasm.RuntimeProjectHost");
    return 0;
  }
  if (argc != 3 || std::string(argv[1]) != "--run") {
    return 2;
  }
  const std::string name(argv[2]);
  int failures = -1;
  if (name == "Illumo.Wasm.ProjectServiceDecoder") {
    failures = testProjectServiceDecoder();
  } else if (name == "Illumo.Wasm.ProjectDeny") {
    failures = testProjectDeny();
  } else if (name == "Illumo.Wasm.GuestProjects") {
    failures = testGuestProjects();
  } else if (name == "Illumo.Wasm.RuntimeProjectHost") {
    failures = testRuntimeProjectHost();
  }
  if (failures < 0) {
    return 2;
  }
  std::printf("%s: %d failure(s)\n", name.c_str(), failures);
  return failures == 0 ? 0 : 1;
}
