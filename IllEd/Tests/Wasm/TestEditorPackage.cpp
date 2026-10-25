#include "EditorAutosave.h"
#include "EditorDocument.h"
#include "IllEdPlatform.h"
#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Content/ScenePlay.h>
#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestAccess.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/ProjectHost.h>
#include <Illumo/Wasm/WasmProgram.h>
#include <IllumoGuest/Dialog.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <thread>

// The complete IllEd editor runs only as IllEd.wasm. This native side is the
// generic host, a scripted keyboard and the native IllEdCore codec as the
// oracle that writes the launch scene and reads the guest's in-place save.

// The UI atlas is the package's only large RGBA texture; fonts are smaller.
class AtlasObservingBackend final : public MockBackend
{
public:
  bool atlasDrawn = false;
  // Host textures shaped like AssetManager's 2x2 RGBA load placeholder, and
  // like the 4x4 project texture the project test places.
  size_t placeholderCreates = 0;
  size_t smallTextureCreates = 0;
  TextureHandle CreateTexture(const unsigned char* data,
                              int width,
                              int height,
                              int channels,
                              const TextureOptions& options) override
  {
    const TextureHandle handle =
      MockBackend::CreateTexture(data, width, height, channels, options);
    if (channels == 4 && width == height) {
      placeholderCreates += width == 2 ? 1 : 0;
      smallTextureCreates += width == 4 ? 1 : 0;
    }
    if (channels == 4 && options.filter == TextureFilter::Nearest &&
        width >= 256 && width == height) {
      m_atlas = handle;
    }
    return handle;
  }
  // Direct draws, and the draws of host visuals inside executed lists
  // (frame schema v7).
  void PushToCommandQueue(RenderCommand command) override
  {
    if (command.commandType == CommandType::ExecuteList &&
        command.executeList.list != nullptr) {
      for (std::size_t index = 0; index < command.executeList.list->size();
           ++index) {
        observe(command.executeList.list->at(index));
      }
    } else {
      observe(command);
    }
    MockBackend::PushToCommandQueue(command);
  }

private:
  void observe(const RenderCommand& command)
  {
    if (command.commandType == CommandType::SetTexture &&
        command.bindTexture.slot == 0) {
      m_bound = command.bindTexture.handle;
    } else if (command.commandType == CommandType::DrawIndexed &&
               m_atlas.isValid() && m_bound.slot == m_atlas.slot &&
               m_bound.generation == m_atlas.generation) {
      atlasDrawn = true;
    }
  }

  TextureHandle m_atlas{};
  TextureHandle m_bound{};
};

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

static WasmLimits
editorLimits()
{
  WasmLimits limits;
  limits.memoryBytes = 512ull * 1024ull * 1024ull;
  limits.meterFuel = false; // As shipped: illumo.json requests epoch metering.
  limits.deadlineMilliseconds = 10000;
  return limits;
}

