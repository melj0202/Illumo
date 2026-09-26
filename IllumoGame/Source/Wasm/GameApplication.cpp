#include "Game/CSimSounds.h"
#include "Game/CSimTypeface.h"
#include "Game/IllumoGameConfig.h"
#include "Game/MainMenuModule.h"
#include "Game/PerformanceOverlay.h"
#include "Game/SimulatorSettings.h"
#include "Game/SoftwareCursor.h"
#include "Wasm/CatalogBootstrap.h"
#include "Wasm/GuestPlatform.h"
#include <Illumo/Content/PackageManifest.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Engine/IModuleHost.h>
#include <IllumoGuest/Program.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Temporary (scene programs M2 to M4): CSim's title and canvas are still
// IModules, so each runs as a scene until they become TitleScene and
// CanvasScene. The module exits when its scene leaves, before the next one
// starts, exactly as a module transition did, and its scene is released
// right after. A module cannot resume, so re-entering one that exited (after
// the next failed to start) closes the product, as a failed transition did.
// Modules shared one camera, and the canvas relies on inheriting the title's
// zoom, so the camera is handed from each module to the next.
class ModuleScene final : public ProgramScene
{
public:
  ModuleScene(std::unique_ptr<IModule> module, std::optional<Camera>& handoff)
    : m_module(std::move(module))
    , m_handoff(handoff)
  {
  }
  bool start(IllumoContext& context) override
  {
    m_context = &context;
    if (m_handoff && context.camera != nullptr) {
      *context.camera = *m_handoff;
    }
    m_running = m_module != nullptr && m_module->Start(&context);
    return m_running;
  }
  void enter() override
  {
    if (!m_running && m_context != nullptr && m_context->window != nullptr) {
      Logger::LogError("The next screen failed to start; closing");
      m_context->window->requestClose();
    }
  }
  void leave() override
  {
    exitModule();
    if (m_context != nullptr && m_context->camera != nullptr) {
      m_handoff = *m_context->camera;
    }
  }
  void update(double elapsed) override
  {
    if (m_running) {
      m_module->Update(elapsed);
    }
  }
  void dispatch(Scene& frame) override
  {
    if (m_running) {
      m_module->DispatchDrawables(&frame);
    }
  }
  void stop() override { exitModule(); }
  bool closeRequested() override
  {
    return !m_running || m_module->OnCloseRequested();
  }

private:
  void exitModule()
  {
    if (m_running) {
      m_running = false;
      m_module->Exit();
    }
  }
  std::unique_ptr<IModule> m_module;
  std::optional<Camera>& m_handoff;
  IllumoContext* m_context = nullptr;
  bool m_running = false;
};

