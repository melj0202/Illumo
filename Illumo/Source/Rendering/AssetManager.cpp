#define STB_IMAGE_IMPLEMENTATION
#include "thirdparty/stb/stb_image.h"
#include <Illumo/Rendering/AssetManager.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

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

AssetManager::AssetManager(Renderer* rendererValue, bool startWorker)
  : renderer(rendererValue)
  , workerEnabled(startWorker)
  , nextHotReloadPoll(std::chrono::steady_clock::now())
{
#if defined(ILLUMO_ENABLE_DEBUG_TOOLS)
  hotReloadEnabled = true;
#endif
  if (workerEnabled) {
    worker = std::thread(&AssetManager::workerMain, this);
  }
}

AssetManager::~AssetManager()
{
  {
    std::lock_guard<std::mutex> lock(queueMutex);
    stopping = true;
    jobs.clear();
  }
  queueCondition.notify_all();
  if (worker.joinable()) {
    worker.join();
  }

  if (renderer != nullptr) {
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
AssetManager::canonicalPath(const std::string& path)
{
  std::error_code error;
  std::filesystem::path absolute = std::filesystem::absolute(path, error);
  if (error) {
    return std::filesystem::path(path).lexically_normal().string();
  }
  std::filesystem::path canonical =
    std::filesystem::weakly_canonical(absolute, error);
  std::string result =
    (error ? absolute.lexically_normal() : canonical).string();
#ifdef _WIN32
  std::transform(result.begin(), result.end(), result.begin(), [](char value) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  });
#endif
  return result;
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
AssetManager::shaderKey(const ShaderPaths& paths)
{
  return canonicalPath(paths.vertexPath) + "|" +
         canonicalPath(paths.fragmentPath);
}

std::filesystem::file_time_type
AssetManager::writeTime(const std::string& path)
{
  std::error_code error;
  std::filesystem::file_time_type time =
    std::filesystem::last_write_time(path, error);
  return error ? std::filesystem::file_time_type{} : time;
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
    LoadResult result = executeJob(job);
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
  (void)mode;
  if (renderer == nullptr) {
    return TextureHandle{};
  }
  std::string combinedKey = "cubemap_6faces";
  for (size_t i = 0; i < 6; ++i) {
    combinedKey += "|" + canonicalPath(facePaths[i]);
  }
  std::unordered_map<std::string, uint32_t>::iterator cached =
    textureCache.find(combinedKey);
  if (cached != textureCache.end()) {
    TextureEntry& entry = textures[cached->second];
    entry.referenceCount += 1;
    return entry.handle;
  }

  std::array<unsigned char*, 6> loadedData{};
  int faceWidth = 0;
  int faceHeight = 0;
  int faceChannels = 0;
  bool loadFailed = false;

  for (size_t i = 0; i < 6; ++i) {
    int w = 0;
    int h = 0;
    int ch = 0;
    std::string path = facePaths[i];
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 0);
    if (!data) {
      path = canonicalPath(facePaths[i]);
      data = stbi_load(path.c_str(), &w, &h, &ch, 0);
    }
    if (!data) {
      Logger::LogError(
        ("acquireCubemap: failed to load face: " + facePaths[i]).c_str());
      loadFailed = true;
      break;
    }
    if (i == 0) {
      faceWidth = w;
      faceHeight = h;
      faceChannels = ch;
    } else if (w != faceWidth || h != faceHeight) {
      Logger::LogError("acquireCubemap: face dimension mismatch");
      stbi_image_free(data);
      loadFailed = true;
      break;
    }
    loadedData[i] = data;
  }

  if (loadFailed) {
    for (size_t i = 0; i < 6; ++i) {
      if (loadedData[i] != nullptr) {
        stbi_image_free(loadedData[i]);
      }
    }
    return TextureHandle{};
  }

  std::array<const unsigned char*, 6> facePtrs = {
    loadedData[0], loadedData[1], loadedData[2],
    loadedData[3], loadedData[4], loadedData[5]
  };
  TextureHandle handle =
    renderer->enrollCubemap(facePtrs, faceWidth, faceHeight, faceChannels);

  for (size_t i = 0; i < 6; ++i) {
    stbi_image_free(loadedData[i]);
  }

  if (!handle.isValid()) {
    return TextureHandle{};
  }

  TextureEntry entry;
  entry.handle = handle;
  entry.path = facePaths[0];
  entry.cacheKey = combinedKey;
  entry.info = { faceWidth, faceHeight, faceChannels };
  entry.state = AssetState::Ready;
  entry.lastWriteTime = writeTime(canonicalPath(facePaths[0]));
  textures[handle.slot] = entry;
  textureCache[combinedKey] = handle.slot;
  return handle;
}

TextureHandle
AssetManager::acquireCubemapFromCross(const std::string& crossPath,
                                      AssetLoadMode mode)
{
  (void)mode;
  if (renderer == nullptr) {
    return TextureHandle{};
  }
  const std::string canonical = canonicalPath(crossPath);
  const std::string key = "cubemap_cross|" + canonical;
  std::unordered_map<std::string, uint32_t>::iterator cached =
    textureCache.find(key);
  if (cached != textureCache.end()) {
    TextureEntry& entry = textures[cached->second];
    entry.referenceCount += 1;
    return entry.handle;
  }

  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char* data =
    stbi_load(crossPath.c_str(), &width, &height, &channels, 0);
  if (!data) {
    data = stbi_load(canonical.c_str(), &width, &height, &channels, 0);
  }
  if (!data) {
    Logger::LogError(
      ("acquireCubemapFromCross: failed to load " + crossPath).c_str());
    return TextureHandle{};
  }

  int faceSize = 0;
  int faceCols[6];
  int faceRows[6];
  bool flipH[6] = { false, false, false, false, false, false };
  bool flipV[6] = { false, false, false, false, false, false };

  if (width * 3 == height * 4) {
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
  } else if (width * 4 == height * 3) {
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
  } else if (width == 6 * height) {
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

  std::vector<std::vector<unsigned char>> faceBuffers(6);
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

  std::array<const unsigned char*, 6> facePtrs = {
    faceBuffers[0].data(), faceBuffers[1].data(), faceBuffers[2].data(),
    faceBuffers[3].data(), faceBuffers[4].data(), faceBuffers[5].data()
  };

  TextureHandle handle =
    renderer->enrollCubemap(facePtrs, faceSize, faceSize, channels);
  stbi_image_free(data);

  if (!handle.isValid()) {
    return TextureHandle{};
  }

  TextureEntry entry;
  entry.handle = handle;
  entry.path = canonical;
  entry.cacheKey = key;
  entry.info = { faceSize, faceSize, channels };
  entry.state = AssetState::Ready;
  entry.lastWriteTime = writeTime(canonical);
  textures[handle.slot] = entry;
  textureCache[key] = handle.slot;
  return handle;
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
    LoadResult result = executeJob(job);
    processResult(result);
  } else {
    queueShader(shaders[handle.slot]);
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
  const std::string canonical = canonicalPath(path);
  size_t count = 0;
  for (std::unordered_map<uint32_t, TextureEntry>::iterator it =
         textures.begin();
       it != textures.end();
       ++it) {
    if (it->second.path == canonical && reload(it->second.handle)) {
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
    std::lock_guard<std::mutex> lock(queueMutex);
    if (stopping) {
      return;
    }
    jobs.push_back(job);
  }
  queueCondition.notify_one();
}

void
AssetManager::workerMain()
{
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
    LoadResult result = executeJob(job);
    {
      std::lock_guard<std::mutex> lock(queueMutex);
      if (!stopping) {
        results.push_back(std::move(result));
      }
    }
  }
}

AssetManager::LoadResult
AssetManager::executeJob(const LoadJob& job)
{
  LoadResult result;
  result.kind = job.kind;
  result.slot = job.slot;
  result.generation = job.generation;
  result.requestSerial = job.requestSerial;
  result.textureOptions = job.textureOptions;

  if (job.kind == AssetKind::Texture) {
    int sourceChannels = 0;
    unsigned char* decoded = stbi_load(
      job.pathA.c_str(), &result.width, &result.height, &sourceChannels, 4);
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
    if (!result.success || !renderer->replaceTexture(entry.handle,
                                                     result.pixels.data(),
                                                     result.width,
                                                     result.height,
                                                     result.channels,
                                                     result.textureOptions)) {
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
    const std::filesystem::file_time_type current = writeTime(entry.path);
    if (!entry.reloadPending && current != std::filesystem::file_time_type{} &&
        current != entry.lastWriteTime) {
      queueTexture(entry);
    }
  }
  for (std::unordered_map<uint32_t, ShaderEntry>::iterator it = shaders.begin();
       it != shaders.end();
       ++it) {
    ShaderEntry& entry = it->second;
    const std::filesystem::file_time_type vertex =
      writeTime(entry.paths.vertexPath);
    const std::filesystem::file_time_type fragment =
      writeTime(entry.paths.fragmentPath);
    bool needsReload = false;
    if (vertex != std::filesystem::file_time_type{} &&
        vertex != entry.vertexWriteTime) {
      needsReload = true;
    } else if (fragment != std::filesystem::file_time_type{} &&
               fragment != entry.fragmentWriteTime) {
      needsReload = true;
    } else {
      for (size_t i = 0; i < entry.dependencies.size(); ++i) {
        const std::string& dep = entry.dependencies[i];
        const std::filesystem::file_time_type depTime = writeTime(dep);
        std::unordered_map<std::string,
                           std::filesystem::file_time_type>::const_iterator
          itDep = entry.dependencyWriteTimes.find(dep);
        if (depTime != std::filesystem::file_time_type{} &&
            (itDep == entry.dependencyWriteTimes.end() ||
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
  std::deque<LoadResult> completed;
  {
    std::lock_guard<std::mutex> lock(queueMutex);
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
    std::lock_guard<std::mutex> lock(queueMutex);
    pending.swap(jobs);
  }
  while (!pending.empty()) {
    LoadResult result = executeJob(pending.front());
    pending.pop_front();
    processResult(result);
  }
  pump();
}

std::vector<std::string>
AssetManager::describeAssets() const
{
  std::vector<std::string> descriptions;
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
