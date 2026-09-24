#include "IllEdPlatform.h"

#include "EditorAssets.h"
#include <Illumo/Content/PackageMounts.h>
#include <Illumo/Content/VirtualFileSystem.h>
#include <iterator>

#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Platform/AtomicFile.h>
#include <Illumo/Platform/Clipboard.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

static void
setError(std::string* error, const char* message)
{
  if (error != nullptr) {
    *error = message;
  }
}

bool
IllEdNativeFiles::readText(const std::string& path,
                           std::string* text,
                           std::string* error)
try {
  if (path.empty()) {
    setError(error, "Scene path is empty");
    return false;
  }
  std::ifstream file(
    std::filesystem::path(std::u8string(path.begin(), path.end())),
    std::ios::binary);
  if (!file.is_open()) {
    setError(error, "Failed to open scene file");
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (!file.good() && !file.eof()) {
    setError(error, "Failed while reading scene file");
    return false;
  }
  *text = buffer.str();
  return true;
} catch (const std::system_error&) {
  setError(error, "Invalid or inaccessible UTF-8 file path");
  return false;
}

bool
IllEdNativeFiles::writeText(const std::string& path,
                            const std::string& text,
                            std::string* error)
try {
  if (path.empty()) {
    setError(error, "Scene path is empty");
    return false;
  }
  return AtomicFile::write(
    std::filesystem::path(std::u8string(path.begin(), path.end())),
    [&text](std::ostream& file, std::string*) {
      file << text;
      return file.good();
    },
    error);
} catch (const std::system_error&) {
  setError(error, "Invalid or inaccessible UTF-8 file path");
  return false;
}

// Synchronous native oracle used by the workspace tests: every completion runs
// before its request returns, through the replaceable SaveLoad functions.
// Locations and labels are both the file path.
class NativeIllEdPlatform final : public IllEdPlatform
{
public:
  void chooseOpenLocation(const SaveLoadDialogSpec& specification,
                          LocationCallback done) override
  {
    const std::string path = SaveLoad::GetLoadLocation(specification);
    done({ path, path });
  }
  void chooseSaveLocation(const SaveLoadDialogSpec& specification,
                          LocationCallback done) override
  {
    std::string path = SaveLoad::GetSaveLocation(specification);
    if (!path.empty()) {
      path = IlscCodec::withExtension(path);
    }
    done({ path, path });
  }
  void read(const std::string& location, ReadCallback done) override
  {
    std::string text;
    std::string error;
    bool success = false;
    if (isTreeLocation(location)) {
      std::vector<uint8_t> bytes;
      const std::shared_ptr<VirtualFileSystem> tree =
        IllEdNativeTree::current();
      success = tree && tree->read(treePath(location), bytes, error);
      text.assign(bytes.begin(), bytes.end());
      if (!tree) {
        error = "No virtual file tree is installed";
      }
    } else {
      success = IllEdNativeFiles::readText(location, &text, &error);
    }
    done(success, text, error);
  }
  void write(const std::string& location,
             std::string text,
             WriteCallback done) override
  {
    std::string error;
    bool success = false;
    if (isTreeLocation(location)) {
      const std::shared_ptr<VirtualFileSystem> tree =
        IllEdNativeTree::current();
      success =
        tree && tree->write(treePath(location),
                            std::vector<uint8_t>(text.begin(), text.end()),
                            error);
      if (!tree) {
        error = "No virtual file tree is installed";
      }
    } else {
      success = IllEdNativeFiles::writeText(location, text, &error);
    }
    done(success, error);
  }
  void listDirectory(const std::string& path, ListCallback done) override
  {
    const std::shared_ptr<VirtualFileSystem> tree = IllEdNativeTree::current();
    std::vector<VfsEntry> entries;
    std::string error;
    if (!tree || !tree->list(path, entries, error)) {
      done(false, {});
      return;
    }
    std::vector<FileEntry> listed;
    for (const VfsEntry& entry : entries) {
      listed.push_back(
        { entry.name, entry.kind == VfsKind::Directory, entry.size });
    }
    done(true, std::move(listed));
  }
  bool hasProject() const override
  {
    const std::shared_ptr<VirtualFileSystem> tree = IllEdNativeTree::current();
    return tree && tree->table()->find("/project") != nullptr;
  }
  void importIntoProject(const SaveLoadDialogSpec& specification,
                         const std::string& folder,
                         ImportCallback done) override
  {
    const std::shared_ptr<VirtualFileSystem> tree = IllEdNativeTree::current();
    if (!tree || !hasProject()) {
      done(false, {}, "No project is mounted");
      return;
    }
    const std::string source = SaveLoad::GetLoadLocation(specification);
    if (source.empty()) {
      done(false, {}, {});
      return;
    }
    const std::filesystem::path path(
      std::u8string(source.begin(), source.end()));
    const std::u8string file = path.filename().u8string();
    const std::string name(file.begin(), file.end());
    std::ifstream input(path, std::ios::binary);
    const std::vector<unsigned char> bytes(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());
    std::string error;
    if (!input.good() && !input.eof()) {
      done(false, {}, "Cannot read " + name);
      return;
    }
    if (!EditorAssets::validateImport(name, bytes, &error)) {
      done(false, {}, error);
      return;
    }
    const std::string target =
      "/project/" +
      (folder.empty()
         ? std::string(EditorAssets::importFolder(EditorAssets::kindFor(name)))
         : folder) +
      "/" + name;
    if (!tree->write(
          target, std::vector<uint8_t>(bytes.begin(), bytes.end()), error)) {
      done(false, {}, error);
      return;
    }
    done(true, target, {});
  }
  void packProject(const SaveLoadDialogSpec& specification,
                   WriteCallback done) override
  {
    const std::shared_ptr<VirtualFileSystem> tree = IllEdNativeTree::current();
    if (!tree || !hasProject()) {
      done(false, "No project is mounted");
      return;
    }
    const std::string destination = SaveLoad::GetSaveLocation(specification);
    if (destination.empty()) {
      done(false, {});
      return;
    }
    std::string error;
    const bool packed =
      PackageMounts::packMounted(*tree,
                                 "/project",
                                 std::filesystem::path(std::u8string(
                                   destination.begin(), destination.end())),
                                 4096ull * 1024ull * 1024ull,
                                 error);
    done(packed, error);
  }
  void setClipboardText(const std::string& text) override
  {
    Clipboard::SetText(text);
  }
  void requestClipboardText(
    std::function<void(const std::string& text)> done) override
  {
    done(Clipboard::GetText());
  }
};

static std::shared_ptr<VirtualFileSystem>&
installedTree()
{
  static std::shared_ptr<VirtualFileSystem> tree;
  return tree;
}

void
IllEdNativeTree::install(std::shared_ptr<VirtualFileSystem> tree)
{
  installedTree() = std::move(tree);
}

std::shared_ptr<VirtualFileSystem>
IllEdNativeTree::current()
{
  return installedTree();
}

IllEdPlatform&
IllEdPlatform::current()
{
  static NativeIllEdPlatform platform;
  return platform;
}
