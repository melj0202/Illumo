#pragma once

#include <Illumo/Rendering/MeshData.h>
#include <memory>
#include <string>
#include <vector>

struct MeshLoadOptions
{
  bool triangulate = true;
  bool generateNormalsIfMissing = true;
  bool flipTexCoordsV = true;
  bool centerAndNormalize = false;
  float targetRadius = 1.0f;
  std::string materialSearchPath;
  // Material library (.mtl) text for loadFromMemory. When non-empty it is
  // parsed instead of searching materialSearchPath, so byte sources (packages)
  // resolve materials without a file system.
  std::string materialText;
};

struct MeshLoadResult
{
  bool success = false;
  std::string error;
  std::string warning;
  MeshData mesh;
};

class IMeshLoaderBackend
{
public:
  virtual ~IMeshLoaderBackend() = default;

  virtual bool loadFromFile(const std::string& filePath,
                            const MeshLoadOptions& options,
                            MeshLoadResult* outResult) = 0;

  virtual bool loadFromMemory(const std::string& fileContent,
                              const MeshLoadOptions& options,
                              const std::string& baseDir,
                              MeshLoadResult* outResult) = 0;
};

// Swappable mesh loading facade. Uses tinyobjloader by default, but permits
// registering custom loaders or alternate implementations without changing
// caller code.
class MeshLoader
{
public:
  static MeshLoadResult loadFromFile(
    const std::string& filePath,
    const MeshLoadOptions& options = MeshLoadOptions{});

  static MeshLoadResult loadFromMemory(
    const std::string& fileContent,
    const MeshLoadOptions& options = MeshLoadOptions{},
    const std::string& baseDir = "");

  // The mtllib names an OBJ declares, in order, without duplicates, with '\'
  // turned into '/'. Names are relative to the OBJ and unvalidated.
  static std::vector<std::string> materialLibraryNames(
    const std::string& objText);

  static void setCustomBackend(std::shared_ptr<IMeshLoaderBackend> backend);
  static void resetBackend();

private:
  static std::shared_ptr<IMeshLoaderBackend> getActiveBackend();
};
