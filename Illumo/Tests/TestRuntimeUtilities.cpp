#include "Rendering/BackendConfig.h"
#include "Rendering/PresentationTiming.h"
#include <GLFW/glfw3.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/SplashText.h>
#include <Illumo/Services/InputContext.h>
#include <Illumo/Testing/TestAccess.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static TestCounters g;

static void
testInputContextBindings()
{
  testSection("InputContext: action bindings");
  InputContext context;
  InputEvent event;
  event.keyCode = KeyCode::A;
  event.inputAction = InputAction::Press;
  context.bindAction("primary", event);
  testEqSize(g, context.getActions().size(), 1, "binding is stored");
  testTrue(g,
           context.getActionTag("primary").keyCode == KeyCode::A,
           "bound key is returned");

  event.keyCode = KeyCode::B;
  event.inputAction = InputAction::Hold;
  context.bindAction("primary", event);
  testEqSize(
    g, context.getActions().size(), 1, "rebinding replaces existing action");
  testTrue(g,
           context.getActionTag("primary").inputAction == InputAction::Hold,
           "rebound action is returned");

  bool threw = false;
  try {
    (void)context.getActionTag("missing");
  } catch (const std::out_of_range&) {
    threw = true;
  }
  testTrue(g, threw, "unknown action reports out_of_range");
}

static void
testInputManagerMappings()
{
  testSection("InputManager: key and action translation");
  InputManager input(nullptr);
  for (int value = static_cast<int>(KeyCode::Space);
       value <= static_cast<int>(KeyCode::MouseButton8);
       ++value) {
    const KeyCode key = static_cast<KeyCode>(value);
    const int glfwKey = InputManagerTestAccess::toGlfw(input, key);
    testTrue(g, glfwKey != -1, "known key maps to GLFW");
    testTrue(g,
             InputManagerTestAccess::fromGlfw(input, glfwKey) == key,
             "GLFW key maps back to engine key");
  }
  testEqInt(g,
            InputManagerTestAccess::toGlfw(input, KeyCode::None),
            -1,
            "None has no GLFW token");
  testTrue(g,
           InputManagerTestAccess::fromGlfw(input, -999) == KeyCode::None,
           "unknown GLFW token maps to None");
  testTrue(g,
           InputManagerTestAccess::actionFromGlfw(input, GLFW_PRESS) ==
             InputAction::Press,
           "press action maps");
  testTrue(g,
           InputManagerTestAccess::actionFromGlfw(input, GLFW_RELEASE) ==
             InputAction::Release,
           "release action maps");
  testTrue(g,
           InputManagerTestAccess::actionFromGlfw(input, GLFW_REPEAT) ==
             InputAction::Hold,
           "repeat action maps to hold");
  testTrue(g,
           InputManagerTestAccess::actionFromGlfw(input, -1) ==
             InputAction::Release,
           "unknown action safely maps to release");
}

static void
testInputManagerHeadlessLifecycle()
{
  testSection("InputManager: null-window lifecycle and callbacks");
  InputManager input(nullptr);
  testTrue(g,
           input.GetInputAction(KeyCode::None) == InputAction::None,
           "None action is inert");
  testTrue(g,
           input.GetInputAction(KeyCode::A) == InputAction::None,
           "null-window key action is inert");
  testTrue(
    g, !input.isKeyPressed(KeyCode::A), "null-window key press is false");
  testTrue(
    g, !input.isKeyReleased(KeyCode::A), "null-window key release is false");
  testTrue(g,
           !input.isMouseButtonPressed(KeyCode::MouseLeft),
           "null-window mouse press is false");
  testTrue(g,
           !input.isMouseButtonReleased(KeyCode::MouseLeft),
           "null-window mouse release is false");
  const std::array<double, 2> mouse = input.getMousePosition();
  testTrue(g,
           mouse[0] == 0.0 && mouse[1] == 0.0,
           "null-window mouse position is origin");

  InputManager::characterCallback(nullptr, static_cast<unsigned int>('x'));
  InputManager::normalKeyCallback(nullptr, GLFW_KEY_A, 0, GLFW_PRESS, 2);
  InputManager::scrollCallback(nullptr, 0.0, -3.5);
  testEqSize(
    g, input.getCharQueue().size(), 1, "character callback queues input");
  testEqSize(g, input.getKeyQueue().size(), 1, "key callback queues input");
  testTrue(g,
           input.getKeyQueue().front().key == KeyCode::A,
           "queued key is translated");
  testTrue(
    g, *input.getMouseScrollOffset() == -3.5, "scroll callback stores offset");
  input.clearCharQueue();
  input.clearKeyQueue();
  testEqSize(g, input.getCharQueue().size(), 0, "character queue clears");
  testEqSize(g, input.getKeyQueue().size(), 0, "key queue clears");

  input.update();
  testTrue(
    g, *input.getMouseScrollOffset() == 0.0, "update resets scroll offset");
  testTrue(g,
           input.GetInputAction(KeyCode::A) == InputAction::None,
           "headless update remains inert");
}

