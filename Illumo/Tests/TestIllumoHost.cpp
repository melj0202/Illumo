#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
#include <Illumo/Engine/DebugOverlay.h>
#endif
#include "Rendering/BackendConfig.h"
#include "Rendering/RenderWindow.h"
#include <Illumo/Engine/Illumo.h>
#include <Illumo/Foundation/BuildInfo.h>
#include <Illumo/Rendering/GLString.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Rendering/RenderWorld.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/IllumoTestAccess.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestAccess.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

static TestCounters g;

class CountingWindow : public NullRenderWindow
{
public:
  explicit CountingWindow(int* destructions, IEnvVars* environment = nullptr)
    : NullRenderWindow(640, 480)
    , m_destructions(destructions)
    , m_environment(environment)
  {
  }

  ~CountingWindow() override { *m_destructions += 1; }

  void toggleFullscreen() override
  {
    if (rejectFullscreen) {
      return;
    }
    NullRenderWindow::toggleFullscreen();
    if (m_environment != nullptr) {
      storeDisplayMode(m_environment, displayMode());
    }
  }
  bool rejectFullscreen{ false };

private:
  int* m_destructions;
  IEnvVars* m_environment;
};

class CountingMockBackend : public MockBackend
{
public:
  CountingMockBackend(int* initializationCount,
                      bool initializeResult = true,
                      int* windowDestructions = nullptr,
                      int* cleanupsWithLiveWindow = nullptr,
                      bool failAfterStarting = false)
    : m_initializationCount(initializationCount)
    , m_initializeResult(initializeResult)
    , m_windowDestructions(windowDestructions)
    , m_cleanupsWithLiveWindow(cleanupsWithLiveWindow)
    , m_failAfterStarting(failAfterStarting)
  {
  }

  bool Initialize() override
  {
    *m_initializationCount += 1;
    if (m_failAfterStarting) {
      // Partly initialized, then failed: cleanup must still run.
      MockBackend::Initialize();
      return false;
    }
    return m_initializeResult && MockBackend::Initialize();
  }

  void Shutdown() override
  {
    if (m_windowDestructions != nullptr &&
        m_cleanupsWithLiveWindow != nullptr && *m_windowDestructions == 0) {
      *m_cleanupsWithLiveWindow += 1;
    }
    MockBackend::Shutdown();
  }

private:
  int* m_initializationCount;
  bool m_initializeResult;
  int* m_windowDestructions;
  int* m_cleanupsWithLiveWindow;
  bool m_failAfterStarting;
};

static std::filesystem::path
temporaryEnvironmentPath(const char* name)
{
  return std::filesystem::temp_directory_path() /
         (std::string("illumo-") + name + ".json");
}

static IllumoConfig
headlessConfig(const std::filesystem::path& path)
{
  IllumoConfig config;
  config.applicationName = "HostTest";
  config.environmentPath = path.string();
  return config;
}

static void
setHeadlessFactories(Illumo& host,
                     int* windowDestructions,
                     int* backendInitializations = nullptr)
{
  IllumoTestAccess::setWindowFactory(
    host,
    [windowDestructions](int, int, const std::string&, IEnvVars* environment) {
      return std::make_unique<CountingWindow>(windowDestructions, environment);
    });
  IllumoTestAccess::setBackendFactory(
    host, [backendInitializations](IRenderWindow*) {
      if (backendInitializations == nullptr) {
        return std::unique_ptr<IBackend>(std::make_unique<MockBackend>());
      }
      return std::unique_ptr<IBackend>(
        std::make_unique<CountingMockBackend>(backendInitializations));
    });
}

// The update half of a frame with nothing between the engine's phases.
static void
updateFrame(Illumo& host, double dt = 0.016)
{
  host.beginUpdate(dt);
  host.endUpdate();
}

// The render half of a frame with nothing dispatched.
static void
renderFrame(Illumo& host)
{
  host.beginRender();
  host.endRender();
}

static void
testCloseNegotiation()
{
  testSection("Illumo: a declined close keeps the window open");
  int windowDestructions = 0;
  Illumo host(headlessConfig(temporaryEnvironmentPath("close")));
  setHeadlessFactories(host, &windowDestructions);
  testTrue(g, host.initialize(), "host initializes");
  testTrue(g, !host.shouldClose(), "no close request means continue");
  host.context().window->requestClose();
  testTrue(g, host.shouldClose(), "a close request is seen");
  host.deferClose();
  testTrue(g, !host.shouldClose(), "deferring clears the native request");
  updateFrame(host);
  testTrue(g, !host.shouldClose(), "a deferred request does not come back");
  host.context().window->requestRestart();
  testTrue(g,
           host.shouldClose() && host.context().window->restartRequested(),
           "a restart request is a close request that remembers the restart");
  host.deferClose();
  testTrue(g,
           !host.shouldClose() && !host.context().window->restartRequested(),
           "deferring the close drops the restart");
  host.context().window->requestClose();
  testTrue(g, host.shouldClose(), "an accepted request stays until shutdown");
  host.shutdown();
  testTrue(g, host.shouldClose(), "a host without a window reports closed");
  host.deferClose();
  testEqInt(g, windowDestructions, 1, "window released once");
}

static void
testGenericConfigurationOwnership()
{
  testSection("Illumo: generic defaults exclude simulator policy");
  const std::filesystem::path path = temporaryEnvironmentPath("config");
  std::error_code error;
  std::filesystem::remove(path, error);
  {
    IllumoConfig config;
    config.applicationName = "EmbeddingHost";
    config.environmentPath = path.string();
    Illumo host(config);
    testTrue(g,
             host.environment().getVars().count("showMemory") == 1 &&
               !host.environment().getVar("showMemory").valueAsBool,
             "engine supplies memory visibility default off");
    testTrue(g,
             host.applicationName() == "EmbeddingHost",
             "host retains application identity");
    testTrue(g,
             host.environment().getVar("WinX").value == "1280",
             "generic window default loaded during construction");
    testTrue(g,
             host.environment().getVar("CanvasX").value.empty(),
             "library does not seed canvas dimensions");
    testTrue(g,
             host.environment().getVar("ModeString").value.empty(),
             "library does not seed a ruleset");
    testTrue(g,
             host.environment().getVar("tps").value.empty(),
             "library does not seed simulation timing");
  }
  std::filesystem::remove(path, error);
}

