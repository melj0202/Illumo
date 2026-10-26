#include "GpuBackend.h"
#include "GpuDriver.h"

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Services/Logger.h>
#include <utility>

GpuBackend::GpuBackend(std::unique_ptr<GpuDriver> driver,
                       IRenderWindow* window,
                       bool present)
  : m_driver(std::move(driver))
  , m_commandQueue(std::make_unique<CommandQueue>())
  , m_window(window)
  , m_present(present)
  , m_fpsStart(std::chrono::steady_clock::now())
{
}

GpuBackend::~GpuBackend()
{
  Shutdown();
}

void
GpuBackend::setGpuCacheDirectory(const std::filesystem::path& directory)
{
  if (m_driver) {
    m_driver->setCacheDirectory(directory);
  }
}

bool
GpuBackend::Initialize()
{
  ILLUMO_PROFILE_ZONE("GpuBackend.Initialize");
  if (!m_driver) {
    return false;
  }
  std::string error;
  if (!m_driver->initialize(m_window, m_present, &error)) {
    Logger::LogError(std::string(m_driver->apiName()) +
                     " backend could not start: " + error);
    m_driver->shutdown();
    return false;
  }
  m_initialized = true;
  return true;
}

void
GpuBackend::Shutdown()
{
  if (m_driver) {
    m_driver->shutdown();
  }
  m_initialized = false;
}

void
GpuBackend::BeginFrame()
{
  m_driver->resetFrameStats();
  m_rejectionsAtFrameStart = m_commandQueue->GetTotalRejected();
  m_driver->resetFrameError();
  m_driver->beginFrame();
}

void
GpuBackend::EndFrame()
{
  ILLUMO_PROFILE_ZONE("GpuBackend.EndFrame");
  m_driver->plotFrameStats(m_commandQueue->GetHighWaterMark());
  {
    ILLUMO_PROFILE_ZONE("GpuBackend.present");
    // The window keeps the vsync preference the swapchain follows.
    if (m_window != nullptr) {
      m_window->swapBuffers();
    }
    m_driver->endFrame();
  }
  m_frameCount += 1;
  const std::chrono::steady_clock::time_point now =
    std::chrono::steady_clock::now();
  const double seconds =
    std::chrono::duration<double>(now - m_fpsStart).count();
  if (seconds >= 1.0) {
    m_fps = static_cast<int>(static_cast<double>(m_frameCount) / seconds + 0.5);
    m_frameCount = 0;
    m_fpsStart = now;
  }
}

void
GpuBackend::SubmitCommandQueue()
{
  ILLUMO_PROFILE_ZONE("GpuBackend.SubmitCommandQueue");
  m_driver->executeQueue(*m_commandQueue);
}

void
GpuBackend::PushToCommandQueue(RenderCommand command)
{
  m_commandQueue->Submit(command);
}

void
GpuBackend::ClearCommandQueue()
{
  m_commandQueue->Reset();
}

size_t
GpuBackend::rejectedCommandCount() const
{
  return m_commandQueue->GetTotalRejected();
}

size_t
GpuBackend::commandHighWaterMark() const
{
  return m_commandQueue->GetHighWaterMark();
}

std::string
GpuBackend::submissionError() const
{
  return m_driver->frameError();
}

FrameReadback
GpuBackend::readBackbuffer(int width, int height)
{
  ILLUMO_PROFILE_ZONE("GpuBackend.readBackbuffer");
  FrameReadback result;
  if (width < 1 || height < 1 || width > 4096 || height > 4096) {
    result.error = "Readback dimensions must be within 1..4096";
    return result;
  }
  if (m_commandQueue->GetTotalRejected() != m_rejectionsAtFrameStart) {
    result.error = "Frame rejected commands at the queue safety ceiling";
    return result;
  }
  if (!m_driver->frameError().empty()) {
    result.error = m_driver->frameError();
    return result;
  }
  return m_driver->readBackbuffer(width, height);
}

bool
GpuBackend::requestFramebufferReadback(std::uint32_t stream,
                                       FramebufferHandle framebuffer,
                                       int width,
                                       int height)
{
  return m_driver->requestFramebufferReadback(
    stream, framebuffer, width, height);
}

bool
GpuBackend::takeFramebufferReadback(std::uint32_t stream,
                                    bool wait,
                                    FrameReadback& out)
{
  return m_driver->takeFramebufferReadback(stream, wait, out);
}

void
GpuBackend::releaseReadbackStream(std::uint32_t stream)
{
  m_driver->releaseReadbackStream(stream);
}