static void
testInputManagerContextsAndCapacity()
{
  testSection("InputManager: contexts, action lookup, and capacity");
  InputManager input(nullptr);
  InputContext first;
  InputEvent event;
  event.keyCode = KeyCode::E;
  event.inputAction = InputAction::Press;
  first.bindAction("toggle", event);
  const long firstId = input.registerInputContext(first);
  testEqInt(g, static_cast<int>(firstId), 0, "first context receives id zero");
  input.setActiveInputContext(firstId);
  testTrue(g,
           input.getActiveInputContext()->getActionTag("toggle").keyCode ==
             KeyCode::E,
           "active context is selected");
  InputManagerTestAccess::setAction(input, KeyCode::E, InputAction::Press);
  testTrue(
    g, input.isActionActive("toggle"), "bound action matches cached state");
  InputManagerTestAccess::setAction(input, KeyCode::E, InputAction::Hold);
  testTrue(
    g, !input.isActionActive("toggle"), "different cached state is inactive");

  for (int i = 1; i < NUM_INPUT_CONTEXTS; ++i) {
    testTrue(g,
             input.registerInputContext(InputContext()) >= 0,
             "context registers below capacity");
  }
  testEqInt(g,
            static_cast<int>(input.registerInputContext(InputContext())),
            -1,
            "context capacity is enforced");
}

static void
testBackendConfigTokens()
{
  testSection("BackendConfig: environment token conversion");
  EnvVars env;
  const BackendDef definitions[] = { BackendDef::OPENGL,
                                     BackendDef::OPENGL_ES,
                                     BackendDef::VULKAN,
                                     BackendDef::DIRECTX12,
                                     BackendDef::DIRECTX11 };
  for (BackendDef definition : definitions) {
    const std::string token = TokenToString(definition);
    env.setVar("GraphicsAPI", token);
    testTrue(g,
             StringToToken(&env) == definition,
             "graphics backend token round-trips");
  }
  env.setVar("GraphicsAPI", "UNKNOWN");
  testTrue(g,
           StringToToken(&env) == BackendDef::OPENGL,
           "unknown backend falls back to OpenGL");
  testTrue(g,
           TokenToString(static_cast<BackendDef>(999)) == "OPENGL",
           "unknown enum falls back to OpenGL");
}