static bool
pumpUntil(WasmProgram& editor, const std::function<bool()>& reached)
{
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (editor.error().empty() &&
         std::chrono::steady_clock::now() < deadline) {
    editor.update(1.0 / 60.0);
    if (reached()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::printf("pump stopped: %s\n", editor.error().c_str());
  return false;
}

static void
renderFrame(WasmProgram& editor,
            Renderer& renderer,
            IRenderWindow& window,
            Camera& camera)
{
  DrawList scene(&window, &camera);
  editor.dispatch(scene);
  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();
}

static bool
editorPackage()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-illed-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::filesystem::path scene = root / "documents" / "scene.ilsc";
  WasmFileRoots files{ root / "package", root / "storage", scene, true };
  std::filesystem::create_directories(files.package / "Assets" / "IllEd");
  std::filesystem::create_directories(files.storage);
  std::filesystem::create_directories(scene.parent_path());
  std::filesystem::copy_file(ILLUMO_ILLED_DEFAULTS,
                             files.package / "envvars.json");
  std::filesystem::copy_file(ILLUMO_ILLED_ATLAS,
                             files.package / "Assets" / "IllEd" /
                               "editor-ui-atlas.jpg");

  // Native oracle: a two-node scene handed to the package with --open.
  {
    EditorDocument document;
    document.createPrimitive(
      false, ScenePrimitiveShape::Cube, {}, Transform3D{});
    document.createPrimitive(
      false, ScenePrimitiveShape::WireSphere, {}, Transform3D{});
    std::string error;
    testTrue(
      counters,
      IllEdNativeFiles::writeText(scene.string(), document.encode(), &error),
      "Oracle writes the launch scene");
  }
  GuestLaunch launch;
  launch.label = "scene.ilsc";
  launch.editable = true;
  launch.size = std::filesystem::file_size(scene);
  GuestWireWriter startup;
  launch.write(startup);

  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  env.setVar("fullscreen", false);
  Camera camera(glm::vec2(0, 0), 1, &env);
  AtlasObservingBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  renderer.ensureBuiltinStyles();
  CommandRegistry commands;
  CommandLine console(&env, &commands, &window, &renderer, "Test");
  Logger::setContext(&env, &console);
  InputManager input(nullptr);
  IllumoContext context;
  context.renderer = &renderer;
  context.window = &window;
  context.inputManager = &input;
  context.envVars = &env;
  context.commandRegistry = &commands;
  context.commandLine = &console;

  WasmProgram editor(readBytes(ILLUMO_ILLED_GUEST),
                     startup.take(),
                     editorLimits(),
                     {},
                     {},
                     files);
  const bool started = editor.start(context);
  testTrue(counters, started, "Generic host starts the IllEd package");
  if (!started) {
    std::printf("%s\n", editor.error().c_str());
    return false;
  }
  int frames = 0;
  pumpUntil(editor, [&]() {
    renderFrame(editor, renderer, window, camera);
    return ++frames > 60;
  });
  testTrue(counters,
           mock.atlasDrawn && renderer.frameError().empty(),
           "The package-preloaded UI atlas reaches the native backend");

  // Pan with a held Right arrow, then save in place with Ctrl+S.
  InputManagerTestAccess::setAction(input, KeyCode::Right, InputAction::Hold);
  frames = 0;
  pumpUntil(editor, [&]() { return ++frames > 30; });
  InputManagerTestAccess::setAction(input, KeyCode::Right, InputAction::None);
  // Camera smoothing keeps moving briefly; view changes never dirty.
  frames = 0;
  pumpUntil(editor, [&]() { return ++frames > 120; });
  input.getKeyQueue().push({ KeyCode::S, InputAction::Press, 2 });
  SceneDocument saved;
  testTrue(counters,
           pumpUntil(editor,
                     [&]() {
                       std::string text;
                       std::string error;
                       return IllEdNativeFiles::readText(
                                scene.string(), &text, &error) &&
                              IlscCodec::parse(text, saved, error) &&
                              saved.hasEditor && saved.editor.cameraX > 0.5;
                     }),
           "Ctrl+S writes the panned camera back to the launch file");
  testTrue(counters,
           saved.nodes.size() == 2,
           "The launch scene loaded in the guest and kept its nodes");
  // The host commits the file before the guest sees the write complete.
  frames = 0;
  pumpUntil(editor, [&]() { return ++frames > 10; });
  testTrue(counters,
           !historyContains(console, "Frame dropped") &&
             !historyContains(console, "Failed"),
           "Every editor frame and transfer completed without rejection");
  testTrue(counters,
           editor.error().empty() && editor.closeRequested(),
           "A saved document closes without confirmation");
  editor.stop();
  if (counters.failures != 0) {
    for (const CommandLine::historyBuffer& entry : console.getHistory()) {
      std::printf("console: %s\n", entry.content.c_str());
    }
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return counters.failures == 0;
}

// A 4x4 uncompressed top-left TGA, which stb_image decodes.
static std::string
smallTga()
{
  std::string bytes(18, '\0');
  bytes[2] = 2;
  bytes[12] = 4;
  bytes[14] = 4;
  bytes[16] = 32;
  bytes[17] = 0x28;
  for (int pixel = 0; pixel < 16; ++pixel) {
    bytes += std::string("\x30\xA0\x40\xFF", 4);
  }
  return bytes;
}

// IllEd with a writable /project: the package places a mesh and a texture
// from the project (fetched through the guest asset cache, the OBJ's MTL
// included) and saves the scene into /project/scenes.
static bool
projectPackage()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-illed-project-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::filesystem::path package = root / "package";
  const std::filesystem::path project = root / "project";
  std::filesystem::create_directories(package / "Assets" / "IllEd");
  std::filesystem::create_directories(root / "storage");
  std::filesystem::create_directories(project / "meshes");
  std::filesystem::create_directories(project / "textures");
  std::filesystem::copy_file(ILLUMO_ILLED_DEFAULTS, package / "envvars.json");
  std::filesystem::copy_file(
    ILLUMO_ILLED_ATLAS, package / "Assets" / "IllEd" / "editor-ui-atlas.jpg");
  std::ofstream(project / "illumo.json")
    << R"({"format":"ilpk","format_version":1,"id":"demo-scene","kind":"content"})";
  std::ofstream(project / "meshes" / "tri.obj")
    << "mtllib tri.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl leaf\nf 1 2 3\n";
  std::ofstream(project / "meshes" / "tri.mtl")
    << "newmtl leaf\nKd 0.2 0.8 0.3\n";
  std::ofstream(project / "textures" / "leaf.tga", std::ios::binary)
    << smallTga();

  std::shared_ptr<VirtualFileSystem> tree =
    std::make_shared<VirtualFileSystem>();
  std::string error;
  VfsMount app;
  app.point = "/app";
  app.layers.push_back(
    { DirectoryVfsBackend::open(package, false, error), "illed" });
  VfsMount writable;
  writable.point = "/project";
  writable.layers.push_back(
    { DirectoryVfsBackend::open(project, true, error), "demo-scene" });
  tree->mount(app, error);
  tree->mount(writable, error);
  WasmFileRoots files;
  files.storage = root / "storage";
  files.packages = tree;

  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  env.setVar("fullscreen", false);
  Camera camera(glm::vec2(0, 0), 1, &env);
  AtlasObservingBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  renderer.ensureBuiltinStyles();
  CommandRegistry commands;
  CommandLine console(&env, &commands, &window, &renderer, "Test");
  Logger::setContext(&env, &console);
  InputManager input(nullptr);
  IllumoContext context;
  context.renderer = &renderer;
  context.window = &window;
  context.inputManager = &input;
  context.envVars = &env;
  context.commandRegistry = &commands;
  context.commandLine = &console;

  WasmProgram editor(
    readBytes(ILLUMO_ILLED_GUEST), {}, editorLimits(), {}, {}, files);
  const bool started = editor.start(context);
  testTrue(counters, started, "The package starts with a project mounted");
  if (!started) {
    std::printf("%s\n", editor.error().c_str());
    return false;
  }
  testTrue(counters,
           pumpUntil(editor,
                     [&]() {
                       renderFrame(editor, renderer, window, camera);
                       return commands.HasCommand("scene_place") &&
                              commands.HasCommand("scene_save_project");
                     }),
           "The editor registers its scene commands");
  commands.QueueCommand("scene_place", { "/project/meshes/tri.obj", "1", "0" });
  commands.QueueCommand("scene_place",
                        { "/project/textures/leaf.tga", "-1", "0" });
  commands.ExecuteQueue();
  int frames = 0;
  pumpUntil(editor, [&]() {
    renderFrame(editor, renderer, window, camera);
    return ++frames > 90;
  });
  std::printf("host texture creations: %zu placeholder(s), %zu of the 4x4 "
              "project texture\n",
              mock.placeholderCreates,
              mock.smallTextureCreates);
  testTrue(counters,
           mock.placeholderCreates == 0 && mock.smallTextureCreates == 1,
           "the UI atlas and the placed texture each reach the host once, "
           "without a placeholder first");
  commands.QueueCommand("scene_save_project");
  commands.ExecuteQueue();
  SceneDocument saved;
  testTrue(counters,
           pumpUntil(editor,
                     [&]() {
                       std::string text;
                       std::string readError;
                       return IllEdNativeFiles::readText(
                                (project / "scenes" / "Untitled.ilsc").string(),
                                &text,
                                &readError) &&
                              IlscCodec::parse(text, saved, readError);
                     }),
           "Save to Project writes /project/scenes/Untitled.ilsc");
  const SceneAsset* mesh = saved.findAsset("tri");
  const SceneAsset* leaf = saved.findAsset("leaf");
  testTrue(counters,
           saved.nodes.size() == 2 && mesh != nullptr &&
             mesh->path == "meshes/tri.obj" && leaf != nullptr &&
             leaf->path == "textures/leaf.tga",
           "placed nodes reference project assets package-relatively");
  testTrue(counters,
           renderer.frameError().empty() &&
             !historyContains(console, "Failed") &&
             !historyContains(console, "Cannot"),
           "fetches, placement and the save complete without errors");
  editor.stop();
  if (counters.failures != 0) {
    for (const CommandLine::historyBuffer& entry : console.getHistory()) {
      std::printf("console: %s\n", entry.content.c_str());
    }
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return counters.failures == 0;
}

// Records launches instead of starting processes.
class RecordingLauncher final : public IAppLauncher
{
public:
  struct Record
  {
    std::vector<std::string> applications;
    std::vector<std::string> names;
    std::vector<std::string> documents;
    bool running = false;
  };
  explicit RecordingLauncher(Record& record)
    : m_record(record)
  {
  }
  bool available() const override { return true; }
  bool launch(const std::string& application,
              const std::string& name,
              std::span<const std::byte> document,
              std::string& error) override
  {
    (void)error;
    m_record.applications.push_back(application);
    m_record.names.push_back(name);
    std::string text;
    for (std::byte value : document) {
      text.push_back(static_cast<char>(value));
    }
    m_record.documents.push_back(text);
    m_record.running = true;
    return true;
  }
  void stop() override { m_record.running = false; }
  bool running() override { return m_record.running; }

private:
  Record& m_record;
};

// Play end to end through the generic host: IllEd reads the installed
// Playground's behaviours.json at /apps/playground, opens a scene using one of
// them, and Play hands the host's launcher that scene with its game and
// package root; Play again stops it.
static bool
playPackage()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-illed-play-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::filesystem::path package = root / "package";
  const std::filesystem::path game = root / "playground";
  const std::filesystem::path scene = root / "documents" / "level.ilsc";
  std::filesystem::create_directories(package / "Assets" / "IllEd");
  std::filesystem::create_directories(game);
  std::filesystem::create_directories(root / "storage");
  std::filesystem::create_directories(scene.parent_path());
  std::filesystem::copy_file(ILLUMO_ILLED_DEFAULTS, package / "envvars.json");
  std::filesystem::copy_file(
    ILLUMO_ILLED_ATLAS, package / "Assets" / "IllEd" / "editor-ui-atlas.jpg");
  std::ofstream(game / "behaviours.json")
    << R"({"format":"illumo-behaviours","format_version":1,"behaviours":[
         {"type":"playground.bob","title":"Bob","fields":[
           {"name":"height","kind":"number","default":0.5}]}]})";
  std::ofstream(scene) << R"({
  "format": "ilsc",
  "format_version": [2, 0],
  "settings": { "world_mode": "3d" },
  "nodes": [
    { "id": "box",
      "components": [
        { "type": "primitive", "shape": "cube" },
        { "type": "playground.bob", "height": 2 } ] }
  ]
})";

  std::shared_ptr<VirtualFileSystem> tree =
    std::make_shared<VirtualFileSystem>();
  std::string error;
  VfsMount app;
  app.point = "/app";
  app.layers.push_back(
    { DirectoryVfsBackend::open(package, false, error), "illed" });
  VfsMount installed;
  installed.point = "/apps/playground";
  installed.layers.push_back(
    { DirectoryVfsBackend::open(game, false, error), "playground" });
  tree->mount(app, error);
  tree->mount(installed, error);
  WasmFileRoots files;
  files.storage = root / "storage";
  files.packages = tree;
  files.launch = scene;
  files.launchEditable = true;
  GuestLaunch launch;
  launch.label = "level.ilsc";
  launch.editable = true;
  launch.size = std::filesystem::file_size(scene);
  GuestWireWriter startup;
  launch.write(startup);

  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  env.setVar("fullscreen", false);
  Camera camera(glm::vec2(0, 0), 1, &env);
  AtlasObservingBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  renderer.ensureBuiltinStyles();
  CommandRegistry commands;
  CommandLine console(&env, &commands, &window, &renderer, "Test");
  Logger::setContext(&env, &console);
  InputManager input(nullptr);
  IllumoContext context;
  context.renderer = &renderer;
  context.window = &window;
  context.inputManager = &input;
  context.envVars = &env;
  context.commandRegistry = &commands;
  context.commandLine = &console;

  RecordingLauncher::Record record;
  WasmProgram editor(readBytes(ILLUMO_ILLED_GUEST),
                     startup.take(),
                     editorLimits(),
                     {},
                     {},
                     files);
  editor.setAppLauncher(std::make_unique<RecordingLauncher>(record));
  const bool started = editor.start(context);
  testTrue(counters, started, "The package starts with a launcher");
  if (!started) {
    std::printf("%s\n", editor.error().c_str());
    return false;
  }
  int frames = 0;
  pumpUntil(editor, [&]() {
    renderFrame(editor, renderer, window, camera);
    return commands.HasCommand("scene_play") && ++frames > 60;
  });
  commands.QueueCommand("scene_play");
  commands.ExecuteQueue();
  testTrue(counters,
           pumpUntil(editor,
                     [&]() {
                       renderFrame(editor, renderer, window, camera);
                       return !record.applications.empty();
                     }),
           "Play reaches the host's launcher");
  SceneDocument played;
  ScenePlay play;
  testTrue(counters,
           !record.applications.empty() &&
             record.applications[0] == "playground" &&
             record.names[0] == "level.ilsc",
           "the scene's behaviour picks the installed game");
  testTrue(counters,
           !record.documents.empty() &&
             IlscCodec::parse(record.documents[0], played, error) &&
             played.findNode("box") != nullptr &&
             ScenePlay::read(played.extensions, &play) &&
             play.application == "playground" && !play.root.empty(),
           "the launch document is the scene with its game and root");
  frames = 0;
  pumpUntil(editor, [&]() {
    renderFrame(editor, renderer, window, camera);
    return ++frames > 10;
  });
  commands.QueueCommand("scene_play");
  commands.ExecuteQueue();
  testTrue(counters,
           pumpUntil(editor,
                     [&]() {
                       renderFrame(editor, renderer, window, camera);
                       return !record.running;
                     }),
           "Play again stops the game");
  testTrue(counters,
           record.applications.size() == 1 && renderer.frameError().empty(),
           "one launch, and every frame renders");
  editor.stop();
  if (counters.failures != 0) {
    for (const CommandLine::historyBuffer& entry : console.getHistory()) {
      std::printf("console: %s\n", entry.content.c_str());
    }
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return counters.failures == 0;
}

