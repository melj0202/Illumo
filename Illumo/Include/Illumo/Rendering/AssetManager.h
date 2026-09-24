#pragma once

#include <Illumo/Rendering/AssetSource.h>
#include <Illumo/Rendering/IShaderProgram.h>
#include <Illumo/Rendering/ITexture.h>
#include <Illumo/Rendering/MeshLoader.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>
#if !defined(ILLUMO_SERIAL_GUEST)
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

class Renderer;

#if defined(ILLUMO_SERIAL_GUEST)
// Serial WASM guests have no threads; every load completes on the caller.
struct AssetQueueMutex
{
  void lock() {}
  void unlock() {}
};
#else
using AssetQueueMutex = std::mutex;
#endif

enum class AssetLoadMode
{
  Async,
  Synchronous
};

enum class AssetState
{
  Pending,
  Ready,
  Failed
};

struct AssetStatus
{
  AssetState state = AssetState::Pending;
  uint64_t revision = 0;
  unsigned int referenceCount = 0;
  bool reloadPending = false;
  std::string path;
  std::string lastError;
};

struct MeshAssetInfo
{
  MeshHandle handle{};
  unsigned int vertexCount = 0;
  unsigned int indexCount = 0;
  glm::vec3 minBounds = glm::vec3(0.0f);
  glm::vec3 maxBounds = glm::vec3(0.0f);

  bool isValid() const { return handle.isValid() && indexCount > 0; }
};

// Cached texture/shader/mesh assets. CPU texture/shader file work may run on
// one worker; pump() performs their backend mutation on the render thread.
// Mesh acquisition is synchronous and main-thread affine. Bytes come from an
// IAssetSource (the filesystem unless another source is supplied); serial
// guests never start a worker and complete queued loads in pump().
class AssetManager
{
public:
  explicit AssetManager(Renderer* renderer,
                        bool startWorker = true,
                        IAssetSource* source = nullptr);
  ~AssetManager();
  AssetManager(const AssetManager&) = delete;
  AssetManager& operator=(const AssetManager&) = delete;
  AssetManager(AssetManager&&) = delete;
  AssetManager& operator=(AssetManager&&) = delete;

  TextureHandle acquireTexture(const std::string& path,
                               const TextureOptions& options = TextureOptions{},
                               AssetLoadMode mode = AssetLoadMode::Async);
  // Initial cubemap acquisition is synchronous; mode is retained for source
  // compatibility. Subsequent reloads use the shared CPU queue and pump.
  TextureHandle acquireCubemap(const std::array<std::string, 6>& facePaths,
                               AssetLoadMode mode = AssetLoadMode::Synchronous);
  TextureHandle acquireCubemapFromCross(
    const std::string& crossPath,
    AssetLoadMode mode = AssetLoadMode::Synchronous);
  ShaderHandle acquireShader(const ShaderPaths& paths,
                             AssetLoadMode mode = AssetLoadMode::Async);
  MeshHandle acquireMesh(const MeshData& mesh);
  MeshHandle acquireMesh(const std::string& path,
                         const MeshLoadOptions& options = MeshLoadOptions{});

  bool retainTexture(TextureHandle handle);
  bool retainShader(ShaderHandle handle);
  bool retainMesh(MeshHandle handle);
  bool releaseTexture(TextureHandle handle);
  bool releaseShader(ShaderHandle handle);
  bool releaseMesh(MeshHandle handle);

  AssetStatus getState(TextureHandle handle) const;
  AssetStatus getState(ShaderHandle handle) const;
  AssetStatus getState(MeshHandle handle) const;
  TextureInfo getTextureInfo(TextureHandle handle) const;
  MeshAssetInfo getMeshInfo(MeshHandle handle) const;
  // The byte source every load reads through (the file system natively, the
  // guest's asset cache in a WASM package).
  IAssetSource* assetSource() const { return source; }

  bool reload(TextureHandle handle);
  bool reload(ShaderHandle handle);
  size_t reload(const std::string& path);
  size_t reloadAll();

  void pump();
  void setHotReloadEnabled(bool enabled) { hotReloadEnabled = enabled; }
  bool isHotReloadEnabled() const { return hotReloadEnabled; }
  std::vector<std::string> describeAssets() const;

  // Deterministic hook for managers constructed with startWorker=false: process
  // queued work synchronously and then pump.
  void completePendingForTests();

private:
  enum class AssetKind
  {
    Texture,
    Shader
  };

  enum class TextureSourceKind
  {
    Image2D,
    CubemapFaces,
    CubemapCross
  };

