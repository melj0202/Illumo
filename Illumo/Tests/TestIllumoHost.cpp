#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
#include <Illumo/Engine/DebugOverlay.h>
#endif
#include "Rendering/RenderWindow.h"
#include <Illumo/Engine/Illumo.h>
#include <Illumo/Foundation/BuildInfo.h>
#include <Illumo/Rendering/GLString.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/IllumoTestAccess.h>
#include <Illumo/Testing/MockBackend.h>
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
    if (!rejectFullscreen && m_environment != nullptr) {
      m_environment->setVar("fullscreen",
                            !m_environment->getVar("fullscreen").valueAsBool);
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
               "SUCCESS: Fullscreen: on",
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
               "SUCCESS: Fullscreen: off",
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
               "SUCCESS: Fullscreen: off",
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
  registry.add("Illumo.Host.ScenePipelineConfigurationFromEnv", []() {
    return runHostCase(testScenePipelineConfigurationFromEnv);
  });
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
  registry.add("Illumo.Host.DebugOverlayConsoleIsGlobal",
               []() { return runHostCase(testDebugOverlayConsoleIsGlobal); });
  registry.add("Illumo.Host.DebugOverlayWatermarkDispatched", []() {
    return runHostCase(testDebugOverlayWatermarkDispatched);
  });
#endif
}