// One editor run over shared package and storage folders, driven through
// console commands whose printed lines the test reads.
struct RecoveryRun
{
  const WasmFileRoots& files;
  NullRenderWindow window{ 1280, 720 };
  EnvVars env;
  Camera camera{ glm::vec2(0, 0), 1, &env };
  MockBackend mock;
  Renderer renderer{ &window, &env, &camera, &mock, false };
  CommandRegistry commands;
  CommandLine console{ &env, &commands, &window, &renderer, "Test" };
  InputManager input{ nullptr };
  IllumoContext context;
  std::unique_ptr<WasmProgram> editor;

  explicit RecoveryRun(const WasmFileRoots& roots)
    : files(roots)
  {
    env.setVar("WinX", 1280);
    env.setVar("WinY", 720);
    env.setVar("fullscreen", false);
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
  ~RecoveryRun()
  {
    if (editor) {
      editor->stop();
    }
    Logger::setContext(nullptr, nullptr);
  }

  // A project host handed to the program before it starts (Project
  // capability), and whether a restart request is honoured.
  std::unique_ptr<IProjectHost> projectHost;
  bool restartAllowed = false;

  bool start()
  {
    editor = std::make_unique<WasmProgram>(readBytes(ILLUMO_ILLED_GUEST),
                                           std::vector<std::byte>{},
                                           editorLimits(),
                                           std::vector<std::byte>{},
                                           std::vector<std::byte>{},
                                           files);
    if (projectHost) {
      editor->setProjectHost(std::move(projectHost));
    }
    editor->setRestartAllowed(restartAllowed);
    if (!editor->start(context)) {
      std::printf("%s\n", editor->error().c_str());
      return false;
    }
    return true;
  }

  // Runs a console command once the guest registered it; the status line
  // printed after it (scene_status) is returned.
  std::string run(const std::string& name,
                  const std::vector<std::string>& args = {})
  {
    bool queued = false;
    std::size_t lines = 0;
    std::string status;
    pumpUntil(*editor, [&]() {
      if (!queued && commands.HasCommand(name) &&
          commands.HasCommand("scene_status")) {
        commands.QueueCommand(name, args);
        commands.QueueCommand("scene_status", {});
        lines = console.getHistory().size();
        commands.ExecuteQueue();
        queued = true;
      }
      if (!queued || console.getHistory().size() <= lines) {
        return false;
      }
      for (const CommandLine::historyBuffer& entry : console.getHistory()) {
        if (entry.content.rfind("scene_status #", 0) == 0) {
          status = entry.content;
        }
      }
      return !status.empty();
    });
    return status;
  }

  void dump(const TestCounters& counters)
  {
    if (counters.failures == 0) {
      return;
    }
    if (editor && !editor->error().empty()) {
      std::printf("guest error: %s\n", editor->error().c_str());
    }
    for (const CommandLine::historyBuffer& entry : console.getHistory()) {
      std::printf("console: %s\n", entry.content.c_str());
    }
  }
};

// Autosave and recovery through the generic host: one run autosaves an
// Tabs through the generic host: a second document opens beside the first,
// switching keeps each one's state, and closing a dirty tab asks first.
static bool
tabsPackage()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-illed-tabs-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  WasmFileRoots files{ root / "package", root / "storage", {}, false };
  std::filesystem::create_directories(files.package / "Assets" / "IllEd");
  std::filesystem::create_directories(files.storage);
  std::filesystem::copy_file(ILLUMO_ILLED_DEFAULTS,
                             files.package / "envvars.json");
  std::filesystem::copy_file(ILLUMO_ILLED_ATLAS,
                             files.package / "Assets" / "IllEd" /
                               "editor-ui-atlas.jpg");
  {
    RecoveryRun run(files);
    testTrue(counters, run.start(), "The editor starts");
    if (!run.editor) {
      return false;
    }
    std::string status = run.run("scene_create", { "cube" });
    testTrue(counters,
             status.find(" 1 nodes dirty") != std::string::npos &&
               status.find(" tab 1/1 ") != std::string::npos,
             "the first tab holds the edit");
    status = run.run("scene_tab", { "open" });
    testTrue(counters,
             status.find(" 0 nodes clean") != std::string::npos &&
               status.find(" tab 2/2 ") != std::string::npos,
             "a second, empty tab opens beside it and is active");
    status = run.run("scene_tab", { "prev" });
    testTrue(counters,
             status.find(" 1 nodes dirty") != std::string::npos &&
               status.find(" tab 1/2 ") != std::string::npos,
             "switching back shows the first tab's document again");
    // Ctrl+Tab cycles forward; Ctrl+W asks before closing the dirty tab.
    run.input.getKeyQueue().push({ KeyCode::Tab, InputAction::Press, 2 });
    status = run.run("scene_status");
    testTrue(counters,
             status.find(" tab 2/2 ") != std::string::npos,
             "Ctrl+Tab cycles to the next tab");
    run.input.getKeyQueue().push({ KeyCode::Tab, InputAction::Press, 3 });
    status = run.run("scene_status");
    testTrue(counters,
             status.find(" tab 1/2 ") != std::string::npos,
             "Ctrl+Shift+Tab cycles back");
    run.input.getKeyQueue().push({ KeyCode::W, InputAction::Press, 2 });
    bool asked = false;
    pumpUntil(*run.editor, [&]() {
      asked = !run.editor->closeRequested() &&
              run.run("scene_status").find(" tab 1/2 ") != std::string::npos;
      return asked;
    });
    testTrue(counters, asked, "Ctrl+W on a dirty tab asks before closing");
    run.input.getKeyQueue().push({ KeyCode::N, InputAction::Press, 0 });
    status = run.run("scene_status");
    testTrue(counters,
             status.find(" tab 1/1 ") != std::string::npos &&
               status.find(" 0 nodes clean") != std::string::npos,
             "Don't Save closes the tab and the empty one remains");
    testTrue(counters,
             !historyContains(run.console, "Frame dropped") &&
               !historyContains(run.console, "Failed"),
             "Every frame completed");
    run.dump(counters);
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return counters.failures == 0;
}

// A project host whose recent list the guest reads and whose open it asks
// for; the restart request that follows reaches the host window.
class FakeProjectHost final : public IProjectHost
{
public:
  bool available() const override { return true; }
  std::vector<ProjectHostEntry> list() override
  {
    ++lists;
    return { { 1, "Forest" }, { 2, "Town" } };
  }
  bool choose(ProjectHostEntry* chosen, bool* cancelled, std::string&) override
  {
    *cancelled = false;
    *chosen = { 1, "Chosen" };
    return true;
  }
  bool create(ProjectHostEntry* created, bool* cancelled, std::string&) override
  {
    *cancelled = false;
    *created = { 1, "Created" };
    return true;
  }
  bool open(std::uint32_t id, std::string&) override
  {
    opened = id;
    return true;
  }
  void clear() override {}

