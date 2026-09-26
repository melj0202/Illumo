#include "SpinningCubeScene.h"

#include <Illumo/Content/SceneDirector.h>
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

// The scene runs as the program runs it, through a director, over a headless
// engine context.
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
  SceneDirector director;
  SpinningCubeScene& cube;
  bool started;

  SpinningCubeFixture()
    : window(1280, 720)
    , env()
    , camera(glm::vec2(0.0f, 0.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , assets(&renderer, false)
    , registry()
    , console(&env, &registry, &window, &renderer, "@PROJECT_NAME@")
    , input(nullptr)
    , scene(&window, &camera)
    , context{ &scene,  &window, &console, &input,   &renderer,
               &assets, &env,    &camera,  &registry }
    , director(context)
    , cube(director.emplace<SpinningCubeScene>("cube"))
    , started(false)
  {
    env.setVar("WinX", 1280);
    env.setVar("WinY", 720);
    mock.Initialize();
    context.scenes = &director;
    started = director.switchTo("cube") && director.applyPending();
  }
};

static void
testStartupAndLifecycle()
{
  testSection("SpinningCubeScene: startup and clean lifecycle");
  SpinningCubeFixture fixture;
  testTrue(g, fixture.started, "scene started through the director");
  testTrue(g,
           fixture.director.active() == &fixture.cube,
           "the cube is the active scene");
  testTrue(g, fixture.cube.cubeVisual() != nullptr, "cube visual created");
  testTrue(g, fixture.cube.gridVisual() != nullptr, "grid visual created");
  testTrue(g, fixture.cube.showGrid(), "grid enabled by default");
  testTrue(g,
           fixture.camera.getProjectionType() == ProjectionType::Perspective,
           "camera switched to perspective projection");

  fixture.director.stopAll();
  testTrue(
    g, fixture.cube.cubeVisual() == nullptr, "cube visual released on stop");
  testTrue(
    g, fixture.cube.gridVisual() == nullptr, "grid visual released on stop");
}

static void
testRotationAndUpdate()
{
  testSection("SpinningCubeScene: rotation update and pause control");
  SpinningCubeFixture fixture;
  testTrue(g, fixture.started, "fixture initialized");

  const float initialAngle = fixture.cube.rotationAngle();
  testTrue(g, initialAngle == 0.0f, "initial rotation angle is 0");

  const float speed = fixture.cube.rotationSpeed();
  testTrue(g, speed > 0.0f, "default rotation speed is positive");

  fixture.director.update(0.5);
  const float expectedAngle = 0.5f * speed;
  testTrue(g,
           std::abs(fixture.cube.rotationAngle() - expectedAngle) < 0.001f,
           "rotation angle advanced after 0.5s update");

  fixture.cube.setPaused(true);
  testTrue(g, fixture.cube.isPaused(), "scene reports paused");
  const float pausedAngle = fixture.cube.rotationAngle();
  fixture.director.update(0.5);
  testTrue(g,
           fixture.cube.rotationAngle() == pausedAngle,
           "rotation angle unchanged while paused");

  fixture.cube.setPaused(false);
  fixture.director.update(0.5);
  testTrue(g,
           fixture.cube.rotationAngle() > pausedAngle,
           "rotation angle resumes advancing after unpause");

  fixture.cube.resetRotation();
  testTrue(g, fixture.cube.rotationAngle() == 0.0f, "rotation reset back to 0");
}

static void
testDispatchDrawables()
{
  testSection("SpinningCubeScene: dispatch drawables to the frame");
  SpinningCubeFixture fixture;
  testTrue(g, fixture.started, "fixture initialized");

  fixture.director.update(0.016);
  fixture.director.dispatch(fixture.scene);
  testTrue(g,
           fixture.scene.drawablesIn(RenderLayerId::World).size() == 2,
           "grid and cube drawn in the world layer");
  fixture.scene.ClearDrawables();
  fixture.cube.setShowGrid(false);
  fixture.director.dispatch(fixture.scene);
  testTrue(g,
           fixture.scene.drawablesIn(RenderLayerId::World).size() == 1,
           "a hidden grid is not drawn");
}

static void
testSpeedCommand()
{
  testSection("SpinningCubeScene: cube_speed is a scene command");
  SpinningCubeFixture fixture;
  testTrue(g,
           fixture.registry.HasCommand("cube_speed"),
           "cube_speed registered while the scene is active");
  fixture.registry.QueueCommand("cube_speed", { "3.5" });
  fixture.registry.ExecuteQueue();
  testTrue(g,
           fixture.cube.rotationSpeed() == 3.5f,
           "cube_speed sets the rotation speed");
  fixture.registry.QueueCommand("cube_speed", { "fast" });
  fixture.registry.ExecuteQueue();
  testTrue(g,
           fixture.cube.rotationSpeed() == 3.5f,
           "an invalid speed is refused");
  fixture.director.stopAll();
  testTrue(g,
           !fixture.registry.HasCommand("cube_speed"),
           "the command goes with the scene");
}

static void
testConsoleCapture()
{
  SpinningCubeFixture fixture;
  SpinningCubeScene& cube = fixture.cube;
  for (KeyCode key :
       { KeyCode::Space, KeyCode::R, KeyCode::G, KeyCode::Up, KeyCode::Down }) {
    cube.setPaused(false);
    cube.setShowGrid(true);
    cube.setRotationSpeed(1.2f);
    cube.update(0.5);
    const float angle = cube.rotationAngle();
    fixture.console.isOpen = true;
    InputManagerTestAccess::setKey(fixture.input, key, true);
    cube.update(0.25);
    testTrue(g,
             !cube.isPaused() && cube.showGrid() &&
               cube.rotationSpeed() == 1.2f &&
               std::abs(cube.rotationAngle() - angle - 0.3f) < 0.001f,
             "console typing and history keys leave controls unchanged while "
             "animation continues");
    fixture.console.isOpen = false;
    const float closingAngle = cube.rotationAngle();
    cube.update(0.25);
    testTrue(g,
             !cube.isPaused() && cube.showGrid() &&
               cube.rotationSpeed() == 1.2f &&
               std::abs(cube.rotationAngle() - closingAngle - 0.3f) < 0.001f,
             "key held across console close stays blocked");
    InputManagerTestAccess::setKey(fixture.input, key, false);
    cube.update(0);
    InputManagerTestAccess::setKey(fixture.input, key, true);
    cube.update(0.1);
    const bool acted = key == KeyCode::Space ? cube.isPaused()
                       : key == KeyCode::G   ? !cube.showGrid()
                       : key == KeyCode::R   ? cube.rotationAngle() < 0.2f
                       : key == KeyCode::Up  ? cube.rotationSpeed() > 1.2f
                                             : cube.rotationSpeed() < 1.2f;
    testTrue(g, acted, "fresh press resumes control after capture");
    InputManagerTestAccess::setKey(fixture.input, key, false);
    cube.update(0);
  }
  SpinningCubeFixture second;
  InputManagerTestAccess::setKey(fixture.input, KeyCode::Space, true);
  InputManagerTestAccess::setKey(second.input, KeyCode::Space, true);
  cube.setPaused(false);
  cube.update(0);
  second.cube.update(0);
  testTrue(g,
           cube.isPaused() && second.cube.isPaused(),
           "instances track edges independently");
}

static int
runSceneCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerSpinningCubeSceneTests(IllumoTestRegistry& registry)
{
  registry.add("@PROJECT_NAME@.Scene.ConsoleCapture",
               []() { return runSceneCase(testConsoleCapture); });
  registry.add("@PROJECT_NAME@.Scene.StartupAndLifecycle",
               []() { return runSceneCase(testStartupAndLifecycle); });
  registry.add("@PROJECT_NAME@.Scene.RotationAndUpdate",
               []() { return runSceneCase(testRotationAndUpdate); });
  registry.add("@PROJECT_NAME@.Scene.DispatchDrawables",
               []() { return runSceneCase(testDispatchDrawables); });
  registry.add("@PROJECT_NAME@.Scene.SpeedCommand",
               []() { return runSceneCase(testSpeedCommand); });
}
