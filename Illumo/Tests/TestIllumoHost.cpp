#ifndef NDEBUG
#include <Illumo/Engine/DebugModule.h>
#endif
#include <Illumo/Engine/IModule.h>
#include <Illumo/Engine/Illumo.h>
#include <Illumo/Rendering/IBackend.h>
#include <Illumo/Testing/IllumoTestAccess.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

static TestCounters g;

struct ModuleProbe
{
  int starts{ 0 };
  int updates{ 0 };
  int draws{ 0 };
  int exits{ 0 };
  int destructions{ 0 };
};

class OrderProbeModule : public IModule
{
public:
  OrderProbeModule(std::string* sequence, char updateTag, char drawTag)
    : m_sequence(sequence)
    , m_updateTag(updateTag)
    , m_drawTag(drawTag)
  {
  }

  bool Start(IllumoContext* context) override
  {
    ic = context;
    return true;
  }

  void Update(double) override
  {
    if (m_sequence != nullptr) {
      m_sequence->push_back(m_updateTag);
    }
  }

  void DispatchDrawables(Scene*) override
  {
    if (m_sequence != nullptr) {
      m_sequence->push_back(m_drawTag);
    }
  }

  void Exit() override {}

private:
  std::string* m_sequence;
  char m_updateTag;
  char m_drawTag;
};

class DrainAllInputModule : public IModule
{
public:
  bool Start(IllumoContext* context) override
  {
    ic = context;
    return context != nullptr && context->inputManager != nullptr;
  }

  void Update(double) override
  {
    if (ic == nullptr || ic->inputManager == nullptr) {
      return;
    }
    ic->inputManager->clearKeyQueue();
    ic->inputManager->clearCharQueue();
  }

  void DispatchDrawables(Scene*) override {}
  void Exit() override {}
};

class ProbeModule : public IModule
{
public:
  ProbeModule(ModuleProbe* probe,
              bool startResult,
              bool throwDuringStart = false,
              bool throwDuringExit = false)
    : m_probe(probe)
    , m_startResult(startResult)
    , m_throwDuringStart(throwDuringStart)
    , m_throwDuringExit(throwDuringExit)
  {
  }

  ~ProbeModule() override { m_probe->destructions += 1; }

  bool Start(IllumoContext* context) override
  {
    ic = context;
    m_probe->starts += 1;
    if (m_throwDuringStart) {
      throw std::runtime_error("probe start failure");
    }
    return m_startResult;
  }

  void Update(double) override { m_probe->updates += 1; }
  void DispatchDrawables(Scene*) override { m_probe->draws += 1; }
  void Exit() override
  {
    m_probe->exits += 1;
    if (m_throwDuringExit) {
      throw std::runtime_error("probe exit failure");
    }
  }

private:
  ModuleProbe* m_probe;
  bool m_startResult;
  bool m_throwDuringStart;
  bool m_throwDuringExit;
};

class CountingWindow : public NullRenderWindow
{
public:
  explicit CountingWindow(int* destructions)
    : NullRenderWindow(640, 480)
    , m_destructions(destructions)
  {
  }

  ~CountingWindow() override { *m_destructions += 1; }

private:
  int* m_destructions;
};

class CountingMockBackend : public MockBackend
{
public:
  CountingMockBackend(int* initializationCount,
                      bool initializeResult = true,
                      int* windowDestructions = nullptr,
                      int* cleanupsWithLiveWindow = nullptr,
                      bool throwDuringInitialize = false)
    : m_initializationCount(initializationCount)
    , m_initializeResult(initializeResult)
    , m_windowDestructions(windowDestructions)
    , m_cleanupsWithLiveWindow(cleanupsWithLiveWindow)
    , m_throwDuringInitialize(throwDuringInitialize)
  {
  }

