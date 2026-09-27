#include <Illumo/Audio/AudioDevice.h>
#include <Illumo/Engine/Illumo.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Testing/IllumoTestAccess.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/RuntimeShell.h>
#include <Illumo/Wasm/WasmProgram.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
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
           shell.program().frameCounters() == nullptr &&
             shell.exitCode() == 0,
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
      RuntimeShell shell(host,
                         nullptr,
                         std::make_unique<WasmProgram>(
                           readGuest(ILLUMO_PADDLE_GUEST)),
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

bool
runRuntimeShellTest(const std::string& name)
{
  if (name == "RuntimeShell") {
    return testRuntimeShell();
  }
  return false;
}
