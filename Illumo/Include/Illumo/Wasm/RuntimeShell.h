#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

class AudioDevice;
class DebugOverlay;
class Illumo;
class Renderer;
class DrawList;
class GuiEngineSplash;
class WasmProgram;
struct IllumoContext;

// --bench-frames: warm up, time a fixed number of frames, print one JSON line
// and close. The optional script queues host console lines, each once its
// command exists (guest commands register asynchronously). A --capture run
// may carry the same script (--capture-script) to reach a later screen.
struct RuntimeBench
{
  static constexpr std::uint64_t kMaximumFrames = 1000000u;
  std::uint64_t frames = 0;
  std::uint64_t warmup = 120;
  std::vector<std::vector<std::string>> script;
};

struct RuntimeShellOptions
{
  // The window title and the application's name in logs and results.
  std::string title;
  std::string application;
  // The bytes of the package's app.ico, shown as the window's icon in place
  // of the engine's (D-E39). Empty keeps the engine icon, as does a file with
  // no usable image.
  std::vector<std::uint8_t> appIcon;
  // --capture: the new PNG written from this frame, counted from the end of
  // the script. Empty for an ordinary run.
  std::filesystem::path capture;
  std::uint64_t captureFrame = 60;
  RuntimeBench bench;
  // The engine splash's logo (GuiEngineSplash), played before the program's
  // first frame. Empty for no splash, as capture and benchmark runs have.
  std::filesystem::path splashImage;
};

// IllumoRuntime's frame around its one program (D-E31), in a fixed order:
// the engine's input and hotkeys, the debug overlay (debug-tool builds), the
// program, then render, where the program dispatches before the overlay. The
// shell also owns the --capture and --bench-frames runs, and the engine
// splash: every app opens behind it, neither updated nor drawn until the
// splash ends, so no program triggers or waits for it. The program's modules
// compile into the cache while the splash plays and it starts, between
// frames, once its module is ready; the last splash frame holds if the
// compile outlasts the splash.
class RuntimeShell
{
public:
  // The audio device, if any, outlives the program, which borrows it.
  RuntimeShell(Illumo& illumo,
               std::unique_ptr<AudioDevice> audio,
               std::unique_ptr<WasmProgram> program,
               RuntimeShellOptions options);
  ~RuntimeShell();
  RuntimeShell(const RuntimeShell&) = delete;
  RuntimeShell& operator=(const RuntimeShell&) = delete;
  RuntimeShell(RuntimeShell&&) = delete;
  RuntimeShell& operator=(RuntimeShell&&) = delete;

  // Starts the program, then the debug overlay; with a splash, begins it and
  // defers the program to frame(). A program that fails to start is
  // reported (to a capture or benchmark caller too) and stopped.
  bool start();
  void frame(double dt);
  // True once the window asked to close and the program agreed, or a capture
  // or benchmark finished. A declined request keeps the window open.
  bool closing();
  // Stops the overlay, then the program; reports an unfinished capture or
  // benchmark.
  void stop();
  // The engine splash is showing; the program is held.
  bool splashing() const { return m_splash != nullptr; }
  // start, paced frames until closing, then stop. Returns exitCode().
  int run(std::chrono::steady_clock::time_point launched);
  // 1 when a capture or benchmark failed or the program did not start.
  int exitCode() const { return m_exitCode; }
  WasmProgram& program() { return *m_program; }

private:
  // Starts the program and, in debug-tool builds, the overlay. A failure is
  // reported (capture/bench JSON, exit code) and leaves the shell stopped.
  bool startProgram();
  void updateSplash(double dt);
  void endSplash();
  void updateProgram(double dt);
  void dispatchProgram(DrawList& scene);
  bool scriptFinished() const;
  bool feedScript(std::string* error);
  void reportBench(const std::string& error);
  void captureFrame(Renderer& renderer);
  void report(bool success, int width, int height, const std::string& error);

  // The debug overlay exists only in debug-tool builds, which alone compile
  // its code; Release never deletes one.
  struct OverlayDeleter
  {
    void operator()(DebugOverlay* overlay) const;
  };

  Illumo& m_illumo;
  IllumoContext* ic = nullptr;
  // Declared before the program so it outlives it.
  std::unique_ptr<AudioDevice> m_audio;
  std::unique_ptr<WasmProgram> m_program;
  std::unique_ptr<DebugOverlay, OverlayDeleter> m_overlay;
  // Present only while the splash shows.
  std::unique_ptr<GuiEngineSplash> m_splash;
  RuntimeShellOptions m_options;
  bool m_started = false;
  // The splash is up and the program waits for its compiled module.
  bool m_programPending = false;
  int m_exitCode = 0;
  std::uint64_t m_frames = 0;
  bool m_hookInstalled = false;
  bool m_clearHook = false;
  bool m_done = false;
  bool m_reported = false;
  static constexpr std::uint64_t kScriptCommandWaitFrames = 600;
  std::size_t m_benchScriptLine = 0;
  std::uint64_t m_benchWaitFrames = 0;
  std::uint64_t m_scriptCommandWaitFrames = 0;
  std::uint64_t m_benchUpdates = 0;
  std::vector<double> m_benchFrameIntervals;
  // The script reached @begin: counting started while later lines still run.
  bool m_scriptBegun = false;
  std::vector<double> m_benchUpdateMilliseconds;
  std::vector<double> m_benchRenderMilliseconds;
  std::vector<double> m_benchPresentMilliseconds;
  std::chrono::steady_clock::time_point m_benchDispatch{};
  std::chrono::steady_clock::time_point m_benchSubmitted{};
  std::chrono::steady_clock::time_point m_benchStart{};
  std::chrono::steady_clock::time_point m_benchEnd{};
  std::chrono::steady_clock::time_point m_benchLastStart{};
  bool m_benchDone = false;
};
