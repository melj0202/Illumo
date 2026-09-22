#define STB_IMAGE_IMPLEMENTATION
#if defined(ILLUMO_SERIAL_GUEST)
// Guests decode from memory only; stdio would import WASI file functions.
#define STBI_NO_STDIO
#endif
#include "thirdparty/stb/stb_image.h"
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Renderer.h>
#if !defined(ILLUMO_SERIAL_GUEST)
#include <Illumo/Rendering/ShaderPreprocessor.h>
#endif
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>

// Scoped queue lock that also works with the serial guest's no-op mutex.
class AssetQueueLock
{
public:
  explicit AssetQueueLock(AssetQueueMutex& mutex)
    : m_mutex(mutex)
  {
    m_mutex.lock();
  }
  ~AssetQueueLock() { m_mutex.unlock(); }
  AssetQueueLock(const AssetQueueLock&) = delete;
  AssetQueueLock& operator=(const AssetQueueLock&) = delete;
  AssetQueueLock(AssetQueueLock&&) = delete;
  AssetQueueLock& operator=(AssetQueueLock&&) = delete;

private:
  AssetQueueMutex& m_mutex;
};

// Decodes an image held in memory to RGBA8. Null on failure.
static unsigned char*
decodeImage(const IAssetSource* source,
            const std::string& path,
            int& width,
            int& height)
{
  std::vector<unsigned char> bytes;
  if (source == nullptr || !source->read(path, bytes) || bytes.empty() ||
      bytes.size() >
        static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return nullptr;
  }
  int sourceChannels = 0;
  return stbi_load_from_memory(bytes.data(),
                               static_cast<int>(bytes.size()),
                               &width,
                               &height,
                               &sourceChannels,
                               STBI_rgb_alpha);
}

struct ManagedMeshVertex
{
  float x;
  float y;
  float z;
  float nx;
  float ny;
  float nz;
  unsigned char r;
  unsigned char g;
  unsigned char b;
  unsigned char a;
  float u;
  float v;
};

static_assert(sizeof(ManagedMeshVertex) == 36);

static unsigned char
meshColorChannel(float value)
{
  return static_cast<unsigned char>(std::clamp(value * 255.0f, 0.0f, 255.0f));
}

static const char* kFallbackVertexShader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aUv;
out vec4 ourColor;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    ourColor = aColor;
}
)";

static const char* kFallbackFragmentShader = R"(
#version 330 core
in vec4 ourColor;
out vec4 FragColor;
void main() { FragColor = vec4(1.0, 0.0, 1.0, 1.0) * ourColor; }
)";

static const char*
assetStateName(AssetState state)
{
  switch (state) {
    case AssetState::Pending:
      return "pending";
    case AssetState::Ready:
      return "ready";
    case AssetState::Failed:
      return "failed";
  }
  return "unknown";
}

AssetManager::AssetManager(Renderer* rendererValue,
                           bool startWorker,
                           IAssetSource* sourceValue)
  : renderer(rendererValue)
  , source(sourceValue != nullptr ? sourceValue : DefaultAssetSource())
  , workerEnabled(startWorker)
  , nextHotReloadPoll(std::chrono::steady_clock::now())
{
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
  hotReloadEnabled = true;
#endif
#if defined(ILLUMO_SERIAL_GUEST)
  workerEnabled = false;
#else
  if (workerEnabled) {
    worker = std::thread(&AssetManager::workerMain, this);
  }
#endif
}

AssetManager::~AssetManager()
{
  {
    AssetQueueLock lock(queueMutex);
    stopping = true;
    jobs.clear();
  }
#if !defined(ILLUMO_SERIAL_GUEST)
  queueCondition.notify_all();
  if (worker.joinable()) {
    worker.join();
  }
#endif

  if (renderer != nullptr) {
    for (std::unordered_map<uint32_t, MeshEntry>::iterator it = meshes.begin();
         it != meshes.end();
         ++it) {
      renderer->destroyMesh(it->second.handle);
    }
    for (std::unordered_map<uint32_t, TextureEntry>::iterator it =
           textures.begin();
         it != textures.end();
         ++it) {
      renderer->destroyTexture(it->second.handle);
    }
    for (std::unordered_map<uint32_t, ShaderEntry>::iterator it =
           shaders.begin();
         it != shaders.end();
         ++it) {
      renderer->destroyShader(it->second.handle);
    }
  }
}

std::string
AssetManager::canonicalPath(const std::string& path) const
{
  return source != nullptr ? source->canonical(path) : path;
}

std::string
AssetManager::textureKey(const std::string& canonical,
                         const TextureOptions& options)
{
  return canonical + "|" + std::to_string(static_cast<int>(options.filter)) +
         "|" + std::to_string(static_cast<int>(options.wrapX)) + "|" +
         std::to_string(static_cast<int>(options.wrapY)) + "|" +
         (options.generateMipmaps ? "1" : "0");
}

std::string
AssetManager::shaderKey(const ShaderPaths& paths) const
{
  return canonicalPath(paths.vertexPath) + "|" +
         canonicalPath(paths.fragmentPath);
}