  bool Initialize() override
  {
    *m_initializationCount += 1;
    if (m_throwDuringInitialize) {
      MockBackend::Initialize();
      throw std::runtime_error("backend initialize failure");
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
  bool m_throwDuringInitialize;
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
    host, [windowDestructions](int, int, const std::string&, IEnvVars*) {
      return std::make_unique<CountingWindow>(windowDestructions);
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

static void
testOptionalModuleFailure()
{
  testSection("Illumo: optional module failure is isolated");
  const std::filesystem::path path = temporaryEnvironmentPath("optional");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  int backendInitializations = 0;
  ModuleProbe accepted;
  ModuleProbe rejected;
  ModuleProbe required;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions, &backendInitializations);
    testTrue(g, host.initialize(), "headless host initializes");
    host.addModule(std::make_unique<ProbeModule>(&accepted, true));
    host.addModule(std::make_unique<ProbeModule>(&rejected, false));
    host.addModule(std::make_unique<ProbeModule>(&required, true),
                   ModuleRequirement::Required);
    testTrue(g, host.startModules(), "optional rejection preserves startup");
    host.update(0.016);
    host.render();
    testEqInt(g,
              rejected.destructions,
              1,
              "rejected optional module is destroyed immediately");
    host.shutdown();
  }
  testEqInt(g, accepted.updates, 1, "accepted optional module updates");
  testEqInt(g, required.draws, 1, "required module contributes drawables");
  testEqInt(g, rejected.updates, 0, "rejected optional module stays inactive");
  testEqInt(g, accepted.exits, 1, "accepted optional module exits once");
  testEqInt(g, required.exits, 1, "required module exits once");
  testEqInt(g, rejected.exits, 0, "rejected optional module is not exited");
  testEqInt(g, windowDestructions, 1, "window released once");
  testEqInt(g,
            backendInitializations,
            1,
            "host initializes the injected backend exactly once");
  std::filesystem::remove(path, error);
}

static void
testRequiredModuleRollback()
{
  testSection("Illumo: required module failure rolls back accepted modules");
  const std::filesystem::path path = temporaryEnvironmentPath("required");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  ModuleProbe accepted;
  ModuleProbe requiredFailure;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "headless host initializes");
    host.addModule(std::make_unique<ProbeModule>(&accepted, true));
    host.addModule(std::make_unique<ProbeModule>(&requiredFailure, false),
                   ModuleRequirement::Required);
    testTrue(g, !host.startModules(), "required rejection fails startup");
    host.update(0.016);
    host.render();
    host.shutdown();
  }
  testEqInt(g, accepted.exits, 1, "accepted module rolled back once");
  testEqInt(g, accepted.updates, 0, "rolled-back module never updates");
  testEqInt(g, accepted.draws, 0, "rolled-back module never renders");
  testEqInt(g, requiredFailure.exits, 0, "failed module was never accepted");
  std::filesystem::remove(path, error);
}

static void
testModuleExceptionContainment()
{
  testSection("Illumo: module exceptions remain inside lifecycle boundary");
  const std::filesystem::path path = temporaryEnvironmentPath("exceptions");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  ModuleProbe accepted;
  ModuleProbe throwingExit;
  ModuleProbe requiredThrow;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "headless host initializes");
    host.addModule(std::make_unique<ProbeModule>(&accepted, true));
    host.addModule(
      std::make_unique<ProbeModule>(&throwingExit, true, false, true));
    host.addModule(
      std::make_unique<ProbeModule>(&requiredThrow, false, true, false),
      ModuleRequirement::Required);
    testTrue(g,
             !host.startModules(),
             "throwing required module is reported as startup failure");
    host.shutdown();
  }
  testEqInt(g,
            requiredThrow.exits,
            1,
            "throwing Start receives one partial-start cleanup attempt");
  testEqInt(g,
            throwingExit.exits,
            1,
            "throwing Exit is attempted once during rollback");
  testEqInt(g,
            accepted.exits,
            1,
            "rollback continues after another module throws during Exit");
  testEqInt(g, windowDestructions, 1, "exceptional rollback releases services");
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

  int throwingWindowDestructions = 0;
  int throwingBackendInitializations = 0;
  int throwingCleanupsWithLiveWindow = 0;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &throwingWindowDestructions);
    IllumoTestAccess::setBackendFactory(host, [&](IRenderWindow*) {
      return std::unique_ptr<IBackend>(
        std::make_unique<CountingMockBackend>(&throwingBackendInitializations,
                                              true,
                                              &throwingWindowDestructions,
                                              &throwingCleanupsWithLiveWindow,
                                              true));
    });
    testTrue(g,
             !host.initialize(),
             "throwing backend initialization is returned without escaping");
    testTrue(g,
             host.context().window == nullptr,
             "throwing backend initialization clears the host context");
  }
  testEqInt(g,
            throwingBackendInitializations,
            1,
            "throwing backend receives exactly one Initialize call");
  testEqInt(g,
            throwingCleanupsWithLiveWindow,
            1,
            "throwing backend is cleaned while its window context is alive");
  testEqInt(g,
            throwingWindowDestructions,
            1,
            "throwing backend failure releases the accepted window");
  std::filesystem::remove(path, error);
}

