#pragma once

#include <Illumo/Rendering/MeshData.h>
#include <memory>
#include <string>

struct MeshLoadOptions
{
  bool triangulate = true;
  bool generateNormalsIfMissing = true;
  bool flipTexCoordsV = true;
  bool centerAndNormalize = false;
  float targetRadius = 1.0f;
  std::string materialSearchPath;
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

  static void setCustomBackend(std::shared_ptr<IMeshLoaderBackend> backend);
  static void resetBackend();

private:
  static std::shared_ptr<IMeshLoaderBackend> getActiveBackend();
};