static void
testPresentationTimingPolicy()
{
  testSection("PresentationTiming: vsync policy and FPS labels");
  testTrue(g,
           isVsyncRequested(nullptr),
           "missing environment defaults to synchronized presentation");

  EnvVars env;
  testTrue(g,
           isVsyncRequested(&env),
           "missing vsync value defaults to synchronized presentation");
  env.setVar("vsync", false);
  testTrue(
    g, !isVsyncRequested(&env), "disabled vsync selects uncapped presentation");
  env.setVar("vsync", true);
  testTrue(g,
           isVsyncRequested(&env),
           "enabled vsync selects frame-paced presentation");

  testTrue(g,
           buildFrameRateLabel(true, 144, 144) ==
             "Paced FPS: 144 | Submit FPS: 144",
           "paced label separates swap cadence from submissions");
  testTrue(g,
           buildFrameRateLabel(false, 999, 4812) ==
             "Paced FPS: off | Submit FPS: 4812",
           "uncapped label does not misreport submissions as presented FPS");

  testTrue(
    g, getTargetFps(nullptr) == 60, "missing environment defaults to 60 FPS");
  EnvVars fpsEnv;
  testTrue(
    g, getTargetFps(&fpsEnv) == 60, "missing fps var defaults to 60 FPS");
  fpsEnv.setVar("fps", 144);
  testTrue(
    g, getTargetFps(&fpsEnv) == 144, "positive fps var returns target FPS");
  fpsEnv.setVar("fps", 0);
  testTrue(g, getTargetFps(&fpsEnv) == 0, "zero fps var selects uncapped");
  fpsEnv.setVar("fps", -10);
  testTrue(g, getTargetFps(&fpsEnv) == 0, "negative fps var selects uncapped");

  testTrue(g,
           calculateTargetFrameDuration(0) == std::chrono::nanoseconds::zero(),
           "zero target FPS has zero target duration");
  testTrue(g,
           calculateTargetFrameDuration(60) ==
             std::chrono::nanoseconds(1'000'000'000LL / 60),
           "60 FPS calculates correct duration");

  // Platform timer resolution RAII scope
  {
    PlatformTimerScope timerScope;
    PlatformCpuPause();
  }

  // shouldPace policy checks
  testTrue(g, !shouldPace(false, 60, 0), "uncapped target FPS does not pace");
  testTrue(g, !shouldPace(false, 60, -10), "negative target FPS does not pace");
  testTrue(g,
           shouldPace(false, 60, 60),
           "positive target FPS paces when VSync is disabled");
  testTrue(g,
           shouldPace(false, 60, 144),
           "high target FPS paces when VSync is disabled");
  testTrue(
    g,
    !shouldPace(true, 60, 60),
    "matching target FPS bypasses software pacing when VSync is enabled");
  testTrue(g,
           !shouldPace(true, 60, 120),
           "higher target FPS bypasses software pacing when VSync is enabled");
  testTrue(g,
           shouldPace(true, 60, 30),
           "lower target FPS engages software pacing when VSync is enabled");
  testTrue(
    g,
    shouldPace(true, 144, 60),
    "60 FPS target on 144 Hz display engages software pacing under VSync");

  // FramePacer cadence and state
  {
    PlatformTimerScope timerScope;
    FramePacer pacer;
    testTrue(g, !pacer.hasTarget(), "new pacer has no initial target");

    // First call sets the target deadline without waiting
    testTrue(g, pacer.pace(100, false, 60), "pacer pace succeeds");
    testTrue(g, pacer.hasTarget(), "pacer establishes target after first pace");
    const auto firstDeadline = pacer.nextDeadline();

    // Second call waits to meet deadline and advances by 10ms (100 FPS)
    testTrue(g, pacer.pace(100, false, 60), "second pace call succeeds");
    const auto secondDeadline = pacer.nextDeadline();
    testTrue(g,
             secondDeadline > firstDeadline,
             "pacer advances deadline by frame duration");

    // Reset clears state
    pacer.reset();
    testTrue(g, !pacer.hasTarget(), "reset clears pacer target");

    // Hitch reset: simulating a long stall
    pacer.pace(100, false, 60);
    std::this_thread::sleep_for(std::chrono::milliseconds(25)); // > 1.5 * 10ms
    const auto beforeHitch = std::chrono::steady_clock::now();
    pacer.pace(100, false, 60);
    const auto afterHitchDeadline = pacer.nextDeadline();
    testTrue(g,
             afterHitchDeadline >= beforeHitch,
             "pacer hard resets deadline after hitch without catch-up burst");
  }

  // Verify paceFrame delays approximately the target duration
  {
    PlatformTimerScope timerScope;
    const auto start = std::chrono::steady_clock::now();
    // 100 FPS = 10ms frame time
    paceFrame(start, 100);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    testTrue(g,
             elapsed >= std::chrono::milliseconds(9),
             "paceFrame waits for at least the requested duration");
    testTrue(g,
             elapsed < std::chrono::milliseconds(50),
             "paceFrame does not overshoot excessively");
  }
}

static void
testAssetManagerEnrollment()
{
  testSection("AssetManager: cache, fallback, and reference counts");
  HeadlessRenderFixture fixture(4, 4);
  AssetManager assets(&fixture.renderer, false);
  const std::filesystem::path path =
    std::filesystem::current_path() / "asset-cache-test.ppm";
  {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n1 1\n255\n";
    const unsigned char pixel[3] = { 255, 32, 16 };
    file.write(reinterpret_cast<const char*>(pixel), 3);
  }

  TextureHandle first = assets.acquireTexture(
    path.string(), TextureOptions{}, AssetLoadMode::Synchronous);
  TextureHandle duplicate =
    assets.acquireTexture((path.parent_path() / "." / path.filename()).string(),
                          TextureOptions{},
                          AssetLoadMode::Async);
  testTrue(g, first == duplicate, "canonical duplicate returns same handle");
  AssetStatus status = assets.getState(first);
  testTrue(g, status.state == AssetState::Ready, "synchronous load is ready");
  testEqInt(g,
            static_cast<int>(status.referenceCount),
            2,
            "duplicate acquisition increments reference count");
  TextureInfo info = assets.getTextureInfo(first);
  testEqInt(g, info.width, 1, "decoded width replaces fallback metadata");
  testTrue(g, assets.releaseTexture(first), "first release succeeds");
  testTrue(g,
           fixture.mock.IsTextureValid(first),
           "resource survives while one reference remains");
  testTrue(g, assets.releaseTexture(first), "last release succeeds");
  testTrue(g,
           !fixture.mock.IsTextureValid(first),
           "last release destroys backend resource");

  TextureHandle missing = assets.acquireTexture(
    "missing-asset.png", TextureOptions{}, AssetLoadMode::Synchronous);
  AssetStatus missingStatus = assets.getState(missing);
  testTrue(g,
           missingStatus.state == AssetState::Failed,
           "missing texture enters failed state");
  testTrue(g,
           fixture.mock.IsTextureValid(missing),
           "failed initial load keeps stable fallback texture");
  testEqInt(g,
            assets.getTextureInfo(missing).width,
            2,
            "fallback texture info remains available");

  const std::filesystem::path atlasPath =
    std::filesystem::path(__FILE__).parent_path().parent_path() / "Assets" /
    "RendererDemo" / "showcase-atlas.ppm";
  TextureHandle atlas = assets.acquireTexture(
    atlasPath.string(), TextureOptions{}, AssetLoadMode::Synchronous);
  testTrue(g,
           assets.getState(atlas).state == AssetState::Ready,
           "first-party renderer atlas decodes successfully");
  TextureInfo atlasInfo = assets.getTextureInfo(atlas);
  testTrue(g,
           atlasInfo.width == 8 && atlasInfo.height == 2,
           "first-party renderer atlas has expected dimensions");
}

static void
testAssetManagerLookupAndShutdown()
{
  testSection("AssetManager: reload retention and pending cancellation");
  HeadlessRenderFixture fixture(4, 4);
  AssetManager assets(&fixture.renderer, false);
  const std::filesystem::path path =
    std::filesystem::current_path() / "asset-reload-test.ppm";
  {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n1 1\n255\n";
    const unsigned char pixel[3] = { 8, 16, 32 };
    file.write(reinterpret_cast<const char*>(pixel), 3);
  }

  TextureHandle handle = assets.acquireTexture(
    path.string(), TextureOptions{}, AssetLoadMode::Synchronous);
  testEqSize(
    g, assets.getState(handle).revision, 1u, "initial revision is one");
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "P6\n2 1\n255\n";
    const unsigned char pixels[6] = { 8, 16, 32, 64, 128, 255 };
    file.write(reinterpret_cast<const char*>(pixels), 6);
  }
  testTrue(g, assets.reload(handle), "explicit reload is queued");
  testTrue(
    g, !assets.reload(handle), "duplicate reload is coalesced while pending");
  assets.completePendingForTests();
  testEqSize(
    g, assets.getState(handle).revision, 2u, "reload increments revision");
  testEqInt(g,
            assets.getTextureInfo(handle).width,
            2,
            "reload replaces texture metadata");

  fixture.mock.setRejectNextTextureReplacement(true);
  testTrue(g, assets.reload(handle), "failed upload reload is queued");
  assets.completePendingForTests();
  AssetStatus failedReload = assets.getState(handle);
  testTrue(g,
           failedReload.state == AssetState::Ready,
           "failed reload retains last ready state");
  testEqSize(g, failedReload.revision, 2u, "failed reload retains revision");
  testTrue(g, !failedReload.lastError.empty(), "failed reload records error");

  TextureHandle pending = assets.acquireTexture(
    "release-while-pending.png", TextureOptions{}, AssetLoadMode::Async);
  testTrue(g,
           assets.getState(pending).state == AssetState::Pending,
           "async request starts pending with fallback");
  testTrue(g, assets.releaseTexture(pending), "pending asset can be released");
  assets.completePendingForTests();
  testTrue(g,
           !fixture.mock.IsTextureValid(pending),
           "obsolete pending result cannot resurrect released handle");

  {
    AssetManager workerAssets(&fixture.renderer);
    workerAssets.acquireTexture(
      "worker-cancel.png", TextureOptions{}, AssetLoadMode::Async);
  }
  testTrue(g, true, "worker joins cleanly with pending work");
}

