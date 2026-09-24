#include "Rendering/BackendConfig.h"
#include <GLFW/glfw3.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Engine/PresentationTiming.h>
#include <Illumo/Platform/SystemInfo.h>
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
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "Engine/DebugOverlayState.h"
#include "Engine/FileTreeOverlay.h"
#include "Engine/ProfilerOverlay.h"
#include <map>

static TestCounters g;

static FrameProfiler::TimePoint
profilerTime(int milliseconds)
{
  return FrameProfiler::TimePoint{} + std::chrono::milliseconds(milliseconds);
}

static void
testFrameProfilerAccounting()
{
  FrameProfiler profiler;
  profiler.beginFrame(profilerTime(0));
  profiler.mark(FramePhase::Input, profilerTime(2));
  profiler.endFrame(profilerTime(10));
  testEqInt(
    g, static_cast<int>(profiler.sampleCount()), 0, "disabled does not record");
  profiler.setEnabled(true);
  profiler.beginFrame(profilerTime(0));
  profiler.mark(FramePhase::Input, profilerTime(1));
  profiler.mark(FramePhase::Commands, profilerTime(3));
  profiler.mark(FramePhase::Presentation, profilerTime(7));
  profiler.mark(FramePhase::Pacing, profilerTime(12));
  profiler.endFrame(profilerTime(16));
  profiler.endFrame(profilerTime(20));
  const FrameProfiler::Sample sample = profiler.average();
  testTrue(g,
           sample[static_cast<size_t>(FramePhase::Other)] == 1.0,
           "unmarked work is explicit Other time");
  testTrue(g,
           sample[static_cast<size_t>(FramePhase::Input)] == 2.0,
           "input time is exclusive");
  testTrue(g,
           sample[static_cast<size_t>(FramePhase::Commands)] == 4.0,
           "submission excludes presentation");
  testTrue(g,
           sample[static_cast<size_t>(FramePhase::Presentation)] == 5.0,
           "presentation has its own elapsed time");
  double total = 0.0;
  for (double phase : sample) {
    total += phase;
  }
  testTrue(
    g, total == 16.0, "slices sum to full frame without double counting");
  testEqInt(
    g, static_cast<int>(profiler.sampleCount()), 1, "duplicate end ignored");

  for (size_t i = 0; i < FrameProfiler::kWindowFrames; ++i) {
    profiler.beginFrame(profilerTime(0));
    profiler.mark(FramePhase::ProductUpdate, profilerTime(0));
    profiler.endFrame(profilerTime(8));
  }
  testEqInt(
    g, static_cast<int>(profiler.sampleCount()), 120, "history is bounded");
  testTrue(g,
           profiler.average()[static_cast<size_t>(FramePhase::ProductUpdate)] ==
             8.0,
           "rolling window evicts the old frame");
  testTrue(g,
           profiler.average()[static_cast<size_t>(FramePhase::Presentation)] ==
             0.0,
           "evicted phases no longer contribute");
  profiler.beginFrame(profilerTime(0));
  profiler.setEnabled(false);
  profiler.endFrame(profilerTime(100));
  profiler.setEnabled(true);
  testEqInt(g,
            static_cast<int>(profiler.sampleCount()),
            0,
            "toggle clears history and partial frame");
  profiler.beginFrame(profilerTime(5));
  profiler.mark(FramePhase::Input, profilerTime(3));
  profiler.mark(FramePhase::Count, profilerTime(6));
  profiler.endFrame(profilerTime(10));
  testTrue(g,
           profiler.average()[static_cast<size_t>(FramePhase::Other)] == 5.0,
           "invalid or backwards marks cannot corrupt samples");
}

