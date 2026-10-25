#pragma once

#include <Illumo/Platform/SaveLoad.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// A scene document's location and what the editor shows for it. Natively
// both are the file path; in the WASM package the location is an opaque file
// grant and the label is only the file's base name.
struct IllEdLocation
{
  std::string location;
  std::string label;
  bool empty() const { return location.empty(); }
};

// Editor I/O seam for dialogs and scene files. Completions may run before the
// request returns (native test oracle) or on a later update (WASM guest
// services); callers must handle both and guard their own lifetime.
class IllEdPlatform
{
public:
  using LocationCallback = std::function<void(const IllEdLocation& chosen)>;
  using ReadCallback = std::function<
    void(bool success, const std::string& text, const std::string& error)>;
  using WriteCallback =
    std::function<void(bool success, const std::string& error)>;

  IllEdPlatform() = default;
  virtual ~IllEdPlatform() = default;
  IllEdPlatform(const IllEdPlatform&) = delete;
  IllEdPlatform& operator=(const IllEdPlatform&) = delete;
  IllEdPlatform(IllEdPlatform&&) = delete;
  IllEdPlatform& operator=(IllEdPlatform&&) = delete;

  // An empty location reports cancellation. Opened documents stay writable,
  // so Save can replace them in place.
  virtual void chooseOpenLocation(const SaveLoadDialogSpec& specification,
                                  LocationCallback done) = 0;
  // The chosen location carries the .ilsc extension.
  virtual void chooseSaveLocation(const SaveLoadDialogSpec& specification,
                                  LocationCallback done) = 0;
  virtual void read(const std::string& location, ReadCallback done) = 0;
  // Atomic replacement: a failed write leaves the previous content intact.
  virtual void write(const std::string& location,
                     std::string text,
                     WriteCallback done) = 0;
  // The document named when the product launched, if any.
  virtual IllEdLocation launchDocument() const { return {}; }
  // System clipboard text. Requests may complete on a later update.
  virtual void setClipboardText(const std::string& text) { (void)text; }
  virtual void requestClipboardText(
    std::function<void(const std::string& text)> done)
  {
    done({});
  }

  // --- The virtual file tree (/app, /engine, /packages/<id>, /project). ---
  // Scene locations in the tree are "vfs:" plus the virtual path; read and
  // write accept them like any other location.
  static constexpr const char* kTreePrefix = "vfs:";
  static bool isTreeLocation(const std::string& location)
  {
    return location.rfind(kTreePrefix, 0) == 0;
  }
  static std::string treePath(const std::string& location)
  {
    return isTreeLocation(location) ? location.substr(4) : std::string();
  }

  struct FileEntry
  {
    std::string name;
    bool directory = false;
    std::uint64_t size = 0;
  };
  using ListCallback =
    std::function<void(bool success, std::vector<FileEntry> entries)>;
  using FetchCallback = std::function<void(std::vector<std::string> missing)>;
  using ImportCallback = std::function<
    void(bool success, const std::string& path, const std::string& error)>;

  // One directory of the tree, sorted by name.
  virtual void listDirectory(const std::string& path, ListCallback done)
  {
    (void)path;
    done(false, {});
  }
  // Whether a writable /project is mounted (--project).
  virtual bool hasProject() const { return false; }
  // Makes virtual paths readable by the document's AssetManager before a
  // scene instantiates them; done receives the paths that could not be read.
  // Fetched bytes stay until the returned ticket (0 on a host that reads
  // files directly) or everything is released.
  virtual std::uint64_t fetchAssets(const std::vector<std::string>& paths,
                                    FetchCallback done)
  {
    (void)paths;
    done({});
    return 0;
  }
  virtual void releaseAssets(std::uint64_t ticket) { (void)ticket; }
  virtual void releaseAssets() {}
  // Picks a file and copies it into /project/<folder>/<its name>; done
  // receives the new virtual path. Images over the texture cap are refused.
  virtual void importIntoProject(const SaveLoadDialogSpec& specification,
                                 const std::string& folder,
                                 ImportCallback done)
  {
    (void)specification;
    (void)folder;
    done(false, {}, "No project is mounted");
  }
  // Picks a location and packs /project into an .ilpk there.
  virtual void packProject(const SaveLoadDialogSpec& specification,
                           WriteCallback done)
  {
    (void)specification;
    done(false, "No project is mounted");
  }