static void
testAssetManagerShaderLifecycle()
{
  testSection("AssetManager: shader cache and failed reload retention");
  HeadlessRenderFixture fixture(4, 4);
  AssetManager assets(&fixture.renderer, false);
  const std::filesystem::path vertexPath =
    std::filesystem::current_path() / "asset-test.vert";
  const std::filesystem::path fragmentPath =
    std::filesystem::current_path() / "asset-test.frag";
  {
    std::ofstream vertex(vertexPath, std::ios::binary);
    std::ofstream fragment(fragmentPath, std::ios::binary);
    vertex << "#version 330 core\nvoid main(){gl_Position=vec4(0.0);}";
    fragment << "#version 330 core\nout vec4 c;void main(){c=vec4(1.0);}";
  }
  ShaderPaths paths;
  paths.vertexPath = vertexPath.string();
  paths.fragmentPath = fragmentPath.string();
  ShaderHandle shader = assets.acquireShader(paths, AssetLoadMode::Synchronous);
  ShaderHandle duplicate = assets.acquireShader(paths, AssetLoadMode::Async);
  testTrue(g, shader == duplicate, "shader cache returns same typed handle");
  AssetStatus status = assets.getState(shader);
  testTrue(g, status.state == AssetState::Ready, "shader becomes ready");
  testEqInt(g,
            static_cast<int>(status.referenceCount),
            2,
            "shader cache increments reference count");

  fixture.mock.setRejectNextShaderReplacement(true);
  testTrue(g, assets.reload(shader), "explicit shader reload queues");
  assets.completePendingForTests();
  AssetStatus failedReload = assets.getState(shader);
  testTrue(g,
           failedReload.state == AssetState::Ready,
           "failed shader reload keeps last good program");
  testEqSize(g,
             failedReload.revision,
             1u,
             "failed shader reload keeps last good revision");
  testTrue(g,
           !failedReload.lastError.empty(),
           "failed shader reload records compile/link error");
  const std::vector<std::string> descriptions = assets.describeAssets();
  bool foundVisibleError = false;
  for (const std::string& description : descriptions) {
    if (description.find("shader state=ready refs=2 rev=1 error=") == 0) {
      foundVisibleError = true;
      break;
    }
  }
  testTrue(g,
           foundVisibleError,
           "asset diagnostics place retained shader error before long paths");
  testTrue(g, assets.releaseShader(shader), "first shader release succeeds");
  testTrue(g, assets.releaseShader(shader), "last shader release succeeds");
  testTrue(g,
           !fixture.mock.IsShaderValid(shader),
           "last shader release destroys backend program");

  ShaderPaths missingPaths;
  missingPaths.vertexPath = "missing.vert";
  missingPaths.fragmentPath = "missing.frag";
  ShaderHandle missing =
    assets.acquireShader(missingPaths, AssetLoadMode::Synchronous);
  testTrue(g,
           assets.getState(missing).state == AssetState::Failed,
           "missing shader files enter failed state");
  testTrue(g,
           fixture.mock.IsShaderValid(missing),
           "failed initial shader retains stable fallback program");
}