// Initializes a headless host whose settings hold WinX/WinY and reports the
// size the host asked its window factory for.
static std::array<int, 2>
requestedWindowSize(const char* savedWidth,
                    const char* savedHeight,
                    std::string* persistedWidth)
{
  const std::filesystem::path path = temporaryEnvironmentPath("window-size");
  std::error_code error;
  std::filesystem::remove(path, error);
  std::array<int, 2> requested = { -1, -1 };
  int windowDestructions = 0;
  {
    Illumo host(headlessConfig(path));
    host.environment().setVar("WinX", savedWidth);
    host.environment().setVar("WinY", savedHeight);
    IllumoTestAccess::setWindowFactory(
      host,
      [&requested, &windowDestructions](
        int width, int height, const std::string&, IEnvVars* environment) {
        requested = { width, height };
        return std::make_unique<CountingWindow>(&windowDestructions,
                                                environment);
      });
    IllumoTestAccess::setBackendFactory(host, [](IRenderWindow*) {
      return std::unique_ptr<IBackend>(std::make_unique<MockBackend>());
    });
    testTrue(g, host.initialize(), "host initializes");
    *persistedWidth = host.environment().getVar("WinX").value;
    host.shutdown();
  }
  std::filesystem::remove(path, error);
  return requested;
}

static void
testSavedWindowSize()
{
  testSection("Illumo: a saved window size that cannot open falls back");
  std::string persistedWidth;
  std::array<int, 2> requested = requestedWindowSize("0", "0", &persistedWidth);
  testTrue(g,
           requested[0] == 1280 && requested[1] == 720,
           "a minimized 0x0 save opens at the default 1280x720");
  testTrue(g,
           persistedWidth == "1280",
           "the fallback replaces the saved size in the settings");
  requested = requestedWindowSize("800", "-5", &persistedWidth);
  testTrue(g,
           requested[0] == 1280 && requested[1] == 720,
           "one non-positive axis falls back to the whole default size");
  requested = requestedWindowSize("", "", &persistedWidth);
  testTrue(g,
           requested[0] == 1280 && requested[1] == 720,
           "an empty saved size opens at the default");
  requested = requestedWindowSize("800", "600", &persistedWidth);
  testTrue(g,
           requested[0] == 800 && requested[1] == 600 &&
             persistedWidth == "800",
           "a valid saved size is used as is");

  testSection("RenderWindow: minimized sizes are not persisted");
  const std::filesystem::path recordPath =
    temporaryEnvironmentPath("record-size");
  std::error_code error;
  std::filesystem::remove(recordPath, error);
  {
    EnvVars environment(recordPath);
    environment.setVar("WinX", 1024);
    environment.setVar("WinY", 768);
    testTrue(g,
             !recordWindowSize(&environment, 0, 0, true),
             "a minimized 0x0 resize is not recorded");
    testTrue(g,
             !recordWindowSize(&environment, 0, 0, false),
             "a 0x0 size is not recorded even without the iconified flag");
    testTrue(g,
             !recordWindowSize(&environment, 1600, 900, true),
             "an iconified window's size is not recorded");
    testTrue(g,
             !recordWindowSize(&environment, 1600, -1, false),
             "a negative axis is not recorded");
    testTrue(g,
             environment.getVar("WinX").value == "1024" &&
               environment.getVar("WinY").value == "768",
             "the last restored size survives");
    testTrue(g,
             recordWindowSize(&environment, 1600, 900, false) &&
               environment.getVar("WinX").value == "1600" &&
               environment.getVar("WinY").value == "900",
             "a restored size is recorded");
    testTrue(g,
             !recordWindowSize(nullptr, 1600, 900, false),
             "a window without settings records nothing");
  }
  std::filesystem::remove(recordPath, error);
}

static void
testFramePhases()
{
  testSection("Illumo: frame phases hand input and the scene to the runtime");
  const std::filesystem::path path = temporaryEnvironmentPath("phases");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  int backendInitializations = 0;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions, &backendInitializations);
    updateFrame(host);
    testTrue(g,
             host.beginRender() == nullptr,
             "phases do nothing before initialization");
    host.endRender();
    testTrue(g, host.initialize(), "host initializes");

    InputManager& input = *host.context().inputManager;
    input.getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::Enter, InputAction::Press, 0 });
    input.getCharQueue().push('a');
    host.beginUpdate(0.016);
    testTrue(g,
             input.getKeyQueue().size() == 1 &&
               input.getCharQueue().size() == 1,
             "input waits for the overlay and program after beginUpdate");
    host.endUpdate();
    testTrue(g,
             input.getKeyQueue().empty() && input.getCharQueue().empty(),
             "input nobody read is dropped at endUpdate");

    {
      DrawList* scene = host.beginRender();
      testTrue(g,
               scene != nullptr && scene == host.context().scene,
               "beginRender hands out the frame's scene");
      GLString label(
        "probe", 255, 255, 255, 255, 12, 0, 0, host.context().renderer);
      scene->AddDrawable(&label, RenderLayerId::UI);
      host.endRender();
      testTrue(g,
               host.beginRender()->drawablesIn(RenderLayerId::UI).empty(),
               "each frame starts with no drawables");
      host.endRender();
    }
    host.shutdown();
    testTrue(
      g, host.beginRender() == nullptr, "phases do nothing after shutdown");
    updateFrame(host);
  }
  testEqInt(g, windowDestructions, 1, "window released once");
  testEqInt(g,
            backendInitializations,
            1,
            "host initializes the injected backend exactly once");
  std::filesystem::remove(path, error);
}

static void
testInitializationRollback()
{
  testSection("Illumo: backend factory failure releases accepted services");
  const std::filesystem::path path = temporaryEnvironmentPath("rollback");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    IllumoTestAccess::setBackendFactory(
      host, [](IRenderWindow*) { return std::unique_ptr<IBackend>(); });
    testTrue(g, !host.initialize(), "backend failure is returned to host");
    testTrue(
      g, host.context().window == nullptr, "context rollback clears window");
    testTrue(g,
             host.context().renderer == nullptr,
             "context rollback clears renderer");
    testTrue(g, host.shouldClose(), "failed host reports closed");
  }
  testEqInt(g, windowDestructions, 1, "accepted window is released on failure");
  std::filesystem::remove(path, error);
}

