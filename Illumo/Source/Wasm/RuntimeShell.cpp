#include <Illumo/Wasm/RuntimeShell.h>

#include <Illumo/Audio/AudioDevice.h>
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
#include <Illumo/Engine/DebugOverlay.h>
#endif
#include <Illumo/Engine/FrameProfiler.h>
#include <Illumo/Engine/Illumo.h>
#include <Illumo/Engine/PresentationTiming.h>
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Gui/GuiEngineBrand.h>
#include <Illumo/Rendering/FrameCapture.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Wasm/WasmProgram.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <iostream>
#include <nlohmann/json.hpp>
#include <tracy/Tracy.hpp>

static double
samplePercentile(std::vector<double> samples, double fraction)
{
  if (samples.empty()) {
    return 0.0;
  }
  std::sort(samples.begin(), samples.end());
  const std::size_t index = static_cast<std::size_t>(
    std::clamp(fraction, 0.0, 1.0) * static_cast<double>(samples.size() - 1));
  return samples[index];
}

static nlohmann::json
distribution(const std::vector<double>& samples)
{
  nlohmann::json result;
  result["p50"] = samplePercentile(samples, 0.5);
  result["p95"] = samplePercentile(samples, 0.95);
  result["p99"] = samplePercentile(samples, 0.99);
  result["max"] = samplePercentile(samples, 1.0);
  return result;
}

static nlohmann::json
rolling(const RollingMetric& metric)
{
  nlohmann::json result;
  result["p50"] = metric.median();
  result["p95"] = metric.p95();
  result["max"] = metric.maximum();
  return result;
}

// A frame count from 1 to RuntimeBench::kMaximumFrames.
static bool
readFrames(const std::string& value, std::uint64_t& output)
{
  std::uint64_t parsed = 0;
  const char* end = value.data() + value.size();
  const std::from_chars_result result =
    std::from_chars(value.data(), end, parsed);
  if (result.ec != std::errc() || result.ptr != end || parsed == 0 ||
      parsed > RuntimeBench::kMaximumFrames) {
    return false;
  }
  output = parsed;
  return true;
}

static bool
keyNamed(const std::string& name, KeyCode& key)
{
#define ILLUMO_GUEST_KEY(keyName, number)                                      \
  if (name == #keyName) {                                                      \
    key = KeyCode::keyName;                                                    \
    return true;                                                               \
  }
#include <IllumoGuest/Keys.inc>
#undef ILLUMO_GUEST_KEY
  return false;
}

void
RuntimeShell::OverlayDeleter::operator()(DebugOverlay* overlay) const
{
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
  delete overlay;
#else
  (void)overlay;
#endif
}

RuntimeShell::RuntimeShell(Illumo& illumo,
                           std::unique_ptr<AudioDevice> audio,
                           std::unique_ptr<WasmProgram> program,
                           RuntimeShellOptions options)
  : m_illumo(illumo)
  , m_audio(std::move(audio))
  , m_program(std::move(program))
  , m_options(std::move(options))
{
}

RuntimeShell::~RuntimeShell()
{
  stop();
}

bool
RuntimeShell::start()
{
  ILLUMO_PROFILE_ZONE("RuntimeShell.start");
  ic = &m_illumo.context();
  if (ic->window != nullptr && !m_options.title.empty()) {
    ic->window->setTitle(m_options.title);
  }
  // The bench reports the host loop's phases too.
  if (m_options.bench.frames != 0 && ic->frameProfiler != nullptr) {
    ic->frameProfiler->setEnabled(true);
  }
  bool started = false;
  try {
    ILLUMO_PROFILE_ZONE("RuntimeShell.startProgram");
    started = m_program->start(*ic);
  } catch (const std::exception& exception) {
    Logger::LogError(std::string("The program threw during startup: ") +
                     exception.what());
  }
  if (!started) {
    Logger::LogError("The " + m_options.application +
                     " package failed to start: " + m_program->error());
    m_exitCode = 1;
    if (!m_options.capture.empty()) {
      report(false, 0, 0, "The package failed to start: " + m_program->error());
    }
    if (m_options.bench.frames != 0) {
      reportBench("The package failed to start: " + m_program->error());
    }
    m_program->stop();
    return false;
  }
  m_started = true;
  Logger::LogInfo("The " + m_options.application + " package started");
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
  m_overlay.reset(new DebugOverlay(&m_illumo.frameProfiler()));
  if (!m_overlay->start(*ic)) {
    Logger::LogWarning("The debug overlay did not start");
    m_overlay->stop();
    m_overlay.reset();
  }
#endif
  if (!m_options.splashImage.empty()) {
    m_splash = std::make_unique<GuiEngineSplash>();
    const bool reducedMotion =
      ic->envVars != nullptr &&
      ic->envVars->getVar("reducedUiMotion").valueAsBool;
    if (!m_splash->begin(ic->window,
                         ic->renderer,
                         ic->assetManager,
                         m_options.splashImage.string(),
                         ic->inputManager,
                         reducedMotion)) {
      m_splash.reset();
    }
  }
  return true;
}