static void
testProfilerOverlayControlsAndTokens()
{
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  FrameProfiler profiler;
  ProfilerOverlay overlay(profiler);
  overlay.prepare(&renderer, &window, &camera);
  testTrue(g,
           !overlay.handleKey(KeyCode::Num1, InputAction::Press, false, false),
           "hidden profiler leaves product number keys alone");
  testTrue(g,
           !overlay.handleKey(KeyCode::F6, InputAction::Press, true, false),
           "console owns keys while open");
  testTrue(g,
           !overlay.handleKey(KeyCode::F6, InputAction::Press, false, true),
           "modified shortcuts are preserved");
  overlay.handleKey(KeyCode::F6, InputAction::Press, false, false);
  overlay.handleKey(KeyCode::F6, InputAction::Hold, false, false);
  testTrue(g, profiler.enabled(), "repeat does not toggle repeatedly");
  InputManager input(nullptr);
  InputManagerTestAccess::setAction(input, KeyCode::Num1, InputAction::Hold);
  InputManagerTestAccess::setAction(input, KeyCode::H, InputAction::Press);
  testTrue(g,
           input.isKeyPressed(KeyCode::Num1),
           "held product shortcut starts active");
  input.getCharQueue().push('1');
  input.getCharQueue().push('A');
  input.getCharQueue().push('4');
  overlay.captureInput(input, false);
  testTrue(g,
           input.getCharQueue().size() == 1 &&
             input.getCharQueue().front() == 'A',
           "navigation cannot type digits into a product text field");
  testTrue(g,
           !input.isKeyPressed(KeyCode::Num1) && input.isKeyPressed(KeyCode::H),
           "profiler captures polled digits without blocking other brush keys");
  testTrue(g,
           input.GetInputAction(KeyCode::Num1) == InputAction::None &&
             !input.isKeyReleased(KeyCode::Num1),
           "capture does not synthesize release");
  input.update();
  InputManagerTestAccess::setAction(input, KeyCode::Num1, InputAction::Hold);
  testTrue(g,
           input.isKeyPressed(KeyCode::Num1),
           "capture clears on next input update");
  overlay.captureInput(input, true);
  testTrue(
    g, input.isKeyPressed(KeyCode::Num1), "console retains input ownership");
  overlay.update(0.0, 640, 480);
  testTrue(
    g, overlay.visual().textCount() > 0, "empty sample has explanatory text");
  profiler.beginFrame(profilerTime(0));
  profiler.mark(FramePhase::Commands, profilerTime(0));
  profiler.mark(FramePhase::Presentation, profilerTime(4));
  profiler.endFrame(profilerTime(10));
  overlay.update(0.25, 640, 480);
  testTrue(g,
           overlay.visual().shapeCount() >= 98,
           "pie uses bounded triangle geometry");
  testTrue(g,
           overlay.visual().getSpace() == PrimitiveSpace::Pixels &&
             overlay.visual().getLayerHint() == RenderLayerId::Debug,
           "profiler uses screen-space debug layer");
  renderer.BeginFrame();
  testTrue(g,
           overlay.visual().AppendCommands(&renderer),
           "overlay emits renderer tokens");
  FrameProfiler::TimePoint presentationStart{};
  const FrameProfiler::TimePoint before = FrameProfiler::Clock::now();
  renderer.EndFrame(&presentationStart);
  testTrue(g,
           presentationStart >= before &&
             presentationStart <= FrameProfiler::Clock::now(),
           "timed EndFrame returns a CPU presentation boundary");
  testTrue(g,
           mock.getLastNonEmptySubmittedCount() > 0,
           "backend receives overlay commands");
  overlay.handleKey(KeyCode::Num2, InputAction::Press, false, false);
  testEqInt(g, overlay.group(), 1, "second root slice opens Rendering");
  overlay.handleKey(KeyCode::Num1, InputAction::Press, false, false);
  testEqInt(g, overlay.group(), 1, "leaf selection does not change groups");
  overlay.update(0.0, 320, 240);
  const Transform2D transform = overlay.visual().getTransform();
  testTrue(g,
           transform.x >= 0 && transform.y >= 0 &&
             transform.x + 470 * transform.scaleX <= 320 &&
             transform.y + 430 * transform.scaleY <= 240,
           "small-window panel bounds fit");
  overlay.handleKey(KeyCode::Num0, InputAction::Press, false, false);
  testEqInt(g, overlay.group(), -1, "zero returns to Frame");
  overlay.handleKey(KeyCode::F6, InputAction::Press, false, false);
  testTrue(g, !profiler.enabled(), "F6 hides and disables collection");
}
static int g_memoryQueries = 0;
static bool g_memoryAvailable = true;
static ProcessMemoryStats g_memorySample;