static void
testFallibleFactoryBoundaries()
{
  testSection(
    "Illumo: window and backend initialization failures are returned");
  const std::filesystem::path path = temporaryEnvironmentPath("factories");
  std::error_code error;
  std::filesystem::remove(path, error);

  IllumoConfig windowFailure;
  windowFailure.environmentPath = path.string();
  {
    Illumo host(windowFailure);
    IllumoTestAccess::setWindowFactory(
      host, [](int, int, const std::string&, IEnvVars*) {
        return std::unique_ptr<IRenderWindow>();
      });
    testTrue(g, !host.initialize(), "null window fails without terminating");
    testTrue(
      g, host.context().window == nullptr, "window failure clears context");
  }

  int windowDestructions = 0;
  int backendInitializations = 0;
  int cleanupsWithLiveWindow = 0;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    IllumoTestAccess::setBackendFactory(host, [&](IRenderWindow*) {
      return std::unique_ptr<IBackend>(
        std::make_unique<CountingMockBackend>(&backendInitializations,
                                              false,
                                              &windowDestructions,
                                              &cleanupsWithLiveWindow));
    });
    testTrue(g,
             !host.initialize(),
             "backend Initialize failure is returned without terminating");
    testTrue(g,
             host.context().renderer == nullptr,
             "backend failure clears renderer context");
  }
  testEqInt(g,
            backendInitializations,
            1,
            "failing backend receives exactly one Initialize call");
  testEqInt(
    g, windowDestructions, 1, "backend failure releases the accepted window");
  testEqInt(g,
            cleanupsWithLiveWindow,
            1,
            "failed backend is cleaned before its window context");

  int partialWindowDestructions = 0;
  int partialBackendInitializations = 0;
  int partialCleanupsWithLiveWindow = 0;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &partialWindowDestructions);
    IllumoTestAccess::setBackendFactory(host, [&](IRenderWindow*) {
      return std::unique_ptr<IBackend>(
        std::make_unique<CountingMockBackend>(&partialBackendInitializations,
                                              true,
                                              &partialWindowDestructions,
                                              &partialCleanupsWithLiveWindow,
                                              true));
    });
    testTrue(g,
             !host.initialize(),
             "a partly initialized backend failure is returned");
    testTrue(g,
             host.context().window == nullptr,
             "a partly initialized backend failure clears the host context");
  }
  testEqInt(
    g,
    partialBackendInitializations,
    1,
    "a partly initialized backend receives exactly one Initialize call");
  testEqInt(
    g,
    partialCleanupsWithLiveWindow,
    1,
    "a partly initialized backend is cleaned while its window is alive");
  testEqInt(
    g,
    partialWindowDestructions,
    1,
    "a partly initialized backend failure releases the accepted window");
  std::filesystem::remove(path, error);
}

#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
static void
testDebugOverlayConsoleIsGlobal()
{
  testSection("Illumo: the debug overlay toggles the console before the "
              "program reads input");
  const std::filesystem::path path =
    temporaryEnvironmentPath("debug-overlay-console");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "host initializes");
    DebugOverlay overlay;
    testTrue(g, overlay.start(host.context()), "overlay starts");
    testTrue(g,
             host.context().commandLine != nullptr &&
               !host.context().commandLine->isOpen,
             "console starts closed");

    host.context().inputManager->getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::Grave, InputAction::Press, 0 });
    host.beginUpdate(0.016);
    overlay.update(0.016);
    // A program that drains every key comes after the overlay.
    host.context().inputManager->clearKeyQueue();
    host.endUpdate();
    testTrue(g,
             host.context().commandLine->isOpen,
             "Grave toggles the console even if the program drains input");

    overlay.stop();
    host.shutdown();
  }
  std::filesystem::remove(path, error);
}

static void
testDebugOverlayWatermarkDispatched()
{
  testSection("Illumo: Debug overlay dispatches development build watermark");
  const std::filesystem::path path =
    temporaryEnvironmentPath("debug-overlay-watermark");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "host initializes");
    DebugOverlay overlay;
    testTrue(g, overlay.start(host.context()), "overlay starts");

    host.beginUpdate(0.016);
    overlay.update(0.016);
    host.endUpdate();
    DrawList* scene = host.beginRender();
    testTrue(g, scene != nullptr, "scene exists");
    overlay.dispatch(*scene);

    const std::vector<DrawableBase*>& debugDrawables =
      scene->drawablesIn(RenderLayerId::Debug);
    bool watermarkFound = false;
    for (DrawableBase* drawable : debugDrawables) {
      GLString* str = dynamic_cast<GLString*>(drawable);
      if (str != nullptr &&
          str->getContent() ==
            std::string("development build ") + BuildInfo::VersionNumber) {
        watermarkFound = true;
        testTrue(g, str->getR() == 245, "watermark is red");
        testTrue(g, str->getA() == 140, "watermark is translucent");
        testTrue(g, str->getSize() == 18, "watermark is expected font size");
        break;
      }
    }
    testTrue(
      g, watermarkFound, "development build watermark is in debug drawables");
    host.endRender();

    overlay.stop();
    host.shutdown();
  }
  std::filesystem::remove(path, error);
}

static void
testDebugOverlayGamepadCommandsAndDispatch()
{
  testSection("Illumo: Debug overlay registers gamepad/joystick_debug commands "
              "and dispatches joystick HUD");
  const std::filesystem::path path =
    temporaryEnvironmentPath("debug-overlay-gamepad");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "host initializes");
    testTrue(g,
             host.environment().getVars().count("showGamepadVisuals") == 1 &&
               !host.environment().getVar("showGamepadVisuals").valueAsBool,
             "host seeds showGamepadVisuals default off");
    DebugOverlay overlay;
    testTrue(g, overlay.start(host.context()), "overlay starts");

    CommandRegistry& registry = *host.context().commandRegistry;
    testTrue(g,
             registry.HasCommand("gamepad") &&
               registry.HasCommand("joystick_debug"),
             "gamepad and joystick_debug commands registered");

    registry.QueueCommand("joystick_debug", { "on" });
    host.beginUpdate(0.016);
    overlay.update(0.016);
    host.endUpdate();
    testTrue(g,
             host.environment().getVar("showGamepadVisuals").valueAsBool,
             "joystick_debug on enables showGamepadVisuals");

    GamepadState gamepad;
    gamepad.connected = true;
    gamepad.axes[static_cast<std::size_t>(GamepadAxis::LeftX)] = 0.5f;
    gamepad.buttons[static_cast<std::size_t>(GamepadButton::A)] =
      InputAction::Press;
    InputManagerTestAccess::setGamepadState(*host.context().inputManager,
                                            gamepad);
    host.beginUpdate(0.016);
    overlay.update(0.016);
    host.endUpdate();

    bool buttonLogged = false;
    for (const CommandLine::historyBuffer& entry :
         host.context().commandLine->getHistory()) {
      if (entry.content.find("Gamepad button pressed: A") !=
          std::string::npos) {
        buttonLogged = true;
        break;
      }
    }
    testTrue(
      g, buttonLogged, "DebugOverlay logs gamepad button press when enabled");

    DrawList* scene = host.beginRender();
    overlay.dispatch(*scene);
    GLString* gamepadStatus = nullptr;
    for (DrawableBase* drawable : scene->drawablesIn(RenderLayerId::Debug)) {
      GLString* str = dynamic_cast<GLString*>(drawable);
      if (str != nullptr &&
          str->getContent().find("Gamepad: [Connected]") != std::string::npos) {
        gamepadStatus = str;
        break;
      }
    }
    testTrue(g,
             gamepadStatus != nullptr,
             "DebugOverlay dispatches gamepad status to Debug layer");
    testTrue(g,
             gamepadStatus != nullptr && gamepadStatus->getX() == 12 &&
               gamepadStatus->getY() == 12,
             "gamepad overlay sits at top-left (12, 12) when FPS and memory "
             "are off");
    host.endRender();

    // Enabling FPS and memory stacks FPS, memory, and gamepad vertically
    // without overlapping.
    host.environment().setVar("showFPS", true);
    host.environment().setVar("showMemory", true);
    host.beginUpdate(0.016);
    overlay.update(0.016);
    host.endUpdate();
    scene = host.beginRender();
    overlay.dispatch(*scene);
    GLString* fpsLabel = nullptr;
    GLString* memLabel = nullptr;
    gamepadStatus = nullptr;
    for (DrawableBase* drawable : scene->drawablesIn(RenderLayerId::Debug)) {
      GLString* str = dynamic_cast<GLString*>(drawable);
      if (str == nullptr) {
        continue;
      }
      if (str->getContent().find("fps") != std::string::npos) {
        fpsLabel = str;
      } else if (str->getContent().find("RAM:") != std::string::npos ||
                 str->getContent().find("Memory:") != std::string::npos) {
        memLabel = str;
      } else if (str->getContent().find("Gamepad: [Connected]") !=
                 std::string::npos) {
        gamepadStatus = str;
      }
    }
    testTrue(g,
             fpsLabel != nullptr && memLabel != nullptr &&
               gamepadStatus != nullptr,
             "FPS, memory, and gamepad labels are all dispatched");
    testTrue(g,
             fpsLabel != nullptr && memLabel != nullptr &&
               gamepadStatus != nullptr && fpsLabel->getY() == 12 &&
               memLabel->getY() > fpsLabel->getY() &&
               gamepadStatus->getY() > memLabel->getY(),
             "DebugOverlay layout stacks FPS, memory, and gamepad in "
             "non-overlapping vertical order");
    host.endRender();

    overlay.stop();
    testTrue(g,
             !registry.HasCommand("gamepad") &&
               !registry.HasCommand("joystick_debug"),
             "overlay stop unregisters gamepad commands");
    host.shutdown();
  }
  std::filesystem::remove(path, error);
}

