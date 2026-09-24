#pragma once

#include <Illumo/Platform/SaveLoad.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// A mesh file's location and the name the viewer shows for it. Natively the
// location is a path; in the WASM package it is an opaque file grant and the
// label is only the file's base name.
struct MeshViewerLocation
{
  std::string location;
  std::string label;
  bool empty() const { return location.empty(); }
};

// Viewer I/O seam. Completions may run before the request returns (native
// test oracle) or on a later update (WASM guest services); callers must
// handle both and guard their own lifetime.
class MeshViewerPlatform
{
public:
  using LocationCallback =
    std::function<void(const MeshViewerLocation& chosen)>;
  using ReadCallback = std::function<
    void(bool success, const std::string& bytes, const std::string& error)>;
  using FetchCallback = std::function<void(std::vector<std::string> missing)>;

  // Locations in the virtual file tree are "vfs:" plus the virtual path
  // (viewer_open); read() accepts them like any other location.
  static constexpr const char* kTreePrefix = "vfs:";
  static bool isTreeLocation(const std::string& location)
  {
    return location.rfind(kTreePrefix, 0) == 0;
  }
  static std::string treePath(const std::string& location)
  {
    return isTreeLocation(location) ? location.substr(4) : std::string();
  }

  MeshViewerPlatform() = default;
  virtual ~MeshViewerPlatform() = default;
  MeshViewerPlatform(const MeshViewerPlatform&) = delete;
  MeshViewerPlatform& operator=(const MeshViewerPlatform&) = delete;
  MeshViewerPlatform(MeshViewerPlatform&&) = delete;
  MeshViewerPlatform& operator=(MeshViewerPlatform&&) = delete;

  // An empty location reports cancellation.
  virtual void chooseMesh(const SaveLoadDialogSpec& specification,
                          LocationCallback done) = 0;
  virtual void read(const std::string& location, ReadCallback done) = 0;
  // The mesh named when the product launched, if any.
  virtual MeshViewerLocation launchMesh() const { return {}; }
  // Makes a scene's virtual paths readable by AssetManager before the scene
  // instantiates; done receives the paths that could not be read. Fetched
  // bytes stay until releaseAssets(). Where AssetManager already reads the
  // tree directly, there is nothing to fetch.
  virtual void fetchAssets(const std::vector<std::string>& paths,
                           FetchCallback done)
  {
    (void)paths;
    done({});
  }
  virtual void releaseAssets() {}

  // Defined once per link: the native oracle or the guest service adapter.
  static MeshViewerPlatform& current();
};

class VirtualFileSystem;

// The native oracle's virtual file tree, installed by tests. Without one,
// "vfs:" reads fail.
class MeshViewerNativeTree
{
public:
  static void install(std::shared_ptr<VirtualFileSystem> tree);
  static std::shared_ptr<VirtualFileSystem> current();
};