static bool
queryTestMemory(ProcessMemoryStats& stats) noexcept
{
  ++g_memoryQueries;
  stats = g_memorySample;
  return g_memoryAvailable;
}

static void
testDebugOverlayMemory()
{
  DebugOverlayState overlay;
  g_memoryQueries = 0;
  g_memoryAvailable = true;
  g_memorySample = { 130547712, 144913203, 163577856 };
  testTrue(g,
           !overlay.update(10.0, false, false, true, 60, queryTestMemory),
           "hidden overlay has no content changes");
  testEqInt(g, g_memoryQueries, 0, "hidden memory does not sample");
  testTrue(g, !overlay.visible(), "both sections disabled hides panel");
  testTrue(g,
           overlay.update(0.0, false, true, true, 60, queryTestMemory),
           "enabling memory updates immediately");
  testTrue(g, overlay.visible(), "memory alone shows panel");
  testTrue(g,
           overlay.content() == "RAM: 124.5 MiB\nPeak RAM: 138.2 MiB\n"
                                "Private commit: 156.0 MiB",
           "memory uses MiB with one decimal and no leading FPS row");
  testEqInt(g, g_memoryQueries, 1, "enabling memory samples once");
  testTrue(g,
           !overlay.update(0.5, false, true, true, 60, queryTestMemory),
           "cached content remains unchanged between samples");
  testEqInt(g, g_memoryQueries, 1, "half-second does not sample");
  testTrue(g,
           !overlay.update(0.5, false, true, true, 60, queryTestMemory),
           "identical values avoid a label rebuild");
  testEqInt(g, g_memoryQueries, 2, "one-second interval samples");
  overlay.update(0.0, true, true, true, 60, queryTestMemory);
  testTrue(g,
           overlay.content().find("Paced FPS: 60 | Submit FPS: 0\nRAM:") == 0,
           "FPS precedes memory when both enabled");
  testEqInt(g, g_memoryQueries, 2, "FPS visibility does not resample memory");
  overlay.update(0.5, true, false, true, 60, queryTestMemory);
  testTrue(g,
           overlay.content() == "Paced FPS: 60 | Submit FPS: 0",
           "FPS-only panel removes memory rows");
  overlay.update(0.5, true, true, true, 60, queryTestMemory);
  testTrue(g,
           overlay.content().find("Submit FPS: 2\nRAM:") != std::string::npos,
           "memory toggles do not reset the FPS interval");
  testEqInt(g, g_memoryQueries, 3, "re-enabling samples immediately");
  g_memoryAvailable = false;
  overlay.update(1.0, false, true, true, 60, queryTestMemory);
  testTrue(g,
           overlay.content() == "Memory: unavailable",
           "failure replaces stale memory values");
  overlay.update(0.5, false, true, true, 60, queryTestMemory);
  testEqInt(g, g_memoryQueries, 4, "failed queries still use normal cadence");
  g_memoryAvailable = true;
  g_memorySample = { 0, 4294967296ULL, 1048576 };
  overlay.update(0.5, false, true, true, 60, queryTestMemory);
  testTrue(g,
           overlay.content() == "RAM: 0.0 MiB\nPeak RAM: 4096.0 MiB\n"
                                "Private commit: 1.0 MiB",
           "query recovers and formats zero and large byte counts");
  overlay.update(5.0, false, true, true, 60, queryTestMemory);
  testEqInt(g, g_memoryQueries, 6, "long frame samples only once");
  overlay.update(0.0, false, false, true, 60, queryTestMemory);
  testTrue(g,
           !overlay.visible() && overlay.content().empty(),
           "disabling both clears panel");
}

static void
testProcessMemoryQuery()
{
  ProcessMemoryStats stats{ 1, 2, 3 };
  const bool available = QueryProcessMemoryStats(stats);
#ifdef _WIN32
  testTrue(g, available, "Windows process query succeeds");
  testTrue(g, stats.residentBytes > 0, "resident memory is nonzero");
  testTrue(g,
           stats.peakResidentBytes >= stats.residentBytes,
           "lifetime peak is at least the current working set");
  testTrue(g, stats.privateCommitBytes > 0, "private commit is nonzero");
#else
  testTrue(g, !available, "unsupported platform reports unavailable");
  testTrue(g,
           stats.residentBytes == 0 && stats.peakResidentBytes == 0 &&
             stats.privateCommitBytes == 0,
           "unavailable query clears output");
#endif
}