std::string
AssetManager::meshKey(const std::string& canonical,
                      const MeshLoadOptions& options) const
{
  std::ostringstream key;
  key.precision(std::numeric_limits<float>::max_digits10);
  key << canonical << '|' << (options.triangulate ? '1' : '0') << '|'
      << (options.generateNormalsIfMissing ? '1' : '0') << '|'
      << (options.flipTexCoordsV ? '1' : '0') << '|'
      << (options.centerAndNormalize ? '1' : '0') << '|' << options.targetRadius
      << '|';
  if (!options.materialSearchPath.empty()) {
    key << canonicalPath(options.materialSearchPath);
  }
  return key.str();
}

std::int64_t
AssetManager::writeTime(const std::string& path) const
{
  return source != nullptr ? source->stamp(path) : 0;
}

TextureHandle
AssetManager::acquireTexture(const std::string& path,
                             const TextureOptions& options,
                             AssetLoadMode mode)
{
  if (renderer == nullptr) {
    return TextureHandle{};
  }
  const std::string canonical = canonicalPath(path);
  const std::string key = textureKey(canonical, options);
  std::unordered_map<std::string, uint32_t>::iterator cached =
    textureCache.find(key);
  if (cached != textureCache.end()) {
    TextureEntry& entry = textures[cached->second];
    entry.referenceCount += 1;
    return entry.handle;
  }

  const unsigned char fallback[16] = {
    255, 0, 255, 255, 32, 32, 32, 255, 32, 32, 32, 255, 255, 0, 255, 255,
  };
  TextureHandle handle = renderer->enrollTexture(fallback, 2, 2, 4, options);
  if (!handle.isValid()) {
    return TextureHandle{};
  }

  TextureEntry entry;
  entry.handle = handle;
  entry.path = canonical;
  entry.cacheKey = key;
  entry.options = options;
  entry.info = { 2, 2, 4 };
  entry.lastWriteTime = writeTime(canonical);
  textures[handle.slot] = entry;
  textureCache[key] = handle.slot;

  if (mode == AssetLoadMode::Synchronous) {
    TextureEntry& stored = textures[handle.slot];
    stored.requestSerial += 1;
    LoadJob job;
    job.kind = AssetKind::Texture;
    job.slot = handle.slot;
    job.generation = handle.generation;
    job.requestSerial = stored.requestSerial;
    job.pathA = stored.path;
    job.textureOptions = stored.options;
    LoadResult result = executeJob(job, source);
    processResult(result);
  } else {
    queueTexture(textures[handle.slot]);
  }
  return handle;
}

TextureHandle
AssetManager::acquireCubemap(const std::array<std::string, 6>& facePaths,
                             AssetLoadMode mode)
{
  (void)mode; // Preserve synchronous initial cubemap acquisition.
  std::array<std::string, 6> paths;
  std::string key = "cubemap_6faces";
  for (size_t i = 0; i < paths.size(); ++i) {
    paths[i] = canonicalPath(facePaths[i]);
    key += "|" + paths[i];
  }
  return acquireCubemapSources(TextureSourceKind::CubemapFaces, paths, key);
}

TextureHandle
AssetManager::acquireCubemapFromCross(const std::string& crossPath,
                                      AssetLoadMode mode)
{
  (void)mode; // Preserve synchronous initial cubemap acquisition.
  std::array<std::string, 6> paths;
  paths[0] = canonicalPath(crossPath);
  return acquireCubemapSources(
    TextureSourceKind::CubemapCross, paths, "cubemap_cross|" + paths[0]);
}

TextureHandle
AssetManager::acquireCubemapSources(TextureSourceKind kind,
                                    const std::array<std::string, 6>& paths,
                                    const std::string& key)
{
  if (renderer == nullptr) {
    return {};
  }
  std::unordered_map<std::string, uint32_t>::iterator cached =
    textureCache.find(key);
  if (cached != textureCache.end()) {
    TextureEntry& entry = textures[cached->second];
    ++entry.referenceCount;
    return entry.handle;
  }
  LoadJob job;
  job.sourceKind = kind;
  job.sourcePaths = paths;
  LoadResult result = executeJob(job, source);
  if (!result.success) {
    Logger::LogError(result.error.c_str());
    return {};
  }
  std::array<const unsigned char*, 6> faces;
  for (size_t i = 0; i < faces.size(); ++i) {
    faces[i] = result.faces[i].data();
  }
  const TextureHandle handle = renderer->enrollCubemap(
    faces, result.width, result.height, result.channels);
  if (!handle.isValid()) {
    return {};
  }
  TextureEntry entry;
  entry.handle = handle;
  entry.path = paths[0];
  entry.sourceKind = kind;
  entry.sourcePaths = paths;
  entry.sourceWriteTimes = result.sourceWriteTimes;
  entry.cacheKey = key;
  entry.info = { result.width, result.height, result.channels };
  entry.state = AssetState::Ready;
  entry.revision = 1;
  textures[handle.slot] = entry;
  textureCache[key] = handle.slot;
  return handle;
}