void
RuntimeShell::frame(double dt)
{
  if (!m_started) {
    return;
  }
  FrameProfiler& profiler = m_illumo.frameProfiler();
  {
    ILLUMO_PROFILE_ZONE("Frame.Update");
    m_illumo.beginUpdate(dt);
    // The overlay reads console keys before the program can drain them.
    profiler.mark(FramePhase::DebugUpdate);
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
    if (m_overlay) {
      m_overlay->update(dt);
    }
#endif
    profiler.mark(FramePhase::ProductUpdate);
    if (m_splash != nullptr) {
      updateSplash(dt);
    } else {
      updateProgram(dt);
    }
    m_illumo.endUpdate();
  }
  {
    ILLUMO_PROFILE_ZONE("Frame.Render");
    DrawList* scene = m_illumo.beginRender();
    if (scene != nullptr) {
      if (m_splash != nullptr) {
        ILLUMO_PROFILE_ZONE("RuntimeShell.splashDrawables");
        m_splash->addDrawables(*scene);
      } else {
        dispatchProgram(*scene);
      }
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
      if (m_overlay) {
        m_overlay->dispatch(*scene);
      }
#endif
    }
    m_illumo.endRender();
  }
}

bool
RuntimeShell::closing()
{
  if (!m_illumo.shouldClose()) {
    return false;
  }
  // A finished capture or benchmark closes without product dialogs, and so
  // does a close during the splash, before the program has run a frame.
  if (!m_started || m_done || m_benchDone || m_splash != nullptr ||
      m_program->closeRequested()) {
    return true;
  }
  m_illumo.deferClose();
  return false;
}

void
RuntimeShell::stop()
{
  if (!m_started) {
    return;
  }
  ILLUMO_PROFILE_ZONE("RuntimeShell.stop");
  m_started = false;
  m_splash.reset();
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
  if (m_overlay) {
    m_overlay->stop();
    m_overlay.reset();
  }
#endif
  if (ic != nullptr && ic->renderer != nullptr && m_hookInstalled) {
    ic->renderer->setBeforePresent({});
    m_hookInstalled = false;
  }
  if (!m_options.capture.empty() && !m_done) {
    report(false, 0, 0, "The runtime closed before the capture frame");
  }
  if (m_options.bench.frames != 0 && !m_benchDone) {
    reportBench("The runtime closed before the benchmark finished");
  }
  m_program->stop();
  Logger::LogInfo("The " + m_options.application + " package stopped");
}

int
RuntimeShell::run(std::chrono::steady_clock::time_point launched)
{
  if (!start()) {
    return exitCode();
  }
  Logger::LogInfo(
    m_illumo.applicationName() + " ready in " +
    std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - launched)
                     .count()) +
    " ms");
  FrameProfiler& profiler = m_illumo.frameProfiler();
  FramePacer framePacer;
  std::chrono::steady_clock::time_point lastTime =
    std::chrono::steady_clock::now();
  while (!closing()) {
    profiler.beginFrame();
    FrameMark;
    const std::chrono::steady_clock::time_point currentTime =
      std::chrono::steady_clock::now();
    const double dt =
      std::chrono::duration<double>(currentTime - lastTime).count();
    lastTime = currentTime;
    frame(dt);
    {
      ILLUMO_PROFILE_ZONE("Frame.Pacing");
      profiler.mark(FramePhase::Pacing);
      const long targetFps = getTargetFps(&m_illumo.environment());
      const bool vsyncEnabled = isVsyncRequested(&m_illumo.environment());
      const int refreshRate =
        ic->window != nullptr ? ic->window->getRefreshRate() : 60;
      framePacer.pace(targetFps, vsyncEnabled, refreshRate);
    }
    profiler.endFrame();
  }
  stop();
  return exitCode();
}

