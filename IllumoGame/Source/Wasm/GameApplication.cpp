#include "Game/CSimSounds.h"
#include "Game/CSimTypeface.h"
#include "Game/IllumoGameConfig.h"
#include "Game/MainMenuModule.h"
#include "Wasm/CatalogBootstrap.h"
#include "Wasm/GuestPlatform.h"
#include <Illumo/Services/Logger.h>
#include <IllumoGuest/ModuleApplication.h>
#include <cstring>
#include <stdexcept>

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

  // The package's envvars.json has already filled first-run values.
  void applyDefaults(IEnvVars& settings) override
  {
    IllumoGameConfig::ApplyDefaults(&settings);
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
    if (!m_catalog.ready() || !CSimTypeface::ready() || !soundsReady) {
      return false;
    }
    RuleSetRegistry::instance() = m_catalog.registry();
    for (const std::string& path : m_catalog.merged()) {
      Logger::LogInfo("Merged package catalog " + path);
    }
    for (const std::string& warning : m_catalog.warnings()) {
      Logger::LogWarning(warning);
    }
    return true;
  }

  std::unique_ptr<IModule> createFirstModule() override
  {
    CSimSounds::play(CSimSound::ProgramStart);
    return std::make_unique<MainMenuModule>();
  }

private:
  // Sound files load beside the catalogs when the host can play sound; they
  // are decoded here, in the store, and only samples go to the host. A
  // missing or damaged file silences just its own cue.
  bool loadSounds()
  {
    IAudio* audio = context().audio;
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
    return true;
  }

  GuestCSimPlatform m_platform;
  CSimCatalogBootstrap m_catalog;
  std::uint64_t m_soundFetch = 0;
  bool m_soundsLoaded = false;
};

std::unique_ptr<GuestApplication>
CreateGuestApplication()
{
  return std::make_unique<IllumoGameGuest>();
}