MeshHandle
GpuBackend::CreateMesh(const void* vertices,
                       size_t vertexSize,
                       const void* indices,
                       size_t indexSize)
{
  return CreateMesh(vertices,
                    vertexSize,
                    indices,
                    indexSize,
                    MeshVertexLayout::Pos3Color3Uv2,
                    false);
}

MeshHandle
GpuBackend::CreateMesh(const void* vertices,
                       size_t vertexSize,
                       const void* indices,
                       size_t indexSize,
                       MeshVertexLayout layout,
                       bool dynamic)
{
  return m_driver->createMesh(
    vertices, vertexSize, indices, indexSize, layout, dynamic);
}

bool
GpuBackend::ReplaceMesh(MeshHandle handle,
                        const void* vertices,
                        size_t vertexSize,
                        const void* indices,
                        size_t indexSize,
                        MeshVertexLayout layout,
                        bool dynamic)
{
  return m_driver->replaceMesh(
    handle, vertices, vertexSize, indices, indexSize, layout, dynamic);
}

bool
GpuBackend::DestroyMesh(MeshHandle handle)
{
  return m_driver->destroyMesh(handle);
}

bool
GpuBackend::IsMeshValid(MeshHandle handle) const
{
  return m_driver->isMeshValid(handle);
}

bool
GpuBackend::preprocess(const ShaderSources& sources,
                       ShaderSources* output) const
{
  PreprocessOptions vertexOptions;
  vertexOptions.defines = sources.defines;
  const PreprocessResult vertex =
    ShaderPreprocessor::Process(sources.vertexSource, vertexOptions);
  if (!vertex.success) {
    Logger::LogError(
      std::string(m_driver->apiName()) +
      " backend: Vertex preprocessor failed: " + vertex.errorMessage);
    return false;
  }
  PreprocessOptions fragmentOptions;
  fragmentOptions.defines = sources.defines;
  const PreprocessResult fragment =
    ShaderPreprocessor::Process(sources.fragmentSource, fragmentOptions);
  if (!fragment.success) {
    Logger::LogError(
      std::string(m_driver->apiName()) +
      " backend: Fragment preprocessor failed: " + fragment.errorMessage);
    return false;
  }
  output->vertexSource = vertex.source;
  output->fragmentSource = fragment.source;
  output->defines.clear();
  return true;
}

bool
GpuBackend::preprocess(const ShaderPaths& paths, ShaderSources* output) const
{
  PreprocessOptions vertexOptions;
  vertexOptions.defines = paths.defines;
  vertexOptions.sourcePath = paths.vertexPath;
  const PreprocessResult vertex =
    ShaderPreprocessor::ProcessFile(paths.vertexPath, vertexOptions);
  if (!vertex.success) {
    Logger::LogError(std::string(m_driver->apiName()) +
                     " backend: Vertex preprocessor failed for " +
                     paths.vertexPath + ": " + vertex.errorMessage);
    return false;
  }
  PreprocessOptions fragmentOptions;
  fragmentOptions.defines = paths.defines;
  fragmentOptions.sourcePath = paths.fragmentPath;
  const PreprocessResult fragment =
    ShaderPreprocessor::ProcessFile(paths.fragmentPath, fragmentOptions);
  if (!fragment.success) {
    Logger::LogError(std::string(m_driver->apiName()) +
                     " backend: Fragment preprocessor failed for " +
                     paths.fragmentPath + ": " + fragment.errorMessage);
    return false;
  }
  output->vertexSource = vertex.source;
  output->fragmentSource = fragment.source;
  output->defines.clear();
  return true;
}

ShaderHandle
GpuBackend::CreateShaderProgram(const ShaderPaths& paths)
{
  ILLUMO_PROFILE_ZONE("GpuBackend.CreateShaderProgram");
  ShaderSources sources;
  std::string error;
  ShaderHandle handle{};
  if (preprocess(paths, &sources)) {
    handle = m_driver->createShader(sources, &error);
  }
  if (!handle.isValid()) {
    if (!error.empty()) {
      Logger::LogError(error);
    }
    Logger::LogError("Shader program " + paths.vertexPath + " + " +
                     paths.fragmentPath + " is unusable");
    return {};
  }
  Logger::LogTrace("Shader program built from " + paths.vertexPath + " + " +
                   paths.fragmentPath);
  return handle;
}