// The splash's last frame (faded out, or the one a skip ended) is still
// drawn; the program takes over on the frame after it.
void
RuntimeShell::updateSplash(double dt)
{
  ILLUMO_PROFILE_ZONE("RuntimeShell.updateSplash");
  if (m_splash->finished()) {
    endSplash();
    updateProgram(dt);
    return;
  }
  m_splash->update(dt, ic->commandLine != nullptr && ic->commandLine->isOpen);
}

void
RuntimeShell::endSplash()
{
  m_splash.reset();
  // Text typed at the splash is not the program's.
  if (ic->inputManager != nullptr) {
    ic->inputManager->clearCharQueue();
  }
}

void
RuntimeShell::updateProgram(double dt)
{
  const RuntimeBench& bench = m_options.bench;
  if (bench.frames == 0 || m_benchDone) {
    if (!m_options.capture.empty() && !m_done) {
      std::string error;
      if (!feedScript(&error)) {
        report(false, 0, 0, error);
        m_done = true;
        if (ic->window != nullptr) {
          ic->window->requestClose();
        }
      }
    }
    m_program->update(dt);
    return;
  }
  std::string scriptError;
  if (!feedScript(&scriptError)) {
    reportBench(scriptError);
    return;
  }
  const std::chrono::steady_clock::time_point start =
    std::chrono::steady_clock::now();
  m_program->update(dt);
  const std::chrono::steady_clock::time_point end =
    std::chrono::steady_clock::now();
  // Warm-up counts from the end of the script.
  if (scriptFinished()) {
    ++m_benchUpdates;
  }
  if (m_benchUpdates > bench.warmup) {
    if (m_benchFrameIntervals.empty() && m_benchUpdateMilliseconds.empty()) {
      m_benchStart = start;
    } else {
      m_benchFrameIntervals.push_back(
        std::chrono::duration<double, std::milli>(start - m_benchLastStart)
          .count());
      // The last frame, split at its submission: dispatch, render and
      // submit; then present, input and the loop until this update.
      if (m_benchDispatch > m_benchLastStart &&
          m_benchSubmitted > m_benchDispatch) {
        m_benchRenderMilliseconds.push_back(
          std::chrono::duration<double, std::milli>(m_benchSubmitted -
                                                    m_benchDispatch)
            .count());
        m_benchPresentMilliseconds.push_back(
          std::chrono::duration<double, std::milli>(start - m_benchSubmitted)
            .count());
      }
    }
    m_benchUpdateMilliseconds.push_back(
      std::chrono::duration<double, std::milli>(end - start).count());
    if (m_benchUpdateMilliseconds.size() >= bench.frames) {
      m_benchEnd = end;
      reportBench({});
    }
  }
  m_benchLastStart = start;
  if (!m_program->error().empty() && !m_benchDone) {
    reportBench("The package failed: " + m_program->error());
  }
}

void
RuntimeShell::dispatchProgram(DrawList& scene)
{
  ILLUMO_PROFILE_ZONE("RuntimeShell.dispatchProgram");
  // Bench and capture never run together, so the bench may own the hook.
  if (m_options.bench.frames != 0 && !m_benchDone && ic->renderer != nullptr) {
    m_benchDispatch = std::chrono::steady_clock::now();
    if (!m_hookInstalled) {
      m_hookInstalled = true;
      ic->renderer->setBeforePresent([this](Renderer&) {
        m_benchSubmitted = std::chrono::steady_clock::now();
      });
    }
  }
  m_program->dispatch(scene);
  if (m_clearHook) {
    // Cleared here, never from inside the running hook.
    ic->renderer->setBeforePresent({});
    m_hookInstalled = false;
    m_clearHook = false;
  }
  if (m_options.capture.empty() || m_done || ic->renderer == nullptr) {
    return;
  }
  // Like bench warm-up, the capture frame counts from the end of the script.
  if (!scriptFinished()) {
    return;
  }
  ++m_frames;
  if (m_frames == m_options.captureFrame && !m_hookInstalled) {
    // The frame dispatched now is presented at the end of this render.
    m_hookInstalled = true;
    ic->renderer->setBeforePresent(
      [this](Renderer& renderer) { captureFrame(renderer); });
  } else if (m_frames > m_options.captureFrame + 2 && !m_done) {
    // Every presentation of the target frame failed (frame errors skip
    // presentation, and with it the hook).
    report(false, 0, 0, "The target frame was never presented");
    m_done = true;
    ic->renderer->setBeforePresent({});
    m_hookInstalled = false;
    ic->window->requestClose();
  }
}

