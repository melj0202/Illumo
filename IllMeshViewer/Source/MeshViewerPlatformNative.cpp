#include "MeshViewerPlatform.h"
#include <Illumo/Content/VirtualFileSystem.h>
#include <Illumo/Platform/PathText.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>

static std::mutex nativeTreeMutex;
static std::shared_ptr<VirtualFileSystem> nativeTree;

void
MeshViewerNativeTree::install(std::shared_ptr<VirtualFileSystem> tree)
{
  const std::lock_guard<std::mutex> lock(nativeTreeMutex);
  nativeTree = std::move(tree);
}

std::shared_ptr<VirtualFileSystem>
MeshViewerNativeTree::current()
{
  const std::lock_guard<std::mutex> lock(nativeTreeMutex);
  return nativeTree;
}

// Synchronous native oracle used by the workspace tests: every completion
// runs before its request returns, through the replaceable SaveLoad
// functions. The label is the file's base name, as the viewer shows it.
class NativeMeshViewerPlatform final : public MeshViewerPlatform
{
public:
  void chooseMesh(const SaveLoadDialogSpec& specification,
                  LocationCallback done) override
  {
    const std::string path = SaveLoad::GetLoadLocation(specification);
    done({ path, label(path) });
  }
  void read(const std::string& location, ReadCallback done) override
  {
    if (isTreeLocation(location)) {
      const std::shared_ptr<VirtualFileSystem> tree =
        MeshViewerNativeTree::current();
      std::vector<uint8_t> bytes;
      std::string error = "No file tree is mounted";
      if (tree == nullptr || !tree->read(treePath(location), bytes, error)) {
        done(false, {}, error);
        return;
      }
      done(true, std::string(bytes.begin(), bytes.end()), {});
      return;
    }
    std::filesystem::path path;
    if (!pathFromUtf8(location, &path)) {
      done(false, {}, "Invalid or inaccessible UTF-8 file path");
      return;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
      done(false, {}, "Failed to open mesh file: " + location);
      return;
    }
    const std::string bytes{ std::istreambuf_iterator<char>(file),
                             std::istreambuf_iterator<char>() };
    if (file.bad()) {
      done(false, {}, "Failed while reading mesh file: " + location);
      return;
    }
    done(true, bytes, {});
  }

private:
  static std::string label(const std::string& path)
  {
    std::filesystem::path parsed;
    return pathFromUtf8(path, &parsed) ? pathToUtf8(parsed.filename()) : path;
  }
};

MeshViewerPlatform&
MeshViewerPlatform::current()
{
  static NativeMeshViewerPlatform platform;
  return platform;
}