void
AssetManager::decodeCubemap(const LoadJob& job,
                            const IAssetSource& source,
                            LoadResult& result)
{
  const size_t count =
    job.sourceKind == TextureSourceKind::CubemapFaces ? 6 : 1;
  for (size_t i = 0; i < count; ++i) {
    result.sourceWriteTimes[i] = source.stamp(job.sourcePaths[i]);
  }
  result.channels = STBI_rgb_alpha;
  if (job.sourceKind == TextureSourceKind::CubemapFaces) {
    for (size_t i = 0; i < count; ++i) {
      int width = 0;
      int height = 0;
      std::unique_ptr<unsigned char, decltype(&stbi_image_free)> decoded(
        decodeImage(&source, job.sourcePaths[i], width, height),
        stbi_image_free);
      if (!decoded || width <= 0 || width != height ||
          (i != 0 && (width != result.width || height != result.height))) {
        result.error = "Unable to decode matching square cubemap face: " +
                       job.sourcePaths[i];
        return;
      }
      result.width = width;
      result.height = height;
      const size_t bytes =
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
      result.faces[i].assign(decoded.get(), decoded.get() + bytes);
    }
    result.success = true;
    return;
  }
  int width = 0;
  int height = 0;
  std::unique_ptr<unsigned char, decltype(&stbi_image_free)> decoded(
    decodeImage(&source, job.sourcePaths[0], width, height), stbi_image_free);
  if (!decoded || width <= 0 || height <= 0) {
    result.error = "Unable to decode cubemap cross: " + job.sourcePaths[0];
    return;
  }
  const unsigned char* data = decoded.get();
  const int channels = STBI_rgb_alpha;
  int faceSize = 0;
  int faceCols[6];
  int faceRows[6];
  bool flipH[6] = { false, false, false, false, false, false };
  bool flipV[6] = { false, false, false, false, false, false };

  if (static_cast<int64_t>(width) * 3 == static_cast<int64_t>(height) * 4) {
    // 4:3 Horizontal Cross (4 columns, 3 rows)
    // OpenGL cubemap face sampling convention views the cube from the inside
    // out: side faces (+X, -X, +Z, -Z) require horizontal flip so left/right
    // panning and seam continuity match, while top (+Y) and bottom (-Y)
    // require vertical flip to align with the front face top/bottom edges.
    faceSize = width / 4;
    faceCols[0] = 2;
    faceRows[0] = 1; // +X (Right)
    flipH[0] = true;
    faceCols[1] = 0;
    faceRows[1] = 1; // -X (Left)
    flipH[1] = true;
    faceCols[2] = 1;
    faceRows[2] = 0; // +Y (Top)
    flipV[2] = true;
    faceCols[3] = 1;
    faceRows[3] = 2; // -Y (Bottom)
    flipV[3] = true;
    faceCols[4] = 3;
    faceRows[4] = 1; // +Z (Back)
    flipH[4] = true;
    faceCols[5] = 1;
    faceRows[5] = 1; // -Z (Front)
    flipH[5] = true;
  } else if (static_cast<int64_t>(width) * 4 ==
             static_cast<int64_t>(height) * 3) {
    // 3:4 Vertical Cross (3 columns, 4 rows)
    faceSize = width / 3;
    faceCols[0] = 2;
    faceRows[0] = 1; // +X (Right)
    flipH[0] = true;
    faceCols[1] = 0;
    faceRows[1] = 1; // -X (Left)
    flipH[1] = true;
    faceCols[2] = 1;
    faceRows[2] = 0; // +Y (Top)
    flipV[2] = true;
    faceCols[3] = 1;
    faceRows[3] = 2; // -Y (Bottom)
    flipV[3] = true;
    faceCols[4] = 1;
    faceRows[4] = 3; // +Z (Back)
    flipH[4] = true;
    faceCols[5] = 1;
    faceRows[5] = 1; // -Z (Front)
    flipH[5] = true;
  } else if (static_cast<int64_t>(width) == 6 * static_cast<int64_t>(height)) {
    // 6:1 Horizontal Strip: +X, -X, +Y, -Y, +Z, -Z
    faceSize = height;
    for (int i = 0; i < 6; ++i) {
      faceCols[i] = i;
      faceRows[i] = 0;
    }
  } else {
    faceSize = std::min(width, height);
    for (int i = 0; i < 6; ++i) {
      faceCols[i] = 0;
      faceRows[i] = 0;
    }
  }

  std::array<std::vector<unsigned char>, 6>& faceBuffers = result.faces;
  for (size_t f = 0; f < 6; ++f) {
    faceBuffers[f].resize(static_cast<size_t>(faceSize) * faceSize * channels);
    const int col = faceCols[f];
    const int row = faceRows[f];
    const bool fH = flipH[f];
    const bool fV = flipV[f];
    const size_t ch = static_cast<size_t>(channels);

    for (int r = 0; r < faceSize; ++r) {
      const int srcRowIdx = fV ? (faceSize - 1 - r) : r;
      const int srcY = row * faceSize + srcRowIdx;
      unsigned char* dstRow =
        faceBuffers[f].data() + static_cast<size_t>(r) * faceSize * ch;

      if (!fH) {
        const int srcX = col * faceSize;
        const unsigned char* srcRow =
          data + (static_cast<size_t>(srcY) * width + srcX) * ch;
        std::memcpy(dstRow, srcRow, static_cast<size_t>(faceSize) * ch);
      } else {
        const int baseSrcX = col * faceSize;
        for (int c = 0; c < faceSize; ++c) {
          const int srcColIdx = faceSize - 1 - c;
          const int srcX = baseSrcX + srcColIdx;
          const unsigned char* srcPixel =
            data + (static_cast<size_t>(srcY) * width + srcX) * ch;
          unsigned char* dstPixel = dstRow + static_cast<size_t>(c) * ch;
          for (size_t k = 0; k < ch; ++k) {
            dstPixel[k] = srcPixel[k];
          }
        }
      }
    }
  }

  result.width = faceSize;
  result.height = faceSize;
  result.success = faceSize > 0;
}