  // --- Play: another installed application (the Launch capability). ---
  using LaunchCallback = std::function<void(bool started)>;
  // Whether this host can launch applications at all.
  virtual bool canLaunch() const { return false; }
  // Starts `application` with `document` (scene text) as its launch document
  // named `name`, replacing any application launched before.
  virtual void launchApp(const std::string& application,
                         const std::string& name,
                         const std::string& document,
                         LaunchCallback done)
  {
    (void)application;
    (void)name;
    (void)document;
    done(false);
  }
  virtual void stopApp() {}
  // Whether the launched application still runs; refreshed while it does.
  virtual bool appRunning() const { return false; }

  // --- Projects: the host's recent project folders (the Project
  // capability). An entry's id is valid until the next listing; its label
  // is the folder's name. Host paths never reach the editor. ---
  struct ProjectEntry
  {
    std::uint32_t id = 0;
    std::string label;
  };
  using ProjectListCallback =
    std::function<void(bool ok, std::vector<ProjectEntry> entries)>;
  using ProjectPickCallback =
    std::function<void(bool ok, bool cancelled, ProjectEntry entry)>;
  using ProjectDoneCallback = std::function<void(bool ok)>;
  // Whether this host keeps projects for the editor.
  virtual bool canSwitchProject() const { return false; }
  virtual void listProjects(ProjectListCallback done) { done(false, {}); }
  // Folder dialogs: an existing project, or a new one the host scaffolds.
  virtual void chooseProject(ProjectPickCallback done)
  {
    done(false, false, {});
  }
  virtual void createProject(ProjectPickCallback done)
  {
    done(false, false, {});
  }
  // Prepares the host's restart on a listed project; the editor then asks
  // to restart.
  virtual void openProject(std::uint32_t id, ProjectDoneCallback done)
  {
    (void)id;
    done(false);
  }
  virtual void clearProjects(ProjectDoneCallback done) { done(false); }

  // Defined once per link: the native oracle or the guest service adapter.
  static IllEdPlatform& current();
};

#if !defined(ILLUMO_SERIAL_GUEST)
class VirtualFileSystem;

// The native oracle's virtual file tree, installed by tests or tools. Without
// one, tree requests fail and hasProject() is false.
class IllEdNativeTree
{
public:
  static void install(std::shared_ptr<VirtualFileSystem> tree);
  static std::shared_ptr<VirtualFileSystem> current();
};

// The native oracle's launcher, for tests: once available, launches are
// recorded (and report started) instead of starting anything.
class IllEdNativeLauncher
{
public:
  struct Launch
  {
    std::string application;
    std::string name;
    std::string document;
  };
  static void setAvailable(bool available);
  static bool available();
  static std::vector<Launch>& launches();
  // Whether the recorded launch still "runs"; tests end it by hand.
  static bool& running();
};

// The native oracle's projects, for tests: once available, the entries are
// listed, a dialog returns nextPick (empty cancels) as a new first entry,
// and opens are recorded.
class IllEdNativeProjects
{
public:
  static void setAvailable(bool available);
  static bool available();
  static std::vector<IllEdPlatform::ProjectEntry>& entries();
  // What the next folder dialog "chooses"; empty cancels it.
  static std::string& nextPick();
  // The id the last open asked for (0 when none).
  static std::uint32_t& opened();
};

// Native scene file access by UTF-8 path, used by the native platform and the
// test oracles. Writes are atomic through AtomicFile.
class IllEdNativeFiles
{
public:
  static bool readText(const std::string& path,
                       std::string* text,
                       std::string* error);
  static bool writeText(const std::string& path,
                        const std::string& text,
                        std::string* error);
};
#endif
