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
  TextureHandle CreateTexture(const unsigned char* data,
                              int width,
                              int height,
                              int channels,
                              const TextureOptions& options) override
  {
    const TextureHandle handle =
      MockBackend::CreateTexture(data, width, height, channels, options);
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

int
main(int argc, char** argv)
{
  if (argc == 2 && std::string(argv[1]) == "--list") {
    std::puts("IllEd.Wasm.Package");
    std::puts("IllEd.Wasm.ProjectPackage");
    std::puts("IllEd.Wasm.PlayPackage");
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
  if (std::string(argv[2]) != "IllEd.Wasm.Package") {
    return 2;
  }
  return editorPackage() ? 0 : 1;
}