static void
testDebugOverlayGraphicsPanel()
{
  testSection("Illumo: DebugOverlay toggles graphics panel via F7 and console "
              "command");
  const std::filesystem::path path =
    temporaryEnvironmentPath("debug-overlay-graphics");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "host initializes");
    DebugOverlay overlay;
    testTrue(g, overlay.start(host.context()), "overlay starts");

    CommandRegistry& registry = *host.context().commandRegistry;
    testTrue(g,
             registry.HasCommand("graphics"),
             "DebugOverlay registers graphics command");

    // F7 opens the floating graphics panel and dispatches it on Debug layer.
    host.context().inputManager->getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::F7, InputAction::Press, 0 });
    host.beginUpdate(0.016);
    overlay.update(0.016);
    host.endUpdate();

    DrawList* scene = host.beginRender();
    overlay.dispatch(*scene);
    const size_t openDebugCount =
      scene->drawablesIn(RenderLayerId::Debug).size();
    host.endRender();

    // Console `graphics off` hides the panel; `graphics reset` restores values.
    host.environment().setVar("exposure", "2.5");
    registry.QueueCommand("graphics", { "off" });
    registry.QueueCommand("graphics", { "reset" });
    host.beginUpdate(0.016);
    overlay.update(0.016);
    host.endUpdate();

    scene = host.beginRender();
    overlay.dispatch(*scene);
    const size_t closedDebugCount =
      scene->drawablesIn(RenderLayerId::Debug).size();
    host.endRender();

    testTrue(g,
             openDebugCount == closedDebugCount + 1,
             "graphics panel adds one Debug-layer drawable while open");
    testTrue(g,
             host.environment().getVar("exposure").value == "0",
             "graphics reset restores pipeline defaults");

    overlay.stop();
    testTrue(g,
             !registry.HasCommand("graphics"),
             "overlay stop unregisters graphics command");
    host.shutdown();
  }
  std::filesystem::remove(path, error);
}

static void
testDebugOverlayAxisCube()
{
  testSection("Illumo: DebugOverlay toggles centered axis cube via F8 and "
              "axis_cube command");
  const std::filesystem::path path =
    temporaryEnvironmentPath("debug-overlay-axis-cube");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "host initializes");
    DebugOverlay overlay;
    testTrue(g, overlay.start(host.context()), "overlay starts");

    CommandRegistry& registry = *host.context().commandRegistry;
    testTrue(g,
             registry.HasCommand("axis_cube"),
             "DebugOverlay registers axis_cube command");

    // F8 enables the transparent axis cube and dispatches it on Debug layer.
    host.context().inputManager->getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::F8, InputAction::Press, 0 });
    host.beginUpdate(0.016);
    overlay.update(0.016);
    host.endUpdate();

    DrawList* scene = host.beginRender();
    overlay.dispatch(*scene);
    const size_t openDebugCount =
      scene->drawablesIn(RenderLayerId::Debug).size();
    host.endRender();

    // Console `axis_cube off` hides the cube and updates showAxisCube.
    registry.QueueCommand("axis_cube", { "off" });
    host.beginUpdate(0.016);
    overlay.update(0.016);
    host.endUpdate();

    scene = host.beginRender();
    overlay.dispatch(*scene);
    const size_t closedDebugCount =
      scene->drawablesIn(RenderLayerId::Debug).size();
    host.endRender();

    testTrue(g,
             openDebugCount == closedDebugCount + 1,
             "axis cube adds one Debug-layer drawable while enabled");
    testTrue(g,
             !host.environment().getVar("showAxisCube").valueAsBool,
             "axis_cube off clears showAxisCube");

    overlay.stop();
    testTrue(g,
             !registry.HasCommand("axis_cube"),
             "overlay stop unregisters axis_cube command");
    host.shutdown();
  }
  std::filesystem::remove(path, error);
}
#endif

