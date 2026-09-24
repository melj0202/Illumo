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
  // Fetched bytes stay until releaseAssets().
  virtual void fetchAssets(const std::vector<std::string>& paths,
                           FetchCallback done)
  {
    (void)paths;
    done({});
  }
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
