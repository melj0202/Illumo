#pragma once

#include <Illumo/Content/SceneDirector.h>
#include <Illumo/Engine/IllumoContext.h>
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/DrawList.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Services/InputManager.h>
#include <IllumoGuest/Application.h>
#include <IllumoGuest/Audio.h>
#include <IllumoGuest/Console.h>
#include <IllumoGuest/Diagnostics.h>
#include <IllumoGuest/Dialog.h>
#include <IllumoGuest/Display.h>
#include <IllumoGuest/Environment.h>
#include <IllumoGuest/Files.h>
#include <IllumoGuest/FontProvider.h>
#include <IllumoGuest/PanelSurfaces.h>
#include <IllumoGuest/RecordingBackend.h>
#include <IllumoGuest/RenderWorld.h>
#include <IllumoGuest/SceneWorlds.h>
#include <IllumoGuest/SnapshotWindow.h>
#include <IllumoGuest/VfsAssets.h>
#include <memory>
#include <optional>
#include <set>

// Product console lines are forwarded to the host console, which owns the
// overlay's drawing, input and built-in commands. The base CommandLine is
// never drawn; its own console_* commands go to a private registry.
class GuestCommandLine final : public CommandLine
{
public:
  GuestCommandLine(IEnvVars* settings,
                   CommandRegistry* builtins,
                   IRenderWindow* window,
                   GuestConsole& console,
                   const std::string& applicationName);
  void onHistoryAppended(const historyBuffer& item, bool erasedFront) override;
  // A repeat collapsed into the newest line is forwarded again; the host
  // console performs its own collapsing.
  void onHistoryBackUpdated() override;

private:
  void forward(const historyBuffer& item);
  GuestConsole& m_console;
};

// Guest-side engine and program (D-E31): composes an IllumoContext entirely
// from guest objects and runs the product's scenes inside one WASM store.
// The program lives as long as the store and owns what every scene shares;
// its SceneDirector holds the scenes and switches between them at frame
// boundaries. Nothing here crosses the ABI; the host sees only input
// snapshots, copied services and IRF1 frames. Products implement bootstrap
// readiness and add their scenes in createScenes.
class GuestProgram : public GuestApplication
{
public:
  GuestProgram(std::string applicationName,
               std::string settingsPath = "envvars.json");
  ~GuestProgram() override;
  GuestProgram(const GuestProgram&) = delete;
  GuestProgram& operator=(const GuestProgram&) = delete;
  GuestProgram(GuestProgram&&) = delete;
  GuestProgram& operator=(GuestProgram&&) = delete;

  bool start(std::span<const std::byte> startup) final;
  void update(const GuestInput& input) final;
  GuestFrame frame() final;
  void recordFrame(GuestFrame& output) final;
  bool close() final;
  void shutdown() final;
  bool closeRequested() final;
  bool restartRequested() final;

protected:
  // Startup bytes are product configuration. The default accepts none or a
  // GuestLaunch record, exposed through launchFile().
  virtual bool acceptStartup(std::span<const std::byte> startup);
  // The document named with --open, granted as the selection "launch".
  const GuestLaunch* launchFile() const
  {
    return m_launch ? &*m_launch : nullptr;
  }
  // Runs once after persisted settings load (or are found absent) and the
  // package's first-run envvars.json has filled any values still unset.
  virtual void applyDefaults(IEnvVars& settings);
  // Package files AssetManager serves to scenes (textures, cubemap crosses,
  // meshes by name), relative to /app or absolute virtual paths. All are read
  // and pinned before bootstrap() first runs.
  virtual std::vector<std::string> packageAssets() const;
  // Pumped every update until true; throw to fail startup visibly.
  virtual bool bootstrap();
  // Runs once after bootstrap: add the program's scenes and switch to the
  // first. False, or a first scene that fails to start, closes the product.
  virtual bool createScenes(SceneDirector& scenes) = 0;
  // Product service adapters pumped every update before scenes run.
  virtual void pumpProduct();
  // Program UI shared by every scene (a Settings menu): updated before the
  // active scene, so it takes input first, and dispatched after it, so it
  // draws on top. Neither runs before the first scene starts.
  virtual void updateProgram(double elapsed);
  virtual void dispatchProgram(DrawList& frame);
  // A product overlay above everything (a software pointer, a transition
  // cover): updated after the scene each running frame, before display
  // settings synchronize, and dispatched last.
  virtual void updateOverlay(double elapsed);
  virtual void dispatchOverlay(DrawList& scene);
  GuestFiles& files() { return m_files; }
  // AssetManager's byte source: pinned preloads, fetch sets and /local bytes.
  GuestVfsAssets& assetCache() { return m_assetCache; }
  GuestEnvironment& settings() { return m_settings; }
  const IllumoContext& context() const { return m_context; }
  // The product may publish extra services through the context.
  IllumoContext& programContext() { return m_context; }
  // Null until the program is running.
  SceneDirector* scenes() { return m_scenes.get(); }
  const SceneDirector* scenes() const { return m_scenes.get(); }
  bool running() const { return m_phase == Phase::Running; }

private:
  enum class Phase
  {
    Settings,
    Bootstrap,
    Running,
    Stopped
  };
  // Applies the rendering switches and creates the program's scenes.
  void startScenes();
  // Reads the package's envvars.json once; true when done (or absent).
  bool applyPackagedDefaults();
  void runConsoleInvocations();
  void synchronizeCommands();

  std::string m_applicationName;
  GuestDiagnostics m_diagnostics;
  GuestFiles m_files;
  GuestEnvironment m_settings;
  GuestDisplay m_display;
  GuestConsole m_console;
  GuestRecordingBackend m_backend;
  GuestSnapshotWindow m_window;
  Camera m_camera;
  Renderer m_renderer;
  GuestVfsAssets m_assetCache;
  // After the renderer: releases its textures and meshes first.
  AssetManager m_assets;
  bool m_assetsRequested = false;
  std::uint64_t m_defaultsTask = 0;
  bool m_defaultsApplied = false;
  GuestFontProvider m_fonts;
  InputManager m_input;
  DrawList m_scene;
  // Detached panel windows (Windows capability); published as
  // IllumoContext::panelSurfaces only when granted.
  GuestPanelSurfaces m_panels;
  // Sound effects (Audio capability); published as IllumoContext::audio only
  // when granted.
  GuestAudio m_audio;
  // Host-owned world objects (HostRender capability), one world per scene;
  // IllumoContext::renderWorld follows the active scene's.
  std::unique_ptr<GuestSceneWorlds> m_worlds;
  CommandRegistry m_commands;
  CommandRegistry m_consoleBuiltins;
  GuestCommandLine m_commandLine;
  IllumoContext m_context;
  // After m_context and every service: scenes stop before they go.
  std::unique_ptr<SceneDirector> m_scenes;
  std::set<std::string> m_forwarded;
  std::string m_lastFrameError;
  std::optional<GuestLaunch> m_launch;
  Phase m_phase = Phase::Settings;
};