bool
RuntimeShell::scriptFinished() const
{
  return m_scriptBegun || (m_benchScriptLine >= m_options.bench.script.size() &&
                           m_benchWaitFrames == 0);
}

// Runs script lines in order: "@wait n" pauses n frames, "@key Name" presses
// one key, "@begin" starts counting (warm-up, timed or capture frames) while
// the lines after it keep running, so a benchmark can time a transition; any
// other line is a console command queued once it exists. An unknown key or
// directive fails the run (false with *error).
bool
RuntimeShell::feedScript(std::string* error)
{
  const std::vector<std::vector<std::string>>& script = m_options.bench.script;
  if (ic->commandRegistry == nullptr || ic->inputManager == nullptr) {
    m_benchScriptLine = script.size();
    m_benchWaitFrames = 0;
    return true;
  }
  if (m_benchWaitFrames > 0) {
    --m_benchWaitFrames;
    return true;
  }
  CommandRegistry& commands = *ic->commandRegistry;
  bool queued = false;
  while (m_benchScriptLine < script.size()) {
    const std::vector<std::string>& words = script[m_benchScriptLine];
    if (words.front() == "@wait") {
      std::uint64_t frames = 0;
      if (words.size() != 2 || !readFrames(words[1], frames)) {
        *error = "Invalid @wait in the script";
        return false;
      }
      m_benchWaitFrames = frames;
      ++m_benchScriptLine;
      break;
    }
    if (words.front() == "@key") {
      KeyCode key = KeyCode::None;
      if (words.size() != 2 || !keyNamed(words[1], key)) {
        *error = "Invalid @key in the script";
        return false;
      }
      ic->inputManager->getKeyQueue().push({ key, InputAction::Press, 0 });
      ++m_benchScriptLine;
      m_benchWaitFrames = 2;
      break;
    }
    if (words.front() == "@begin") {
      m_scriptBegun = true;
      ++m_benchScriptLine;
      continue;
    }
    if (words.front().starts_with("@")) {
      *error = "Unknown script directive " + words.front();
      return false;
    }
    if (!commands.HasCommand(words.front())) {
      // Guest commands register asynchronously; one that never appears
      // fails the run instead of stalling it.
      if (++m_scriptCommandWaitFrames > kScriptCommandWaitFrames) {
        *error = "Script command never registered: " + words.front();
        return false;
      }
      break;
    }
    m_scriptCommandWaitFrames = 0;
    commands.QueueCommand(
      words.front(), std::vector<std::string>(words.begin() + 1, words.end()));
    ++m_benchScriptLine;
    queued = true;
  }
  if (queued) {
    ILLUMO_PROFILE_ZONE("RuntimeShell.scriptCommands");
    commands.ExecuteQueue();
  }
  return true;
}