ShaderHandle
GpuBackend::CreateShaderProgram(const ShaderSources& sources)
{
  ILLUMO_PROFILE_ZONE("GpuBackend.CreateShaderProgram");
  ShaderSources prepared;
  if (!preprocess(sources, &prepared)) {
    return {};
  }
  std::string error;
  const ShaderHandle handle = m_driver->createShader(prepared, &error);
  if (!handle.isValid() && !error.empty()) {
    Logger::LogError(error);
  }
  return handle;
}

bool
GpuBackend::ReplaceShaderProgram(ShaderHandle handle,
                                 const ShaderSources& sources)
{
  ILLUMO_PROFILE_ZONE("GpuBackend.ReplaceShaderProgram");
  if (!m_driver->isShaderValid(handle)) {
    Logger::LogWarning("ReplaceShaderProgram: stale shader handle ignored");
    return false;
  }
  ShaderSources prepared;
  std::string error;
  if (!preprocess(sources, &prepared) ||
      !m_driver->replaceShader(handle, prepared, &error)) {
    if (!error.empty()) {
      Logger::LogError(error);
    }
    Logger::LogWarning("ReplaceShaderProgram: the new program is unusable; "
                       "keeping the previous one");
    return false;
  }
  return true;
}

bool
GpuBackend::DestroyShaderProgram(ShaderHandle handle)
{
  return m_driver->destroyShader(handle);
}

bool
GpuBackend::IsShaderValid(ShaderHandle handle) const
{
  return m_driver->isShaderValid(handle);
}

TextureHandle
GpuBackend::CreateTexture(const unsigned char* data,
                          const int width,
                          const int height)
{
  return CreateTexture(data, width, height, 4, TextureOptions{});
}

TextureHandle
GpuBackend::CreateTexture(const unsigned char* data,
                          const int width,
                          const int height,
                          int channels,
                          const TextureOptions& options)
{
  return m_driver->createTexture(data, width, height, channels, options);
}

TextureHandle
GpuBackend::CreateCubemap(const std::array<const unsigned char*, 6>& facesData,
                          int width,
                          int height,
                          int channels)
{
  return m_driver->createCubemap(facesData, width, height, channels);
}

bool
GpuBackend::ReplaceTexture(TextureHandle handle,
                           const unsigned char* data,
                           int width,
                           int height,
                           int channels,
                           const TextureOptions& options)
{
  return m_driver->replaceTexture(
    handle, data, width, height, channels, options);
}

bool
GpuBackend::ReplaceCubemap(TextureHandle handle,
                           const std::array<const unsigned char*, 6>& faces,
                           int width,
                           int height,
                           int channels)
{
  return m_driver->replaceCubemap(handle, faces, width, height, channels);
}

bool
GpuBackend::DestroyTexture(TextureHandle handle)
{
  return m_driver->destroyTexture(handle);
}

bool
GpuBackend::IsTextureValid(TextureHandle handle) const
{
  return m_driver->isTextureValid(handle);
}

TextureInfo
GpuBackend::GetTextureInfo(TextureHandle handle) const
{
  return m_driver->textureInfo(handle);
}

FramebufferHandle
GpuBackend::CreateFramebuffer(const FramebufferDesc& desc,
                              FramebufferAttachments* outAttachments)
{
  return m_driver->createFramebuffer(desc, outAttachments);
}

FramebufferHandle
GpuBackend::CreateDepthFramebuffer(int width,
                                   int height,
                                   TextureHandle* outDepthTexture)
{
  FramebufferDesc desc;
  desc.width = width;
  desc.height = height;
  desc.depthStencilFormat = TextureFormat::Depth24;
  desc.depthFilter = TextureFilter::Nearest;
  FramebufferAttachments attachments;
  const FramebufferHandle handle = CreateFramebuffer(desc, &attachments);
  if (outDepthTexture != nullptr) {
    *outDepthTexture = attachments.depthStencilTexture;
  }
  return handle;
}

bool
GpuBackend::DestroyFramebuffer(FramebufferHandle handle)
{
  return m_driver->destroyFramebuffer(handle);
}

bool
GpuBackend::IsFramebufferValid(FramebufferHandle handle) const
{
  return m_driver->isFramebufferValid(handle);
}

BufferHandle
GpuBackend::CreateBuffer(BufferUsage usage, size_t capacityBytes)
{
  return m_driver->createBuffer(usage, capacityBytes);
}

bool
GpuBackend::DestroyBuffer(BufferHandle handle)
{
  return m_driver->destroyBuffer(handle);
}

bool
GpuBackend::IsBufferValid(BufferHandle handle) const
{
  return m_driver->isBufferValid(handle);
}
