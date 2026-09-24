#include "EditorAssets.h"
#include "EditorModule.h"
#include "EditorUiAtlas.h"
#include "IllEdConfig.h"
#include "IllEdPlatform.h"
#include <Illumo/Services/Logger.h>
#include <IllumoGuest/Clipboard.h>
#include <IllumoGuest/Documents.h>
#include <IllumoGuest/FileTree.h>
#include <IllumoGuest/ModuleApplication.h>
#include <IllumoGuest/SceneFetches.h>
#include <stdexcept>

// IllEdPlatform over guest services: opened scenes are editable grants, so
// Save writes them in place; the host never discloses their paths.
class GuestIllEdPlatform final : public IllEdPlatform
{
public:
  GuestIllEdPlatform(GuestServiceQueue& services,
                     GuestFiles& files,
                     GuestVfsAssets& cache)
    : m_documents(services, files)
    , m_clipboard(services)
    , m_tree(files)
    , m_fetches(cache)
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
    if (isTreeLocation(location)) {
      m_tree.read(
        treePath(location),
        [done](GuestFileOutcome outcome, std::vector<std::byte> bytes) {
          std::string text;
          for (std::byte value : bytes) {
            text.push_back(static_cast<char>(value));
          }
          done(outcome == GuestFileOutcome::Success,
               text,
               outcome == GuestFileOutcome::Success
                 ? std::string()
                 : std::string("Cannot read the scene"));
        });
      return;
    }
    m_documents.read(location, std::move(done));
  }
  void write(const std::string& location,
             std::string text,
             WriteCallback done) override
  {
    if (isTreeLocation(location)) {
      std::vector<std::byte> bytes;
      bytes.reserve(text.size());
      for (char value : text) {
        bytes.push_back(static_cast<std::byte>(value));
      }
      m_tree.write(
        treePath(location), std::move(bytes), [done](GuestFileOutcome outcome) {
          const bool saved = outcome == GuestFileOutcome::Success;
          done(saved,
               saved ? std::string()
                     : std::string("Cannot write to the project"));
        });
      return;
    }
    m_documents.write(location, std::move(text), std::move(done));
  }
  void listDirectory(const std::string& path, ListCallback done) override
  {
    const bool queued = m_tree.list(
      path,
      [done](GuestFileOutcome outcome, std::vector<GuestFileEntry> entries) {
        std::vector<FileEntry> listed;
        for (GuestFileEntry& entry : entries) {
          listed.push_back(
            { std::move(entry.name), entry.directory, entry.size });
        }
        done(outcome == GuestFileOutcome::Success, std::move(listed));
      });
    if (!queued) {
      done(false, {});
    }
  }
  bool hasProject() const override { return m_project; }
  void setProject(bool project) { m_project = project; }
  void fetchAssets(const std::vector<std::string>& paths,
                   FetchCallback done) override
  {
    m_fetches.fetch(paths, std::move(done));
  }
  void releaseAssets() override { m_fetches.release(); }
  void importIntoProject(const SaveLoadDialogSpec& specification,
                         const std::string& folder,
                         ImportCallback done) override
  {
    if (!m_project) {
      done(false, {}, "No project is mounted");
      return;
    }
    m_documents.choose(
      GuestDocumentDialog::Open,
      specification.fileDescription,
      specification.defaultFilename,
      specification.extensionPattern,
      [this, folder, done](const GuestDocumentLocation& chosen) {
        if (chosen.empty()) {
          done(false, {}, {});
          return;
        }
        // Read the pick first so oversized textures are refused before
        // anything is copied into the project.
        m_documents.read(
          chosen.location,
          [this, folder, done, chosen](
            bool success, const std::string& bytes, const std::string& error) {
            std::string reason = error;
            if (!success ||
                !EditorAssets::validateImport(
                  chosen.label,
                  std::vector<unsigned char>(bytes.begin(), bytes.end()),
                  &reason)) {
              done(false, {}, reason);
              return;
            }
            const std::string grant = grantOf(chosen.location);
            const std::string target =
              "/project/" +
              (folder.empty() ? std::string(EditorAssets::importFolder(
                                  EditorAssets::kindFor(chosen.label)))
                              : folder) +
              "/" + chosen.label;
            const bool queued = m_tree.importFile(
              grant, target, [done, target](GuestFileOutcome outcome) {
                const bool imported = outcome == GuestFileOutcome::Success;
                done(imported,
                     imported ? target : std::string(),
                     imported ? std::string()
                              : std::string("The import was refused"));
              });
            if (!queued) {
              done(false, {}, "Too many file operations in flight");
            }
          });
      });
  }
  void packProject(const SaveLoadDialogSpec& specification,
                   WriteCallback done) override
  {
    if (!m_project) {
      done(false, "No project is mounted");
      return;
    }
    choose(GuestDocumentDialog::Save,
           specification,
           [this, done](const IllEdLocation& chosen) {
             if (chosen.empty()) {
               done(false, {});
               return;
             }
             const bool queued = m_tree.pack(
               grantOf(chosen.location),
               "/project",
               [done](GuestFileOutcome outcome) {
                 const bool packed = outcome == GuestFileOutcome::Success;
                 done(packed,
                      packed ? std::string()
                             : std::string("The project could not be packed"));
               });
             if (!queued) {
               done(false, "Too many file operations in flight");
             }
           });
  }
  IllEdLocation launchDocument() const override { return m_launch; }
  void setLaunch(const GuestLaunch& launch)
  {
    const GuestDocumentLocation document = GuestDocuments::launch(launch);
    m_launch = { document.location, document.label };
  }
  void setClipboardText(const std::string& text) override
  {
    m_clipboard.set(text);
  }
  void requestClipboardText(
    std::function<void(const std::string& text)> done) override
  {
    m_clipboard.get();
    m_pasteWaiting = std::move(done);
  }
  void pump()
  {
    m_documents.pump();
    m_clipboard.pump();
    m_tree.pump();
    m_fetches.pump();
    if (m_pasteWaiting && m_clipboard.idle()) {
      std::function<void(const std::string& text)> done =
        std::move(m_pasteWaiting);
      m_pasteWaiting = nullptr;
      done(m_clipboard.text());
    }
  }

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
  // "selected:<grant>" locations name a dialog grant.
  static std::string grantOf(const std::string& location)
  {
    const std::string prefix = "selected:";
    return location.rfind(prefix, 0) == 0 ? location.substr(prefix.size())
                                          : location;
  }
  GuestDocuments m_documents;
  GuestClipboard m_clipboard;
  GuestFileTree m_tree;
  GuestSceneFetches m_fetches;
  bool m_project = false;
  std::function<void(const std::string& text)> m_pasteWaiting;
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
    , m_platform(services(), files(), assetCache())
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
               static_cast<std::uint32_t>(GuestCapability::Clipboard) |
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
    m_platform.setProject(granted(GuestCapability::ProjectFiles));
    Logger::LogTrace(granted(GuestCapability::ProjectFiles)
                       ? "IllEd has project access: saves, imports and packs "
                         "target /project"
                       : "IllEd runs without a project; scenes save through "
                         "file dialogs");
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
