#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/WasmProgram.h>
#include <IllumoGuest/Dialog.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

// The Playground runs only as Playground.wasm. This native side is the
// generic host: the package must play its default scene's behaviours, and a
// launched scene (as IllEd's Play sends one) with its own. The test reads
// what the guest prints for play_status and play_node.

static std::vector<std::byte>
readBytes(const std::filesystem::path& path)
{
  std::ifstream stream(path, std::ios::binary);
  const std::vector<char> characters{ std::istreambuf_iterator<char>(stream),
                                      {} };
  std::vector<std::byte> bytes(characters.size());
  std::memcpy(bytes.data(), characters.data(), characters.size());
  return bytes;
}

// The newest console line starting with `prefix`, or empty.
static std::string
lastLine(const CommandLine& console, const std::string& prefix)
{
  std::string found;
  for (const CommandLine::historyBuffer& entry : console.getHistory()) {
    if (entry.content.rfind(prefix, 0) == 0) {
      found = entry.content;
    }
  }
  return found;
}

static bool
historyContains(const CommandLine& console, const std::string& text)
{
  for (const CommandLine::historyBuffer& entry : console.getHistory()) {
    if (entry.content.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

// "play_node <id> x y z" as a position; false when the line is missing.
static bool
nodePosition(const std::string& line, float* x, float* y, float* z)
{
  std::istringstream fields(line);
  std::string command;
  std::string id;
  return static_cast<bool>(fields >> command >> id >> *x >> *y >> *z);
}

// One playground run through the generic host.
struct PlaygroundRun
{
  std::filesystem::path root;
  NullRenderWindow window{ 1280, 720 };
  EnvVars env;
  Camera camera{ glm::vec2(0, 0), 1, &env };
  MockBackend mock;
  Renderer renderer{ &window, &env, &camera, &mock, false };
  CommandRegistry commands;
  CommandLine console{ &env, &commands, &window, &renderer, "Test" };
  InputManager input{ nullptr };
  IllumoContext context;
  std::unique_ptr<WasmProgram> program;

  explicit PlaygroundRun(const std::string& name)
  {
    root = std::filesystem::temp_directory_path() /
           ("illumo-playground-" + name + "-" +
            std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::filesystem::path package = root / "package";
    const std::filesystem::path source = ILLUMO_PLAYGROUND_SOURCE;
    std::filesystem::create_directories(package / "Scenes");
    std::filesystem::create_directories(root / "storage");
    std::filesystem::copy_file(source / "envvars.json",
                               package / "envvars.json");
    std::filesystem::copy_file(source / "behaviours.json",
                               package / "behaviours.json");
    std::filesystem::copy_file(source / "Scenes" / "demo.ilsc",
                               package / "Scenes" / "demo.ilsc");
    env.setVar("WinX", 1280);
    env.setVar("WinY", 720);
    env.setVar("fullscreen", false);
    // Warnings (the guest's included) reach the console the test reads.
    env.setVar("logLevel", 2);
    mock.Initialize();
    renderer.ensureBuiltinStyles();
    Logger::setContext(&env, &console);
    context.renderer = &renderer;
    context.window = &window;
    context.inputManager = &input;
    context.envVars = &env;
    context.commandRegistry = &commands;
    context.commandLine = &console;
  }
  ~PlaygroundRun()
  {
    if (program) {
      program->stop();
    }
    Logger::setContext(nullptr, nullptr);
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }

  bool start(const std::filesystem::path& launch)
  {
    std::shared_ptr<VirtualFileSystem> tree =
      std::make_shared<VirtualFileSystem>();
    std::string error;
    VfsMount app;
    app.point = "/app";
    app.layers.push_back(
      { DirectoryVfsBackend::open(root / "package", false, error),
        "playground" });
    tree->mount(app, error);
    WasmFileRoots files;
    files.storage = root / "storage";
    files.packages = tree;
    std::vector<std::byte> startup;
    if (!launch.empty()) {
      files.launch = launch;
      GuestLaunch record;
      record.label = launch.filename().string();
      record.size = std::filesystem::file_size(launch);
      GuestWireWriter writer;
      record.write(writer);
      startup = writer.take();
    }
    WasmLimits limits;
    limits.memoryBytes = 512ull * 1024ull * 1024ull;
    limits.meterFuel = false;
    limits.deadlineMilliseconds = 10000;
    program = std::make_unique<WasmProgram>(readBytes(ILLUMO_PLAYGROUND_GUEST),
                                            startup,
                                            limits,
                                            std::vector<std::byte>{},
                                            std::vector<std::byte>{},
                                            files);
    if (!program->start(context)) {
      std::printf("%s\n", program->error().c_str());
      return false;
    }
    return true;
  }

  void frame()
  {
    program->update(1.0 / 60.0);
    DrawList scene(&window, &camera);
    program->dispatch(scene);
    renderer.BeginFrame();
    renderer.RenderScene(&scene, &camera);
    renderer.EndFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  // Runs a console command once it exists and waits for its printed line.
  std::string ask(const std::string& name,
                  const std::vector<std::string>& args,
                  const std::string& prefix)
  {
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
    const std::string before = lastLine(console, prefix);
    bool queued = false;
    std::size_t lines = console.getHistory().size();
    while (program->error().empty() &&
           std::chrono::steady_clock::now() < deadline) {
      if (!queued && commands.HasCommand(name)) {
        commands.QueueCommand(name, args);
        commands.ExecuteQueue();
        queued = true;
        lines = console.getHistory().size();
      }
      frame();
      if (queued && console.getHistory().size() > lines) {
        const std::string line = lastLine(console, prefix);
        if (!line.empty()) {
          return line;
        }
      }
    }
    (void)before;
    return {};
  }

  // Frames until play_status reports the scene playing; returns that line.
  // (The console merges a repeated identical line, so it is asked once.)
  std::string waitPlaying()
  {
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < deadline) {
      const std::string status = ask("play_status", {}, "play_status");
      if (status.find("playing") != std::string::npos) {
        return status;
      }
      if (!program->error().empty()) {
        return {};
      }
    }
    return {};
  }

  void dumpOnFailure(const TestCounters& counters)
  {
    if (counters.failures == 0) {
      return;
    }
    if (program && !program->error().empty()) {
      std::printf("guest error: %s\n", program->error().c_str());
    }
    for (const CommandLine::historyBuffer& entry : console.getHistory()) {
      std::printf("console: %s\n", entry.content.c_str());
    }
  }
};

static bool
defaultScene()
{
  TestCounters counters;
  PlaygroundRun run("default");
  testTrue(
    counters, run.start({}), "The generic host starts the Playground package");
  if (!run.program) {
    return false;
  }
  const std::string status = run.waitPlaying();
  testTrue(
    counters, !status.empty(), "The packaged default scene loads and plays");
  testTrue(counters,
           status.find("7 nodes 4 behaviours") != std::string::npos,
           "Every sample behaviour attaches to its node");
  float x0 = 0.0f, y0 = 0.0f, z0 = 0.0f;
  float x1 = 0.0f, y1 = 0.0f, z1 = 0.0f;
  const bool first = nodePosition(
    run.ask("play_node", { "moon" }, "play_node moon"), &x0, &y0, &z0);
  for (int index = 0; index < 20; ++index) {
    run.frame();
  }
  const bool second = nodePosition(
    run.ask("play_node", { "moon" }, "play_node moon"), &x1, &y1, &z1);
  const float radius = std::sqrt(x1 * x1 + (z1 + 2.0f) * (z1 + 2.0f));
  testTrue(counters,
           first && second && (x0 != x1 || z0 != z1) &&
             std::fabs(radius - 1.5f) < 0.01f,
           "The orbiting moon moves around the beacon at its radius");
  testTrue(counters,
           !historyContains(run.console, "registered but") &&
             !historyContains(run.console, "no code is registered") &&
             !historyContains(run.console, "Frame dropped"),
           "Code and behaviours.json agree and every frame completes");
  run.dumpOnFailure(counters);
  return counters.failures == 0;
}

static bool
launchedScene()
{
  TestCounters counters;
  PlaygroundRun run("launched");
  // As IllEd's Play writes it: an unknown namespaced component is kept and
  // ignored, a bad value falls back to its default with a warning.
  const std::filesystem::path scene = run.root / "edited.ilsc";
  std::ofstream(scene) << R"({
  "format": "ilsc",
  "format_version": [2, 0],
  "settings": { "world_mode": "3d" },
  "nodes": [
    { "id": "box", "transform": { "position": [1, 0, 0] },
      "components": [
        { "type": "primitive", "shape": "cube" },
        { "type": "playground.bob", "height": 1, "period": 1 },
        { "type": "studio.unknown", "value": 3 } ] },
    { "id": "odd",
      "components": [ { "type": "playground.spinner", "speed": "fast" } ] }
  ],
  "extensions": { "illumo.play": { "app": "playground", "root": "/local" } }
})";
  testTrue(
    counters, run.start(scene), "The Playground starts with a launched scene");
  if (!run.program) {
    return false;
  }
  const std::string status = run.waitPlaying();
  testTrue(counters, !status.empty(), "The launched scene plays");
  testTrue(counters,
           status.find("2 nodes 2 behaviours") != std::string::npos,
           "Known behaviours attach and the unknown component is ignored");
  float x = 0.0f, y = 0.0f, z = 0.0f;
  bool moved = false;
  for (int attempt = 0; attempt < 10 && !moved; ++attempt) {
    for (int index = 0; index < 5; ++index) {
      run.frame();
    }
    moved = nodePosition(
              run.ask("play_node", { "box" }, "play_node box"), &x, &y, &z) &&
            std::fabs(y) > 0.05f && x == 1.0f;
  }
  testTrue(
    counters, moved, "The launched scene's bob behaviour moves its node");
  run.dumpOnFailure(counters);
  return counters.failures == 0;
}

int
main(int argc, char** argv)
{
  if (argc == 2 && std::string(argv[1]) == "--list") {
    std::puts("Playground.Wasm.DefaultScene");
    std::puts("Playground.Wasm.LaunchedScene");
    return 0;
  }
  if (argc != 3 || std::string(argv[1]) != "--run") {
    return 2;
  }
  if (std::string(argv[2]) == "Playground.Wasm.DefaultScene") {
    return defaultScene() ? 0 : 1;
  }
  if (std::string(argv[2]) == "Playground.Wasm.LaunchedScene") {
    return launchedScene() ? 0 : 1;
  }
  return 2;
}
