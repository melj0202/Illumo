#include "MeshViewerConfig.h"
#include "MeshViewerModule.h"
#include "MeshViewerPlatform.h"
#include <IllumoGuest/Documents.h>
#include <IllumoGuest/ModuleApplication.h>
#include <stdexcept>

// MeshViewerPlatform over guest services: a chosen mesh is a read-only grant
// whose bytes are parsed in memory; the host never discloses its path.
class GuestMeshViewerPlatform final : public MeshViewerPlatform
{
public:
  GuestMeshViewerPlatform(GuestServiceQueue& services, GuestFiles& files)
    : m_documents(services, files)
  {
  }
  void chooseMesh(const SaveLoadDialogSpec& specification,
                  LocationCallback done) override
  {
    m_documents.choose(GuestDocumentDialog::Open,
                       specification.fileDescription,
                       specification.defaultFilename,
                       specification.extensionPattern,
                       [done](const GuestDocumentLocation& chosen) {
                         done({ chosen.location, chosen.label });
                       });
  }
  void read(const std::string& location, ReadCallback done) override
  {
    m_documents.read(location, std::move(done));
  }
  MeshViewerLocation launchMesh() const override { return m_launch; }
  void setLaunch(const GuestLaunch& launch)
  {
    const GuestDocumentLocation document = GuestDocuments::launch(launch);
    m_launch = { document.location, document.label };
  }
  void pump() { m_documents.pump(); }

private:
  GuestDocuments m_documents;
  MeshViewerLocation m_launch;
};

static GuestMeshViewerPlatform* installedPlatform = nullptr;

MeshViewerPlatform&
MeshViewerPlatform::current()
{
  if (installedPlatform == nullptr) {
    throw std::logic_error("No guest MeshViewerPlatform is installed");
  }
  return *installedPlatform;
}

// IllMeshViewer as a WASM package: OBJ parsing, orbit camera, lighting,
// shadows, grid, wireframe and the skybox all run in this store. Loaded
// meshes are retained on the host (frame schema v3) instead of re-sent.
class MeshViewerGuest final : public GuestModuleApplication
{
public:
  MeshViewerGuest()
    : GuestModuleApplication(MeshViewerConfig::applicationName())
    , m_platform(services(), files())
  {
    installedPlatform = &m_platform;
  }
  ~MeshViewerGuest() override
  {
    if (installedPlatform == &m_platform) {
      installedPlatform = nullptr;
    }
  }
  MeshViewerGuest(const MeshViewerGuest&) = delete;
  MeshViewerGuest& operator=(const MeshViewerGuest&) = delete;
  MeshViewerGuest(MeshViewerGuest&&) = delete;
  MeshViewerGuest& operator=(MeshViewerGuest&&) = delete;

  GuestDescriptor describe() const override
  {
    return { GuestRole::Game,
             static_cast<std::uint32_t>(GuestCapability::Render) |
               static_cast<std::uint32_t>(GuestCapability::Assets) |
               static_cast<std::uint32_t>(GuestCapability::Storage) |
               static_cast<std::uint32_t>(GuestCapability::SelectedFiles) |
               static_cast<std::uint32_t>(GuestCapability::Console) |
               static_cast<std::uint32_t>(GuestCapability::Display),
             "meshviewer",
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
    MeshViewerConfig::ApplyDefaults(&settings);
  }
  std::vector<std::string> packageAssets() const override
  {
    return { "Assets/Skybox/skybox-daylight.png" };
  }
  void pumpProduct() override { m_platform.pump(); }
  std::unique_ptr<IModule> createFirstModule() override
  {
    return std::make_unique<MeshViewerModule>();
  }

private:
  GuestMeshViewerPlatform m_platform;
};

std::unique_ptr<GuestApplication>
CreateGuestApplication()
{
  return std::make_unique<MeshViewerGuest>();
}
