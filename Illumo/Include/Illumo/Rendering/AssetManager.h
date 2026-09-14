#pragma once

#include <Illumo/Rendering/IShaderProgram.h>
#include <Illumo/Rendering/ITexture.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class Renderer;

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

// Cached texture/shader assets. CPU file/decode work may run on one worker;
// pump() performs every backend mutation on the render thread.
class AssetManager
{
public:
  explicit AssetManager(Renderer* renderer, bool startWorker = true);
  ~AssetManager();

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

  bool retainTexture(TextureHandle handle);
  bool retainShader(ShaderHandle handle);
  bool releaseTexture(TextureHandle handle);
  bool releaseShader(ShaderHandle handle);

  AssetStatus getState(TextureHandle handle) const;
  AssetStatus getState(ShaderHandle handle) const;
  TextureInfo getTextureInfo(TextureHandle handle) const;

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
    std::array<std::filesystem::file_time_type, 6> sourceWriteTimes{};
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
    std::filesystem::file_time_type lastWriteTime{};
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
    std::filesystem::file_time_type vertexWriteTime{};
    std::filesystem::file_time_type fragmentWriteTime{};
    std::vector<std::string> dependencies;
    std::unordered_map<std::string, std::filesystem::file_time_type>
      dependencyWriteTimes;
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
    std::array<std::filesystem::file_time_type, 6> sourceWriteTimes{};
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
  std::unordered_map<std::string, uint32_t> textureCache;
  std::unordered_map<std::string, uint32_t> shaderCache;

  mutable std::mutex queueMutex;
  std::condition_variable queueCondition;
  std::deque<LoadJob> jobs;
  std::deque<LoadResult> results;
  std::thread worker;
  bool workerEnabled = true;
  bool stopping = false;
  bool hotReloadEnabled = false;
  std::chrono::steady_clock::time_point nextHotReloadPoll;

  static std::string canonicalPath(const std::string& path);
  static std::string textureKey(const std::string& canonical,
                                const TextureOptions& options);
  static std::string shaderKey(const ShaderPaths& paths);
  static std::filesystem::file_time_type writeTime(const std::string& path);
  static LoadResult executeJob(const LoadJob& job);
  static void decodeCubemap(const LoadJob& job, LoadResult& result);
  TextureHandle acquireCubemapSources(TextureSourceKind kind,
                                      const std::array<std::string, 6>& paths,
                                      const std::string& key);

  void workerMain();
  void enqueue(const LoadJob& job);
  void processResult(LoadResult& result);
  void queueTexture(TextureEntry& entry);
  void queueShader(ShaderEntry& entry);
  void pollHotReload();
};