static void
testGlobalHotkeys()
{
  testSection("Illumo: host handles F11, F3, and F5 global shortcuts");
  const std::filesystem::path path = temporaryEnvironmentPath("global-hotkeys");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "host initializes");

    // 1. Test F11 (Fullscreen toggle)
    const bool initialFullscreen =
      host.environment().getVar("fullscreen").valueAsBool;
    host.context().inputManager->getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::F11, InputAction::Press, 0 });
    updateFrame(host);
    testEqInt(g,
              host.environment().getVar("fullscreen").valueAsBool,
              !initialFullscreen,
              "F11 toggles fullscreen envvar");
    testTrue(g,
             host.context().commandLine->getHistory().back().content ==
               "SUCCESS: Display mode: Fullscreen window",
             "F11 reports enabled state");
    host.context().inputManager->getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::F11, InputAction::Press, 0 });
    updateFrame(host);
    testEqInt(g,
              host.environment().getVar("fullscreen").valueAsBool,
              initialFullscreen,
              "second F11 restores windowed state");
    testTrue(g,
             host.context().commandLine->getHistory().back().content ==
               "SUCCESS: Display mode: Window",
             "F11 reports disabled state");
    CountingWindow* window =
      static_cast<CountingWindow*>(host.context().window);
    window->rejectFullscreen = true;
    host.context().inputManager->getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::F11, InputAction::Press, 0 });
    updateFrame(host);
    testEqInt(g,
              host.environment().getVar("fullscreen").valueAsBool,
              initialFullscreen,
              "rejected fullscreen toggle preserves state");
    testTrue(g,
             host.context().commandLine->getHistory().back().content ==
               "SUCCESS: Display mode: Window",
             "rejected toggle reports unchanged state");

    // 2. Test F3 (FPS overlay toggle)
    const bool initialShowFps =
      host.environment().getVar("showFPS").valueAsBool;
    host.context().inputManager->getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::F3, InputAction::Press, 0 });
    updateFrame(host);
    testEqInt(g,
              host.environment().getVar("showFPS").valueAsBool,
              !initialShowFps,
              "F3 toggles showFPS envvar");

    // 3. Test F5 (Asset reload): the hotkey is taken before the program runs,
    // while other keys stay for it.
    host.context().inputManager->getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::F5, InputAction::Press, 0 });
    host.context().inputManager->getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::Enter, InputAction::Press, 0 });
    host.beginUpdate(0.016);
    testTrue(g,
             host.context().inputManager->getKeyQueue().size() == 1 &&
               host.context().inputManager->getKeyQueue().front().key ==
                 KeyCode::Enter,
             "F5 is consumed and other keys remain for the program");
    host.endUpdate();

    // 4. Test console open suppresses hotkey interception
    if (host.context().commandLine != nullptr) {
      host.context().commandLine->isOpen = true;
      const bool beforeHotkeys =
        host.environment().getVar("showFPS").valueAsBool;
      host.context().inputManager->getKeyQueue().push(
        InputManager::KeyPressEvent{ KeyCode::F3, InputAction::Press, 0 });
      updateFrame(host);
      testEqInt(g,
                host.environment().getVar("showFPS").valueAsBool,
                beforeHotkeys,
                "F3 ignored while console is open");
      host.context().commandLine->isOpen = false;
    }

    host.shutdown();
  }
  std::filesystem::remove(path, error);
}

static void
testScenePipelineConfigurationFromEnv()
{
  std::error_code error;
  const std::filesystem::path path =
    std::filesystem::current_path() / "test-pipeline-env.json";
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  int backendInitializations = 0;

  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions, &backendInitializations);
    testTrue(g, host.initialize(), "host initialized");

    DrawList* scene = IllumoTestAccess::getScene(host);
    EnvVars* env = IllumoTestAccess::getEnvironment(host);
    testTrue(g, scene != nullptr, "scene exists");
    testTrue(g, env != nullptr, "env exists");

    // By default without motionBlurEnabled, World has no custom passes
    IllumoTestAccess::configureScenePipeline(host);
    testTrue(g,
             !scene->hasCustomPasses(RenderLayerId::World),
             "World layer has no custom passes when motionBlurEnabled is off");

    // Enable motionBlurEnabled in env
    env->setVar("motionBlurEnabled", "1");
    env->setVar("motionBlurAmount", 0.75);
    env->setVar("motionBlurMax", 0.15);
    env->setVar("motionBlurSamples", static_cast<long>(12));
    IllumoTestAccess::configureScenePipeline(host);

    testTrue(g,
             scene->hasCustomPasses(RenderLayerId::World),
             "World layer has custom passes when motionBlurEnabled is on");
    const std::vector<RenderPassDesc>& passes =
      scene->passesIn(RenderLayerId::World);
    testEqSize(g, passes.size(), 2u, "World layer has 2 passes");
    testTrue(g, passes[0].name == "WorldGeomPass", "pass 0 is WorldGeomPass");
    testTrue(g, !passes[0].useScreenTarget, "pass 0 targets pooled target");
    testTrue(
      g, passes[1].name == "MotionBlurResolve", "pass 1 is MotionBlurResolve");
    testTrue(g, passes[1].useScreenTarget, "pass 1 targets screen");
    testEqSize(g,
               passes[1].inputTargetTextures.size(),
               2u,
               "post pass binds 2 target textures");

    // Disable motionBlurEnabled
    env->setVar("motionBlurEnabled", "0");
    IllumoTestAccess::configureScenePipeline(host);
    testTrue(g,
             !scene->hasCustomPasses(RenderLayerId::World),
             "World layer custom passes cleared when motionBlurEnabled is off");

    host.shutdown();
  }
  std::filesystem::remove(path, error);
}

// A client (the program, a scene) replacing the World layer's passes.
static void
installPass(DrawList& scene, const char* name, int* executions = nullptr)
{
  RenderPassDesc pass;
  pass.name = name;
  if (executions != nullptr) {
    pass.type = PassType::Custom;
    pass.customExecution = [counter = executions](Renderer*) { ++*counter; };
  }
  scene.SetLayerPasses(RenderLayerId::World, { pass });
}