static void
testSystemInfoQuery()
{
  const SystemInfo info = QuerySystemInfo();
  testTrue(g, info.logicalProcessors > 0, "logical processors are reported");
#ifdef _WIN32
  testTrue(g,
           info.operatingSystem.rfind("Windows", 0) == 0,
           "Windows names its operating system");
  testTrue(g, !info.architecture.empty(), "architecture is named");
  testTrue(g, !info.cpuName.empty(), "processor name is read");
  testTrue(g,
           info.physicalCores > 0 &&
             info.physicalCores <= info.logicalProcessors,
           "physical cores are nonzero and at most the logical count");
  testTrue(g,
           info.totalMemoryBytes > 0 &&
             info.availableMemoryBytes <= info.totalMemoryBytes,
           "available memory is within total memory");
#endif
}

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

  for (int i = 0; i < MAX_INPUT_EVENTS + 7; ++i) {
    InputManager::characterCallback(nullptr, static_cast<unsigned int>('x'));
    InputManager::normalKeyCallback(nullptr, GLFW_KEY_A, 0, GLFW_PRESS, 0);
  }
  testEqSize(g,
             input.getCharQueue().size(),
             MAX_INPUT_EVENTS,
             "character callbacks are bounded");
  testEqSize(g,
             input.getKeyQueue().size(),
             MAX_INPUT_EVENTS,
             "key callbacks are bounded");
  testEqSize(
    g, input.droppedCharEvents(), 7, "character overflow is observable");
  testEqSize(g, input.droppedKeyEvents(), 7, "key overflow is observable");
  InputManagerTestAccess::setAction(input, KeyCode::A, InputAction::Release);
  testTrue(g, input.isKeyReleased(KeyCode::A), "release query uses frame edge");
  input.suppressKeyForFrame(KeyCode::A);
  testTrue(
    g, !input.isKeyReleased(KeyCode::A), "suppression masks release edge");
  InputManagerTestAccess::setAction(
    input, KeyCode::MouseLeft, InputAction::Release);
  testTrue(g,
           input.isMouseButtonReleased(KeyCode::MouseLeft),
           "mouse release uses frame edge");

  input.update();
  testTrue(
    g, !input.isKeyReleased(KeyCode::A), "release edge clears on next update");
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
  InputContext* selected = input.getActiveInputContext();
  testTrue(g, !input.setActiveInputContext(-1), "negative ID rejected");
  testTrue(g, !input.setActiveInputContext(999), "unknown ID rejected");
  testTrue(g,
           input.getActiveInputContext() == selected,
           "invalid activation preserves selection");
  testTrue(g, !input.isActionActive("missing"), "missing action is inert");
  testTrue(g, input.unregisterInputContext(firstId), "active context retired");
  testTrue(g, !input.isActionActive("toggle"), "retired actions are inert");
  testTrue(
    g, !input.unregisterInputContext(firstId), "double retirement rejected");
  for (int i = 0; i < NUM_INPUT_CONTEXTS * 4; ++i) {
    const long replacement = input.registerInputContext(first);
    testTrue(g, replacement > firstId, "free storage receives fresh ID");
    testTrue(
      g, input.setActiveInputContext(replacement), "replacement activates");
    testTrue(
      g, !input.setActiveInputContext(firstId), "stale ID never aliases");
    testTrue(g,
             !input.unregisterInputContext(firstId),
             "stale retirement never aliases");
    testTrue(
      g, input.unregisterInputContext(replacement), "replacement retires");
  }
  InputManagerTestAccess::setNextContextId(input,
                                           std::numeric_limits<long>::max());
  testTrue(g,
           input.registerInputContext(first) == -1,
           "ID exhaustion fails without overflow");
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
testAssetManagerMeshLifecycle()
{
  testSection("AssetManager: shared mesh cache and reference lifetime");
  HeadlessRenderFixture fixture(4, 4);
  AssetManager assets(&fixture.renderer, false);
  const std::filesystem::path path =
    std::filesystem::current_path() / "asset-cache-test.obj";
  {
    std::ofstream file(path, std::ios::binary);
    file << "v 0 0 0\n"
            "v 1 0 0\n"
            "v 0 1 0\n"
            "vt 0 0\n"
            "vt 1 0\n"
            "vt 0 1\n"
            "f 1/1 2/2 3/3\n";
  }

  const size_t createsBefore = fixture.mock.getCreateCount();
  const MeshHandle first = assets.acquireMesh(path.string());
  const MeshHandle duplicate =
    assets.acquireMesh((path.parent_path() / "." / path.filename()).string());
  testTrue(g,
           first.isValid() && first == duplicate,
           "canonical duplicate mesh returns one handle");
  testEqSize(g,
             fixture.mock.getCreateCount(),
             createsBefore + 1,
             "duplicate mesh performs one backend upload");
  const AssetStatus sharedStatus = assets.getState(first);
  testTrue(g,
           sharedStatus.state == AssetState::Ready &&
             sharedStatus.referenceCount == 2,
           "duplicate mesh acquisition increments references");
  const MeshAssetInfo info = assets.getMeshInfo(first);
  testTrue(g,
           info.isValid() && info.vertexCount == 3 && info.indexCount == 3,
           "managed mesh exposes immutable draw metadata");

  testTrue(g, assets.releaseMesh(first), "first mesh release succeeds");
  testTrue(g,
           fixture.mock.IsMeshValid(first),
           "shared mesh survives while one reference remains");
  testTrue(g, assets.releaseMesh(first), "last mesh release succeeds");
  testTrue(g,
           !fixture.mock.IsMeshValid(first),
           "last mesh release destroys backend resource");

  MeshLoadOptions alternateOptions;
  alternateOptions.flipTexCoordsV = false;
  const MeshHandle alternate =
    assets.acquireMesh(path.string(), alternateOptions);
  testTrue(g,
           alternate.isValid() && alternate != first,
           "geometry-affecting loader options use a separate cache entry");
  testTrue(g,
           assets.retainMesh(alternate),
           "explicit mesh retain supports sharing an uncopied handle");
  testTrue(g,
           assets.getState(alternate).referenceCount == 2,
           "explicit retain increments the managed reference count");
  testTrue(g, assets.releaseMesh(alternate), "retained mesh releases once");
  testTrue(g, assets.releaseMesh(alternate), "retained mesh releases finally");
  std::error_code removeError;
  std::filesystem::remove(path, removeError);
  testTrue(g, !removeError, "mesh cache fixture is removed");
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

// A small fixed tree for the file browser.
class FakeFileTree final : public IFileTreeSource
{
public:
  std::map<std::string, std::vector<FileTreeEntry>> directories{
    { "/", { { "app", true, 0 }, { "packages", true, 0 } } },
    { "/app",
      { { "Scenes", true, 0 },
        { "envvars.json", false, 120 },
        { "illumo.json", false, 300 } } },
    { "/app/Scenes", { { "demo.ilsc", false, 900 } } },
    { "/packages", { { "forest", true, 0 } } },
  };
  mutable int listings = 0;
  bool list(const std::string& directory,
            std::vector<FileTreeEntry>& entries) const override
  {
    ++listings;
    const std::map<std::string, std::vector<FileTreeEntry>>::const_iterator
      found = directories.find(directory);
    if (found == directories.end()) {
      return false;
    }
    entries = found->second;
    return true;
  }
  bool stat(const std::string& path, FileTreeStatus& status) const override
  {
    if (directories.count(path) != 0) {
      status = { true, 0, path == "/app" ? "game" : "" };
      return true;
    }
    if (path == "/app/illumo.json") {
      status = { false, 300, "game" };
      return true;
    }
    return false;
  }
};

static void
testFileTreeOverlayNavigation()
{
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  FileTreeOverlay overlay;
  overlay.prepare(&renderer, &window, &camera);
  FakeFileTree tree;
  testTrue(g,
           !overlay.handleKey(KeyCode::Down, InputAction::Press, false),
           "a hidden browser leaves arrow keys to the product");

  overlay.show(&tree, "/");
  testTrue(g, overlay.visible(), "show opens the browser");
  testEqSize(g, overlay.rows().size(), 2u, "the root lists its mounts");
  testTrue(g, overlay.selectedPath() == "/app", "the first row is selected");
  testTrue(g,
           overlay.status().find("from game") != std::string::npos,
           "the status names the supplying package");
  testTrue(g,
           !overlay.handleKey(KeyCode::Down, InputAction::Press, true),
           "the console owns keys while open");
  testTrue(g,
           !overlay.handleKey(KeyCode::A, InputAction::Press, false),
           "other keys stay with the product");

  overlay.handleKey(KeyCode::Right, InputAction::Press, false);
  testEqSize(g, overlay.rows().size(), 5u, "Right expands /app in place");
  overlay.handleKey(KeyCode::Down, InputAction::Press, false);
  overlay.handleKey(KeyCode::Down, InputAction::Press, false);
  overlay.handleKey(KeyCode::Down, InputAction::Press, false);
  testTrue(g,
           overlay.selectedPath() == "/app/illumo.json",
           "Down walks the flattened rows");
  testTrue(g,
           overlay.status().find("300 bytes") != std::string::npos,
           "a file shows its size");
  overlay.handleKey(KeyCode::Left, InputAction::Press, false);
  testTrue(
    g, overlay.selectedPath() == "/app", "Left on a file selects its parent");
  overlay.handleKey(KeyCode::Left, InputAction::Press, false);
  testEqSize(g, overlay.rows().size(), 2u, "Left collapses an open directory");
  const int listed = tree.listings;
  overlay.handleKey(KeyCode::Enter, InputAction::Press, false);
  testEqInt(g, tree.listings, listed, "reopening reuses the listing");
  testTrue(g,
           overlay.handleKey(KeyCode::Down, InputAction::Release, false),
           "releases are consumed too");
  overlay.handleKey(KeyCode::End, InputAction::Press, false);
  testTrue(
    g, overlay.selectedPath() == "/packages", "End selects the last row");
  testTrue(g, overlay.scroll(1.0), "the wheel is consumed");
  testTrue(g,
           overlay.selectedPath() == "/app/Scenes",
           "the wheel moves up three rows");
  testTrue(g, !overlay.scroll(0.0), "no wheel motion is not consumed");

  tree.directories.erase("/packages");
  overlay.handleKey(KeyCode::End, InputAction::Press, false);
  overlay.handleKey(KeyCode::Right, InputAction::Press, false);
  testTrue(g,
           overlay.status().find("Cannot list /packages") != std::string::npos,
           "a failed listing is reported");

  overlay.update(640, 480);
  testTrue(g,
           overlay.visual().textCount() >= 4,
           "the panel draws a title, rows and a footer");
  testTrue(g,
           overlay.handleKey(KeyCode::Escape, InputAction::Press, false) &&
             !overlay.visible(),
           "Escape closes the browser");
}

void
registerRuntimeUtilityTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Debug.FileTreeOverlay", []() {
    return runRuntimeUtilityCase(testFileTreeOverlayNavigation);
  });
  registry.add("Illumo.Profiler.Accounting", []() {
    return runRuntimeUtilityCase(testFrameProfilerAccounting);
  });
  registry.add("Illumo.Profiler.ControlsAndTokens", []() {
    return runRuntimeUtilityCase(testProfilerOverlayControlsAndTokens);
  });
  registry.add("Illumo.DebugOverlay.Memory",
               []() { return runRuntimeUtilityCase(testDebugOverlayMemory); });
  registry.add("Illumo.Platform.ProcessMemory",
               []() { return runRuntimeUtilityCase(testProcessMemoryQuery); });
  registry.add("Illumo.Platform.SystemInfo",
               []() { return runRuntimeUtilityCase(testSystemInfoQuery); });
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
  registry.add("Illumo.AssetManager.MeshLifecycle", []() {
    return runRuntimeUtilityCase(testAssetManagerMeshLifecycle);
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
