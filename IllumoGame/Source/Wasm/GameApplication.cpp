#include "Game/IllumoGameConfig.h"
#include "Game/MainMenuModule.h"
#include "Wasm/CatalogBootstrap.h"
#include "Wasm/GuestPlatform.h"
#include <IllumoGuest/ModuleApplication.h>
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
  GuestDescriptor describe() const override
  {
    // Jobs is optional: generations run with serial kernels in this store.
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

  bool bootstrap() override
  {
    m_catalog.pump();
    if (m_catalog.failed()) {
      throw std::runtime_error(m_catalog.error());
    }
    if (!m_catalog.ready()) {
      return false;
    }
    RuleSetRegistry::instance() = m_catalog.registry();
    return true;
  }

  std::unique_ptr<IModule> createFirstModule() override
  {
    return std::make_unique<MainMenuModule>();
  }

private:
  GuestCSimPlatform m_platform;
  CSimCatalogBootstrap m_catalog;
};

std::unique_ptr<GuestApplication>
CreateGuestApplication()
{
  return std::make_unique<IllumoGameGuest>();
}
