#pragma once

#include <Illumo/Platform/SaveLoad.h>
#include <functional>
#include <string>

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

  // Defined once per link: the native oracle or the guest service adapter.
  static MeshViewerPlatform& current();
};
