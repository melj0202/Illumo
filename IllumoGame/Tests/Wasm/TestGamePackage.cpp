#include "Game/IllumoCodec.h"
#include "Rulesets/RuleSetRegistry.h"
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
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <thread>

// The complete IllumoGame product runs only as IllumoGame.wasm. This native
// side is the generic host, a scripted console/keyboard, and a reference
// oracle that decodes the guest's save; it links no product modules.

// The guest canvas is the only RGB texture (font atlases are 1 or 4 channel).
class CanvasObservingBackend final : public MockBackend
{
public:
  bool canvasDrawn = false;
  // 3D test mode: draws inside the shared shadow pass and lit-mesh draws.
  ShaderHandle litShader{};
  std::size_t shadowDraws = 0;
  std::size_t litDraws = 0;
  TextureHandle CreateTexture(const unsigned char* data,
                              int width,
                              int height,
                              int channels,
                              const TextureOptions& options) override
  {
    const TextureHandle handle =
      MockBackend::CreateTexture(data, width, height, channels, options);
    if (channels == 3) {
      m_canvas = handle;
    }
    return handle;
  }
  void PushToCommandQueue(RenderCommand command) override
  {
    if (command.commandType == CommandType::SetTexture &&
        command.bindTexture.slot == 0) {
      m_bound = command.bindTexture.handle;
    } else if (command.commandType == CommandType::SetShader) {
      m_shader = command.bindShader.handle;
    } else if (command.commandType == CommandType::SetFramebuffer) {
      m_offscreen = command.bindFramebuffer.handle.isValid();
    } else if (command.commandType == CommandType::DrawIndexed) {
      if (m_offscreen) {
        ++shadowDraws;
      } else if (m_shader.isValid() && m_shader.slot == litShader.slot &&
                 m_shader.generation == litShader.generation) {
        ++litDraws;
      } else if (m_canvas.isValid() && m_bound.slot == m_canvas.slot &&
                 m_bound.generation == m_canvas.generation) {
        canvasDrawn = true;
      }
    }
    MockBackend::PushToCommandQueue(command);
  }

private:
  TextureHandle m_canvas{};
  TextureHandle m_bound{};
  ShaderHandle m_shader{};
  bool m_offscreen = false;
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

static std::string
readText(const std::filesystem::path& path)
{
  std::ifstream stream(path, std::ios::binary);
  return { std::istreambuf_iterator<char>(stream), {} };
}

// FNV-1a over canonical chunk records; identical to the DomainParity hash.
static std::uint64_t
worldHash(const SparseCellGrid& grid)
{
  std::uint64_t result = 14695981039346656037ULL;
  for (const SparseChunkRecord& chunk : grid.collectChunkRecords()) {
    for (const std::int64_t coordinate : { chunk.chunkX, chunk.chunkY }) {
      const std::uint64_t bits = static_cast<std::uint64_t>(coordinate);
      for (unsigned int index = 0; index < 8u; ++index) {
        result = (result ^ ((bits >> (index * 8u)) & 255u)) * 1099511628211ULL;
      }
    }
    for (unsigned char cell : chunk.cells) {
      result = (result ^ cell) * 1099511628211ULL;
    }
  }
  return result;
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

// Package budgets are explicit; the generic defaults stay for small guests.
static WasmLimits
gameLimits()
{
  WasmLimits limits;
  limits.memoryBytes = 512ull * 1024ull * 1024ull;
  limits.fuelPerCall = 2000000000u;
  limits.deadlineMilliseconds = 10000;
  return limits;
}

static bool
pumpUntil(WasmGameModule& game, const std::function<bool()>& reached)
{
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (game.error().empty() && std::chrono::steady_clock::now() < deadline) {
    game.Update(1.0 / 60.0);
    if (reached()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::printf("pump stopped: %s\n", game.error().c_str());
  return false;
}

static void
execute(CommandRegistry& commands,
        const std::string& name,
        const std::vector<std::string>& arguments = {})
{
  commands.QueueCommand(name, arguments);
  commands.ExecuteQueue();
}

// Opens canvas setup through the menu's console command, then selects and
// confirms Create with the keyboard, as a player would.
static bool
enterCanvas(WasmGameModule& game,
            CommandRegistry& commands,
            InputManager& input)
{
  if (!pumpUntil(game, [&]() { return commands.HasCommand("play"); })) {
    return false;
  }
  execute(commands, "play");
  int frames = 0;
  pumpUntil(game, [&]() { return ++frames > 20; });
  for (KeyCode key : { KeyCode::End, KeyCode::Up, KeyCode::Enter }) {
    input.getKeyQueue().push({ key, InputAction::Press, 0 });
    frames = 0;
    pumpUntil(game, [&]() { return ++frames > 3; });
  }
  return pumpUntil(game, [&]() {
    for (const char* name :
         { "clear_canvas", "setcell", "step", "save", "load" }) {
      if (!commands.HasCommand(name)) {
        return false;
      }
    }
    return true;
  });
}

static bool
gamePackage()
{
  TestCounters counters;
  const std::string families = readText(ILLUMO_FAMILIES);
  const std::string rules = readText(ILLUMO_RULES);
  RuleSetRegistry registry;
  if (!registry.loadFromCatalogTexts(families, rules)) {
    return false;
  }
  RuleSetRegistry::instance() = registry;
  std::unique_ptr<RuleSet> rule = registry.createRuleSet("GAME_OF_LIFE");
  if (!rule) {
    return false;
  }
  // Reference: a horizontal blinker advanced one generation.
  SparseCellGrid reference;
  for (std::int64_t x = 0; x < 3; ++x) {
    reference.setCell({ x, 0 }, 0);
  }
  if (!reference.advance(*rule)) {
    return false;
  }
  const std::uint64_t expectedHash = worldHash(reference);

  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-game-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  WasmFileRoots files{ root / "package", root / "storage" };
  std::filesystem::create_directories(files.package);
  std::filesystem::create_directories(files.storage);
  std::filesystem::copy_file(ILLUMO_FAMILIES, files.package / "families.json");
  std::filesystem::copy_file(ILLUMO_RULES, files.package / "rulesets.json");
  std::filesystem::copy_file(ILLUMO_GAME_DEFAULTS,
                             files.package / "envvars.json");

  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  env.setVar("fullscreen", false);
  Camera camera(glm::vec2(0, 0), 1, &env);
  CanvasObservingBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  renderer.ensureBuiltinStyles();
  CommandRegistry commands;
  CommandLine console(&env, &commands, &window, &renderer, "Test");
  // Guest Logger output arrives through the host Logger; show it here so
  // frame and service errors are observable, as in the runtime console.
  Logger::setContext(&env, &console);
  InputManager input(nullptr);
  IllumoContext context;
  context.renderer = &renderer;
  context.window = &window;
  context.inputManager = &input;
  context.envVars = &env;
  context.commandRegistry = &commands;
  context.commandLine = &console;

  WasmGameModule game(
    readBytes(ILLUMO_GAME_GUEST), {}, gameLimits(), {}, {}, files);
  const bool started = game.Start(&context);
  testTrue(counters, started, "Generic host starts the IllumoGame package");
  if (!started) {
    std::printf("%s\n", game.error().c_str());
    return false;
  }
  testTrue(counters,
           enterCanvas(game, commands, input),
           "Menu and keyboard canvas setup start the game module");
  testTrue(counters,
           !commands.HasCommand("play"),
           "Menu commands retire with their module");

  execute(commands, "clear_canvas");
  for (const char* x : { "0", "1", "2" }) {
    execute(commands, "setcell", { x, "0", "0" });
  }
  execute(commands, "step", { "1" });
  testTrue(counters,
           pumpUntil(game,
                     [&]() {
                       return historyContains(console, "Advanced 1 generation");
                     }),
           "Serial guest runner completes a console step");
  execute(commands, "save", { "smoke" });
  const std::filesystem::path saved = files.storage / "smoke.csim";
  testTrue(counters,
           pumpUntil(game,
                     [&]() {
                       return std::filesystem::exists(saved) &&
                              historyContains(console,
                                              "Saved canvas to smoke.csim");
                     }),
           "Console-driven edit, step and save complete in the guest");

  std::ifstream stream(saved, std::ios::binary);
  IllumoDocument document;
  std::string error;
  const bool decoded = IllumoCodec::readStream(stream, &document, &error);
  testTrue(counters,
           decoded && document.grid && document.version == 4 &&
             document.ruleString == "GAME_OF_LIFE",
           "Guest save is a sparse v4 document with rule identity");
  testTrue(counters,
           decoded && document.grid &&
             worldHash(*document.grid) == expectedHash,
           "Guest generation matches the native reference");

  execute(commands, "clear_canvas");
  execute(commands, "load", { "smoke" });
  testTrue(counters,
           pumpUntil(game,
                     [&]() {
                       return historyContains(console,
                                              "Loaded canvas from smoke.csim");
                     }),
           "Guest reloads its own save through storage");

  Scene scene(&window, &camera);
  game.DispatchDrawables(&scene);
  mock.canvasDrawn = false;
  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();
  testTrue(counters,
           mock.canvasDrawn && renderer.frameError().empty(),
           "Guest canvas reaches the native backend");
  testTrue(counters,
           !historyContains(console, "Frame dropped"),
           "Every menu, setup and canvas frame recorded without rejection");
  testTrue(counters,
           game.error().empty() && game.OnCloseRequested(),
           "Guest accepts a host close request");
  game.Exit();

  // 3D render test mode: persisted guest settings enable it. SceneGraph and
  // MeshVisual run in the guest; the host re-runs the shared shadow pass from
  // the frame's casters and world camera, then draws the lit meshes.
  std::ofstream(files.storage / "envvars.json", std::ios::binary)
    << "{\n \"render3dTest\": \"1\"\n}\n";
  mock.litShader = renderer.getStyle(RenderStyleId::LitMesh)->shaderHandle;
  WasmGameModule world(
    readBytes(ILLUMO_GAME_GUEST), {}, gameLimits(), {}, {}, files);
  testTrue(counters,
           world.Start(&context) && enterCanvas(world, commands, input),
           "The 3D test mode starts from persisted guest settings");
  int frames = 0;
  pumpUntil(world, [&]() { return ++frames > 30; });
  mock.shadowDraws = 0;
  mock.litDraws = 0;
  for (int pass = 0; pass < 2; ++pass) {
    world.Update(1.0 / 60.0);
    Scene worldScene(&window, &camera);
    world.DispatchDrawables(&worldScene);
    renderer.BeginFrame();
    renderer.RenderScene(&worldScene, &camera);
    renderer.EndFrame();
  }
  std::printf("3D frame: %zu shadow draws, %zu lit draws\n",
              mock.shadowDraws,
              mock.litDraws);
  testTrue(counters,
           mock.shadowDraws > 0 && renderer.frameError().empty(),
           "Guest lit meshes cast into the host's shared shadow pass");
  testTrue(counters,
           mock.litDraws > 0,
           "Guest lit meshes draw through the host LitMesh style");
  testTrue(counters,
           world.error().empty() && !historyContains(console, "Frame dropped"),
           "Every 3D frame records without rejection");
  world.Exit();
  if (counters.failures != 0) {
    for (const CommandLine::historyBuffer& entry : console.getHistory()) {
      std::printf("console: %s\n", entry.content.c_str());
    }
  }

  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  if (counters.failures == 0) {
    std::puts("IllumoGame package menu, edit, step, save and load: PASS");
  }
  return counters.failures == 0;
}

int
main(int argc, char** argv)
{
  if (argc == 2 && std::string(argv[1]) == "--list") {
    std::puts("IllumoGame.Wasm.GamePackage");
    return 0;
  }
  if (argc != 3 || std::string(argv[1]) != "--run" ||
      std::string(argv[2]) != "IllumoGame.Wasm.GamePackage") {
    return 2;
  }
  return gamePackage() ? 0 : 1;
}