// IllumoGame as a WASM package: the complete product (menus, canvas, editor,
// console commands, persistence, workshop) runs in this store on top of the
// guest-side engine. The host provides only generic services and frames.
class IllumoGameGuest final
  : public GuestProgram
  , public IModuleHost
{
public:
  IllumoGameGuest()
    : GuestProgram("CSim")
    , m_platform(services(), files())
    , m_catalog(files())
  {
    m_platform.install();
  }
  ~IllumoGameGuest() override { CSimSounds::uninstall(); }
  IllumoGameGuest(const IllumoGameGuest&) = delete;
  IllumoGameGuest& operator=(const IllumoGameGuest&) = delete;
  IllumoGameGuest(IllumoGameGuest&&) = delete;
  IllumoGameGuest& operator=(IllumoGameGuest&&) = delete;
  GuestDescriptor describe() const override
  {
    // Jobs and Audio are optional: generations run with serial kernels in
    // this store, and without Audio the game is silent.
    return { GuestRole::Game,
             static_cast<std::uint32_t>(GuestCapability::Render) |
               static_cast<std::uint32_t>(GuestCapability::Assets) |
               static_cast<std::uint32_t>(GuestCapability::Storage) |
               static_cast<std::uint32_t>(GuestCapability::SelectedFiles) |
               static_cast<std::uint32_t>(GuestCapability::Clipboard) |
               static_cast<std::uint32_t>(GuestCapability::Console) |
               static_cast<std::uint32_t>(GuestCapability::Display),
             "csim",
             {} };
  }

protected:
  void pumpProduct() override { m_platform.pump(); }

  // CSim draws its own pointer over every screen and hides the system
  // cursor while it does. The host console draws above the game, so while it
  // is open the system cursor returns. `softwareCursor 0` turns it off.
  void updateOverlay(double elapsed) override
  {
    const IllumoContext& engine = context();
    if (engine.window == nullptr || engine.renderer == nullptr) {
      return;
    }
    if (!m_cursorPrepared) {
      m_cursor.prepare(engine.window, engine.renderer);
      m_cursorPrepared = true;
    }
    const EnvVar& enabled = settings().getVar("softwareCursor");
    const bool consoleOpen =
      engine.commandLine != nullptr && engine.commandLine->isOpen;
    const bool active =
      (enabled.value.empty() || enabled.valueAsBool) && !consoleOpen;
    engine.window->setSystemCursorHidden(active);
    const std::array<double, 2> mouse = engine.window->getMouseCoords();
    const std::array<int, 2> size = engine.window->getWindowDimensions();
    const bool inside = mouse[0] >= 0.0 && mouse[1] >= 0.0 &&
                        mouse[0] < static_cast<double>(size[0]) &&
                        mouse[1] < static_cast<double>(size[1]);
    const float scale = std::max(0.01f, engine.renderer->getUiScale());
    m_cursor.update(
      static_cast<float>(elapsed),
      static_cast<float>(mouse[0]) / scale,
      static_cast<float>(mouse[1]) / scale,
      active && inside,
      engine.inputManager != nullptr &&
        engine.inputManager->isMouseButtonPressed(KeyCode::MouseLeft),
      settings().getVar("reducedUiMotion").valueAsBool);

    // The corner performance readout (Video settings). Memory is this
    // store's linear memory, which only grows.
    if (!m_performancePrepared) {
      m_performance.prepare(engine.window, engine.renderer);
      m_performancePrepared = true;
    }
    m_performance.update(
      static_cast<float>(elapsed),
      SimulatorSettings::flag(&settings(), "showFPS", false),
      SimulatorSettings::flag(&settings(), "showMemory", false),
      static_cast<std::uint64_t>(__builtin_wasm_memory_size(0)) * 65536u);
  }

  void dispatchOverlay(Scene& scene) override
  {
    // Under the pointer, so the cursor stays on top.
    if (m_performance.isVisible()) {
      scene.AddDrawable(&m_performance.getVisual(), RenderLayerId::UI);
    }
    if (m_cursor.isVisible()) {
      scene.AddDrawable(&m_cursor.getVisual(), RenderLayerId::UI);
    }
  }

  // The package's envvars.json has already filled first-run values.
  void applyDefaults(IEnvVars& settings) override
  {
    IllumoGameConfig::ApplyDefaults(&settings);
    Logger::LogTrace("CSim settings ready; preferred ruleset " +
                     settings.getVar("RuleSetString").value);
  }

  // The render3dTest diagnostic scene, pinned in the asset cache.
  std::vector<std::string> packageAssets() const override
  {
    return { "Scenes/render3d-test.ilsc" };
  }

  bool bootstrap() override
  {
    // Kikuta replaces the engine default; its weights load beside the
    // catalogs, and the menu waits only for the rest weight.
    if (!CSimTypeface::installed()) {
      CSimTypeface::install();
    }
    m_catalog.pump();
    if (m_catalog.failed()) {
      throw std::runtime_error(m_catalog.error());
    }
    const bool soundsReady = loadSounds();
    const bool versionReady = readPackageVersion();
    if (!m_catalog.ready() || !CSimTypeface::ready() || !soundsReady ||
        !versionReady) {
      return false;
    }
    RuleSetRegistry::instance() = m_catalog.registry();
    for (const std::string& path : m_catalog.merged()) {
      Logger::LogInfo("Merged package catalog " + path);
    }
    for (const std::string& warning : m_catalog.warnings()) {
      Logger::LogWarning(warning);
    }
    Logger::LogInfo(
      "Rule catalogs ready: " +
      std::to_string(RuleSetRegistry::instance().getKnownFamilies().size()) +
      " families, " +
      std::to_string(RuleSetRegistry::instance().getKnownRules().size()) +
      " rulesets");
    if (CSimTypeface::installed()) {
      Logger::LogTrace("Kikuta typeface installed as the default font");
    } else {
      Logger::LogWarning(
        "Kikuta typeface unavailable; using the engine default font");
    }
    return true;
  }

  bool createScenes(SceneDirector& scenes) override
  {
    (void)scenes;
    Logger::LogTrace("CSim bootstrap complete; opening the main menu");
    CSimSounds::play(CSimSound::ProgramStart);
    programContext().moduleHost = this;
    return addModuleScene(std::make_unique<MainMenuModule>());
  }

  // A module that has left is released once the switch has applied.
  void updateProgram(double elapsed) override
  {
    (void)elapsed;
    SceneDirector* director = scenes();
    if (director == nullptr) {
      return;
    }
    for (std::vector<std::string>::iterator it = m_retired.begin();
         it != m_retired.end();) {
      if (director->activeName() == *it || director->hasPendingSwitch()) {
        ++it;
        continue;
      }
      director->release(*it);
      it = m_retired.erase(it);
    }
  }

public:
  void RequestTransition(std::unique_ptr<IModule> nextModule) override
  {
    if (nextModule == nullptr || HasPendingTransition()) {
      Logger::LogWarning("Module transition rejected: empty request or "
                         "transition already pending");
      return;
    }
    addModuleScene(std::move(nextModule));
  }
  bool HasPendingTransition() const override
  {
    return scenes() != nullptr && scenes()->hasPendingSwitch();
  }

private:
  bool addModuleScene(std::unique_ptr<IModule> module)
  {
    SceneDirector* director = scenes();
    if (director == nullptr) {
      return false;
    }
    const std::string name = "module-" + std::to_string(++m_moduleScenes);
    director->emplace<ModuleScene>(name, std::move(module), m_cameraHandoff);
    if (!director->switchTo(name)) {
      return false;
    }
    if (!m_current.empty()) {
      m_retired.push_back(m_current);
    }
    m_current = name;
    return true;
  }

  // The staged illumo.json carries the build version (D-F2) that the main
  // menu shows. An unreadable manifest only leaves the menu without one.
  bool readPackageVersion()
  {
    if (m_versionRead) {
      return true;
    }
    if (m_versionTask == 0) {
      m_versionTask = files().read(
        GuestFileArea::Package, PackageManifest::kFileName, 64u * 1024u);
      if (m_versionTask == 0) {
        return false;
      }
    }
    GuestFileResult result;
    if (!files().take(m_versionTask, result)) {
      return false;
    }
    m_versionTask = 0;
    m_versionRead = true;
    PackageManifest manifest;
    std::string error;
    const std::string_view text(
      reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size());
    if (result.outcome != GuestFileOutcome::Success ||
        !decodePackageManifest(text, PackageCeilings{}, manifest, error)) {
      Logger::LogWarning("CSim cannot read its package manifest; the menu "
                         "shows no version" +
                         (error.empty() ? std::string() : ": " + error));
      return true;
    }
    if (!manifest.version.empty()) {
      Logger::LogInfo("CSim v" + manifest.version);
    }
    m_platform.setPackageVersion(manifest.version);
    return true;
  }

  // Sound files load beside the catalogs when the host can play sound; they
  // are decoded here, in the store, and only samples go to the host. A
  // missing or damaged file silences just its own cue.
  bool loadSounds()
  {
    IAudio* audio = context().audio;
    if (audio == nullptr && !m_soundsLoaded && !m_audioAbsenceReported) {
      m_audioAbsenceReported = true;
      Logger::LogInfo("Audio is not granted to CSim; it plays silently");
    }
    if (m_soundsLoaded || audio == nullptr) {
      return true;
    }
    if (m_soundFetch == 0) {
      m_soundFetch = assetCache().fetch(CSimSounds::fileNames());
    }
    std::vector<std::string> missing;
    if (!assetCache().fetched(m_soundFetch, &missing)) {
      return false;
    }
    std::vector<std::string> problems;
    CSimSounds::install(
      audio,
      &settings(),
      [this](const std::string& path, std::vector<std::byte>& bytes) {
        std::vector<unsigned char> raw;
        if (!assetCache().read(assetCache().canonical(path), raw)) {
          return false;
        }
        bytes.resize(raw.size());
        std::memcpy(bytes.data(), raw.data(), raw.size());
        return true;
      },
      problems);
    assetCache().release(m_soundFetch);
    m_soundsLoaded = true;
    if (missing.size() == CSimSounds::fileNames().size()) {
      Logger::LogInfo("No sound files are packaged; CSim plays silently");
    } else {
      for (const std::string& problem : problems) {
        Logger::LogWarning("Sound unavailable: " + problem);
      }
    }
    const std::size_t cueCount = CSimSounds::fileNames().size();
    if (!CSimSounds::installed()) {
      Logger::LogInfo("Audio output is unavailable; CSim plays silently");
    } else if (missing.size() != cueCount) {
      const std::size_t loaded =
        problems.size() < cueCount ? cueCount - problems.size() : 0u;
      Logger::LogInfo("Sound cues loaded: " + std::to_string(loaded) + " of " +
                      std::to_string(cueCount));
    }
    return true;
  }

  GuestCSimPlatform m_platform;
  CSimCatalogBootstrap m_catalog;
  std::uint64_t m_versionTask = 0;
  // The module scene now running, those waiting to be released, and a
  // counter that keeps scene names unique.
  std::string m_current;
  std::vector<std::string> m_retired;
  // The camera as the last module left it.
  std::optional<Camera> m_cameraHandoff;
  unsigned m_moduleScenes = 0;
  bool m_versionRead = false;
  std::uint64_t m_soundFetch = 0;
  bool m_soundsLoaded = false;
  bool m_audioAbsenceReported = false;
  // The software pointer; it outlives module transitions.
  SoftwareCursor m_cursor;
  bool m_cursorPrepared = false;
  PerformanceOverlay m_performance;
  bool m_performancePrepared = false;
};

std::unique_ptr<GuestApplication>
CreateGuestApplication()
{
  return std::make_unique<IllumoGameGuest>();
}