  int lists = 0;
  std::uint32_t opened = 0;
};

// Projects through the generic host: the guest lists the host's recent
// projects, and opening one makes it ask the host to restart.
static bool
projectsPackage()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-illed-projects-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  WasmFileRoots files{ root / "package", root / "storage", {}, false };
  std::filesystem::create_directories(files.package / "Assets" / "IllEd");
  std::filesystem::create_directories(files.storage);
  std::filesystem::copy_file(ILLUMO_ILLED_DEFAULTS,
                             files.package / "envvars.json");
  std::filesystem::copy_file(ILLUMO_ILLED_ATLAS,
                             files.package / "Assets" / "IllEd" /
                               "editor-ui-atlas.jpg");
  {
    RecoveryRun run(files);
    std::unique_ptr<FakeProjectHost> owned =
      std::make_unique<FakeProjectHost>();
    FakeProjectHost* host = owned.get();
    run.projectHost = std::move(owned);
    run.restartAllowed = true;
    testTrue(counters, run.start(), "The editor starts with a project host");
    if (!run.editor) {
      return false;
    }
    // The listing lands a few updates after the start.
    testTrue(counters,
             pumpUntil(*run.editor,
                       [&]() {
                         run.run("scene_project");
                         return historyContains(run.console,
                                                "available 2 recent");
                       }),
             "The guest lists the host's two recent projects");
    testTrue(counters,
             historyContains(run.console, "scene_project 1 Forest") &&
               historyContains(run.console, "scene_project 2 Town"),
             "The listing names them in order");
    run.run("scene_project", { "open", "2" });
    testTrue(counters,
             pumpUntil(*run.editor,
                       [&]() {
                         return host->opened == 2 &&
                                run.window.restartRequested();
                       }),
             "Opening a project asks the host for it and then to restart");
    testTrue(counters,
             !historyContains(run.console, "Frame dropped") &&
               !historyContains(run.console, "Failed"),
             "Every frame completed");
    run.dump(counters);
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return counters.failures == 0;
}

// edited document into the package's storage, the next offers it back.
static bool
recoveryPackage()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-illed-recovery-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  WasmFileRoots files{ root / "package", root / "storage", {}, false };
  std::filesystem::create_directories(files.package / "Assets" / "IllEd");
  std::filesystem::create_directories(files.storage);
  std::filesystem::copy_file(ILLUMO_ILLED_DEFAULTS,
                             files.package / "envvars.json");
  std::filesystem::copy_file(ILLUMO_ILLED_ATLAS,
                             files.package / "Assets" / "IllEd" /
                               "editor-ui-atlas.jpg");
  const std::filesystem::path slot =
    files.storage / EditorAutosaveRecord::slotName(0);
  {
    RecoveryRun first(files);
    testTrue(counters, first.start(), "The first editor run starts");
    if (!first.editor) {
      return false;
    }
    std::string status = first.run("scene_create", { "cube" });
    testTrue(counters,
             status.find(" 1 nodes dirty") != std::string::npos,
             "scene_create makes the document dirty");
    first.run("scene_autosave");
    // The storage write lands on a later update; the guest learns of it
    // later still.
    testTrue(counters,
             pumpUntil(*first.editor,
                       [&]() {
                         return std::filesystem::exists(slot) &&
                                std::filesystem::file_size(slot) > 0;
                       }),
             "scene_autosave writes the slot into the package's storage");
    testTrue(counters,
             pumpUntil(*first.editor,
                       [&]() {
                         return first.run("scene_status").find("autosaved") !=
                                std::string::npos;
                       }),
             "The guest sees its autosave complete");
    SceneDocument autosaved;
    EditorAutosaveRecord record;
    std::string text;
    std::string error;
    testTrue(counters,
             IllEdNativeFiles::readText(slot.string(), &text, &error) &&
               IlscCodec::parse(text, autosaved, error) &&
               autosaved.nodes.size() == 1 && record.read(autosaved) &&
               !record.savedAt.empty(),
             "The slot carries the scene and its illed.autosave record");
    testTrue(counters,
             !first.editor->closeRequested(),
             "The dirty document still asks before closing");
    first.dump(counters);
    // Stopped without an answer, as a crash would leave it.
  }
  {
    RecoveryRun second(files);
    testTrue(counters, second.start(), "The second editor run starts");
    if (!second.editor) {
      return false;
    }
    std::string status;
    testTrue(counters,
             pumpUntil(*second.editor,
                       [&]() {
                         status = second.run("scene_status");
                         return status.find("recovery") != std::string::npos;
                       }),
             "The next start offers the autosaved document");
    status = second.run("scene_recover", { "recover" });
    testTrue(counters,
             status.find(" 1 nodes dirty") != std::string::npos &&
               status.find("Untitled (recovered)") != std::string::npos,
             "Recover loads the lost node as a dirty recovered document");
    testTrue(counters,
             !second.editor->closeRequested(),
             "Recovered work asks before closing");
    testTrue(counters,
             !historyContains(second.console, "Frame dropped") &&
               !historyContains(second.console, "Failed"),
             "Every frame and transfer completed");
    second.dump(counters);
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return counters.failures == 0;
}

int
main(int argc, char** argv)
{
  if (argc == 2 && std::string(argv[1]) == "--list") {
    std::puts("IllEd.Wasm.Package");
    std::puts("IllEd.Wasm.ProjectPackage");
    std::puts("IllEd.Wasm.PlayPackage");
    std::puts("IllEd.Wasm.Recovery");
    std::puts("IllEd.Wasm.Tabs");
    std::puts("IllEd.Wasm.Projects");
    return 0;
  }
  if (argc != 3 || std::string(argv[1]) != "--run") {
    return 2;
  }
  if (std::string(argv[2]) == "IllEd.Wasm.ProjectPackage") {
    return projectPackage() ? 0 : 1;
  }
  if (std::string(argv[2]) == "IllEd.Wasm.PlayPackage") {
    return playPackage() ? 0 : 1;
  }
  if (std::string(argv[2]) == "IllEd.Wasm.Recovery") {
    return recoveryPackage() ? 0 : 1;
  }
  if (std::string(argv[2]) == "IllEd.Wasm.Tabs") {
    return tabsPackage() ? 0 : 1;
  }
  if (std::string(argv[2]) == "IllEd.Wasm.Projects") {
    return projectsPackage() ? 0 : 1;
  }
  if (std::string(argv[2]) != "IllEd.Wasm.Package") {
    return 2;
  }
  return editorPackage() ? 0 : 1;
}