  struct TextureEntry
  {
    TextureSourceKind sourceKind = TextureSourceKind::Image2D;
    std::array<std::string, 6> sourcePaths;
    std::array<std::int64_t, 6> sourceWriteTimes{};
    TextureHandle handle{};
    std::string path;
    std::string cacheKey;
    TextureOptions options;
    AssetState state = AssetState::Pending;
    TextureInfo info;
    uint64_t revision = 0;
    uint64_t requestSerial = 0;
    unsigned int referenceCount = 1;
    bool reloadPending = false;
    std::string lastError;
    std::int64_t lastWriteTime = 0;
  };

  struct ShaderEntry
  {
    ShaderHandle handle{};
    ShaderPaths paths;
    std::string cacheKey;
    AssetState state = AssetState::Pending;
    uint64_t revision = 0;
    uint64_t requestSerial = 0;
    unsigned int referenceCount = 1;
    bool reloadPending = false;
    std::string lastError;
    std::int64_t vertexWriteTime = 0;
    std::int64_t fragmentWriteTime = 0;
    std::vector<std::string> dependencies;
    std::unordered_map<std::string, std::int64_t> dependencyWriteTimes;
  };

  struct MeshEntry
  {
    MeshHandle handle{};
    MeshAssetInfo info;
    std::string path;
    std::string cacheKey;
    unsigned int referenceCount = 1;
  };

  struct LoadJob
  {
    TextureSourceKind sourceKind = TextureSourceKind::Image2D;
    std::array<std::string, 6> sourcePaths;
    AssetKind kind = AssetKind::Texture;
    uint32_t slot = 0;
    uint32_t generation = 0;
    uint64_t requestSerial = 0;
    std::string pathA;
    std::string pathB;
    std::vector<std::string> defines;
    TextureOptions textureOptions;
  };

  struct LoadResult
  {
    TextureSourceKind sourceKind = TextureSourceKind::Image2D;
    std::array<std::vector<unsigned char>, 6> faces;
    std::array<std::int64_t, 6> sourceWriteTimes{};
    AssetKind kind = AssetKind::Texture;
    uint32_t slot = 0;
    uint32_t generation = 0;
    uint64_t requestSerial = 0;
    bool success = false;
    int width = 0;
    int height = 0;
    int channels = 0;
    TextureOptions textureOptions;
    std::vector<unsigned char> pixels;
    ShaderSources shaderSources;
    std::vector<std::string> dependencies;
    std::string error;
  };

  Renderer* renderer;
  std::unordered_map<uint32_t, TextureEntry> textures;
  std::unordered_map<uint32_t, ShaderEntry> shaders;
  std::unordered_map<uint32_t, MeshEntry> meshes;
  std::unordered_map<std::string, uint32_t> textureCache;
  std::unordered_map<std::string, uint32_t> shaderCache;
  std::unordered_map<std::string, uint32_t> meshCache;

  // Borrowed; outlives the manager. Null only in guests without a source,
  // where every load fails visibly.
  IAssetSource* source;
  mutable AssetQueueMutex queueMutex;
#if !defined(ILLUMO_SERIAL_GUEST)
  std::condition_variable queueCondition;
  std::thread worker;
#endif
  std::deque<LoadJob> jobs;
  std::deque<LoadResult> results;
  bool workerEnabled = true;
  bool stopping = false;
  bool hotReloadEnabled = false;
  std::chrono::steady_clock::time_point nextHotReloadPoll;

  std::string canonicalPath(const std::string& path) const;
  static std::string textureKey(const std::string& canonical,
                                const TextureOptions& options);
  std::string shaderKey(const ShaderPaths& paths) const;
  std::string meshKey(const std::string& canonical,
                      const MeshLoadOptions& options) const;
  std::int64_t writeTime(const std::string& path) const;
  static LoadResult executeJob(const LoadJob& job, const IAssetSource* source);
  static void decodeCubemap(const LoadJob& job,
                            const IAssetSource& source,
                            LoadResult& result);
  TextureHandle acquireCubemapSources(TextureSourceKind kind,
                                      const std::array<std::string, 6>& paths,
                                      const std::string& key);
  MeshHandle enrollMesh(const MeshData& mesh,
                        const std::string& path,
                        const std::string& cacheKey);

  void workerMain();
  void enqueue(const LoadJob& job);
  void processResult(LoadResult& result);
  void queueTexture(TextureEntry& entry);
  void queueShader(ShaderEntry& entry);
  void pollHotReload();
};