static void
testScenePipelineOverrides()
{
  std::error_code error;
  const std::filesystem::path path =
    std::filesystem::current_path() / "test-pipeline-overrides-env.json";
  std::filesystem::remove(path, error);
  int windows = 0;
  int backends = 0;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windows, &backends);
    testTrue(g, host.initialize(), "host initialized");
    DrawList* scene = host.context().scene;
    const std::function<void(const char*)> checkOverride =
      [scene](const char* name) {
        const std::vector<RenderPassDesc>& passes =
          scene->passesIn(RenderLayerId::World);
        testTrue(g,
                 passes.size() == 1 && passes[0].name == name,
                 "client pass survives host configuration");
      };
    installPass(*scene, "ClientStart");
    renderFrame(host);
    checkOverride("ClientStart");
    host.environment().setVar("motionBlurEnabled", true);
    renderFrame(host);
    checkOverride("ClientStart");
    host.beginUpdate(0.016);
    installPass(*scene, "ClientUpdate");
    host.endUpdate();
    renderFrame(host);
    checkOverride("ClientUpdate");
    host.environment().setVar("motionBlurAmount", 0.625);
    renderFrame(host);
    checkOverride("ClientUpdate");
    scene->ClearLayerPasses(RenderLayerId::World);
    const std::vector<RenderPassDesc>& defaults =
      scene->passesIn(RenderLayerId::World);
    testTrue(g,
             defaults.size() == 2 &&
               defaults[1].uniformFloats[0].value == 0.625f,
             "clearing override reveals updated host defaults");
    host.environment().setVar("motionBlurEnabled", false);
    installPass(*scene, "ClientUpdate");
    renderFrame(host);
    checkOverride("ClientUpdate");
    installPass(*host.beginRender(), "ClientDispatch");
    host.endRender();
    checkOverride("ClientDispatch");
    renderFrame(host);
    checkOverride("ClientDispatch");
    scene->SetLayerPasses(RenderLayerId::World, {});
    testTrue(g,
             !scene->hasCustomPasses(RenderLayerId::World),
             "empty override restores ordinary draw when blur is off");
    host.environment().setVar("motionBlurEnabled", true);
    IllumoTestAccess::configureScenePipeline(host);
    scene->ResetDefaultPasses();
    testEqSize(g,
               scene->passesIn(RenderLayerId::World).size(),
               2u,
               "reset restores host defaults");
    int executions = 0;
    installPass(*scene, "ClientCustom", &executions);
    renderFrame(host);
    testEqInt(g, executions, 1, "a client's custom pass executes");
    // What a scene switch does before the next scene enters.
    scene->ResetDefaultPasses();
    renderFrame(host);
    testEqInt(g, executions, 1, "a reset detaches the client's callback");
    testEqSize(g,
               scene->passesIn(RenderLayerId::World).size(),
               2u,
               "a reset keeps the host defaults");
    host.shutdown();
    testTrue(g, host.initialize(), "host reinitializes");
    host.environment().setVar("motionBlurEnabled", true);
    IllumoTestAccess::configureScenePipeline(host);
    testEqSize(g,
               host.context().scene->passesIn(RenderLayerId::World).size(),
               2u,
               "new scene receives host defaults after reinitialization");
    host.shutdown();
  }
  std::filesystem::remove(path, error);
}

// Initializes a headless host with GraphicsAPI set to `setting` and reports
// the APIs it started windows and backends for, in order. Backends for
// `failingApi` fail to initialize.
static std::string
startedGraphicsApis(const char* setting,
                    BackendDef failingApi,
                    bool* initialized,
                    std::string* savedSetting)
{
  const std::filesystem::path path = temporaryEnvironmentPath("graphics-api");
  std::error_code error;
  std::filesystem::remove(path, error);
  std::string started;
  int windowDestructions = 0;
  int initializations = 0;
  {
    Illumo host(headlessConfig(path));
    host.environment().setVar("GraphicsAPI", setting);
    IllumoTestAccess::setGraphicsApiFactories(
      host,
      [&started, &windowDestructions](
        int, int, const std::string&, IEnvVars* environment, BackendDef api) {
        started += "window:" + TokenToString(api) + " ";
        return std::make_unique<CountingWindow>(&windowDestructions,
                                                environment);
      },
      [&started, &initializations, failingApi](IRenderWindow*, BackendDef api) {
        started += "backend:" + TokenToString(api) + " ";
        return std::unique_ptr<IBackend>(std::make_unique<CountingMockBackend>(
          &initializations, api != failingApi));
      });
    *initialized = host.initialize();
    *savedSetting = host.environment().getVar("GraphicsAPI").value;
    host.shutdown();
  }
  std::filesystem::remove(path, error);
  return started;
}

static void
testGraphicsApiSelection()
{
  testSection("Illumo: GraphicsAPI picks the backend, OpenGL is the fallback");
  bool initialized = false;
  std::string saved;
  std::string started =
    startedGraphicsApis("", BackendDef::DIRECTX11, &initialized, &saved);
  testTrue(g,
           initialized && started == "window:OPENGL backend:OPENGL ",
           "no setting starts OpenGL");
  started =
    startedGraphicsApis("vulkan", BackendDef::DIRECTX11, &initialized, &saved);
  testTrue(g,
           initialized && started == "window:VULKAN backend:VULKAN ",
           "a lowercase vulkan setting starts Vulkan alone");
  started =
    startedGraphicsApis("Vulkan", BackendDef::VULKAN, &initialized, &saved);
  testTrue(g,
           initialized && started == "window:VULKAN backend:VULKAN "
                                     "window:OPENGL backend:OPENGL ",
           "a Vulkan start that fails falls back to OpenGL in a new window");
  testTrue(g,
           saved == "Vulkan",
           "the fallback keeps the saved preference for the next launch");
  started =
    startedGraphicsApis("DIRECTX11", BackendDef::VULKAN, &initialized, &saved);
  testTrue(g,
           initialized && started == "window:OPENGL backend:OPENGL ",
           "an unimplemented backend starts OpenGL");
  started =
    startedGraphicsApis("metal", BackendDef::VULKAN, &initialized, &saved);
  testTrue(g,
           initialized && started == "window:OPENGL backend:OPENGL ",
           "an unknown name starts OpenGL");
  started =
    startedGraphicsApis("OPENGL", BackendDef::OPENGL, &initialized, &saved);
  testTrue(g,
           !initialized && started == "window:OPENGL backend:OPENGL ",
           "a failing OpenGL start fails without a second attempt");
}