ShaderHandle
AssetManager::acquireShader(const ShaderPaths& paths, AssetLoadMode mode)
{
  if (renderer == nullptr) {
    return ShaderHandle{};
  }
  ShaderPaths canonicalPaths;
  canonicalPaths.vertexPath = canonicalPath(paths.vertexPath);
  canonicalPaths.fragmentPath = canonicalPath(paths.fragmentPath);
  const std::string key = shaderKey(canonicalPaths);
  std::unordered_map<std::string, uint32_t>::iterator cached =
    shaderCache.find(key);
  if (cached != shaderCache.end()) {
    ShaderEntry& entry = shaders[cached->second];
    entry.referenceCount += 1;
    return entry.handle;
  }

  ShaderSources fallback;
  fallback.vertexSource = kFallbackVertexShader;
  fallback.fragmentSource = kFallbackFragmentShader;
  ShaderHandle handle = renderer->enrollShader(fallback);
  if (!handle.isValid()) {
    return ShaderHandle{};
  }

  ShaderEntry entry;
  entry.handle = handle;
  entry.paths = canonicalPaths;
  entry.cacheKey = key;
  entry.vertexWriteTime = writeTime(canonicalPaths.vertexPath);
  entry.fragmentWriteTime = writeTime(canonicalPaths.fragmentPath);
  shaders[handle.slot] = entry;
  shaderCache[key] = handle.slot;

  if (mode == AssetLoadMode::Synchronous) {
    ShaderEntry& stored = shaders[handle.slot];
    stored.requestSerial += 1;
    LoadJob job;
    job.kind = AssetKind::Shader;
    job.slot = handle.slot;
    job.generation = handle.generation;
    job.requestSerial = stored.requestSerial;
    job.pathA = stored.paths.vertexPath;
    job.pathB = stored.paths.fragmentPath;
    job.defines = stored.paths.defines;
    LoadResult result = executeJob(job, source);
    processResult(result);
  } else {
    queueShader(shaders[handle.slot]);
  }
  return handle;
}

MeshHandle
AssetManager::acquireMesh(const MeshData& mesh)
{
  return enrollMesh(mesh, "memory", "");
}

MeshHandle
AssetManager::acquireMesh(const std::string& path,
                          const MeshLoadOptions& options)
{
  if (renderer == nullptr || path.empty()) {
    return MeshHandle{};
  }
  const std::string canonical = canonicalPath(path);
  const std::string key = meshKey(canonical, options);
  std::unordered_map<std::string, uint32_t>::iterator cached =
    meshCache.find(key);
  if (cached != meshCache.end()) {
    MeshEntry& entry = meshes[cached->second];
    entry.referenceCount += 1;
    return entry.handle;
  }

  // Native files keep tinyobj's material search beside the OBJ; byte sources
  // (package preloads) parse the bytes alone.
  MeshLoadResult loaded;
  std::vector<unsigned char> bytes;
  if (source != nullptr && source->hasFileSystem()) {
    loaded = MeshLoader::loadFromFile(canonical, options);
  } else if (source != nullptr && source->read(canonical, bytes)) {
    loaded = MeshLoader::loadFromMemory(
      std::string(bytes.begin(), bytes.end()), options, "");
  } else {
    loaded.error = "Unable to read mesh: " + canonical;
  }
  if (!loaded.success) {
    Logger::LogError(loaded.error.c_str());
    return MeshHandle{};
  }
  return enrollMesh(loaded.mesh, canonical, key);
}

MeshHandle
AssetManager::enrollMesh(const MeshData& mesh,
                         const std::string& path,
                         const std::string& cacheKey)
{
  if (renderer == nullptr || mesh.vertices.empty() || mesh.indices.empty() ||
      mesh.vertices.size() > std::numeric_limits<unsigned int>::max() ||
      mesh.indices.size() > std::numeric_limits<unsigned int>::max()) {
    return MeshHandle{};
  }
  for (size_t i = 0; i < mesh.indices.size(); ++i) {
    if (mesh.indices[i] >= mesh.vertices.size()) {
      return MeshHandle{};
    }
  }

  std::vector<ManagedMeshVertex> vertices;
  vertices.reserve(mesh.vertices.size());
  glm::vec3 minBounds(std::numeric_limits<float>::max());
  glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
  for (size_t i = 0; i < mesh.vertices.size(); ++i) {
    const MeshVertex& input = mesh.vertices[i];
    vertices.push_back({ input.position.x,
                         input.position.y,
                         input.position.z,
                         input.normal.x,
                         input.normal.y,
                         input.normal.z,
                         meshColorChannel(input.color.r),
                         meshColorChannel(input.color.g),
                         meshColorChannel(input.color.b),
                         meshColorChannel(input.color.a),
                         input.texCoords.x,
                         input.texCoords.y });
    minBounds = glm::min(minBounds, input.position);
    maxBounds = glm::max(maxBounds, input.position);
  }

  const MeshHandle handle =
    renderer->enrollMesh(vertices.data(),
                         vertices.size() * sizeof(ManagedMeshVertex),
                         mesh.indices.data(),
                         mesh.indices.size() * sizeof(uint32_t),
                         MeshVertexLayout::Pos3Norm3Color4U8Uv2,
                         false);
  if (!handle.isValid()) {
    return MeshHandle{};
  }

  MeshEntry entry;
  entry.handle = handle;
  entry.info.handle = handle;
  entry.info.vertexCount = static_cast<unsigned int>(mesh.vertices.size());
  entry.info.indexCount = static_cast<unsigned int>(mesh.indices.size());
  entry.info.minBounds = minBounds;
  entry.info.maxBounds = maxBounds;
  entry.path = path;
  entry.cacheKey = cacheKey;
  meshes[handle.slot] = entry;
  if (!cacheKey.empty()) {
    meshCache[cacheKey] = handle.slot;
  }
  return handle;
}