void
RuntimeShell::reportBench(const std::string& error)
{
  if (m_benchDone) {
    return;
  }
  ILLUMO_PROFILE_ZONE("RuntimeShell.reportBench");
  m_benchDone = true;
  m_exitCode = error.empty() ? 0 : 1;
  nlohmann::json result;
  result["success"] = error.empty();
  result["application"] = m_options.application;
  result["error"] = error;
  result["warmupFrames"] = m_options.bench.warmup;
  result["frames"] = m_benchUpdateMilliseconds.size();
  const double seconds =
    std::chrono::duration<double>(m_benchEnd - m_benchStart).count();
  result["seconds"] = seconds;
  result["fps"] =
    seconds > 0.0 ? static_cast<double>(m_benchFrameIntervals.size()) / seconds
                  : 0.0;
  result["frameIntervalMs"] = distribution(m_benchFrameIntervals);
  result["moduleUpdateMs"] = distribution(m_benchUpdateMilliseconds);
  result["renderMs"] = distribution(m_benchRenderMilliseconds);
  result["presentMs"] = distribution(m_benchPresentMilliseconds);
  if (ic != nullptr && ic->frameProfiler != nullptr &&
      ic->frameProfiler->sampleCount() != 0) {
    // Mean milliseconds per phase over the last profiler window.
    static const char* const kPhaseNames[FrameProfiler::kPhaseCount] = {
      "input",  "camera",   "debugUpdate",  "productUpdate", "scene",
      "assets", "commands", "presentation", "pacing",        "other"
    };
    const FrameProfiler::Sample average = ic->frameProfiler->average();
    nlohmann::json phases;
    for (std::size_t phase = 0; phase < FrameProfiler::kPhaseCount; ++phase) {
      phases[kPhaseNames[phase]] = average[phase];
    }
    result["phasesMs"] = phases;
  }
  const WasmFrameStats& stats = m_program->stats();
  nlohmann::json host;
  host["services"] = rolling(stats.servicesMilliseconds);
  host["update"] = rolling(stats.updateMilliseconds);
  host["receive"] = rolling(stats.receiveMilliseconds);
  host["frame"] = rolling(stats.frameMilliseconds);
  host["accept"] = rolling(stats.acceptMilliseconds);
  host["frameBytes"] = rolling(stats.frameBytes);
  result["wasmMs"] = host;
  const WasmFrameCounters* counters = m_program->frameCounters();
  if (counters != nullptr) {
    nlohmann::json frame;
    frame["batches"] = counters->batches;
    frame["retainedBatches"] = counters->retainedBatches;
    frame["inlineVertexBytes"] = counters->inlineVertexBytes;
    frame["inlineIndexBytes"] = counters->inlineIndexBytes;
    frame["textureWrites"] = counters->textureWrites;
    frame["textureWriteBytes"] = counters->textureWriteBytes;
    frame["meshWriteBytes"] = counters->meshWriteBytes;
    frame["meshEnrollments"] = counters->meshEnrollments;
    frame["meshReplacements"] = counters->meshReplacements;
    frame["worldOperations"] = counters->worldOperations;
    frame["worldInstances"] = counters->worldInstances;
    frame["visualOperations"] = counters->visualOperations;
    frame["visuals"] = counters->visuals;
    result["lastFrame"] = frame;
  }
  if (!m_program->error().empty()) {
    result["guestError"] = m_program->error();
  }
  std::cout << result.dump() << std::endl;
  if (ic != nullptr && ic->window != nullptr) {
    ic->window->requestClose();
  }
}

void
RuntimeShell::captureFrame(Renderer& renderer)
{
  ILLUMO_PROFILE_ZONE("RuntimeShell.captureFrame");
  m_done = true;
  const std::array<int, 2> size = ic->window->getWindowDimensions();
  FrameReadback image = renderer.getBackend()->readBackbuffer(size[0], size[1]);
  // The window presents opaquely whatever alpha translucent UI leaves in the
  // backbuffer, so the screenshot must be opaque to match it.
  for (std::size_t index = 3; index < image.pixels.size(); index += 4) {
    image.pixels[index] = 255;
  }
  std::string error = image.error;
  if (image.success() &&
      !FrameCapture::savePng(m_options.capture, image, &error) &&
      error.empty()) {
    error = "The PNG could not be written";
  }
  report(image.success() && error.empty(), image.width, image.height, error);
  // The hook must not outlive this presentation; clear it after returning.
  m_clearHook = true;
  ic->window->requestClose();
}

void
RuntimeShell::report(bool success,
                     int width,
                     int height,
                     const std::string& error)
{
  if (m_reported) {
    return;
  }
  m_reported = true;
  m_exitCode = success ? 0 : 1;
  nlohmann::json result;
  result["success"] = success;
  result["application"] = m_options.application;
  const std::u8string output = m_options.capture.u8string();
  result["output"] =
    std::string(reinterpret_cast<const char*>(output.data()), output.size());
  result["frame"] = m_frames;
  result["width"] = width;
  result["height"] = height;
  result["error"] = error;
  if (!m_program->error().empty()) {
    result["guestError"] = m_program->error();
  }
  std::cout << result.dump() << std::endl;
}