static void
testModuleTransitionSuccess()
{
  testSection("Illumo: runtime module transition replaces primary module");
  const std::filesystem::path path =
    temporaryEnvironmentPath("transition-success");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  ModuleProbe initialPrimary;
  ModuleProbe nextPrimary;
  ModuleProbe optionalOverlay;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "host initializes");
    testTrue(g,
             host.context().moduleHost != nullptr,
             "moduleHost is wired in context");

    host.addModule(std::make_unique<ProbeModule>(&initialPrimary, true),
                   ModuleRequirement::Required);
    host.addModule(std::make_unique<ProbeModule>(&optionalOverlay, true),
                   ModuleRequirement::Optional);
    testTrue(g, host.startModules(), "modules start");

    host.update(0.016);
    host.render();
    testEqInt(g, initialPrimary.updates, 1, "initial module updated once");
    testEqInt(g, optionalOverlay.updates, 1, "overlay updated once");

    // Request transition
    host.context().moduleHost->RequestTransition(
      std::make_unique<ProbeModule>(&nextPrimary, true));
    testTrue(g, host.HasPendingTransition(), "pending transition reported");

    // Frame update triggers transition
    host.update(0.016);
    host.render();

    testTrue(g, !host.HasPendingTransition(), "pending transition cleared");
    testEqInt(g, initialPrimary.exits, 1, "initial module exited once");
    testEqInt(g, initialPrimary.destructions, 1, "initial module destroyed");
    testEqInt(g, nextPrimary.starts, 1, "next module started once");
    testEqInt(g, nextPrimary.updates, 1, "next module received update");
    testEqInt(g, nextPrimary.draws, 1, "next module received draw");
    testEqInt(g,
              optionalOverlay.updates,
              2,
              "overlay continues updating across transitions");
    testEqInt(
      g, optionalOverlay.exits, 0, "overlay was not exited during transition");

    host.shutdown();
  }
  testEqInt(g, nextPrimary.exits, 1, "next module exited on shutdown");
  testEqInt(g, optionalOverlay.exits, 1, "overlay exited on shutdown");
  std::filesystem::remove(path, error);
}

static void
testModuleTransitionFailureShutdown()
{
  testSection("Illumo: failing module transition safely closes application");
  const std::filesystem::path path =
    temporaryEnvironmentPath("transition-fail");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  ModuleProbe initialPrimary;
  ModuleProbe failingPrimary;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "host initializes");

    host.addModule(std::make_unique<ProbeModule>(&initialPrimary, true),
                   ModuleRequirement::Required);
    testTrue(g, host.startModules(), "module starts");

    host.context().moduleHost->RequestTransition(std::make_unique<ProbeModule>(
      &failingPrimary, false, true)); // throws during Start

    host.update(0.016);
    testTrue(g, host.shouldClose(), "failed transition requests close");
    testEqInt(
      g, initialPrimary.exits, 1, "initial module exited before failing start");

    host.shutdown();
  }
  std::filesystem::remove(path, error);
}

