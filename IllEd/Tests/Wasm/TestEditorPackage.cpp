#include "EditorDocument.h"
#include "IlscCodec.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestAccess.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/WasmGameModule.h>
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
  void PushToCommandQueue(RenderCommand command) override
  {
    if (command.commandType == CommandType::SetTexture &&
        command.bindTexture.slot == 0) {
      m_bound = command.bindTexture.handle;
    } else if (command.commandType == CommandType::DrawIndexed &&
               m_atlas.isValid() && m_bound.slot == m_atlas.slot &&
               m_bound.generation == m_atlas.generation) {
      atlasDrawn = true;
    }
    MockBackend::PushToCommandQueue(command);
  }

private:
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
  limits.meterFuel = false; // As shipped: app.json requests epoch metering.
  limits.deadlineMilliseconds = 10000;
  return limits;
}

static bool
pumpUntil(WasmGameModule& editor, const std::function<bool()>& reached)
{
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (editor.error().empty() &&
         std::chrono::steady_clock::now() < deadline) {
    editor.Update(1.0 / 60.0);
    if (reached()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::printf("pump stopped: %s\n", editor.error().c_str());
  return false;
}

static void
renderFrame(WasmGameModule& editor,
            Renderer& renderer,
            IRenderWindow& window,
            Camera& camera)
{
  Scene scene(&window, &camera);
  editor.DispatchDrawables(&scene);
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
    document.createNode(SceneNodeKind::SolidCube, {});
    document.createNode(SceneNodeKind::WireSphere, {});
    std::string error;
    testTrue(counters,
             document.saveToFile(scene.string(), &error),
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

  WasmGameModule editor(readBytes(ILLUMO_ILLED_GUEST),
                        startup.take(),
                        editorLimits(),
                        {},
                        {},
                        files);
  const bool started = editor.Start(&context);
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

  // Pan with a held D, then save in place with Ctrl+S.
  InputManagerTestAccess::setAction(input, KeyCode::D, InputAction::Hold);
  frames = 0;
  pumpUntil(editor, [&]() { return ++frames > 30; });
  InputManagerTestAccess::setAction(input, KeyCode::D, InputAction::None);
  // Camera smoothing keeps moving (and re-dirtying the document) briefly.
  frames = 0;
  pumpUntil(editor, [&]() { return ++frames > 120; });
  input.getKeyQueue().push({ KeyCode::S, InputAction::Press, 2 });
  IlscDocument saved;
  testTrue(counters,
           pumpUntil(editor,
                     [&]() {
                       std::string error;
                       return IlscCodec::readFile(
                                scene.string(), &saved, &error) &&
                              saved.camera.x > 0.5;
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
           editor.error().empty() && editor.OnCloseRequested(),
           "A saved document closes without confirmation");
  editor.Exit();
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
    return 0;
  }
  if (argc != 3 || std::string(argv[1]) != "--run" ||
      std::string(argv[2]) != "IllEd.Wasm.Package") {
    return 2;
  }
  return editorPackage() ? 0 : 1;
}
