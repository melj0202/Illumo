#include "EditorModule.h"
#include "EditorUiAtlas.h"
#include "IllEdConfig.h"
#include "IllEdPlatform.h"
#include <IllumoGuest/Documents.h>
#include <IllumoGuest/ModuleApplication.h>
#include <stdexcept>

// IllEdPlatform over guest services: opened scenes are editable grants, so
// Save writes them in place; the host never discloses their paths.
class GuestIllEdPlatform final : public IllEdPlatform
{
public:
  GuestIllEdPlatform(GuestServiceQueue& services, GuestFiles& files)
    : m_documents(services, files)
  {
  }
  void chooseOpenLocation(const SaveLoadDialogSpec& specification,
                          LocationCallback done) override
  {
    choose(GuestDocumentDialog::Edit, specification, std::move(done));
  }
  void chooseSaveLocation(const SaveLoadDialogSpec& specification,
                          LocationCallback done) override
  {
    choose(GuestDocumentDialog::Save, specification, std::move(done));
  }
  void read(const std::string& location, ReadCallback done) override
  {
    m_documents.read(location, std::move(done));
  }
  void write(const std::string& location,
             std::string text,
             WriteCallback done) override
  {
    m_documents.write(location, std::move(text), std::move(done));
  }
  IllEdLocation launchDocument() const override { return m_launch; }
  void setLaunch(const GuestLaunch& launch)
  {
    const GuestDocumentLocation document = GuestDocuments::launch(launch);
    m_launch = { document.location, document.label };
  }
  void pump() { m_documents.pump(); }

private:
  void choose(GuestDocumentDialog mode,
              const SaveLoadDialogSpec& specification,
              LocationCallback done)
  {
    m_documents.choose(mode,
                       specification.fileDescription,
                       specification.defaultFilename,
                       specification.extensionPattern,
                       [done](const GuestDocumentLocation& chosen) {
                         done({ chosen.location, chosen.label });
                       });
  }
  GuestDocuments m_documents;
  IllEdLocation m_launch;
};

static GuestIllEdPlatform* installedPlatform = nullptr;

IllEdPlatform&
IllEdPlatform::current()
{
  if (installedPlatform == nullptr) {
    throw std::logic_error("No guest IllEdPlatform is installed");
  }
  return *installedPlatform;
}

// IllEd as a WASM package: the complete editor (panels, gizmos, picking,
// scene graph, .ilsc documents) runs in this store on the guest-side engine.
class IllEdGuest final : public GuestModuleApplication
{
public:
  IllEdGuest()
    : GuestModuleApplication(IllEdConfig::applicationName())
    , m_platform(services(), files())
  {
    installedPlatform = &m_platform;
  }
  ~IllEdGuest() override
  {
    if (installedPlatform == &m_platform) {
      installedPlatform = nullptr;
    }
  }
  IllEdGuest(const IllEdGuest&) = delete;
  IllEdGuest& operator=(const IllEdGuest&) = delete;
  IllEdGuest(IllEdGuest&&) = delete;
  IllEdGuest& operator=(IllEdGuest&&) = delete;

  GuestDescriptor describe() const override
  {
    return { GuestRole::Game,
             static_cast<std::uint32_t>(GuestCapability::Render) |
               static_cast<std::uint32_t>(GuestCapability::Assets) |
               static_cast<std::uint32_t>(GuestCapability::Storage) |
               static_cast<std::uint32_t>(GuestCapability::SelectedFiles) |
               static_cast<std::uint32_t>(GuestCapability::Console) |
               static_cast<std::uint32_t>(GuestCapability::Display),
             "illed",
             {} };
  }

protected:
  bool acceptStartup(std::span<const std::byte> startup) override
  {
    if (!GuestModuleApplication::acceptStartup(startup)) {
      return false;
    }
    if (launchFile() != nullptr) {
      m_platform.setLaunch(*launchFile());
    }
    return true;
  }
  void applyDefaults(IEnvVars& settings) override
  {
    IllEdConfig::ApplyDefaults(&settings);
  }
  std::vector<std::string> packageAssets() const override
  {
    return { EditorUiAtlas::relativePath() };
  }
  void pumpProduct() override { m_platform.pump(); }
  std::unique_ptr<IModule> createFirstModule() override
  {
    return std::make_unique<EditorModule>();
  }

private:
  GuestIllEdPlatform m_platform;
};

std::unique_ptr<GuestApplication>
CreateGuestApplication()
{
  return std::make_unique<IllEdGuest>();
}