static void
testHdrPipelineAndPure2DBypass()
{
  testSection(
    "Illumo: 3D worlds activate HDR + bloom + AgX + FXAA while pure-2D "
    "frames bypass offscreen passes");
  const std::filesystem::path path = temporaryEnvironmentPath("hdr-bypass");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "host initializes");

    RenderWorld world;
    Renderer* renderer = host.context().renderer;
    const std::array<unsigned char, 36 * 3> verts{};
    const std::array<unsigned int, 3> inds{ 0, 1, 2 };
    const MeshHandle mesh =
      renderer->enrollMesh(verts.data(),
                           verts.size(),
                           inds.data(),
                           sizeof(inds),
                           MeshVertexLayout::Pos3Norm3Color4U8Uv2,
                           false);

    // Pure-2D frame: empty RenderWorld in World + GLString in UI.
    {
      DrawList* scene = host.beginRender();
      GLString uiText("2D", 255, 255, 255, 255, 12, 0, 0, renderer);
      scene->AddDrawable(&world, RenderLayerId::World);
      scene->AddDrawable(&uiText, RenderLayerId::UI);
      host.endRender();
      testTrue(g,
               !scene->hasCustomPasses(RenderLayerId::World) &&
                 renderer->getRenderTargetPool().count() == 0,
               "pure-2D frame with empty RenderWorld bypasses HDR passes");
    }

    // Populate 3D world: HDR + 5-mip bloom + AgX + FXAA must engage.
    testTrue(g, world.createMaterial(1, RenderMaterialDesc{}), "mat created");
    RenderInstanceDesc inst;
    inst.mesh = mesh;
    inst.indexCount = 3;
    inst.material = 1;
    inst.world = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    testTrue(g, world.createInstance(1, inst), "3D instance created");
    {
      DrawList* scene = host.beginRender();
      scene->AddDrawable(&world, RenderLayerId::World);
      host.endRender();
      const std::vector<RenderPassDesc>& passes =
        scene->passesIn(RenderLayerId::World);
      // 1 WorldHdrGeom + 5 BloomDown + 4 BloomUp + 1 HdrComposite + 1
      // FxaaResolve = 12
      testEqSize(g, passes.size(), 12u, "full 12-pass HDR chain configured");
      if (passes.size() == 12u) {
        testTrue(g,
                 passes[0].name == "WorldHdrGeomPass" &&
                   passes[1].name == "BloomDown0" &&
                   passes[6].name == "BloomUp3" &&
                   passes[10].name == "HdrComposite" &&
                   passes[11].name == "FxaaResolve",
                 "HDR pass sequence ordered accurately");
      }
      const PooledRenderTarget mip0 = renderer->getRenderTarget("BloomMip0");
      const PooledRenderTarget mip4 = renderer->getRenderTarget("BloomMip4");
      testTrue(g,
               mip0.isValid() && mip0.width == 320 && mip0.height == 240 &&
                 mip4.isValid() && mip4.width == 20 && mip4.height == 15,
               "5-mip bloom pyramid sized accurately from 640x480 window");
    }

    // Enabling Sobel inserts SobelEdge between HdrComposite and FxaaResolve
    // and binds WorldHdrVelocity depth for silhouette edge detection.
    host.environment().setVar("sobelEnabled", true);
    {
      DrawList* scene = host.beginRender();
      scene->AddDrawable(&world, RenderLayerId::World);
      host.endRender();
      const std::vector<RenderPassDesc>& passes =
        scene->passesIn(RenderLayerId::World);
      testEqSize(g, passes.size(), 13u, "HDR + Sobel + FXAA builds 13 passes");
      if (passes.size() == 13u) {
        bool bindsDepth = false;
        for (const PassInputTargetBinding& binding :
             passes[11].inputTargetTextures) {
          if (binding.targetName == "WorldHdrVelocity" && binding.bindDepth) {
            bindsDepth = true;
          }
        }
        testTrue(g,
                 passes[10].name == "HdrComposite" &&
                   passes[11].name == "SobelEdge" &&
                   passes[12].name == "FxaaResolve" && bindsDepth,
                 "SobelEdge runs before FxaaResolve and binds scene depth");
      }
    }

    // Matrix Phosphor FX: mode 1 (Matrix) builds FeedbackAccum +
    // FeedbackStore (RGBA16F) + FeedbackDisplay, priming history on frame 1 and
    // reading WorldFeedbackHistory on frame 2; mode 4 (Glyph) binds uTime
    // and sets uVariant=4 on FeedbackDisplay without adding SobelEdge.
    host.environment().setVar("sobelEnabled", false);
    host.environment().setVar("motionFxMode", 1);
    {
      DrawList* scene = host.beginRender();
      scene->AddDrawable(&world, RenderLayerId::World);
      host.endRender();
      const std::vector<RenderPassDesc>& passes =
        scene->passesIn(RenderLayerId::World);
      testEqSize(
        g, passes.size(), 15u, "HDR + Phosphor + FXAA builds 15 passes");
      if (passes.size() == 15u) {
        int histValid = -1;
        for (const PassUniformInt& u : passes[11].uniformInts) {
          if (u.name == "uHistoryValid") {
            histValid = u.value;
          }
        }
        testTrue(g,
                 passes[11].name == "FeedbackAccum" &&
                   passes[12].name == "FeedbackStore" &&
                   passes[13].name == "FeedbackDisplay" &&
                   passes[14].name == "FxaaResolve" && histValid == 0 &&
                   passes[11].targetDesc.colorAttachments.size() == 2u &&
                   passes[11].targetDesc.colorAttachments[0].format ==
                     TextureFormat::RGBA16F &&
                   passes[11].targetDesc.colorAttachments[1].format ==
                     TextureFormat::RGBA16F &&
                   passes[12].targetDesc.colorAttachments.size() == 2u &&
                   passes[12].targetDesc.colorAttachments[0].format ==
                     TextureFormat::RGBA16F &&
                   passes[12].targetDesc.colorAttachments[1].format ==
                     TextureFormat::RGBA16F,
                 "Phosphor frame 1 primes 2-MRT RGBA16F feedback state");
      }
    }
    {
      DrawList* scene = host.beginRender();
      scene->AddDrawable(&world, RenderLayerId::World);
      host.endRender();
      const std::vector<RenderPassDesc>& passes =
        scene->passesIn(RenderLayerId::World);
      int histValid = -1;
      bool readsHistory = false;
      if (passes.size() == 15u) {
        for (const PassUniformInt& u : passes[11].uniformInts) {
          if (u.name == "uHistoryValid") {
            histValid = u.value;
          }
        }
        for (const PassInputTargetBinding& b : passes[11].inputTargetTextures) {
          if (b.samplerUniformName == "uHistoryTexture" &&
              b.targetName == "WorldFeedbackHistory") {
            readsHistory = true;
          }
        }
      }
      testTrue(g,
               histValid == 1 && readsHistory &&
                 passes[12].name == "FeedbackStore" &&
                 passes[12].skipWithoutFeedbackTick,
               "Phosphor frame 2 switches to live WorldFeedbackHistory and "
               "gates FeedbackStore on feedback ticks");
    }
    host.environment().setVar("motionFxMode", 4);
    {
      DrawList* scene = host.beginRender();
      scene->AddDrawable(&world, RenderLayerId::World);
      host.endRender();
      const std::vector<RenderPassDesc>& passes =
        scene->passesIn(RenderLayerId::World);
      int displayVariant = 0;
      bool displayBindsDepth = false;
      if (passes.size() == 15u) {
        for (const PassUniformInt& u : passes[13].uniformInts) {
          if (u.name == "uVariant") {
            displayVariant = u.value;
          }
        }
        for (const PassInputTargetBinding& b : passes[13].inputTargetTextures) {
          if (b.samplerUniformName == "uDepthTexture" &&
              b.targetName == "WorldHdrVelocity" && b.bindDepth) {
            displayBindsDepth = true;
          }
        }
      }
      testTrue(g,
               passes.size() == 15u && passes[13].name == "FeedbackDisplay" &&
                 passes[13].bindTime && displayVariant == 4 &&
                 displayBindsDepth,
               "FeedbackDisplay binds scene depth and uTime for variant 4");
    }
    host.environment().setVar("sobelEnabled", true);
    host.environment().setVar("motionFxMode", 5);
    {
      DrawList* scene = host.beginRender();
      scene->AddDrawable(&world, RenderLayerId::World);
      host.endRender();
      const std::vector<RenderPassDesc>& passes =
        scene->passesIn(RenderLayerId::World);
      testTrue(g,
               passes.size() == 16u && passes[10].name == "HdrComposite" &&
                 passes[11].name == "SobelEdge" &&
                 passes[12].name == "FeedbackAccum" &&
                 passes[13].name == "FeedbackStore" &&
                 passes[14].name == "FeedbackDisplay" &&
                 passes[15].name == "FxaaResolve",
               "SobelEdge runs before Feedback passes when both are enabled");
    }
    host.environment().setVar("posterizeEnabled", true);
    host.environment().setVar("posterizeMode", 1);
    host.environment().setVar("posterizeLevels", 8);
    host.environment().setVar("posterizeDitherMode", 2);
    {
      DrawList* scene = host.beginRender();
      scene->AddDrawable(&world, RenderLayerId::World);
      host.endRender();
      const std::vector<RenderPassDesc>& passes =
        scene->passesIn(RenderLayerId::World);
      int posterMode = -1;
      int posterDither = -1;
      float posterLevels = 0.0f;
      if (passes.size() == 17u) {
        for (const PassUniformInt& u : passes[16].uniformInts) {
          if (u.name == "uPosterizeMode") {
            posterMode = u.value;
          } else if (u.name == "uPosterizeDitherMode") {
            posterDither = u.value;
          }
        }
        for (const PassUniformFloat& u : passes[16].uniformFloats) {
          if (u.name == "uPosterizeLevels") {
            posterLevels = u.value;
          }
        }
      }
      testTrue(
        g,
        passes.size() == 17u && passes[15].name == "FxaaResolve" &&
          !passes[15].useScreenTarget &&
          passes[15].pooledTargetName == "WorldLdrPrePosterize" &&
          passes[16].name == "Posterize" && passes[16].useScreenTarget &&
          !passes[16].inputTargetTextures.empty() &&
          passes[16].inputTargetTextures[0].targetName ==
            "WorldLdrPrePosterize" &&
          posterMode == 1 && posterDither == 2 && posterLevels == 8.0f,
        "Posterize runs after FxaaResolve to screen with configured uniforms");
    }
    host.environment().setVar("posterizeEnabled", false);
    host.environment().setVar("motionFxMode", 0);
    host.environment().setVar("sobelEnabled", true);

    // Runtime kill-switch hdrEnabled=0 with sobelEnabled=1 runs LDR Sobel;
    // posterizeEnabled=1 runs LDR Posterize; disabling returns to backbuffer.
    host.environment().setVar("hdrEnabled", false);
    {
      DrawList* scene = host.beginRender();
      scene->AddDrawable(&world, RenderLayerId::World);
      host.endRender();
      const std::vector<RenderPassDesc>& passes =
        scene->passesIn(RenderLayerId::World);
      testTrue(
        g,
        passes.size() == 2u && passes[0].name == "WorldGeomPass" &&
          passes[1].name == "SobelEdge",
        "hdrEnabled=0 with sobelEnabled=1 runs WorldGeomPass + SobelEdge");
    }
    host.environment().setVar("sobelEnabled", false);
    host.environment().setVar("posterizeEnabled", true);
    {
      DrawList* scene = host.beginRender();
      scene->AddDrawable(&world, RenderLayerId::World);
      host.endRender();
      const std::vector<RenderPassDesc>& passes =
        scene->passesIn(RenderLayerId::World);
      testTrue(
        g,
        passes.size() == 2u && passes[0].name == "WorldGeomPass" &&
          passes[1].name == "Posterize" && passes[1].useScreenTarget &&
          !passes[1].inputTargetTextures.empty() &&
          passes[1].inputTargetTextures[0].targetName == "WorldColorVelocity",
        "hdrEnabled=0 with posterizeEnabled=1 runs WorldGeomPass + Posterize");
    }
    host.environment().setVar("posterizeEnabled", false);
    {
      DrawList* scene = host.beginRender();
      scene->AddDrawable(&world, RenderLayerId::World);
      host.endRender();
      testTrue(g,
               !scene->hasCustomPasses(RenderLayerId::World),
               "hdrEnabled=0 with post-FX disabled bypasses offscreen passes");
    }

    world.releaseResources();
    renderer->destroyMesh(mesh);
    host.shutdown();
  }
  std::filesystem::remove(path, error);
}

