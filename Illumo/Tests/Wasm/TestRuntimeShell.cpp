#include <Illumo/Audio/AudioDevice.h>
#include <Illumo/Engine/Illumo.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/IllumoTestAccess.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/RuntimeShell.h>
#include <Illumo/Wasm/WasmModuleCache.h>
#include <Illumo/Wasm/WasmProgram.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

static std::vector<std::byte>
readGuest(const char* path)
{
  std::ifstream binary(path, std::ios::binary);
  const std::vector<char> characters{ std::istreambuf_iterator<char>(binary),
                                      {} };
  std::vector<std::byte> bytes(characters.size());
  std::memcpy(bytes.data(), characters.data(), characters.size());
  return bytes;
}

// A headless engine whose backend the test can inspect.
static void
setHeadlessFactories(Illumo& host, MockBackend** backend)
{
  IllumoTestAccess::setWindowFactory(
    host, [](int, int, const std::string&, IEnvVars*) {
      return std::make_unique<NullRenderWindow>(640, 480);
    });
  IllumoTestAccess::setBackendFactory(host, [backend](IRenderWindow*) {
    std::unique_ptr<MockBackend> created = std::make_unique<MockBackend>();
    *backend = created.get();
    return std::unique_ptr<IBackend>(std::move(created));
  });
}

// Whole frames, close negotiation and stop for a program that starts.
static void
runShell(TestCounters& counters,
         Illumo& host,
         MockBackend& backend,
         RuntimeShell& shell)
{
  testTrue(counters, shell.start(), "the program starts under the shell");
  const std::uint64_t started = shell.program().stats().updates;
  for (int frame = 0; frame < 5; ++frame) {
    shell.frame(1.0 / 60.0);
  }
  testTrue(counters,
           shell.program().stats().updates == started + 5 &&
             shell.program().error().empty(),
           "each frame updates the program once");
  testTrue(counters,
           backend.countNonEmptyOfType(CommandType::DrawIndexed) >= 1 &&
             host.context().renderer->frameError().empty(),
           "the program's geometry is rendered and presented");
  testTrue(counters, !shell.closing(), "no close request keeps running");
  host.context().window->requestClose();
  testTrue(counters,
           shell.closing(),
           "a close request the program accepts ends the run");
  shell.stop();
  testTrue(counters,
           shell.program().frameCounters() == nullptr && shell.exitCode() == 0,
           "stopping retires the program with a clean exit code");
  shell.stop();
}

// The runtime's frame around a real program (D-E31): the shell starts the
// program, runs whole frames through the engine's phases, negotiates close
// and stops it.
static bool
testRuntimeShell()
{
  TestCounters counters;
  testSection("RuntimeShell: frames, close and stop around a WASM program");
  const std::filesystem::path environment =
    std::filesystem::temp_directory_path() / "illumo-runtime-shell.json";
  std::error_code error;
  std::filesystem::remove(environment, error);
  {
    IllumoConfig config;
    config.applicationName = "ShellTest";
    config.environmentPath = environment.string();
    Illumo host(config);
    MockBackend* backend = nullptr;
    setHeadlessFactories(host, &backend);
    testTrue(counters, host.initialize(), "engine initializes");

    RuntimeShellOptions options;
    options.title = "Shell test";
    options.application = "paddle";
    // Shells, and the programs they own, go before the engine shuts down.
    {
      RuntimeShell shell(
        host,
        nullptr,
        std::make_unique<WasmProgram>(readGuest(ILLUMO_PADDLE_GUEST)),
        options);
      runShell(counters, host, *backend, shell);
    }
    {
      RuntimeShell failed(
        host,
        nullptr,
        std::make_unique<WasmProgram>(std::vector<std::byte>{}),
        options);
      testTrue(counters,
               !failed.start() && failed.exitCode() == 1,
               "a program that fails to start fails the run");
      failed.frame(1.0 / 60.0);
      testTrue(counters,
               failed.closing(),
               "a failed run closes without asking the program");
    }
    host.shutdown();
  }
  std::filesystem::remove(environment, error);
  return counters.failures == 0;
}

