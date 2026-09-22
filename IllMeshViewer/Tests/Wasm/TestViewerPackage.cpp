#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/WasmGameModule.h>
#include <IllumoGuest/Dialog.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <thread>

// The complete IllMeshViewer runs only as IllMeshViewer.wasm. This native
// side is the generic host plus an observing backend: the launch mesh must
// arrive as one retained static host mesh, and the preloaded skybox cross
// as a cubemap drawn by the built-in skybox style.

class ViewerObservingBackend final : public MockBackend
{
public:
  static constexpr std::size_t TorusVertexBytes = 96u * 48u * 36u;
  std::size_t retainedMeshes = 0;
  std::size_t cubemaps = 0;
  std::size_t retainedDraws = 0;
  MeshHandle CreateMesh(const void* vertices,
                        std::size_t vertexBytes,
                        const void* indices,
                        std::size_t indexBytes,
                        MeshVertexLayout layout,
                        bool dynamic) override
  {
    const MeshHandle handle = MockBackend::CreateMesh(
      vertices, vertexBytes, indices, indexBytes, layout, dynamic);
    if (!dynamic && layout == MeshVertexLayout::Pos3Norm3Color4U8Uv2 &&
        vertexBytes == TorusVertexBytes) {
      ++retainedMeshes;
      m_retained = handle;
    }
    return handle;
  }
  TextureHandle CreateCubemap(const std::array<const unsigned char*, 6>& faces,
                              int width,
                              int height,
                              int channels) override
  {
    ++cubemaps;
    return MockBackend::CreateCubemap(faces, width, height, channels);
  }
  void PushToCommandQueue(RenderCommand command) override
  {
    if (command.commandType == CommandType::SetMesh) {
      m_bound = command.bindMesh.handle;
    } else if (command.commandType == CommandType::DrawIndexed &&
               m_retained.isValid() && m_bound == m_retained) {
      ++retainedDraws;
    }
    MockBackend::PushToCommandQueue(command);
  }

private:
  MeshHandle m_retained{};
  MeshHandle m_bound{};
};

// A 96x48 torus with outward winding and no normals: 4608 vertices, large
// enough (166 KB of lit vertices) to be uploaded as a retained mesh.
static std::string
torusObj()
{
  std::string text = "# generated torus\n";
  const int around = 96;
  const int tube = 48;
  const float pi = 3.14159265f;
  for (int i = 0; i < around; ++i) {
    const float a = 2.0f * pi * static_cast<float>(i) / around;
    for (int j = 0; j < tube; ++j) {
      const float b = 2.0f * pi * static_cast<float>(j) / tube;
      const float ring = 1.0f + 0.35f * std::cos(b);
      text += "v " + std::to_string(ring * std::cos(a)) + " " +
              std::to_string(0.35f * std::sin(b)) + " " +
              std::to_string(ring * std::sin(a)) + "\n";
    }
  }
  for (int i = 0; i < around; ++i) {
    for (int j = 0; j < tube; ++j) {
      const int a = i * tube + j + 1;
      const int b = ((i + 1) % around) * tube + j + 1;
      const int c = ((i + 1) % around) * tube + (j + 1) % tube + 1;
      const int d = i * tube + (j + 1) % tube + 1;
      text += "f " + std::to_string(a) + " " + std::to_string(d) + " " +
              std::to_string(c) + " " + std::to_string(b) + "\n";
    }
  }
  return text;
}

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

static bool
viewerPackage()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-viewer-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  const std::filesystem::path mesh = root / "models" / "torus.obj";
  WasmFileRoots files{ root / "package", root / "storage", mesh, false };
  std::filesystem::create_directories(files.package / "Assets" / "Skybox");
  std::filesystem::create_directories(files.storage);
  std::filesystem::create_directories(mesh.parent_path());
  std::filesystem::copy_file(ILLUMO_VIEWER_DEFAULTS,
                             files.package / "envvars.json");
  std::filesystem::copy_file(ILLUMO_VIEWER_SKYBOX,
                             files.package / "Assets" / "Skybox" /
                               "skybox-daylight.png");
  std::ofstream(mesh, std::ios::binary) << torusObj();
  GuestLaunch launch;
  launch.label = "torus.obj";
  launch.size = std::filesystem::file_size(mesh);
  GuestWireWriter startup;
  launch.write(startup);

  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  env.setVar("fullscreen", false);
  Camera camera(glm::vec2(0, 0), 1, &env);
  ViewerObservingBackend mock;
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

  WasmLimits limits;
  limits.memoryBytes = 1024ull * 1024ull * 1024ull;
  limits.fuelPerCall = 20000000000ull;
  limits.deadlineMilliseconds = 10000;
  WasmGameModule viewer(
    readBytes(ILLUMO_VIEWER_GUEST), startup.take(), limits, {}, {}, files);
  const bool started = viewer.Start(&context);
  testTrue(counters, started, "Generic host starts the IllMeshViewer package");
  if (!started) {
    std::printf("%s\n", viewer.error().c_str());
    return false;
  }
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(60);
  std::size_t frames = 0;
  while (viewer.error().empty() &&
         std::chrono::steady_clock::now() < deadline &&
         (mock.retainedDraws < 3 || frames < 30)) {
    viewer.Update(1.0 / 60.0);
    Scene scene(&window, &camera);
    viewer.DispatchDrawables(&scene);
    renderer.BeginFrame();
    renderer.RenderScene(&scene, &camera);
    renderer.EndFrame();
    ++frames;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::printf("viewer: %zu frames, %zu retained meshes, %zu retained draws, "
              "%zu cubemaps\n",
              frames,
              mock.retainedMeshes,
              mock.retainedDraws,
              mock.cubemaps);
  testTrue(counters,
           mock.retainedMeshes == 1,
           "The launch mesh becomes one retained static host mesh");
  testTrue(counters,
           mock.retainedDraws >= 3 && renderer.frameError().empty(),
           "Later frames draw the retained mesh without re-sending it");
  testTrue(counters,
           mock.cubemaps == 1,
           "The preloaded skybox cross becomes one host cubemap");
  testTrue(counters,
           viewer.error().empty() &&
             !historyContains(console, "Frame dropped") &&
             !historyContains(console, "Failed"),
           "Every viewer frame and transfer completed without rejection");
  testTrue(counters,
           viewer.OnCloseRequested(),
           "The viewer accepts a host close request");
  viewer.Exit();
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
    std::puts("IllMeshViewer.Wasm.Package");
    return 0;
  }
  if (argc != 3 || std::string(argv[1]) != "--run" ||
      std::string(argv[2]) != "IllMeshViewer.Wasm.Package") {
    return 2;
  }
  return viewerPackage() ? 0 : 1;
}