static int
runHostCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerIllumoHostTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Host.ScenePipelineOverrides",
               []() { return runHostCase(testScenePipelineOverrides); });
  registry.add("Illumo.Host.CloseNegotiation",
               []() { return runHostCase(testCloseNegotiation); });
  registry.add("Illumo.Host.ConfigurationOwnership",
               []() { return runHostCase(testGenericConfigurationOwnership); });
  registry.add("Illumo.Host.SavedWindowSize",
               []() { return runHostCase(testSavedWindowSize); });
  registry.add("Illumo.Host.FramePhases",
               []() { return runHostCase(testFramePhases); });
  registry.add("Illumo.Host.InitializationRollback",
               []() { return runHostCase(testInitializationRollback); });
  registry.add("Illumo.Host.FallibleFactoryBoundaries",
               []() { return runHostCase(testFallibleFactoryBoundaries); });
  registry.add("Illumo.Host.GlobalHotkeys",
               []() { return runHostCase(testGlobalHotkeys); });
  registry.add("Illumo.Host.GraphicsApiSelection",
               []() { return runHostCase(testGraphicsApiSelection); });
  registry.add("Illumo.Host.ScenePipelineConfigurationFromEnv", []() {
    return runHostCase(testScenePipelineConfigurationFromEnv);
  });
  registry.add("Illumo.Host.HdrPipelineAndPure2DBypass",
               []() { return runHostCase(testHdrPipelineAndPure2DBypass); });
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
  registry.add("Illumo.Host.DebugOverlayConsoleIsGlobal",
               []() { return runHostCase(testDebugOverlayConsoleIsGlobal); });
  registry.add("Illumo.Host.DebugOverlayWatermarkDispatched", []() {
    return runHostCase(testDebugOverlayWatermarkDispatched);
  });
  registry.add("Illumo.Host.DebugOverlayGamepad", []() {
    return runHostCase(testDebugOverlayGamepadCommandsAndDispatch);
  });
  registry.add("Illumo.Host.DebugOverlayGraphics",
               []() { return runHostCase(testDebugOverlayGraphicsPanel); });
  registry.add("Illumo.Host.DebugOverlayAxisCube",
               []() { return runHostCase(testDebugOverlayAxisCube); });
#endif
}