bool
AssetManager::retainTexture(TextureHandle handle)
{
  std::unordered_map<uint32_t, TextureEntry>::iterator it =
    textures.find(handle.slot);
  if (it == textures.end() || it->second.handle != handle) {
    return false;
  }
  it->second.referenceCount += 1;
  return true;
}

bool
AssetManager::retainShader(ShaderHandle handle)
{
  std::unordered_map<uint32_t, ShaderEntry>::iterator it =
    shaders.find(handle.slot);
  if (it == shaders.end() || it->second.handle != handle) {
    return false;
  }
  it->second.referenceCount += 1;
  return true;
}

bool
AssetManager::retainMesh(MeshHandle handle)
{
  std::unordered_map<uint32_t, MeshEntry>::iterator it =
    meshes.find(handle.slot);
  if (it == meshes.end() || it->second.handle != handle) {
    return false;
  }
  it->second.referenceCount += 1;
  return true;
}

bool
AssetManager::releaseTexture(TextureHandle handle)
{
  std::unordered_map<uint32_t, TextureEntry>::iterator it =
    textures.find(handle.slot);
  if (it == textures.end() || it->second.handle != handle) {
    return false;
  }
  if (it->second.referenceCount > 1) {
    it->second.referenceCount -= 1;
    return true;
  }
  textureCache.erase(it->second.cacheKey);
  renderer->destroyTexture(handle);
  textures.erase(it);
  return true;
}

bool
AssetManager::releaseShader(ShaderHandle handle)
{
  std::unordered_map<uint32_t, ShaderEntry>::iterator it =
    shaders.find(handle.slot);
  if (it == shaders.end() || it->second.handle != handle) {
    return false;
  }
  if (it->second.referenceCount > 1) {
    it->second.referenceCount -= 1;
    return true;
  }
  shaderCache.erase(it->second.cacheKey);
  renderer->destroyShader(handle);
  shaders.erase(it);
  return true;
}

bool
AssetManager::releaseMesh(MeshHandle handle)
{
  std::unordered_map<uint32_t, MeshEntry>::iterator it =
    meshes.find(handle.slot);
  if (it == meshes.end() || it->second.handle != handle) {
    return false;
  }
  if (it->second.referenceCount > 1) {
    it->second.referenceCount -= 1;
    return true;
  }
  if (!it->second.cacheKey.empty()) {
    meshCache.erase(it->second.cacheKey);
  }
  renderer->destroyMesh(handle);
  meshes.erase(it);
  return true;
}

AssetStatus
AssetManager::getState(TextureHandle handle) const
{
  AssetStatus status;
  std::unordered_map<uint32_t, TextureEntry>::const_iterator it =
    textures.find(handle.slot);
  if (it == textures.end() || it->second.handle != handle) {
    status.state = AssetState::Failed;
    status.lastError = "Unknown or stale texture handle";
    return status;
  }
  status.state = it->second.state;
  status.revision = it->second.revision;
  status.referenceCount = it->second.referenceCount;
  status.reloadPending = it->second.reloadPending;
  status.path = it->second.path;
  status.lastError = it->second.lastError;
  return status;
}

AssetStatus
AssetManager::getState(ShaderHandle handle) const
{
  AssetStatus status;
  std::unordered_map<uint32_t, ShaderEntry>::const_iterator it =
    shaders.find(handle.slot);
  if (it == shaders.end() || it->second.handle != handle) {
    status.state = AssetState::Failed;
    status.lastError = "Unknown or stale shader handle";
    return status;
  }
  status.state = it->second.state;
  status.revision = it->second.revision;
  status.referenceCount = it->second.referenceCount;
  status.reloadPending = it->second.reloadPending;
  status.path =
    it->second.paths.vertexPath + " | " + it->second.paths.fragmentPath;
  status.lastError = it->second.lastError;
  return status;
}

AssetStatus
AssetManager::getState(MeshHandle handle) const
{
  AssetStatus status;
  std::unordered_map<uint32_t, MeshEntry>::const_iterator it =
    meshes.find(handle.slot);
  if (it == meshes.end() || it->second.handle != handle) {
    status.state = AssetState::Failed;
    status.lastError = "Unknown or stale mesh handle";
    return status;
  }
  status.state = AssetState::Ready;
  status.revision = 1;
  status.referenceCount = it->second.referenceCount;
  status.path = it->second.path;
  return status;
}

