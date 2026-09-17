#include "SpinningCubeModule.h"

#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>

static TestCounters g;

class InputManagerTestAccess
{
public:
  static void setKey(InputManager& input, KeyCode key, bool down)
  {
    input.inputStatesCurrent[key] =
      down ? InputAction::Press : InputAction::Release;
  }
};

struct SpinningCubeFixture
{
  NullRenderWindow window;
  EnvVars env;
  Camera camera;
  MockBackend mock;
  Renderer renderer;
  AssetManager assets;
  CommandRegistry registry;
  CommandLine console;
  InputManager input;
  Scene scene;
  IllumoContext context;
  SpinningCubeModule module;
  bool started;

  SpinningCubeFixture()
    : window(1280, 720)
    , env()
    , camera(glm::vec2(0.0f, 0.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , assets(&renderer, false)
    , registry()
    , console(&env, &registry, &window, &renderer, "SpinningCube")
    , input(nullptr)
    , scene(&window, &camera)
    , context{ &scene,  &window, &console, &input,   &renderer,
               &assets, &env,    &camera,  &registry }
    , module()
    , started(false)
  {
    env.setVar("WinX", 1280);
    env.setVar("WinY", 720);
    mock.Initialize();
    started = module.Start(&context);
  }

  ~SpinningCubeFixture()
  {
    if (started) {
      module.Exit();
    }
  }
};

static void
testModuleStartupAndLifecycle()
{
  testSection("SpinningCubeModule: startup and clean lifecycle");
  SpinningCubeFixture fixture;
  testTrue(g, fixture.started, "module started successfully");
  testTrue(g, fixture.module.cubeVisual() != nullptr, "cube visual created");
  testTrue(g, fixture.module.gridVisual() != nullptr, "grid visual created");
  testTrue(g, fixture.module.showGrid(), "grid enabled by default");
  testTrue(g,
           fixture.camera.getProjectionType() == ProjectionType::Perspective,
           "camera switched to perspective projection");

  fixture.module.Exit();
  testTrue(
    g, fixture.module.cubeVisual() == nullptr, "cube visual released on exit");
  testTrue(
    g, fixture.module.gridVisual() == nullptr, "grid visual released on exit");
}

static void
testRotationAndUpdate()
{
  testSection("SpinningCubeModule: rotation update and pause control");
  SpinningCubeFixture fixture;
  testTrue(g, fixture.started, "fixture initialized");

  const float initialAngle = fixture.module.rotationAngle();
  testTrue(g, initialAngle == 0.0f, "initial rotation angle is 0");

  const float speed = fixture.module.rotationSpeed();
  testTrue(g, speed > 0.0f, "default rotation speed is positive");

  fixture.module.Update(0.5);
  const float expectedAngle = 0.5f * speed;
  testTrue(g,
           std::abs(fixture.module.rotationAngle() - expectedAngle) < 0.001f,
           "rotation angle advanced after 0.5s update");

  // Test pause
  fixture.module.setPaused(true);
  testTrue(g, fixture.module.isPaused(), "module reports paused");
  const float pausedAngle = fixture.module.rotationAngle();
  fixture.module.Update(0.5);
  testTrue(g,
           fixture.module.rotationAngle() == pausedAngle,
           "rotation angle unchanged while paused");

  // Test resume
  fixture.module.setPaused(false);
  fixture.module.Update(0.5);
  testTrue(g,
           fixture.module.rotationAngle() > pausedAngle,
           "rotation angle resumes advancing after unpause");

  // Test reset
  fixture.module.resetRotation();
  testTrue(
    g, fixture.module.rotationAngle() == 0.0f, "rotation reset back to 0");
}

static void
testDispatchDrawables()
{
  testSection("SpinningCubeModule: dispatch drawables to scene");
  SpinningCubeFixture fixture;
  testTrue(g, fixture.started, "fixture initialized");

  fixture.module.Update(0.016);
  fixture.module.DispatchDrawables(&fixture.scene);
  // Successfully dispatched without throwing or crashing
  testTrue(g, true, "drawables dispatched to scene");
}

static void
testConsoleCapture()
{
  SpinningCubeFixture fixture;
  for (KeyCode key :
       { KeyCode::Space, KeyCode::R, KeyCode::G, KeyCode::Up, KeyCode::Down }) {
    fixture.module.setPaused(false);
    fixture.module.setShowGrid(true);
    fixture.module.setRotationSpeed(1.2f);
    fixture.module.Update(0.5);
    const float angle = fixture.module.rotationAngle();
    fixture.console.isOpen = true;
    InputManagerTestAccess::setKey(fixture.input, key, true);
    fixture.module.Update(0.25);
    testTrue(g,
             !fixture.module.isPaused() && fixture.module.showGrid() &&
               fixture.module.rotationSpeed() == 1.2f &&
               std::abs(fixture.module.rotationAngle() - angle - 0.3f) < 0.001f,
             "console typing and history keys leave controls unchanged while "
             "animation continues");
    fixture.console.isOpen = false;
    const float closingAngle = fixture.module.rotationAngle();
    fixture.module.Update(0.25);
    testTrue(g,
             !fixture.module.isPaused() && fixture.module.showGrid() &&
               fixture.module.rotationSpeed() == 1.2f &&
               std::abs(fixture.module.rotationAngle() - closingAngle - 0.3f) <
                 0.001f,
             "key held across console close stays blocked");
    InputManagerTestAccess::setKey(fixture.input, key, false);
    fixture.module.Update(0);
    InputManagerTestAccess::setKey(fixture.input, key, true);
    fixture.module.Update(0.1);
    const bool acted =
      key == KeyCode::Space ? fixture.module.isPaused()
      : key == KeyCode::G   ? !fixture.module.showGrid()
      : key == KeyCode::R   ? fixture.module.rotationAngle() < 0.2f
      : key == KeyCode::Up  ? fixture.module.rotationSpeed() > 1.2f
                            : fixture.module.rotationSpeed() < 1.2f;
    testTrue(g, acted, "fresh press resumes control after capture");
    InputManagerTestAccess::setKey(fixture.input, key, false);
    fixture.module.Update(0);
  }
  SpinningCubeFixture second;
  InputManagerTestAccess::setKey(fixture.input, KeyCode::Space, true);
  InputManagerTestAccess::setKey(second.input, KeyCode::Space, true);
  fixture.module.setPaused(false);
  fixture.module.Update(0);
  second.module.Update(0);
  testTrue(g,
           fixture.module.isPaused() && second.module.isPaused(),
           "instances track edges independently");
}

static int
runModuleCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerSpinningCubeModuleTests(IllumoTestRegistry& registry)
{
  registry.add("@PROJECT_NAME@.Module.ConsoleCapture",
               []() { return runModuleCase(testConsoleCapture); });
  registry.add("@PROJECT_NAME@.Module.StartupAndLifecycle",
               []() { return runModuleCase(testModuleStartupAndLifecycle); });
  registry.add("@PROJECT_NAME@.Module.RotationAndUpdate",
               []() { return runModuleCase(testRotationAndUpdate); });
  registry.add("@PROJECT_NAME@.Module.DispatchDrawables",
               []() { return runModuleCase(testDispatchDrawables); });
}