static void
testContextRequirementChecks()
{
  testSection("IllumoContext: required service checks");
  NullRenderWindow window(320, 240);
  EnvVars env;
  Camera camera(glm::vec2(1.0f, 1.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  AssetManager assetManager(&renderer, false);
  CommandRegistry registry;
  CommandLine console(&env, &registry, &window, &renderer);
  InputManager input(nullptr);
  Scene scene(&window, &camera);
  IllumoContext context{ &scene,        &window, &console, &input,   &renderer,
                         &assetManager, &env,    &camera,  &registry };
  testTrue(g,
           IllumoContextHasDebugCore(&context),
           "debug core accepts complete services");
  context.scene = nullptr;
  testTrue(g,
           IllumoContextHasDebugCore(&context),
           "debug core does not require scene");
  context.camera = nullptr;
  testTrue(
    g, !IllumoContextHasDebugCore(&context), "debug core requires camera");
  context.camera = &camera;
  context.assetManager = nullptr;
  testTrue(g,
           !IllumoContextHasDebugCore(&context),
           "debug core requires managed assets");
  context.assetManager = &assetManager;
  context.commandLine = nullptr;
  testTrue(g,
           !IllumoContextHasDebugCore(&context),
           "debug core requires command line");
  testTrue(g, !IllumoContextHasDebugCore(nullptr), "null context rejected");
}

static void
testSplashWakeAndTokens()
{
  testSection("SplashText: wake and command emission");
  HeadlessRenderFixture fixture(320, 240);
  GLString::setRenderWindow(&fixture.window);
  SplashText splash("EDIT", 255, 230, 120, 255, 24, 8, 24, &fixture.renderer);
  testTrue(g, !splash.isVisible(), "splash starts hidden");
  testTrue(g,
           splash.AppendCommands(&fixture.renderer),
           "hidden splash stays on token path");
  testEqSize(g,
             fixture.mock.getPendingCommandCount(),
             0,
             "hidden splash emits no commands");
  splash.Wake();
  testTrue(g, splash.isVisible(), "Wake makes splash visible");
  testTrue(g,
           splash.AppendCommands(&fixture.renderer),
           "awake splash emits through GLString token path");
  testTrue(g,
           fixture.mock.getPendingCommandCount() > 0,
           "awake splash emits render commands");
  testEqSize(g,
             splash.getVisual().shapeCount(),
             4u,
             "awake splash composes panel chrome from shape primitives");
  testEqSize(g,
             splash.getVisual().textCount(),
             1u,
             "awake splash composes one text primitive");
}

static void
testSplashFadeCompletion()
{
  testSection("SplashText: fade completion");
  SplashText splash;
  splash.Wake();
  std::this_thread::sleep_for(std::chrono::milliseconds(1600));
  splash.Fade();
  testTrue(g, !splash.isVisible(), "splash hides after wake duration");
}

static int
runRuntimeUtilityCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerRuntimeUtilityTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.InputContext.Bindings", []() {
    return runRuntimeUtilityCase(testInputContextBindings);
  });
  registry.add("Illumo.InputManager.KeyMappings", []() {
    return runRuntimeUtilityCase(testInputManagerMappings);
  });
  registry.add("Illumo.InputManager.HeadlessLifecycle", []() {
    return runRuntimeUtilityCase(testInputManagerHeadlessLifecycle);
  });
  registry.add("Illumo.InputManager.ContextCapacity", []() {
    return runRuntimeUtilityCase(testInputManagerContextsAndCapacity);
  });
  registry.add("Illumo.BackendConfig.TokenConversion",
               []() { return runRuntimeUtilityCase(testBackendConfigTokens); });
  registry.add("Illumo.Presentation.FramePacingPolicy", []() {
    return runRuntimeUtilityCase(testPresentationTimingPolicy);
  });
  registry.add("Illumo.AssetManager.Enrollment", []() {
    return runRuntimeUtilityCase(testAssetManagerEnrollment);
  });
  registry.add("Illumo.AssetManager.LookupAndShutdown", []() {
    return runRuntimeUtilityCase(testAssetManagerLookupAndShutdown);
  });
  registry.add("Illumo.AssetManager.ShaderLifecycle", []() {
    return runRuntimeUtilityCase(testAssetManagerShaderLifecycle);
  });
  registry.add("Illumo.IllumoContext.RequiredServices", []() {
    return runRuntimeUtilityCase(testContextRequirementChecks);
  });
  registry.add("Illumo.SplashText.WakeAndTokens",
               []() { return runRuntimeUtilityCase(testSplashWakeAndTokens); });
  registry.add("Illumo.SplashText.FadeCompletion", []() {
    return runRuntimeUtilityCase(testSplashFadeCompletion);
  });
}