TextureInfo
AssetManager::getTextureInfo(TextureHandle handle) const
{
  std::unordered_map<uint32_t, TextureEntry>::const_iterator it =
    textures.find(handle.slot);
  if (it == textures.end() || it->second.handle != handle) {
    return TextureInfo{};
  }
  return it->second.info;
}

MeshAssetInfo
AssetManager::getMeshInfo(MeshHandle handle) const
{
  std::unordered_map<uint32_t, MeshEntry>::const_iterator it =
    meshes.find(handle.slot);
  if (it == meshes.end() || it->second.handle != handle) {
    return MeshAssetInfo{};
  }
  return it->second.info;
}

bool
AssetManager::reload(TextureHandle handle)
{
  std::unordered_map<uint32_t, TextureEntry>::iterator it =
    textures.find(handle.slot);
  if (it == textures.end() || it->second.handle != handle ||
      it->second.reloadPending) {
    return false;
  }
  queueTexture(it->second);
  return true;
}

bool
AssetManager::reload(ShaderHandle handle)
{
  std::unordered_map<uint32_t, ShaderEntry>::iterator it =
    shaders.find(handle.slot);
  if (it == shaders.end() || it->second.handle != handle ||
      it->second.reloadPending) {
    return false;
  }
  queueShader(it->second);
  return true;
}

size_t
AssetManager::reload(const std::string& path)
{
  if (path.empty()) {
    return 0;
  }
  const std::string canonical = canonicalPath(path);
  size_t count = 0;
  for (std::unordered_map<uint32_t, TextureEntry>::iterator it =
         textures.begin();
       it != textures.end();
       ++it) {
    const TextureEntry& entry = it->second;
    const bool matches = entry.path == canonical ||
                         std::find(entry.sourcePaths.begin(),
                                   entry.sourcePaths.end(),
                                   canonical) != entry.sourcePaths.end();
    if (matches && reload(entry.handle)) {
      count += 1;
    }
  }
  for (std::unordered_map<uint32_t, ShaderEntry>::iterator it = shaders.begin();
       it != shaders.end();
       ++it) {
    if ((it->second.paths.vertexPath == canonical ||
         it->second.paths.fragmentPath == canonical) &&
        reload(it->second.handle)) {
      count += 1;
    }
  }
  return count;
}

size_t
AssetManager::reloadAll()
{
  size_t count = 0;
  for (std::unordered_map<uint32_t, TextureEntry>::iterator it =
         textures.begin();
       it != textures.end();
       ++it) {
    if (reload(it->second.handle)) {
      count += 1;
    }
  }
  for (std::unordered_map<uint32_t, ShaderEntry>::iterator it = shaders.begin();
       it != shaders.end();
       ++it) {
    if (reload(it->second.handle)) {
      count += 1;
    }
  }
  return count;
}

void
AssetManager::queueTexture(TextureEntry& entry)
{
  entry.requestSerial += 1;
  entry.reloadPending = true;
  if (entry.revision == 0) {
    entry.state = AssetState::Pending;
  }
  LoadJob job;
  job.kind = AssetKind::Texture;
  job.slot = entry.handle.slot;
  job.generation = entry.handle.generation;
  job.requestSerial = entry.requestSerial;
  job.pathA = entry.path;
  job.sourceKind = entry.sourceKind;
  job.sourcePaths = entry.sourcePaths;
  job.textureOptions = entry.options;
  enqueue(job);
}

void
AssetManager::queueShader(ShaderEntry& entry)
{
  entry.requestSerial += 1;
  entry.reloadPending = true;
  if (entry.revision == 0) {
    entry.state = AssetState::Pending;
  }
  LoadJob job;
  job.kind = AssetKind::Shader;
  job.slot = entry.handle.slot;
  job.generation = entry.handle.generation;
  job.requestSerial = entry.requestSerial;
  job.pathA = entry.paths.vertexPath;
  job.pathB = entry.paths.fragmentPath;
  job.defines = entry.paths.defines;
  enqueue(job);
}

void
AssetManager::enqueue(const LoadJob& job)
{
  {
    AssetQueueLock lock(queueMutex);
    if (stopping) {
      return;
    }
    jobs.push_back(job);
  }
#if !defined(ILLUMO_SERIAL_GUEST)
  queueCondition.notify_one();
#endif
}

void
AssetManager::workerMain()
{
#if !defined(ILLUMO_SERIAL_GUEST)
  while (true) {
    LoadJob job;
    {
      std::unique_lock<std::mutex> lock(queueMutex);
      queueCondition.wait(lock, [this]() { return stopping || !jobs.empty(); });
      if (stopping) {
        return;
      }
      job = jobs.front();
      jobs.pop_front();
    }
    LoadResult result = executeJob(job, source);
    {
      AssetQueueLock lock(queueMutex);
      if (!stopping) {
        results.push_back(std::move(result));
      }
    }
  }
#endif
}

