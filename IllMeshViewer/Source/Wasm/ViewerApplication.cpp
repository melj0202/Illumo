#include "MeshViewerConfig.h"
#include "MeshViewerModule.h"
#include "MeshViewerPlatform.h"
#include <Illumo/Services/Logger.h>
#include <IllumoGuest/Documents.h>
#include <IllumoGuest/FileTree.h>
#include <IllumoGuest/ModuleApplication.h>
#include <IllumoGuest/SceneFetches.h>
#include <stdexcept>

// MeshViewerPlatform over guest services: a chosen mesh is a read-only grant
// whose bytes are parsed in memory; the host never discloses its path.
// "vfs:" locations read the host's virtual file tree, and a scene's assets
// are fetched into the guest asset cache before it instantiates.
class GuestMeshViewerPlatform final : public MeshViewerPlatform
{
public:
  GuestMeshViewerPlatform(GuestServiceQueue& services,
                          GuestFiles& files,
                          GuestVfsAssets& cache)
    : m_documents(services, files)
    , m_tree(files)
    , m_fetches(cache)
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
    if (isTreeLocation(location)) {
      const bool queued = m_tree.read(
        treePath(location),
        [done](GuestFileOutcome outcome, std::vector<std::byte> bytes) {
          std::string text;
          text.reserve(bytes.size());
          for (std::byte value : bytes) {
            text.push_back(static_cast<char>(value));
          }
          const bool success = outcome == GuestFileOutcome::Success;
          done(success,
               text,
               success ? std::string()
                       : std::string("Cannot read the file from the tree"));
        });
      if (!queued) {
        done(false, {}, "Too many file operations in flight");
      }
      return;
    }
    m_documents.read(location, std::move(done));
  }
  void fetchAssets(const std::vector<std::string>& paths,
                   FetchCallback done) override
  {
    m_fetches.fetch(paths, std::move(done));
  }
  void releaseAssets() override { m_fetches.release(); }
  MeshViewerLocation launchMesh() const override { return m_launch; }
  void setLaunch(const GuestLaunch& launch)
  {
    const GuestDocumentLocation document = GuestDocuments::launch(launch);
    m_launch = { document.location, document.label };
  }
  void pump()
  {
    m_documents.pump();
    m_tree.pump();
    m_fetches.pump();
  }

private:
  GuestDocuments m_documents;
  GuestFileTree m_tree;
  GuestSceneFetches m_fetches;
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
    , m_platform(services(), files(), assetCache())
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
      Logger::LogTrace("Mesh viewer launch file: " + launchFile()->label);
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
