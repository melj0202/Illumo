#include <Illumo/Rendering/Camera.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iterator>
#include <string>
#include <vector>

static TestCounters g;

static bool
nearlyEqual(float first, float second, float tolerance = 0.0001f)
{
  return std::fabs(first - second) <= tolerance;
}

static bool
historyContains(const CommandLine& console, const std::string& text)
{
  const std::vector<CommandLine::historyBuffer>& history = console.getHistory();
  for (const CommandLine::historyBuffer& line : history) {
    if (line.content.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

static void
testCameraInitializationAndControls()
{
  testSection("Camera: initialization, pan, zoom, rotation, reset");
  EnvVars env;
  env.setVar("CanvasX", 100);
  env.setVar("CanvasY", 60);
  env.setVar("WinX", 800);
  env.setVar("WinY", 600);

  Camera centered(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  testTrue(g,
           nearlyEqual(centered.GetPosition().x, 0.0f),
           "zero position is the world origin x");
  testTrue(g,
           nearlyEqual(centered.GetPosition().y, 0.0f),
           "zero position is the world origin y");

  Camera camera(glm::vec2(12.0f, 34.0f), 2.0f, &env);
  testTrue(g,
           nearlyEqual(camera.GetPosition().x, 12.0f),
           "explicit x position is preserved");
  testTrue(g,
           nearlyEqual(camera.GetPosition().y, 34.0f),
           "explicit y position is preserved");
  testTrue(
    g, nearlyEqual(camera.GetZoom(), 2.0f), "explicit zoom is preserved");

  camera.SetSmoothingSpeed(10.0f);
  testTrue(g,
           nearlyEqual(camera.GetSmoothingSpeed(), 10.0f),
           "smoothing speed setter");
  camera.Pan(glm::vec2(20.0f, -10.0f));
  camera.Update(0.05f);
  testTrue(g,
           nearlyEqual(camera.GetPosition().x, 17.0f),
           "pan interpolates x toward zoom-scaled target");
  testTrue(g,
           nearlyEqual(camera.GetPosition().y, 31.5f),
           "pan interpolates y toward zoom-scaled target");

  const glm::vec2 beforeZeroDelta = camera.GetPosition();
  camera.Update(0.0f);
  testTrue(g,
           camera.GetPosition() == beforeZeroDelta,
           "zero delta leaves camera unchanged");

  camera.ZoomAt(2.0f, glm::vec2(100.0f, 100.0f));
  camera.Update(1.0f);
  testTrue(g, nearlyEqual(camera.GetZoom(), 4.0f), "zoom factor updates zoom");
  testTrue(g,
           nearlyEqual(camera.GetPosition().x, 61.0f),
           "zoom keeps x center stable");
  testTrue(g,
           nearlyEqual(camera.GetPosition().y, 64.5f),
           "zoom keeps y center stable");

  camera.ZoomAt(1000.0f, glm::vec2(100.0f, 100.0f));
  camera.Update(1.0f);
  testTrue(
    g, nearlyEqual(camera.GetZoom(), 100.0f), "zoom clamps to upper bound");
  camera.ZoomAt(0.00001f, glm::vec2(100.0f, 100.0f));
  camera.Update(1.0f);
  testTrue(
    g, nearlyEqual(camera.GetZoom(), 0.1f), "zoom clamps to lower bound");

  camera.Rotate(90.0f);
  camera.Update(1.0f);
  const glm::vec2 centerWorld = camera.ScreenToWorld(glm::vec2(400.0f, 300.0f));
  testTrue(g,
           nearlyEqual(centerWorld.x, camera.GetPosition().x),
           "screen center maps to camera x");
  testTrue(g,
           nearlyEqual(centerWorld.y, camera.GetPosition().y),
           "screen center maps to camera y");

  camera.Reset();
  camera.Update(1.0f);
  testTrue(g,
           nearlyEqual(camera.GetPosition().x, 0.0f),
           "reset restores world origin x");
  testTrue(g,
           nearlyEqual(camera.GetPosition().y, 0.0f),
           "reset restores world origin y");
  testTrue(g, nearlyEqual(camera.GetZoom(), 1.0f), "reset restores zoom");

  Camera defaultCentered(glm::vec2(0.0f, 0.0f), 1.0f, nullptr);
  testTrue(g,
           nearlyEqual(defaultCentered.GetPosition().x, 0.0f),
           "no-env camera uses world origin x");
  testTrue(g,
           nearlyEqual(defaultCentered.GetPosition().y, 0.0f),
           "no-env camera uses world origin y");
}

static void
testCameraCoordinatesAndMatrices()
{
  testSection("Camera: coordinate conversion and matrices");
  EnvVars env;
  env.setVar("WinX", 800);
  env.setVar("WinY", 600);
  env.setVar("CanvasX", 80);
  env.setVar("CanvasY", 60);
  Camera camera(glm::vec2(200.0f, 150.0f), 2.0f, &env);

  const glm::vec2 world = camera.ScreenToWorld(glm::vec2(500.0f, 250.0f));
  testTrue(g, nearlyEqual(world.x, 250.0f), "screen x converts through zoom");
  testTrue(g,
           nearlyEqual(world.y, 175.0f),
           "screen y converts through zoom and y inversion");

  const glm::mat4 projection = camera.GetProjectionMatrix(4.0f / 3.0f);
  testTrue(g,
           nearlyEqual(projection[0][0], 2.0f / 800.0f),
           "orthographic projection x scale");
  testTrue(g,
           nearlyEqual(projection[1][1], 2.0f / 600.0f),
           "orthographic projection y scale");

  const glm::mat4 view = camera.GetViewMatrix();
  const glm::mat4 mvp = camera.GetMVPMatrix(4.0f / 3.0f);
  testTrue(g,
           std::isfinite(view[3][0]) && std::isfinite(view[3][1]),
           "view matrix is finite");
  testTrue(g,
           std::isfinite(mvp[0][0]) && std::isfinite(mvp[3][1]),
           "MVP matrix is finite");
}

static void
testCameraPerspectiveLookAt()
{
  testSection("Camera: perspective look-at and orthographic restore");
  EnvVars env;
  env.setVar("WinX", 800);
  env.setVar("WinY", 600);
  Camera camera(glm::vec2(200.0f, 150.0f), 2.0f, &env);
  testTrue(g,
           camera.getProjectionType() == ProjectionType::Orthographic,
           "default projection is orthographic");

  const glm::vec2 worldBefore = camera.ScreenToWorld(glm::vec2(500.0f, 250.0f));
  camera.lookAt(glm::vec3(12.0f, 9.0f, 12.0f),
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f));
  camera.setPerspective(55.0f, 0.1f, 100.0f);
  camera.setProjectionType(ProjectionType::Perspective);

  const float aspect = 800.0f / 600.0f;
  const glm::mat4 expectedView = glm::lookAt(glm::vec3(12.0f, 9.0f, 12.0f),
                                             glm::vec3(0.0f, 0.0f, 0.0f),
                                             glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::mat4 view = camera.GetViewMatrix();
  bool viewMatches = true;
  for (int i = 0; i < 16; ++i) {
    if (std::fabs(glm::value_ptr(view)[i] - glm::value_ptr(expectedView)[i]) >
        0.0001f) {
      viewMatches = false;
    }
  }
  testTrue(g, viewMatches, "perspective view matches glm::lookAt");

  const glm::mat4 expectedProjection =
    glm::perspective(glm::radians(55.0f), aspect, 0.1f, 100.0f);
  const glm::mat4 projection = camera.GetProjectionMatrix(aspect);
  bool projectionMatches = true;
  for (int i = 0; i < 16; ++i) {
    if (std::fabs(glm::value_ptr(projection)[i] -
                  glm::value_ptr(expectedProjection)[i]) > 0.0001f) {
      projectionMatches = false;
    }
  }
  testTrue(
    g, projectionMatches, "perspective projection matches glm::perspective");

  camera.setProjectionType(ProjectionType::Orthographic);
  const glm::vec2 worldAfter = camera.ScreenToWorld(glm::vec2(500.0f, 250.0f));
  testTrue(g,
           nearlyEqual(worldBefore.x, worldAfter.x) &&
             nearlyEqual(worldBefore.y, worldAfter.y),
           "restoring orthographic keeps ScreenToWorld");
  testTrue(g,
           nearlyEqual(camera.GetPosition().x, 200.0f),
           "2D pan state survives perspective");
}

static void
testEnvVarsTypesAndPersistence()
{
  testSection(
    "EnvVars: typed conversion, explicit-path persistence, malformed input");
  const std::filesystem::path configPath = "test-envvars.json";
  std::error_code removeError;
  std::filesystem::remove(configPath, removeError);

  {
    std::ofstream fixture(configPath, std::ios::trunc);
    fixture << "{\"plain\":\"42\",\"wrapped\":{\"value\":\"yes\"},"
               "\"ignoredNumber\":17}";
  }

  {
    EnvVars env(configPath);
    testEqInt(g,
              static_cast<int>(env.getVar("plain").valueAsLong),
              42,
              "loads string values");
    testTrue(
      g, env.getVar("wrapped").valueAsBool, "loads legacy object values");
    testTrue(g,
             env.getVar("ignoredNumber").value.empty(),
             "unsupported JSON types are ignored");

    env.setVar("double", 2.5);
    env.setVar("integer", 7);
    env.setVar("long", 8L);
    env.setVar("boolean", true);
    env.setVar("unsignedInt", static_cast<unsigned int>(9));
    env.setVar("unsignedLong", static_cast<unsigned long>(10));
    env.setVar("unsignedLongLong", static_cast<unsigned long long>(11));
    env.setVar("character", 'A');
    env.setVar("cstring", "on");
    env.setVar("invalidNumeric", std::string("not-a-number"));

    testTrue(g,
             std::fabs(env.getVar("double").valueAsDouble - 2.5) < 0.0001,
             "double overload converts value");
    testEqInt(g,
              static_cast<int>(env.getVar("integer").valueAsLong),
              7,
              "int overload converts value");
    testTrue(
      g, env.getVar("boolean").valueAsBool, "bool overload converts value");
    testEqInt(g,
              static_cast<int>(env.getVar("character").valueAsLong),
              65,
              "char overload stores numeric code");
    testTrue(g,
             env.getVar("cstring").valueAsBool,
             "C-string overload converts boolean aliases");
    testEqInt(g,
              static_cast<int>(env.getVar("invalidNumeric").valueAsLong),
              0,
              "invalid long conversion falls back to zero");
    testTrue(g,
             env.getVar("missing").value.empty(),
             "missing key returns default value");
    testTrue(g,
             env.getVars().count("unsignedLongLong") == 1u,
             "getVars exposes stored entries");
  }

  {
    EnvVars reloaded(configPath);
    testEqInt(g,
              static_cast<int>(reloaded.getVar("unsignedLong").valueAsLong),
              10,
              "saved value reloads");
    testTrue(g,
             reloaded.getVar("cstring").valueAsBool,
             "saved boolean-like string reloads");
  }

  {
    std::ofstream malformed(configPath, std::ios::trunc);
    malformed << "{";
  }
  {
    EnvVars invalid(configPath);
    testTrue(g,
             invalid.getVars().empty(),
             "malformed JSON is ignored without partial state");
  }

  std::filesystem::remove(configPath, removeError);
}

static void
testEnvVarsApplicationPath()
{
  testSection("EnvVars: application configuration ignores working directory");
#ifdef _WIN32
  const std::filesystem::path originalDirectory =
    std::filesystem::current_path();
  const std::filesystem::path configPath = EnvVars::ApplicationConfigPath();
  const std::filesystem::path changedDirectory =
    originalDirectory / "envvars-working-directory-test";
  std::error_code directoryError;
  std::filesystem::create_directory(changedDirectory, directoryError);
  std::filesystem::current_path(changedDirectory);
  const std::filesystem::path changedConfigPath =
    EnvVars::ApplicationConfigPath();
  std::filesystem::current_path(originalDirectory);
  std::filesystem::remove(changedDirectory, directoryError);

  testTrue(
    g, configPath.is_absolute(), "application configuration path is absolute");
  testTrue(
    g,
    configPath == changedConfigPath,
    "application configuration path is independent of working directory");
#else
  testTrue(g, true, "Windows production path is covered on Windows");
#endif
}

static void
testCommandRegistryMetadata()
{
  testSection("CommandRegistry: metadata, sorted names, unregister");
  CommandRegistry registry;
  registry.RegisterCommand("zeta", [](const std::vector<std::string>&) {});
  registry.RegisterCommand("alpha",
                           [](const std::vector<std::string>&) {},
                           "alpha <value>",
                           "Alpha command",
                           { "ONE", "TWO" });

  testTrue(g, registry.HasCommand("alpha"), "registered command is present");
  testTrue(g,
           registry.GetCommandUsage("alpha") == "alpha <value>",
           "usage metadata is returned");
  testTrue(g,
           registry.GetCommandDescription("alpha") == "Alpha command",
           "description metadata is returned");
  const std::vector<std::string> completions =
    registry.GetCommandCompletions("alpha");
  testEqSize(g, completions.size(), 2u, "completion metadata is returned");
  const std::vector<std::string> names = registry.GetCommandNames();
  testEqSize(g, names.size(), 2u, "registered names are listed");
  testTrue(
    g, names[0] == "alpha" && names[1] == "zeta", "command names are sorted");
  testTrue(
    g, registry.GetCommandUsage("missing").empty(), "missing usage is empty");
  testTrue(g,
           registry.GetCommandDescription("missing").empty(),
           "missing description is empty");
  testTrue(g,
           registry.GetCommandCompletions("missing").empty(),
           "missing completions are empty");

  registry.UnregisterCommand("alpha");
  testTrue(g,
           !registry.HasCommand("alpha"),
           "unregister removes command and metadata");
}

static void
testCommandRegistryQueueLifecycle()
{
  testSection("CommandRegistry: queue, clear, execute, empty callback");
  CommandRegistry registry;
  int calls = 0;
  std::vector<std::string> received;
  registry.RegisterCommand(
    "probe", [&calls, &received](const std::vector<std::string>& args) {
      calls += 1;
      received = args;
    });
  registry.RegisterCommand("empty", CommandFn());

  testTrue(
    g, !registry.QueueCommand("missing"), "unknown command is not queued");
  testTrue(
    g, registry.QueueCommand("probe", { "discarded" }), "known command queues");
  registry.ClearQueue();
  registry.ExecuteQueue();
  testEqInt(g, calls, 0, "cleared queue does not execute");

  testTrue(g,
           registry.QueueCommand("probe", { "one", "two" }),
           "command re-queues after clear");
  testTrue(
    g, registry.QueueCommand("empty"), "empty callback can be queued safely");
  registry.ExecuteQueue();
  testEqInt(g, calls, 1, "queued callback executes once");
  testEqSize(g, received.size(), 2u, "queued arguments are preserved");
  registry.ExecuteQueue();
  testEqInt(g, calls, 1, "execute clears the queue");
}

static void
testLoggerLevelsAndSinks()
{
  testSection("Logger: levels, console sink, file sink, lifecycle");
  Logger::shutdownLogger();
  testTrue(g,
           Logger::getCommandLine() == nullptr,
           "logger starts without command line context");
  Logger::LogInfo(static_cast<const char*>(nullptr));
  Logger::LogWarning("");

  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  env.setVar("logLevel", 4);
  Camera camera(glm::vec2(1.0f, 1.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  CommandRegistry registry;
  CommandLine console(&env, &registry, &window, &renderer);

  testTrue(g, Logger::initLogger(&env, &console), "logger initializes");
  testTrue(g, Logger::initLogger(), "second initialization is harmless");
  testTrue(g,
           Logger::getCommandLine() == &console,
           "logger exposes active console context");
  Logger::LogInfo("info-message");
  Logger::LogWarning(std::string("warning-message"));
  Logger::LogError("error-message");
  Logger::Log("plain-message");
  Logger::LogTrace("trace-message");
  char mutableMessage[] = "mutable-message";
  Logger::LogInfo(mutableMessage);
  Logger::LogWarning(mutableMessage);
  Logger::LogError(mutableMessage);
  Logger::Log(mutableMessage);
  Logger::LogTrace(mutableMessage);
  Logger::LogWError(L"wide");
  Logger::LogWWarning(L"wide");
  Logger::LogWInfo(L"wide");
  Logger::LogW(L"wide");

  testTrue(
    g, historyContains(console, "info-message"), "info reaches console sink");
  testTrue(g,
           historyContains(console, "warning-message"),
           "warning reaches console sink");
  testTrue(
    g, historyContains(console, "error-message"), "error reaches console sink");
  testTrue(
    g, historyContains(console, "trace-message"), "trace reaches console sink");

  const size_t historyBeforeSuppression = console.getHistory().size();
  env.setVar("logLevel", 0);
  Logger::LogError("suppressed-message");
  testEqSize(g,
             console.getHistory().size(),
             historyBeforeSuppression,
             "log level suppresses console output");

  env.setVar("logLevel", 4);
  Logger::setContext(&env, nullptr);
  Logger::LogInfo("file-only-message");
  Logger::setContext(&env, &console);
  Logger::shutdownLogger();
  testTrue(
    g, Logger::getCommandLine() == nullptr, "shutdown clears logger instance");
  Logger::LogWarning("post-shutdown-message");

  std::ifstream logFile("log.txt");
  const std::string logContents((std::istreambuf_iterator<char>(logFile)),
                                std::istreambuf_iterator<char>());
  testTrue(g,
           logContents.find("INFO: info-message") != std::string::npos,
           "file sink records info prefix");
  testTrue(g,
           logContents.find("TRACE: trace-message") != std::string::npos,
           "file sink records trace prefix");
  testTrue(g,
           logContents.find("file-only-message") != std::string::npos,
           "file sink works without console");
}

static int
runServiceCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  Logger::shutdownLogger();
  return g.failures;
}

void
registerServiceTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Camera.InitializationAndControls", []() {
    return runServiceCase(testCameraInitializationAndControls);
  });
  registry.add("Illumo.Camera.CoordinatesAndMatrices", []() {
    return runServiceCase(testCameraCoordinatesAndMatrices);
  });
  registry.add("Illumo.Camera.PerspectiveLookAt",
               []() { return runServiceCase(testCameraPerspectiveLookAt); });
  registry.add("Illumo.EnvVars.TypesAndPersistence",
               []() { return runServiceCase(testEnvVarsTypesAndPersistence); });
  registry.add("Illumo.EnvVars.ApplicationPath",
               []() { return runServiceCase(testEnvVarsApplicationPath); });
  registry.add("Illumo.CommandRegistry.Metadata",
               []() { return runServiceCase(testCommandRegistryMetadata); });
  registry.add("Illumo.CommandRegistry.QueueLifecycle", []() {
    return runServiceCase(testCommandRegistryQueueLifecycle);
  });
  registry.add("Illumo.Logger.LevelsAndSinks",
               []() { return runServiceCase(testLoggerLevelsAndSinks); });
}
