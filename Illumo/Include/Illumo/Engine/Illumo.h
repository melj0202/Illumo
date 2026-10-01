#pragma once

#include <Illumo/Engine/FrameProfiler.h>
#include <Illumo/Engine/IllumoContext.h>

#include <functional>
#include <memory>
#include <string>

class AssetManager;
class Camera;
class CommandLine;
class CommandRegistry;
class EnvVars;
class IBackend;
class IEnvVars;
class InputManager;
class Renderer;
class DrawList;
enum class BackendDef;

struct IllumoConfig
{
  std::string applicationName{ "Illumo" };
  // UTF-8; empty selects EnvVars::ApplicationConfigPath().
  std::string environmentPath;
};

class IllumoTestAccess;

// The engine's services (window, renderer, assets, console, input, scene) and
// the fixed parts of a frame. The runtime (D-E31) drives a frame in phases,
// with its debug overlay and program in between:
//   1. beginUpdate: input, global hotkeys (F11, F3, F5) and the camera;
//   2. the debug overlay's update, then the program's;
//   3. endUpdate: key and character events nobody read are dropped;
//   4. beginRender: the frame's drawables are cleared;
//   5. the program dispatches, then the debug overlay (drawn on top);
//   6. endRender: assets are pumped and the frame is rendered and presented.
// Every phase does nothing before initialize or after shutdown.
class Illumo
{
public:
  explicit Illumo(IllumoConfig config = {});
  ~Illumo();

  Illumo(const Illumo&) = delete;
  Illumo& operator=(const Illumo&) = delete;
  Illumo(Illumo&&) = delete;
  Illumo& operator=(Illumo&&) = delete;

  EnvVars& environment();
  const EnvVars& environment() const;
  const std::string& applicationName() const;
  FrameProfiler& frameProfiler() { return m_frameProfiler; }

  bool initialize();
  void beginUpdate(double dt);
  void endUpdate();
  // The frame's draw list, emptied for this frame's drawables. Null before
  // initialize.
  DrawList* beginRender();
  void endRender();
  // Releases every service. The program must have stopped first.
  void shutdown() noexcept;

  IllumoContext& context();
  const IllumoContext& context() const;
  // The window asked to close (or restart), or there is no window.
  bool shouldClose() const;
  // Keeps the window open after a close request the program declined; a
  // restart that rode on the request is dropped too.
  void deferClose();

private:
  friend class IllumoTestAccess;

  // Both receive the graphics API being started: the GraphicsAPI setting,
  // then OpenGL when that one fails.
  using WindowFactory = std::function<std::unique_ptr<
    IRenderWindow>(int, int, const std::string&, IEnvVars*, BackendDef)>;
  using BackendFactory =
    std::function<std::unique_ptr<IBackend>(IRenderWindow*, BackendDef)>;

  void applyHostDefaults();
  void clearContext();
  void processGlobalHotkeys();
  void configureScenePipeline();
  void releaseServices();
  // Creates the window and an initialized backend for one API. False, with
  // both released, when either fails.
  bool startGraphics(BackendDef api,
                     int width,
                     int height,
                     std::unique_ptr<IBackend>* backend);

  FrameProfiler m_frameProfiler;
  std::string m_applicationName;
  WindowFactory m_windowFactory;
  BackendFactory m_backendFactory;
  std::unique_ptr<EnvVars> m_environment;
  std::unique_ptr<Camera> m_camera;
  std::unique_ptr<IRenderWindow> m_window;
  std::unique_ptr<Renderer> m_renderer;
  std::unique_ptr<AssetManager> m_assetManager;
  std::unique_ptr<CommandRegistry> m_commandRegistry;
  std::unique_ptr<CommandLine> m_commandLine;
  std::unique_ptr<InputManager> m_inputManager;
  std::unique_ptr<DrawList> m_scene;
  IllumoContext m_context{};
  bool m_initialized{ false };
  bool m_motionBlurPipelineConfigured{ false };
  float m_configuredBlurAmount{ 0.0f };
  float m_configuredBlurMax{ 0.0f };
  int m_configuredBlurSamples{ 0 };
};