// The engine splash (GuiEngineSplash): the shell plays it before the
// program's first frame and holds the program, which never triggers it.
static bool
testRuntimeShellSplash()
{
  TestCounters counters;
  testSection("RuntimeShell: the engine splash holds the program");
  const std::filesystem::path environment =
    std::filesystem::temp_directory_path() / "illumo-runtime-splash.json";
  std::error_code error;
  std::filesystem::remove(environment, error);
  {
    IllumoConfig config;
    config.applicationName = "SplashTest";
    config.environmentPath = environment.string();
    Illumo host(config);
    MockBackend* backend = nullptr;
    setHeadlessFactories(host, &backend);
    testTrue(counters, host.initialize(), "engine initializes");
    RuntimeShellOptions options;
    options.title = "Splash test";
    options.application = "paddle";
    options.splashImage = std::filesystem::path(ILLUMO_ENGINE_ASSETS) /
                          "Branding" / "illumo-splash.png";
    {
      RuntimeShell shell(
        host,
        nullptr,
        std::make_unique<WasmProgram>(readGuest(ILLUMO_PADDLE_GUEST)),
        options);
      testTrue(counters,
               shell.start() && shell.splashing(),
               "an interactive launch opens on the splash");
      // Without a cache the module is ready at once: the first frame starts
      // the program (whose start runs its first update).
      shell.frame(1.0 / 60.0);
      const std::uint64_t started = shell.program().stats().updates;
      for (int frame = 1; frame < 30; ++frame) {
        shell.frame(1.0 / 60.0);
      }
      testTrue(counters,
               shell.splashing() && shell.program().stats().updates == started,
               "the program is not updated while the splash shows");
      host.context().inputManager->getKeyQueue().push(
        { KeyCode::Space, InputAction::Press, 0 });
      shell.frame(1.0 / 60.0);
      testTrue(counters,
               shell.splashing() && shell.program().stats().updates == started,
               "a skipped splash still draws its last frame");
      shell.frame(1.0 / 60.0);
      shell.frame(1.0 / 60.0);
      testTrue(counters,
               !shell.splashing() &&
                 shell.program().stats().updates == started + 2 &&
                 shell.program().error().empty(),
               "the program runs from the frame after the splash");
      shell.stop();
    }
    {
      // With a compiled-code cache the module compiles while the splash
      // plays, and the program starts between frames once it is ready.
      WasmLimits limits;
      limits.compiledCode = std::make_shared<WasmModuleCache>();
      RuntimeShell shell(
        host,
        nullptr,
        std::make_unique<WasmProgram>(
          readGuest(ILLUMO_PADDLE_GUEST), std::vector<std::byte>{}, limits),
        options);
      const bool deferred = shell.start() && shell.splashing() &&
                            shell.program().frameCounters() == nullptr;
      const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
      while (shell.program().frameCounters() == nullptr &&
             std::chrono::steady_clock::now() < deadline) {
        shell.frame(1.0 / 60.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      // Started (its start ran the first update) but held by the splash.
      const bool startedDuringSplash =
        shell.program().frameCounters() != nullptr && shell.splashing() &&
        shell.program().stats().updates == 1u;
      host.context().inputManager->getKeyQueue().push(
        { KeyCode::Space, InputAction::Press, 0 });
      for (int frame = 0; frame < 4; ++frame) {
        shell.frame(1.0 / 60.0);
      }
      testTrue(counters,
               deferred && startedDuringSplash && !shell.splashing() &&
                 shell.program().stats().updates > 1u &&
                 shell.program().error().empty() &&
                 limits.compiledCode->counters().compiles == 1u,
               "the program compiles behind the splash and starts after it");
      shell.stop();
    }
    {
      RuntimeShellOptions missing = options;
      missing.splashImage = "missing/illumo-splash.png";
      RuntimeShell shell(
        host,
        nullptr,
        std::make_unique<WasmProgram>(readGuest(ILLUMO_PADDLE_GUEST)),
        missing);
      testTrue(counters,
               shell.start() && !shell.splashing(),
               "without the logo the program starts directly");
      shell.stop();
    }
    {
      RuntimeShell shell(
        host,
        nullptr,
        std::make_unique<WasmProgram>(readGuest(ILLUMO_PADDLE_GUEST)),
        options);
      testTrue(counters, shell.start(), "the program starts again");
      shell.frame(1.0 / 60.0);
      host.context().window->requestClose();
      testTrue(counters,
               shell.closing(),
               "closing during the splash does not wait for the program");
      shell.stop();
    }
    host.shutdown();
  }
  std::filesystem::remove(environment, error);
  return counters.failures == 0;
}

// A one-image .ico holding a 1x1 opaque red bitmap.
static std::vector<std::uint8_t>
oneRedPixelIcon()
{
  static const std::uint8_t kBytes[] = {
    0,  0, 1,   0,   1,  0,                    // icon directory, one entry
    1,  1, 0,   0,   1,  0, 32, 0,             // 1x1, 32 bits
    48, 0, 0,   0,   22, 0, 0,  0,             // 48 bytes at offset 22
    40, 0, 0,   0,   1,  0, 0,  0, 2, 0, 0, 0, // header: width, doubled height
    1,  0, 32,  0,   0,  0, 0,  0, 0, 0, 0, 0, // planes, bits, BI_RGB, size
    0,  0, 0,   0,   0,  0, 0,  0, 0, 0, 0, 0,
    0,  0, 0,   0,   // resolution, palettes
    0,  0, 255, 255, // the pixel: BGRA red
    0,  0, 0,   0    // the AND mask row
  };
  return std::vector<std::uint8_t>(std::begin(kBytes), std::end(kBytes));
}

// The package's app.ico replaces the engine's window icon (D-E39); without a
// usable one the window keeps the engine icon.
static bool
testRuntimeShellIcon()
{
  TestCounters counters;
  testSection("RuntimeShell: an app.ico replaces the engine's window icon");
  const std::filesystem::path environment =
    std::filesystem::temp_directory_path() / "illumo-runtime-icon.json";
  std::error_code error;
  std::filesystem::remove(environment, error);
  {
    IllumoConfig config;
    config.applicationName = "IconTest";
    config.environmentPath = environment.string();
    Illumo host(config);
    MockBackend* backend = nullptr;
    setHeadlessFactories(host, &backend);
    testTrue(counters, host.initialize(), "engine initializes");
    NullRenderWindow* window =
      static_cast<NullRenderWindow*>(host.context().window);
    RuntimeShellOptions options;
    options.title = "Icon test";
    options.application = "paddle";
    const std::vector<std::vector<std::uint8_t>> icons = { {},
                                                           { 1, 2, 3, 4 },
                                                           oneRedPixelIcon() };
    for (std::size_t index = 0; index < icons.size(); ++index) {
      options.appIcon = icons[index];
      RuntimeShell shell(
        host,
        nullptr,
        std::make_unique<WasmProgram>(readGuest(ILLUMO_PADDLE_GUEST)),
        options);
      const int before = window->iconCalls;
      testTrue(counters, shell.start(), "the program starts");
      if (index < 2) {
        testTrue(counters,
                 window->iconCalls == before,
                 index == 0 ? "no app.ico leaves the engine icon"
                            : "an unusable app.ico leaves the engine icon");
      } else {
        testTrue(counters,
                 window->iconCalls == before + 1 && window->icon.size() == 1 &&
                   window->icon[0].width == 1 && window->icon[0].height == 1 &&
                   window->icon[0].rgba ==
                     std::vector<std::uint8_t>{ 255, 0, 0, 255 },
                 "a usable app.ico becomes the window icon");
      }
      shell.stop();
    }
    host.shutdown();
  }
  std::filesystem::remove(environment, error);
  return counters.failures == 0;
}

bool
runRuntimeShellTest(const std::string& name)
{
  if (name == "RuntimeShellIcon") {
    return testRuntimeShellIcon();
  }
  if (name == "RuntimeShell") {
    return testRuntimeShell();
  }
  if (name == "RuntimeShellSplash") {
    return testRuntimeShellSplash();
  }
  return false;
}