static void
testOptionalOverlayUpdatesBeforeRequired()
{
  testSection("Illumo: optional overlays update first and draw last");
  const std::filesystem::path path = temporaryEnvironmentPath("overlay-order");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  std::string sequence;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "host initializes");

    host.addModule(std::make_unique<OrderProbeModule>(&sequence, 'R', 'r'),
                   ModuleRequirement::Required);
    host.addModule(std::make_unique<OrderProbeModule>(&sequence, 'O', 'o'),
                   ModuleRequirement::Optional);
    testTrue(g, host.startModules(), "modules start");

    host.update(0.016);
    testTrue(g,
             sequence == "OR",
             "optional overlay updates before the required module");
    sequence.clear();
    host.render();
    testTrue(
      g, sequence == "ro", "required module draws before optional overlays");

    host.shutdown();
  }
  std::filesystem::remove(path, error);
}

#ifndef NDEBUG
static void
testDebugOverlayConsoleIsGlobal()
{
  testSection("Illumo: Debug overlay toggles console before product input");
  const std::filesystem::path path =
    temporaryEnvironmentPath("debug-overlay-console");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "host initializes");

    host.addModule(std::make_unique<DrainAllInputModule>(),
                   ModuleRequirement::Required);
    host.addModule(std::make_unique<DebugModule>(),
                   ModuleRequirement::Optional);
    testTrue(g, host.startModules(), "modules start");
    testTrue(g,
             host.context().commandLine != nullptr &&
               !host.context().commandLine->isOpen,
             "console starts closed");

    host.context().inputManager->getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::Grave, InputAction::Press, 0 });
    host.update(0.016);
    testTrue(g,
             host.context().commandLine->isOpen,
             "Grave toggles console even if the required module drains input");

    host.shutdown();
  }
  std::filesystem::remove(path, error);
}
#endif

static void
testInputQueueFlushedOnTransition()
{
  testSection("Illumo: input queues are cleared on module transition");
  const std::filesystem::path path =
    temporaryEnvironmentPath("transition-input");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  ModuleProbe initialPrimary;
  ModuleProbe nextPrimary;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "host initializes");

    host.addModule(std::make_unique<ProbeModule>(&initialPrimary, true),
                   ModuleRequirement::Required);
    testTrue(g, host.startModules(), "module starts");

    // Queue dummy key and char
    host.context().inputManager->getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::Enter, InputAction::Press, 0 });
    host.context().inputManager->getCharQueue().push('a');
    testTrue(g,
             !host.context().inputManager->getKeyQueue().empty(),
             "key queue has input");
    testTrue(g,
             !host.context().inputManager->getCharQueue().empty(),
             "char queue has input");

    host.context().moduleHost->RequestTransition(
      std::make_unique<ProbeModule>(&nextPrimary, true));

    // Update triggers transition
    host.update(0.016);

    testTrue(g,
             host.context().inputManager->getKeyQueue().empty(),
             "key queue flushed on transition");
    testTrue(g,
             host.context().inputManager->getCharQueue().empty(),
             "char queue flushed on transition");

    host.shutdown();
  }
  std::filesystem::remove(path, error);
}