AssetManager::LoadResult
AssetManager::executeJob(const LoadJob& job, const IAssetSource* source)
{
  LoadResult result;
  result.kind = job.kind;
  result.slot = job.slot;
  result.generation = job.generation;
  result.requestSerial = job.requestSerial;
  result.textureOptions = job.textureOptions;
  result.sourceKind = job.sourceKind;

  if (source == nullptr) {
    result.error = "No asset source: " + job.pathA;
    return result;
  }
  if (job.kind == AssetKind::Texture) {
    if (job.sourceKind != TextureSourceKind::Image2D) {
      decodeCubemap(job, *source, result);
      return result;
    }
    unsigned char* decoded =
      decodeImage(source, job.pathA, result.width, result.height);
    if (decoded == nullptr) {
      result.error = "Unable to decode texture: " + job.pathA;
      return result;
    }
    result.channels = 4;
    const size_t byteCount = static_cast<size_t>(result.width) *
                             static_cast<size_t>(result.height) * 4;
    result.pixels.assign(decoded, decoded + byteCount);
    stbi_image_free(decoded);
    result.success = true;
    return result;
  }

#if defined(ILLUMO_SERIAL_GUEST)
  // Shader programs are host policy; guests record built-in styles only.
  result.error = "Shader files are unavailable in a WASM guest: " + job.pathA;
  return result;
#else
  if (!source->hasFileSystem()) {
    result.error = "Shader includes need the native file system: " + job.pathA;
    return result;
  }
  PreprocessOptions vsOptions;
  vsOptions.defines = job.defines;
  vsOptions.sourcePath = job.pathA;
  PreprocessResult vsResult =
    ShaderPreprocessor::ProcessFile(job.pathA, vsOptions);
  if (!vsResult.success) {
    result.error = "Vertex shader preprocessor failed for " + job.pathA + ": " +
                   vsResult.errorMessage;
    return result;
  }

  PreprocessOptions fsOptions;
  fsOptions.defines = job.defines;
  fsOptions.sourcePath = job.pathB;
  PreprocessResult fsResult =
    ShaderPreprocessor::ProcessFile(job.pathB, fsOptions);
  if (!fsResult.success) {
    result.error = "Fragment shader preprocessor failed for " + job.pathB +
                   ": " + fsResult.errorMessage;
    return result;
  }

  result.shaderSources.vertexSource = vsResult.source;
  result.shaderSources.fragmentSource = fsResult.source;
  result.shaderSources.defines = job.defines;
  result.dependencies = vsResult.fileDependencies;
  for (size_t i = 0; i < fsResult.fileDependencies.size(); ++i) {
    const std::string& dep = fsResult.fileDependencies[i];
    if (std::find(result.dependencies.begin(),
                  result.dependencies.end(),
                  dep) == result.dependencies.end()) {
      result.dependencies.push_back(dep);
    }
  }

  if (result.shaderSources.vertexSource.empty() ||
      result.shaderSources.fragmentSource.empty()) {
    result.error = "Shader source is empty";
    return result;
  }
  result.success = true;
  return result;
#endif
}

void
AssetManager::processResult(LoadResult& result)
{
  if (result.kind == AssetKind::Texture) {
    std::unordered_map<uint32_t, TextureEntry>::iterator it =
      textures.find(result.slot);
    if (it == textures.end() ||
        it->second.handle.generation != result.generation ||
        it->second.requestSerial != result.requestSerial) {
      return;
    }
    TextureEntry& entry = it->second;
    entry.reloadPending = false;
    entry.lastWriteTime = writeTime(entry.path);
    bool uploaded = false;
    if (entry.sourceKind != TextureSourceKind::Image2D) {
      entry.sourceWriteTimes = result.sourceWriteTimes;
      if (result.success) {
        std::array<const unsigned char*, 6> faces;
        for (size_t i = 0; i < faces.size(); ++i) {
          faces[i] = result.faces[i].data();
        }
        uploaded = renderer->replaceCubemap(
          entry.handle, faces, result.width, result.height, result.channels);
      }
    } else if (result.success) {
      uploaded = renderer->replaceTexture(entry.handle,
                                          result.pixels.data(),
                                          result.width,
                                          result.height,
                                          result.channels,
                                          result.textureOptions);
    }
    if (!uploaded) {
      entry.lastError =
        result.error.empty() ? "Texture upload failed" : result.error;
      if (entry.revision == 0) {
        entry.state = AssetState::Failed;
      }
      return;
    }
    entry.state = AssetState::Ready;
    entry.revision += 1;
    entry.info = { result.width, result.height, result.channels };
    entry.lastError.clear();
    return;
  }

  std::unordered_map<uint32_t, ShaderEntry>::iterator it =
    shaders.find(result.slot);
  if (it == shaders.end() ||
      it->second.handle.generation != result.generation ||
      it->second.requestSerial != result.requestSerial) {
    return;
  }
  ShaderEntry& entry = it->second;
  entry.reloadPending = false;
  entry.vertexWriteTime = writeTime(entry.paths.vertexPath);
  entry.fragmentWriteTime = writeTime(entry.paths.fragmentPath);
  entry.dependencies = result.dependencies;
  entry.dependencyWriteTimes.clear();
  for (size_t i = 0; i < entry.dependencies.size(); ++i) {
    const std::string& dep = entry.dependencies[i];
    entry.dependencyWriteTimes[dep] = writeTime(dep);
  }

  if (!result.success ||
      !renderer->replaceShader(entry.handle, result.shaderSources)) {
    entry.lastError =
      result.error.empty() ? "Shader compile/link failed" : result.error;
    if (entry.revision == 0) {
      entry.state = AssetState::Failed;
    }
    return;
  }
  entry.state = AssetState::Ready;
  entry.revision += 1;
  entry.lastError.clear();
}

