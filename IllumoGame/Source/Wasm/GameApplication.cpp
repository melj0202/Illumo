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
#include <IllumoGuest/ModuleApplication.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

// IllumoGame as a WASM package: the complete product (menus, canvas, editor,
// console commands, persistence, workshop) runs in this store on top of the
// guest-side engine. The host provides only generic services and frames.
class IllumoGameGuest final : public GuestModuleApplication
{
public:
  IllumoGameGuest()
    : GuestModuleApplication("CSim")
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

  std::unique_ptr<IModule> createFirstModule() override
  {
    Logger::LogTrace("CSim bootstrap complete; opening the main menu");
    CSimSounds::play(CSimSound::ProgramStart);
    return std::make_unique<MainMenuModule>();
  }

private:
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