static void
testGlobalHotkeys()
{
  testSection("Illumo: host handles F11, F3, and F5 global shortcuts");
  const std::filesystem::path path = temporaryEnvironmentPath("global-hotkeys");
  std::error_code error;
  std::filesystem::remove(path, error);
  int windowDestructions = 0;
  ModuleProbe primaryProbe;
  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions);
    testTrue(g, host.initialize(), "host initializes");

    host.addModule(std::make_unique<ProbeModule>(&primaryProbe, true),
                   ModuleRequirement::Required);
    testTrue(g, host.startModules(), "modules start");

    // 1. Test F11 (Fullscreen toggle)
    const bool initialFullscreen =
      host.environment().getVar("fullscreen").valueAsBool;
    host.context().inputManager->getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::F11, InputAction::Press, 0 });
    host.update(0.016);
    testEqInt(g,
              host.environment().getVar("fullscreen").valueAsBool,
              !initialFullscreen,
              "F11 toggles fullscreen envvar");

    // 2. Test F3 (FPS overlay toggle)
    const bool initialShowFps =
      host.environment().getVar("showFPS").valueAsBool;
    host.context().inputManager->getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::F3, InputAction::Press, 0 });
    host.update(0.016);
    testEqInt(g,
              host.environment().getVar("showFPS").valueAsBool,
              !initialShowFps,
              "F3 toggles showFPS envvar");

    // 3. Test F5 (Asset reload)
    host.context().inputManager->getKeyQueue().push(
      InputManager::KeyPressEvent{ KeyCode::F5, InputAction::Press, 0 });
    host.update(0.016);
    testTrue(g,
             host.context().inputManager->getKeyQueue().empty(),
             "F5 event consumed");

    // 4. Test console open suppresses hotkey interception
    if (host.context().commandLine != nullptr) {
      host.context().commandLine->isOpen = true;
      const bool beforeHotkeys =
        host.environment().getVar("showFPS").valueAsBool;
      host.context().inputManager->getKeyQueue().push(
        InputManager::KeyPressEvent{ KeyCode::F3, InputAction::Press, 0 });
      // Clear queue at end of update normally, but during update global hotkeys
      // shouldn't fire We can verify showFPS did not change
      host.update(0.016);
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
  ModuleProbe probe;

  {
    Illumo host(headlessConfig(path));
    setHeadlessFactories(host, &windowDestructions, &backendInitializations);
    testTrue(g, host.initialize(), "host initialized");

    host.addModule(std::make_unique<ProbeModule>(&probe, true),
                   ModuleRequirement::Required);
    testTrue(g, host.startModules(), "modules started");

    Scene* scene = IllumoTestAccess::getScene(host);
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
  registry.add("Illumo.Host.ConfigurationOwnership",
               []() { return runHostCase(testGenericConfigurationOwnership); });
  registry.add("Illumo.Host.OptionalModuleFailure",
               []() { return runHostCase(testOptionalModuleFailure); });
  registry.add("Illumo.Host.RequiredModuleRollback",
               []() { return runHostCase(testRequiredModuleRollback); });
  registry.add("Illumo.Host.ModuleExceptionContainment",
               []() { return runHostCase(testModuleExceptionContainment); });
  registry.add("Illumo.Host.InitializationRollback",
               []() { return runHostCase(testInitializationRollback); });
  registry.add("Illumo.Host.FallibleFactoryBoundaries",
               []() { return runHostCase(testFallibleFactoryBoundaries); });
  registry.add("Illumo.Host.ModuleTransitionSuccess",
               []() { return runHostCase(testModuleTransitionSuccess); });
  registry.add("Illumo.Host.ModuleTransitionFailureShutdown", []() {
    return runHostCase(testModuleTransitionFailureShutdown);
  });
  registry.add("Illumo.Host.InputQueueFlushedOnTransition",
               []() { return runHostCase(testInputQueueFlushedOnTransition); });
  registry.add("Illumo.Host.OptionalOverlayUpdatesFirst", []() {
    return runHostCase(testOptionalOverlayUpdatesBeforeRequired);
  });
  registry.add("Illumo.Host.GlobalHotkeys",
               []() { return runHostCase(testGlobalHotkeys); });
  registry.add("Illumo.Host.ScenePipelineConfigurationFromEnv", []() {
    return runHostCase(testScenePipelineConfigurationFromEnv);
  });
#ifndef NDEBUG
  registry.add("Illumo.Host.DebugOverlayConsoleIsGlobal",
               []() { return runHostCase(testDebugOverlayConsoleIsGlobal); });
#endif
}
