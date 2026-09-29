#include "../BenchWorlds.h"
#include "Game/CSimSounds.h"
#include "Game/IllumoCodec.h"
#include "Rulesets/RuleSetRegistry.h"
#include "Wasm/CatalogBootstrap.h"
#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Foundation/ParseNumber.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/WorldLook.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Testing/AudioFixtures.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/WasmFileServices.h>
#include <Illumo/Wasm/WasmProgram.h>
#include <IllumoGuest/Audio.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <new>
#include <thread>
#ifdef _WIN32
#include <malloc.h>
#endif

// Host heap allocations made by the counting thread (the control frame) while
// a bounded check runs. Guest allocations stay inside WASM linear memory and
// lane workers run on other threads, so neither is counted here.
static thread_local bool g_countAllocations = false;
static thread_local std::size_t g_allocations = 0;
void*
operator new(std::size_t size)
{
  if (g_countAllocations) {
    ++g_allocations;
  }
  void* pointer = std::malloc(size == 0 ? 1 : size);
  if (pointer == nullptr) {
    std::abort(); // allocation failure is fatal (no exceptions)
  }
  return pointer;
}
void*
operator new[](std::size_t size)
{
  return ::operator new(size);
}
void
operator delete(void* pointer) noexcept
{
  std::free(pointer);
}
void
operator delete[](void* pointer) noexcept
{
  std::free(pointer);
}
void
operator delete(void* pointer, std::size_t) noexcept
{
  std::free(pointer);
}
void
operator delete[](void* pointer, std::size_t) noexcept
{
  std::free(pointer);
}
void*
operator new(std::size_t size, const std::nothrow_t&) noexcept
{
  if (g_countAllocations) {
    ++g_allocations;
  }
  return std::malloc(size == 0 ? 1 : size);
}
void*
operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
  return ::operator new(size, std::nothrow);
}
void
operator delete(void* pointer, const std::nothrow_t&) noexcept
{
  ::operator delete(pointer);
}
void
operator delete[](void* pointer, const std::nothrow_t&) noexcept
{
  ::operator delete[](pointer);
}
void*
operator new(std::size_t size, std::align_val_t alignment)
{
  if (g_countAllocations) {
    ++g_allocations;
  }
  const std::size_t align = static_cast<std::size_t>(alignment);
#ifdef _WIN32
  void* pointer = _aligned_malloc(size == 0 ? 1 : size, align);
#else
  const std::size_t rounded =
    ((size == 0 ? 1 : size) + align - 1) / align * align;
  void* pointer = std::aligned_alloc(align, rounded);
#endif
  if (pointer == nullptr) {
    std::abort(); // allocation failure is fatal (no exceptions)
  }
  return pointer;
}
void*
operator new[](std::size_t size, std::align_val_t alignment)
{
  return ::operator new(size, alignment);
}
void
operator delete(void* pointer, std::align_val_t) noexcept
{
#ifdef _WIN32
  _aligned_free(pointer);
#else
  std::free(pointer);
#endif
}
void
operator delete[](void* pointer, std::align_val_t alignment) noexcept
{
  ::operator delete(pointer, alignment);
}
void
operator delete(void* pointer, std::size_t, std::align_val_t alignment) noexcept
{
  ::operator delete(pointer, alignment);
}
void
operator delete[](void* pointer,
                  std::size_t,
                  std::align_val_t alignment) noexcept
{
  ::operator delete(pointer, alignment);
}

// The complete IllumoGame product runs only as IllumoGame.wasm. This native
// side is the generic host, a scripted console/keyboard, and a reference
// oracle that decodes the guest's save; it links no product modules.

// The guest canvas is the texture bound when a batch sets the canvas fade.
class CanvasObservingBackend final : public MockBackend
{
public:
  bool canvasDrawn = false;
  // 3D test mode: draws inside the shared shadow pass, lit-mesh draws, and
  // instanced draws of the host render world (inside executed lists).
  ShaderHandle litShader{};
  ShaderHandle worldShader{};
  std::size_t shadowDraws = 0;
  std::size_t litDraws = 0;
  std::size_t worldDraws = 0;
  TextureHandle CreateTexture(const unsigned char* data,
                              int width,
                              int height,
                              int channels,
                              const TextureOptions& options) override
  {
    return MockBackend::CreateTexture(data, width, height, channels, options);
  }
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
    } else if (command.commandType == CommandType::SetUniformVec3 &&
               std::strcmp(command.uniformVec3.name,
                           WorldLook::kCanvasFadeUniform) == 0) {
      // Only a canvas batch sets the fade, after binding its texture.
      m_canvas = m_bound;
    } else if (command.commandType == CommandType::SetShader) {
      m_shader = command.bindShader.handle;
    } else if (command.commandType == CommandType::SetFramebuffer) {
      m_offscreen = command.bindFramebuffer.handle.isValid();
    } else if (command.commandType == CommandType::DrawIndexedInstanced) {
      if (m_offscreen) {
        ++shadowDraws;
      } else if (m_shader == worldShader) {
        ++worldDraws;
      }
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
  }

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
// As shipped, the package is epoch-metered; benchmarks also measure fuel.
static WasmLimits
gameLimits(bool meterFuel = false)
{
  WasmLimits limits;
  limits.memoryBytes = 512ull * 1024ull * 1024ull;
  limits.meterFuel = meterFuel;
  limits.fuelPerCall = 20000000000ull;
  limits.deadlineMilliseconds = 10000;
  return limits;
}