void
AssetManager::pollHotReload()
{
  if (!hotReloadEnabled) {
    return;
  }
  const std::chrono::steady_clock::time_point now =
    std::chrono::steady_clock::now();
  if (now < nextHotReloadPoll) {
    return;
  }
  nextHotReloadPoll = now + std::chrono::milliseconds(500);

  for (std::unordered_map<uint32_t, TextureEntry>::iterator it =
         textures.begin();
       it != textures.end();
       ++it) {
    TextureEntry& entry = it->second;
    if (entry.sourceKind != TextureSourceKind::Image2D) {
      const size_t count =
        entry.sourceKind == TextureSourceKind::CubemapFaces ? 6 : 1;
      bool changed = false;
      for (size_t i = 0; i < count; ++i) {
        changed = changed ||
                  writeTime(entry.sourcePaths[i]) != entry.sourceWriteTimes[i];
      }
      if (changed && !entry.reloadPending) {
        queueTexture(entry);
      }
      continue;
    }
    const std::int64_t current = writeTime(entry.path);
    if (!entry.reloadPending && current != 0 &&
        current != entry.lastWriteTime) {
      queueTexture(entry);
    }
  }
  for (std::unordered_map<uint32_t, ShaderEntry>::iterator it = shaders.begin();
       it != shaders.end();
       ++it) {
    ShaderEntry& entry = it->second;
    const std::int64_t vertex = writeTime(entry.paths.vertexPath);
    const std::int64_t fragment = writeTime(entry.paths.fragmentPath);
    bool needsReload = false;
    if (vertex != 0 && vertex != entry.vertexWriteTime) {
      needsReload = true;
    } else if (fragment != 0 && fragment != entry.fragmentWriteTime) {
      needsReload = true;
    } else {
      for (size_t i = 0; i < entry.dependencies.size(); ++i) {
        const std::string& dep = entry.dependencies[i];
        const std::int64_t depTime = writeTime(dep);
        std::unordered_map<std::string, std::int64_t>::const_iterator itDep =
          entry.dependencyWriteTimes.find(dep);
        if (depTime != 0 && (itDep == entry.dependencyWriteTimes.end() ||
                             itDep->second != depTime)) {
          needsReload = true;
          break;
        }
      }
    }
    if (!entry.reloadPending && needsReload) {
      queueShader(entry);
    }
  }
}

void
AssetManager::pump()
{
#if defined(ILLUMO_SERIAL_GUEST)
  // No worker: queued (Async) loads complete here, on the caller.
  std::deque<LoadJob> queued;
  queued.swap(jobs);
  while (!queued.empty()) {
    results.push_back(executeJob(queued.front(), source));
    queued.pop_front();
  }
#endif
  std::deque<LoadResult> completed;
  {
    AssetQueueLock lock(queueMutex);
    completed.swap(results);
  }
  while (!completed.empty()) {
    LoadResult result = std::move(completed.front());
    completed.pop_front();
    processResult(result);
  }
  pollHotReload();
}

void
AssetManager::completePendingForTests()
{
  std::deque<LoadJob> pending;
  {
    AssetQueueLock lock(queueMutex);
    pending.swap(jobs);
  }
  while (!pending.empty()) {
    LoadResult result = executeJob(pending.front(), source);
    pending.pop_front();
    processResult(result);
  }
  pump();
}

std::vector<std::string>
AssetManager::describeAssets() const
{
  std::vector<std::string> descriptions;
  for (std::unordered_map<uint32_t, MeshEntry>::const_iterator it =
         meshes.begin();
       it != meshes.end();
       ++it) {
    const MeshEntry& entry = it->second;
    descriptions.push_back(
      "mesh state=ready refs=" + std::to_string(entry.referenceCount) +
      " rev=1 path=" + entry.path);
  }
  for (std::unordered_map<uint32_t, TextureEntry>::const_iterator it =
         textures.begin();
       it != textures.end();
       ++it) {
    const TextureEntry& entry = it->second;
    descriptions.push_back(
      "texture state=" + std::string(assetStateName(entry.state)) +
      " refs=" + std::to_string(entry.referenceCount) +
      " rev=" + std::to_string(entry.revision) +
      (entry.lastError.empty() ? "" : " error=" + entry.lastError) +
      " path=" + entry.path);
  }
  for (std::unordered_map<uint32_t, ShaderEntry>::const_iterator it =
         shaders.begin();
       it != shaders.end();
       ++it) {
    const ShaderEntry& entry = it->second;
    descriptions.push_back(
      "shader state=" + std::string(assetStateName(entry.state)) +
      " refs=" + std::to_string(entry.referenceCount) +
      " rev=" + std::to_string(entry.revision) +
      (entry.lastError.empty() ? "" : " error=" + entry.lastError) +
      " path=" + entry.paths.vertexPath + " | " + entry.paths.fragmentPath);
  }
  return descriptions;
}