static bool
pumpUntil(WasmProgram& game, const std::function<bool()>& reached)
{
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (game.error().empty() && std::chrono::steady_clock::now() < deadline) {
    game.update(1.0 / 60.0);
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
enterCanvas(WasmProgram& game, CommandRegistry& commands, InputManager& input)
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
  std::filesystem::create_directories(files.package / "Scenes");
  std::filesystem::copy_file(ILLUMO_RENDER3D_SCENE,
                             files.package / "Scenes" / "render3d-test.ilsc");
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

  WasmProgram game(
    readBytes(ILLUMO_GAME_GUEST), {}, gameLimits(), {}, {}, files);
  const bool started = game.start(context);
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

  execute(commands, "clear_canvas", { "yes" });
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

  execute(commands, "clear_canvas", { "yes" });
  execute(commands, "load", { "smoke" });
  testTrue(counters,
           pumpUntil(game,
                     [&]() {
                       return historyContains(console,
                                              "Loaded canvas from smoke.csim");
                     }),
           "Guest reloads its own save through storage");

  DrawList scene(&window, &camera);
  game.dispatch(scene);
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
           game.error().empty() && game.closeRequested(),
           "Guest accepts a host close request");
  game.stop();

  // 3D render test mode: persisted guest settings enable it. The scene's
  // meshes are host render world instances (HostRender, frame schema v6):
  // the host fits the shared shadow pass to them and draws them instanced.
  std::ofstream(files.storage / "envvars.json", std::ios::binary)
    << "{\n \"render3dTest\": \"1\"\n}\n";
  mock.litShader = renderer.getStyle(RenderStyleId::LitMesh)->shaderHandle;
  mock.worldShader =
    renderer.getStyle(RenderStyleId::LitMeshInstanced)->shaderHandle;
  WasmProgram world(
    readBytes(ILLUMO_GAME_GUEST), {}, gameLimits(), {}, {}, files);
  testTrue(counters,
           world.start(context) && enterCanvas(world, commands, input),
           "The 3D test mode starts from persisted guest settings");
  int frames = 0;
  pumpUntil(world, [&]() { return ++frames > 30; });
  mock.shadowDraws = 0;
  mock.litDraws = 0;
  mock.worldDraws = 0;
  for (int pass = 0; pass < 2; ++pass) {
    world.update(1.0 / 60.0);
    DrawList worldScene(&window, &camera);
    world.dispatch(worldScene);
    renderer.BeginFrame();
    renderer.RenderScene(&worldScene, &camera);
    renderer.EndFrame();
  }
  std::printf("3D frame: %zu shadow draws, %zu lit draws, %zu world draws\n",
              mock.shadowDraws,
              mock.litDraws,
              mock.worldDraws);
  testTrue(counters,
           mock.shadowDraws > 0 && renderer.frameError().empty(),
           "Scene meshes cast into the host's shared shadow pass");
  testTrue(counters,
           mock.worldDraws > 0 && mock.litDraws == 0,
           "Scene meshes draw instanced from the host render world, not as "
           "guest batches");
  testTrue(counters,
           world.error().empty() && !historyContains(console, "Frame dropped"),
           "Every 3D frame records without rejection");
  world.stop();
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

// Writes the shared benchmark worlds as v4 saves named <world>.csim.
static bool
writeBenchWorlds(const std::filesystem::path& directory)
{
  // The codec validates rule identity against the loaded catalogs.
  RuleSetRegistry registry;
  if (!registry.loadFromCatalogTexts(readText(ILLUMO_FAMILIES),
                                     readText(ILLUMO_RULES))) {
    return false;
  }
  RuleSetRegistry::instance() = registry;
  for (const BenchWorld& world : kBenchWorlds) {
    SparseCellGrid grid;
    seedBenchWorld(grid, world);
    IllumoDocument document;
    document.familyString = "LIFE_LIKE_BINARY";
    document.ruleString = "GAME_OF_LIFE";
    document.sourceGrid = &grid;
    const std::filesystem::path path =
      directory / (std::string(world.name) + ".csim");
    std::string error;
    if (!IllumoCodec::writeFile(path.string(), document, &error)) {
      std::printf(
        "Cannot write %s: %s\n", path.string().c_str(), error.c_str());
      return false;
    }
  }
  return true;
}

static std::int64_t
lastGeneration(const CommandLine& console, std::size_t* reports = nullptr)
{
  std::int64_t generation = -1;
  std::size_t count = 0;
  for (const CommandLine::historyBuffer& entry : console.getHistory()) {
    if (entry.content.rfind("Generation: ", 0) == 0) {
      if (!parseInteger(entry.content.substr(12), &generation)) {
        generation = -1;
      }
      ++count;
    }
  }
  if (reports != nullptr) {
    *reports = count;
  }
  return generation;
}

// Guest commands run when the guest next polls its services, so a status
// report arrives a frame or two later; pump (with rendering) until it does.
static std::int64_t
requestGeneration(WasmProgram& game,
                  CommandRegistry& commands,
                  const CommandLine& console,
                  const std::function<void()>& frame)
{
  std::size_t before = 0;
  lastGeneration(console, &before);
  execute(commands, "status");
  for (int attempt = 0; attempt < 600 && game.error().empty(); ++attempt) {
    game.update(1.0 / 60.0);
    frame();
    std::size_t after = 0;
    const std::int64_t generation = lastGeneration(console, &after);
    if (after > before) {
      return generation;
    }
  }
  return -1;
}

static double
percentileOf(std::vector<double> samples, double fraction)
{
  if (samples.empty()) {
    return 0.0;
  }
  std::sort(samples.begin(), samples.end());
  return samples[static_cast<std::size_t>(
    fraction * static_cast<double>(samples.size() - 1))];
}

// End-to-end headless benchmark: the real package loads each shared world,
// runs uncapped (tps 1000) and is pumped with MockBackend rendering. Reports
// host Update time, achieved TPS and WASM exchange statistics per world. Not
// part of IllumoWorkspace; compare with IllumoGame.Sim.RunnerBench.
static bool
packageBench(bool meterFuel, std::uint32_t lanes)
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-bench-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  WasmFileRoots files{ root / "package", root / "storage" };
  std::filesystem::create_directories(files.package);
  std::filesystem::create_directories(files.storage);
  std::filesystem::copy_file(ILLUMO_FAMILIES, files.package / "families.json");
  std::filesystem::copy_file(ILLUMO_RULES, files.package / "rulesets.json");
  std::filesystem::create_directories(files.package / "Scenes");
  std::filesystem::copy_file(ILLUMO_RENDER3D_SCENE,
                             files.package / "Scenes" / "render3d-test.ilsc");
  std::filesystem::copy_file(ILLUMO_GAME_DEFAULTS,
                             files.package / "envvars.json");
  if (!writeBenchWorlds(files.storage)) {
    return false;
  }

  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  env.setVar("fullscreen", false);
  Camera camera(glm::vec2(0, 0), 1, &env);
  CanvasObservingBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  renderer.ensureBuiltinStyles();
  CommandRegistry commands;
  CommandLine console(&env, &commands, &window, &renderer, "Bench");
  Logger::setContext(&env, &console);
  InputManager input(nullptr);
  IllumoContext context;
  context.renderer = &renderer;
  context.window = &window;
  context.inputManager = &input;
  context.envVars = &env;
  context.commandRegistry = &commands;
  context.commandLine = &console;

  WasmProgram game(readBytes(ILLUMO_GAME_GUEST),
                   {},
                   gameLimits(meterFuel),
                   {},
                   lanes == 0u ? std::vector<std::byte>{}
                               : readBytes(ILLUMO_SIMULATION_WORKER),
                   files);
  WasmLimits laneLimits = gameLimits(meterFuel);
  laneLimits.fuelPerCall = 1000000000u;
  game.setWorkerLimits(laneLimits, lanes == 0u ? 1u : lanes);
  const bool started =
    game.start(context) && enterCanvas(game, commands, input);
  testTrue(counters, started, "Benchmark package reaches the canvas");
  if (!started) {
    std::printf("%s\n", game.error().c_str());
    return false;
  }
  const std::function<void()> frame = [&]() {
    DrawList scene(&window, &camera);
    game.dispatch(scene);
    renderer.BeginFrame();
    renderer.RenderScene(&scene, &camera);
    renderer.EndFrame();
  };
  for (const BenchWorld& world : kBenchWorlds) {
    const std::string loaded =
      std::string("Loaded canvas from ") + world.name + ".csim";
    execute(commands, "pause");
    execute(commands, "load", { world.name });
    const bool ready =
      pumpUntil(game, [&]() { return historyContains(console, loaded); });
    testTrue(counters, ready, "Benchmark world loads in the guest");
    if (!ready) {
      break;
    }
    execute(commands, "tps", { "1000" });
    execute(commands, "run");
    for (int warmup = 0; warmup < 20 && game.error().empty(); ++warmup) {
      game.update(1.0 / 60.0);
      frame();
    }
    // Lane stores compile in parallel while serial generations run; time
    // only once the runner reports lanes (at most 30 seconds).
    const std::chrono::steady_clock::time_point lanesDeadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (lanes != 0u && game.error().empty() &&
           std::chrono::steady_clock::now() < lanesDeadline) {
      requestGeneration(game, commands, console, frame);
      if (historyContains(console, "simulation lanes; round trip")) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const std::int64_t firstGeneration =
      requestGeneration(game, commands, console, frame);
    std::vector<double> updates;
    // At least 120 frames and two seconds: with lanes a frame no longer
    // waits for its generation, so frames alone would be a sub-round-trip
    // window.
    const std::size_t frames = 120;
    const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
    while (game.error().empty() && (updates.size() < frames ||
                                    std::chrono::steady_clock::now() - start <
                                      std::chrono::seconds(2))) {
      const std::chrono::steady_clock::time_point before =
        std::chrono::steady_clock::now();
      game.update(1.0 / 60.0);
      updates.push_back(std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - before)
                          .count());
      frame();
    }
    const double frameSeconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
        .count();
    const std::int64_t lastReported =
      requestGeneration(game, commands, console, frame);
    // TPS spans both status round trips, which also advance generations.
    const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
        .count();
    const std::int64_t generations =
      firstGeneration >= 0 && lastReported >= firstGeneration
        ? lastReported - firstGeneration
        : 0;
    const WasmFrameStats& stats = game.stats();
    const WasmFrameCounters* frameCounters = game.frameCounters();
    std::printf(
      "BENCH-JSON {\"bench\":\"wasm-package\",\"world\":\"%s\","
      "\"metering\":\"%s\",\"lanes\":%u,\"frames\":%zu,\"fps\":%.2f,"
      "\"tps\":%.2f,"
      "\"updateMsP50\":%.3f,\"updateMsP95\":%.3f,\"updateMsMax\":%.3f,"
      "\"guestUpdateMsP50\":%.3f,\"guestFrameMsP50\":%.3f,"
      "\"acceptMsP50\":%.3f,\"frameBytesP50\":%.0f,"
      "\"textureWriteBytes\":%llu,\"inlineVertexBytes\":%llu}\n",
      world.name,
      meterFuel ? "fuel" : "epoch",
      lanes,
      updates.size(),
      frameSeconds > 0.0 ? static_cast<double>(updates.size()) / frameSeconds
                         : 0.0,
      seconds > 0.0 ? static_cast<double>(generations) / seconds : 0.0,
      percentileOf(updates, 0.5),
      percentileOf(updates, 0.95),
      percentileOf(updates, 1.0),
      stats.updateMilliseconds.median(),
      stats.frameMilliseconds.median(),
      stats.acceptMilliseconds.median(),
      stats.frameBytes.median(),
      static_cast<unsigned long long>(
        frameCounters != nullptr ? frameCounters->textureWriteBytes : 0u),
      static_cast<unsigned long long>(
        frameCounters != nullptr ? frameCounters->inlineVertexBytes : 0u));
    // The guest's own stage split from the status report just requested.
    std::string stages;
    std::string execution;
    for (const CommandLine::historyBuffer& entry : console.getHistory()) {
      if (entry.content.rfind("Worker stages", 0) == 0 ||
          entry.content.rfind("Presentation:", 0) == 0) {
        stages = entry.content.rfind("Worker stages", 0) == 0
                   ? entry.content
                   : stages + " | " + entry.content;
      } else if (entry.content.rfind("Execution:", 0) == 0) {
        execution = entry.content;
      }
    }
    // Frames that did real work (publication, presentation, generation).
    std::size_t heavyFrames = 0;
    double heavyMilliseconds = 0.0;
    double totalMilliseconds = 0.0;
    for (double update : updates) {
      totalMilliseconds += update;
      if (update > 1.0) {
        ++heavyFrames;
        heavyMilliseconds += update;
      }
    }
    std::printf("BENCH-LOAD %s lanes=%u updates=%zu totalMs=%.1f heavy=%zu "
                "heavyMeanMs=%.3f\n",
                world.name,
                lanes,
                updates.size(),
                totalMilliseconds,
                heavyFrames,
                heavyFrames == 0 ? 0.0 : heavyMilliseconds / heavyFrames);
    std::printf("BENCH-STAGES %s lanes=%u %s\nBENCH-EXEC %s lanes=%u %s\n",
                world.name,
                lanes,
                stages.c_str(),
                world.name,
                lanes,
                execution.c_str());
    testTrue(counters,
             game.error().empty() && generations > 0,
             "Benchmark world advances in the guest");
  }
  game.stop();
  if (counters.failures != 0) {
    for (const CommandLine::historyBuffer& entry : console.getHistory()) {
      std::printf("console: %s\n", entry.content.c_str());
    }
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return counters.failures == 0;
}

// The shipped configuration end to end: the real package with real WASM
// simulation lanes runs a dense world, pauses (retiring any in-flight or
// speculative generation) and saves; the save equals a native serial
// reference advanced the same number of generations.
static bool
gamePackageLanes()
{
  TestCounters counters;
  RuleSetRegistry registry;
  if (!registry.loadFromCatalogTexts(readText(ILLUMO_FAMILIES),
                                     readText(ILLUMO_RULES))) {
    return false;
  }
  RuleSetRegistry::instance() = registry;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-lanes-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  WasmFileRoots files{ root / "package", root / "storage" };
  std::filesystem::create_directories(files.package);
  std::filesystem::create_directories(files.storage);
  std::filesystem::copy_file(ILLUMO_FAMILIES, files.package / "families.json");
  std::filesystem::copy_file(ILLUMO_RULES, files.package / "rulesets.json");
  std::filesystem::create_directories(files.package / "Scenes");
  std::filesystem::copy_file(ILLUMO_RENDER3D_SCENE,
                             files.package / "Scenes" / "render3d-test.ilsc");
  std::filesystem::copy_file(ILLUMO_GAME_DEFAULTS,
                             files.package / "envvars.json");
  if (!writeBenchWorlds(files.storage)) {
    return false;
  }
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  env.setVar("fullscreen", false);
  Camera camera(glm::vec2(0, 0), 1, &env);
  CanvasObservingBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  renderer.ensureBuiltinStyles();
  CommandRegistry commands;
  CommandLine console(&env, &commands, &window, &renderer, "Lanes");
  Logger::setContext(&env, &console);
  InputManager input(nullptr);
  IllumoContext context;
  context.renderer = &renderer;
  context.window = &window;
  context.inputManager = &input;
  context.envVars = &env;
  context.commandRegistry = &commands;
  context.commandLine = &console;
  WasmProgram game(readBytes(ILLUMO_GAME_GUEST),
                   {},
                   gameLimits(),
                   {},
                   readBytes(ILLUMO_SIMULATION_WORKER),
                   files);
  WasmLimits laneLimits = gameLimits();
  game.setWorkerLimits(laneLimits, 4u);
  const std::function<void()> frame = [&]() {
    DrawList scene(&window, &camera);
    game.dispatch(scene);
    renderer.BeginFrame();
    renderer.RenderScene(&scene, &camera);
    renderer.EndFrame();
  };
  testTrue(counters,
           game.start(context) && enterCanvas(game, commands, input),
           "Package with simulation lanes reaches the canvas");
  execute(commands, "load", { "bench-dense32" });
  testTrue(counters,
           pumpUntil(game,
                     [&]() {
                       return historyContains(
                         console, "Loaded canvas from bench-dense32.csim");
                     }),
           "Dense world loads");
  execute(commands, "tps", { "1000" });
  execute(commands, "run");
  bool lanesActive = false;
  std::int64_t generation = 0;
  const std::chrono::steady_clock::time_point deadline =
    std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (game.error().empty() && std::chrono::steady_clock::now() < deadline &&
         !(lanesActive && generation >= 60)) {
    for (int step = 0; step < 20; ++step) {
      game.update(1.0 / 60.0);
      frame();
    }
    generation = requestGeneration(game, commands, console, frame);
    lanesActive = historyContains(console, "simulation lanes; round trip");
  }
  testTrue(counters,
           lanesActive && generation >= 60,
           "Generations run on simulation lanes");
  execute(commands, "pause");
  execute(commands, "save", { "lanes" });
  const std::filesystem::path saved = files.storage / "lanes.csim";
  testTrue(counters,
           pumpUntil(game,
                     [&]() {
                       return historyContains(console,
                                              "Saved canvas to lanes.csim");
                     }),
           "The paused lane world saves");
  const std::int64_t published =
    requestGeneration(game, commands, console, frame);
  IllumoDocument document;
  std::string error;
  std::ifstream stream(saved, std::ios::binary);
  const bool decoded = IllumoCodec::readStream(stream, &document, &error);
  std::unique_ptr<RuleSet> rule = registry.createRuleSet("GAME_OF_LIFE");
  SparseCellGrid reference;
  seedBenchWorld(reference, kBenchWorlds[0]);
  for (std::int64_t step = 0; rule && step < published; ++step) {
    reference.advance(*rule);
  }
  testTrue(counters,
           decoded && document.grid && published > 0 &&
             worldHash(*document.grid) == worldHash(reference),
           "Lane generations equal the native serial reference");
  std::printf("Lanes: %lld generations published\n",
              static_cast<long long>(published));
  testTrue(counters,
           game.error().empty() && !historyContains(console, "Frame dropped"),
           "Every lane frame recorded without rejection");
  game.stop();
  if (counters.failures != 0) {
    for (const CommandLine::historyBuffer& entry : console.getHistory()) {
      std::printf("console: %s\n", entry.content.c_str());
    }
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return counters.failures == 0;
}

// The catalog bootstrap against the host file service: package catalogs
// under /packages/<id>/csim merge between the packaged pair and the user
// overlays; an invalid one is skipped with a warning.
static bool
catalogMerge()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-catalogs-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root / "app");
  std::filesystem::create_directories(root / "storage");
  std::filesystem::create_directories(root / "extra" / "csim");
  std::filesystem::create_directories(root / "broken" / "csim");
  std::filesystem::copy_file(ILLUMO_FAMILIES, root / "app" / "families.json");
  std::filesystem::copy_file(ILLUMO_RULES, root / "app" / "rulesets.json");
  std::ofstream(root / "extra" / "csim" / "rulesets.json")
    << R"({"schema_version":3,"rules":[{"id":"PACKAGE_LIFE","name":"Package Life",)"
       R"("family_id":"LIFE_LIKE_BINARY","birth":[3,6],"survive":[1,2,3]}]})";
  std::ofstream(root / "broken" / "csim" / "rulesets.json") << "{ not json";
  // The player's overlay still wins over a package's definition.
  std::ofstream(root / "storage" / "rulesets.user.json")
    << R"({"schema_version":3,"rules":[{"id":"PACKAGE_LIFE","name":"Player Life",)"
       R"("family_id":"LIFE_LIKE_BINARY","birth":[3],"survive":[2,3]}]})";

  std::shared_ptr<VirtualFileSystem> tree =
    std::make_shared<VirtualFileSystem>();
  std::string error;
  const char* names[] = { "extra", "broken" };
  VfsMount app;
  app.point = "/app";
  app.layers.push_back(
    { DirectoryVfsBackend::open(root / "app", false, error), "csim" });
  tree->mount(app, error);
  for (const char* name : names) {
    VfsMount mount;
    mount.point = std::string("/packages/") + name;
    mount.layers.push_back(
      { DirectoryVfsBackend::open(root / name, false, error), name });
    tree->mount(mount, error);
  }
  WasmFileServices service(
    9,
    static_cast<std::uint32_t>(GuestCapability::Assets) |
      static_cast<std::uint32_t>(GuestCapability::Storage),
    tree,
    root / "storage");
  GuestServiceQueue queue;
  GuestFiles files(queue);
  CSimCatalogBootstrap bootstrap(files);
  for (int step = 0; step < 5000 && !bootstrap.ready() && !bootstrap.failed();
       ++step) {
    GuestServices completions = service.poll();
    GuestWireWriter writer;
    completions.write(writer);
    std::vector<std::byte> outgoing;
    GuestServices requests;
    if (!queue.exchange(writer.data(), outgoing) ||
        !GuestServices::read(outgoing, requests, true) ||
        (!requests.records.empty() && !service.submit(requests))) {
      testTrue(counters, false, "service exchange");
      break;
    }
    files.pump();
    bootstrap.pump();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  testTrue(counters,
           bootstrap.ready(),
           "the bootstrap completes with packages mounted");
  const RuleSetDefinition* merged =
    bootstrap.registry().getRuleSetDefinition("PACKAGE_LIFE");
  testTrue(counters,
           merged != nullptr && bootstrap.registry().getRuleSetDefinition(
                                  "GAME_OF_LIFE") != nullptr,
           "a package rule set joins the packaged catalog");
  testTrue(counters,
           merged != nullptr && merged->name == "Player Life",
           "the user overlay applies after package catalogs");
  testTrue(counters,
           bootstrap.merged().size() == 1 &&
             bootstrap.merged().front() == "/packages/extra/csim/rulesets.json",
           "the merge reports what it took");
  testTrue(counters,
           bootstrap.warnings().size() == 1 &&
             bootstrap.warnings().front().find("broken") != std::string::npos,
           "an invalid package catalog is skipped with a warning");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  if (counters.failures != 0) {
    std::printf("catalog error: %s\n", bootstrap.error().c_str());
  }
  return counters.failures == 0;
}

// The package's sound cues through the generic host: the guest decodes its
// own Sounds/*.wav, registers them over the Audio service and plays cues as
// the player moves through the menus and into and out of the canvas.
static bool
gamePackageAudio()
{
  TestCounters counters;
  RuleSetRegistry registry;
  if (!registry.loadFromCatalogTexts(readText(ILLUMO_FAMILIES),
                                     readText(ILLUMO_RULES))) {
    return false;
  }
  RuleSetRegistry::instance() = registry;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-game-audio-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  WasmFileRoots files{ root / "package", root / "storage" };
  std::filesystem::create_directories(files.package / "Sounds");
  std::filesystem::create_directories(files.package / "Music");
  std::filesystem::create_directories(files.storage);
  std::filesystem::copy_file(ILLUMO_FAMILIES, files.package / "families.json");
  std::filesystem::copy_file(ILLUMO_RULES, files.package / "rulesets.json");
  std::filesystem::create_directories(files.package / "Scenes");
  std::filesystem::copy_file(ILLUMO_RENDER3D_SCENE,
                             files.package / "Scenes" / "render3d-test.ilsc");
  std::filesystem::copy_file(ILLUMO_GAME_DEFAULTS,
                             files.package / "envvars.json");
  // Each cue's file has its own length, which identifies it on the host. The
  // menu music is longer than one upload chunk, so it reaches the host in
  // several Audio records over several exchanges.
  const std::vector<std::string> sounds = CSimSounds::fileNames();
  const std::string music = CSimSounds::fileName(CSimMusic::MainMenu);
  const std::size_t musicFrames =
    GuestAudioRequest::MaximumChunkSamples + 4410u;
  for (std::size_t cue = 0; cue < sounds.size(); ++cue) {
    const std::size_t frames =
      sounds[cue] == music ? musicFrames : 100u * (cue + 1u);
    const std::vector<std::byte> wav =
      makeWav(makeTone(frames, 1, 22050), 1, 22050);
    std::ofstream(files.package / sounds[cue], std::ios::binary)
      .write(reinterpret_cast<const char*>(wav.data()),
             static_cast<std::streamsize>(wav.size()));
  }

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
  Logger::setContext(&env, &console);
  InputManager input(nullptr);
  IllumoContext context;
  context.renderer = &renderer;
  context.window = &window;
  context.inputManager = &input;
  context.envVars = &env;
  context.commandRegistry = &commands;
  context.commandLine = &console;

  RecordingAudio audio;
  // Plays of one cue, identified by its registered clip's length.
  const std::function<std::size_t(CSimSound)> plays = [&](CSimSound cue) {
    const std::size_t frames = 100u * (static_cast<std::size_t>(cue) + 1u);
    std::size_t count = 0;
    for (const RecordingAudio::Play& play : audio.plays) {
      count += play.sound.slot >= 1 && play.sound.slot <= audio.clips.size() &&
                   audio.clips[play.sound.slot - 1].frames() == frames
                 ? 1u
                 : 0u;
    }
    return count;
  };
  // The canvas edit music is short, identified by its length as a cue is.
  const std::size_t editFrames =
    100u *
    (static_cast<std::size_t>(
       std::find(sounds.begin(),
                 sounds.end(),
                 CSimSounds::fileName(CSimMusic::CanvasEdit)) -
       sounds.begin()) +
     1u);
  // A track's registered clip, looping plays of it and fading stops of it.
  const std::function<SoundHandle(std::size_t)> musicSound =
    [&](std::size_t frames) {
      for (std::size_t index = 0; index < audio.clips.size(); ++index) {
        if (audio.clips[index].frames() == frames) {
          return SoundHandle{ static_cast<std::uint32_t>(index + 1u), 1u };
        }
      }
      return SoundHandle{};
    };
  const std::function<std::size_t(std::size_t)> musicPlays =
    [&](std::size_t frames) {
      std::size_t count = 0;
      for (const RecordingAudio::Play& play : audio.plays) {
        count += play.sound == musicSound(frames) && play.playback.loop &&
                     play.playback.fadeInSeconds > 0.0f &&
                     std::abs(play.playback.volume - 0.45f * 0.8f) < 1e-4f
                   ? 1u
                   : 0u;
      }
      return count;
    };
  const std::function<std::size_t(std::size_t)> musicStops =
    [&](std::size_t frames) {
      std::size_t count = 0;
      for (const RecordingAudio::Stop& stop : audio.soundStops) {
        count +=
          stop.sound == musicSound(frames) && stop.fadeSeconds > 0.0f ? 1u : 0u;
      }
      return count;
    };

  WasmProgram game(
    readBytes(ILLUMO_GAME_GUEST), {}, gameLimits(), {}, {}, files);
  game.setAudio(&audio);
  testTrue(counters, game.start(context), "the package starts with audio");
  testTrue(counters,
           pumpUntil(game, [&]() { return commands.HasCommand("play"); }),
           "the main menu comes up");
  // The music's later chunks, and the plays queued behind them, follow in
  // the next exchanges.
  testTrue(counters,
           pumpUntil(game, [&]() { return musicPlays(musicFrames) == 1; }),
           "the main menu loops its music, fading in at the music level");
  testTrue(counters,
           audio.clips.size() == sounds.size() &&
             audio.clips[musicSound(musicFrames).slot - 1u].samples.size() ==
               musicFrames,
           "every packaged sound is decoded in the guest and registered, the "
           "music whole across several upload chunks");
  testTrue(counters,
           plays(CSimSound::ProgramStart) == 1,
           "the start cue plays with the first screen");
  const std::size_t hoversBefore = plays(CSimSound::MenuHover);
  input.getKeyQueue().push({ KeyCode::Down, InputAction::Press, 0 });
  int frame = 0;
  pumpUntil(game, [&]() { return ++frame > 3; });
  testTrue(counters,
           plays(CSimSound::MenuHover) == hoversBefore + 1,
           "moving the menu selection plays the hover cue");
  bool quiet = false;
  for (const RecordingAudio::Play& play : audio.plays) {
    if (audio.clips[play.sound.slot - 1].frames() == 200u) {
      quiet = std::abs(play.playback.volume - 0.35f * 0.8f) < 1e-4f;
    }
  }
  testTrue(counters, quiet, "the packaged 80% volume scales the cue mix");
  input.getKeyQueue().push({ KeyCode::Up, InputAction::Press, 0 });
  testTrue(counters,
           enterCanvas(game, commands, input) &&
             plays(CSimSound::CanvasEnter) == 1,
           "entering the canvas plays the enter cue");
  testTrue(counters,
           musicStops(musicFrames) == 1 && musicPlays(musicFrames) == 1,
           "leaving the main menu fades its music out");
  testTrue(counters,
           musicPlays(editFrames) == 1 && musicStops(editFrames) == 0,
           "the canvas opens in EDIT and loops the edit music");
  execute(commands, "run");
  frame = 0;
  pumpUntil(game, [&]() { return ++frame > 3; });
  testTrue(counters,
           plays(CSimSound::CanvasModeSwitch) == 1,
           "switching the canvas to NORMAL plays the mode cue");
  testTrue(counters,
           musicStops(editFrames) == 1,
           "running the canvas fades the edit music out");
  execute(commands, "pause");
  frame = 0;
  pumpUntil(game, [&]() { return ++frame > 3; });
  testTrue(counters,
           musicPlays(editFrames) == 2,
           "pausing back into EDIT starts the edit music again");
  execute(commands, "menu");
  testTrue(counters,
           pumpUntil(game, [&]() { return commands.HasCommand("play"); }) &&
             plays(CSimSound::CanvasExit) == 1,
           "returning to the menu plays the exit cue");
  frame = 0;
  pumpUntil(game, [&]() { return ++frame > 3; });
  testTrue(counters,
           musicPlays(musicFrames) == 2 && musicStops(editFrames) == 2,
           "the edit music fades out and the menu music starts over when the "
           "main menu returns");
  game.stop();
  testTrue(counters,
           audio.destroyed.size() == sounds.size() && audio.stops >= 1,
           "the host releases the guest's sounds when it exits");
  Logger::setContext(nullptr, nullptr);
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return counters.failures == 0;
}

// Changes MSAA in the real package's settings, answers its restart prompt
// with Y, and reports what the host saw. `allowed` is the host's policy
// (setRestartAllowed): off, the request is only a close.
static bool
restartThroughSettings(bool allowed, TestCounters& counters)
{
  RuleSetRegistry registry;
  if (!registry.loadFromCatalogTexts(readText(ILLUMO_FAMILIES),
                                     readText(ILLUMO_RULES))) {
    return false;
  }
  RuleSetRegistry::instance() = registry;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-game-restart-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  WasmFileRoots files{ root / "package", root / "storage" };
  std::filesystem::create_directories(files.package / "Scenes");
  std::filesystem::create_directories(files.storage);
  std::filesystem::copy_file(ILLUMO_FAMILIES, files.package / "families.json");
  std::filesystem::copy_file(ILLUMO_RULES, files.package / "rulesets.json");
  std::filesystem::copy_file(ILLUMO_RENDER3D_SCENE,
                             files.package / "Scenes" / "render3d-test.ilsc");
  std::filesystem::copy_file(ILLUMO_GAME_DEFAULTS,
                             files.package / "envvars.json");

  NullRenderWindow window(640, 480);
  // The running window was created with 4x MSAA.
  window.msaaSamples = 4;
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  env.setVar("fullscreen", false);
  env.setVar("msaa", 4);
  Camera camera(glm::vec2(0, 0), 1, &env);
  CanvasObservingBackend mock;
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

  WasmProgram game(
    readBytes(ILLUMO_GAME_GUEST), {}, gameLimits(), {}, {}, files);
  game.setRestartAllowed(allowed);
  bool reached = game.start(context) &&
                 pumpUntil(game, [&]() { return commands.HasCommand("play"); });
  const std::function<void(KeyCode)> press = [&](KeyCode key) {
    input.getKeyQueue().push({ key, InputAction::Press, 0 });
    int frames = 0;
    pumpUntil(game, [&]() { return ++frames > 3; });
  };
  // Settings: VIDEO (third tab), Anti-aliasing (fourth row) 4x -> 8x, Apply.
  if (reached) {
    press(KeyCode::F1);
    press(KeyCode::Tab);
    press(KeyCode::Tab);
    for (int row = 0; row < 3; ++row) {
      press(KeyCode::Down);
    }
    press(KeyCode::Right);
    for (int row = 0; row < 8; ++row) {
      press(KeyCode::Down);
    }
    press(KeyCode::Enter);
    reached =
      pumpUntil(game, [&]() { return env.getVar("msaa").valueAsLong == 8; });
  }
  testTrue(counters,
           reached && !window.closeRequested && !window.restartRequested(),
           "applying a new MSAA saves it for the next window without closing");
  press(KeyCode::Y);
  const bool closing = pumpUntil(game, [&]() { return window.closeRequested; });
  if (allowed) {
    testTrue(counters,
             closing && window.restartRequested(),
             "answering the restart prompt asks the host to relaunch");
  } else {
    testTrue(counters,
             closing && !window.restartRequested(),
             "a host that disallows restarts only closes");
  }
  game.stop();
  Logger::setContext(nullptr, nullptr);
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return true;
}

// Warmed control frames of the real package allocate nothing on the host:
// the guest exchanges, frame decode/accept and service round trips reuse
// retained buffers (docs/plans/hot-loop-allocations.md). Measured paused and
// while generations run; each phase warms up first so first-use growth and
// lazily created resources are excluded.
static bool
packageFrameAllocations()
{
  TestCounters counters;
  const std::filesystem::path root =
    std::filesystem::temp_directory_path() /
    ("illumo-allocations-" +
     std::to_string(
       std::chrono::steady_clock::now().time_since_epoch().count()));
  WasmFileRoots files{ root / "package", root / "storage" };
  std::filesystem::create_directories(files.package);
  std::filesystem::create_directories(files.storage);
  std::filesystem::copy_file(ILLUMO_FAMILIES, files.package / "families.json");
  std::filesystem::copy_file(ILLUMO_RULES, files.package / "rulesets.json");
  std::filesystem::create_directories(files.package / "Scenes");
  std::filesystem::copy_file(ILLUMO_RENDER3D_SCENE,
                             files.package / "Scenes" / "render3d-test.ilsc");
  std::filesystem::copy_file(ILLUMO_GAME_DEFAULTS,
                             files.package / "envvars.json");
  if (!writeBenchWorlds(files.storage)) {
    return false;
  }
  // Scoped so the host and its settings close before the files are removed.
  {
    NullRenderWindow window(1280, 720);
    EnvVars env(root / "host-envvars.json");
    env.setVar("WinX", 1280);
    env.setVar("WinY", 720);
    env.setVar("fullscreen", false);
    Camera camera(glm::vec2(0, 0), 1, &env);
    CanvasObservingBackend mock;
    mock.Initialize();
    Renderer renderer(&window, &env, &camera, &mock, false);
    renderer.ensureBuiltinStyles();
    CommandRegistry commands;
    CommandLine console(&env, &commands, &window, &renderer, "Allocations");
    Logger::setContext(&env, &console);
    InputManager input(nullptr);
    IllumoContext context;
    context.renderer = &renderer;
    context.window = &window;
    context.inputManager = &input;
    context.envVars = &env;
    context.commandRegistry = &commands;
    context.commandLine = &console;
    WasmProgram game(
      readBytes(ILLUMO_GAME_GUEST), {}, gameLimits(false), {}, {}, files);
    const bool started =
      game.start(context) && enterCanvas(game, commands, input);
    testTrue(counters, started, "Allocation package reaches the canvas");
    if (!started) {
      std::printf("%s\n", game.error().c_str());
      return false;
    }
    const std::function<void()> frame = [&]() {
      DrawList scene(&window, &camera);
      game.dispatch(scene);
      renderer.BeginFrame();
      renderer.RenderScene(&scene, &camera);
      renderer.EndFrame();
    };
    const char* world = "bench-sparse64";
    const std::string loaded =
      std::string("Loaded canvas from ") + world + ".csim";
    execute(commands, "pause");
    execute(commands, "load", { world });
    const bool ready =
      pumpUntil(game, [&]() { return historyContains(console, loaded); });
    testTrue(counters, ready, "Allocation world loads in the guest");
    // Counts host allocations made by Update alone; the mock render stays out.
    const std::function<std::size_t(int)> measure = [&](int frames) {
      for (int warmup = 0; warmup < 120 && game.error().empty(); ++warmup) {
        game.update(1.0 / 60.0);
        frame();
      }
      std::size_t total = 0;
      for (int index = 0; index < frames && game.error().empty(); ++index) {
        g_allocations = 0;
        g_countAllocations = true;
        game.update(1.0 / 60.0);
        g_countAllocations = false;
        total += g_allocations;
        frame();
      }
      return total;
    };
    const std::size_t paused = ready ? measure(120) : 0u;
    execute(commands, "tps", { "60" });
    execute(commands, "run");
    const std::size_t running = ready ? measure(120) : 0u;
    std::printf("Host heap allocations over 120 warmed package frames: "
                "paused %zu, running %zu\n",
                paused,
                running);
    // Before retained exchange buffers this was about 88 per frame. Running
    // frames may still grow a retained texture-write buffer when the canvas
    // uploads a dirty rectangle larger than any seen so far; that high-water
    // growth is bounded, so allow fewer than one per ten frames.
    testTrue(counters, paused == 0, "Paused package frames allocate nothing");
    testTrue(counters,
             running < 12,
             "Running package frames allocate only on high-water growth");

    // Scene programs (D-E31): the canvas is kept on a trip to the title, the
    // title's first row resumes it, and its frames settle back to none.
    execute(commands, "menu");
    bool resumed = pumpUntil(game, [&]() {
      return commands.HasCommand("play") && !commands.HasCommand("pause");
    });
    if (resumed) {
      int frames = 0;
      pumpUntil(game, [&]() { return ++frames > 20; });
      input.getKeyQueue().push({ KeyCode::Enter, InputAction::Press, 0 });
      resumed = pumpUntil(game, [&]() { return commands.HasCommand("pause"); });
    }
    testTrue(counters, resumed, "The kept canvas resumes from the title");
    execute(commands, "pause");
    const std::size_t roundTrip = resumed ? measure(120) : 0u;
    std::printf("After a round trip to the title: %zu\n", roundTrip);
    testTrue(counters,
             roundTrip == 0,
             "Paused frames after a round trip to the title allocate nothing");
    testTrue(counters, game.error().empty(), "The package ran without error");
    game.stop();
    if (counters.failures != 0) {
      for (const CommandLine::historyBuffer& entry : console.getHistory()) {
        std::printf("console: %s\n", entry.content.c_str());
      }
    }
  }
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return counters.failures == 0;
}

static bool
gamePackageRestart()
{
  TestCounters counters;
  const bool allowed = restartThroughSettings(true, counters);
  const bool refused = restartThroughSettings(false, counters);
  return allowed && refused && counters.failures == 0;
}

int
main(int argc, char** argv)
{
  if (argc == 2 && std::string(argv[1]) == "--list") {
    std::puts("IllumoGame.Wasm.GamePackage");
    std::puts("IllumoGame.Wasm.GamePackageAudio");
    std::puts("IllumoGame.Wasm.GamePackageRestart");
    std::puts("IllumoGame.Wasm.CatalogMerge");
    std::puts("IllumoGame.Wasm.GamePackageLanes");
    std::puts("IllumoGame.Wasm.PackageBench");
    std::puts("IllumoGame.Wasm.PackageFrameAllocations");
    return 0;
  }
  if (argc == 3 && std::string(argv[1]) == "--write-bench-worlds") {
    std::filesystem::create_directories(argv[2]);
    return writeBenchWorlds(argv[2]) ? 0 : 1;
  }
  if (argc != 3 || std::string(argv[1]) != "--run") {
    return 2;
  }
  if (std::string(argv[2]) == "IllumoGame.Wasm.CatalogMerge") {
    return catalogMerge() ? 0 : 1;
  }
  if (std::string(argv[2]) == "IllumoGame.Wasm.GamePackage") {
    return gamePackage() ? 0 : 1;
  }
  if (std::string(argv[2]) == "IllumoGame.Wasm.GamePackageRestart") {
    return gamePackageRestart() ? 0 : 1;
  }
  if (std::string(argv[2]) == "IllumoGame.Wasm.GamePackageAudio") {
    return gamePackageAudio() ? 0 : 1;
  }
  if (std::string(argv[2]) == "IllumoGame.Wasm.GamePackageLanes") {
    return gamePackageLanes() ? 0 : 1;
  }
  if (std::string(argv[2]) == "IllumoGame.Wasm.PackageFrameAllocations") {
    return packageFrameAllocations() ? 0 : 1;
  }
  if (std::string(argv[2]) == "IllumoGame.Wasm.PackageBench") {
    // Serial generations in the game store, then as shipped (8 lanes).
    const bool serial = packageBench(false, 0u);
    const bool lanes = packageBench(false, 8u);
    return serial && lanes ? 0 : 1;
  }
  return 2;
}
