#include "D3D12Backend.h"
#include "D3D12Device.h"

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Services/Logger.h>

D3D12Backend::D3D12Backend(IRenderWindow* window, bool present)
  : m_device(std::make_unique<D3D12Device>())
  , m_commandQueue(std::make_unique<CommandQueue>())
  , m_window(window)
  , m_present(present)
  , m_fpsStart(std::chrono::steady_clock::now())
{
}

D3D12Backend::~D3D12Backend()
{
  Shutdown();
}

bool
D3D12Backend::Initialize()
{
  ILLUMO_PROFILE_ZONE("D3D12Backend.Initialize");
  if (!m_device) {
    return false;
  }
  std::string error;
  if (!m_device->initialize(m_window, m_present, &error)) {
    Logger::LogError("Direct3D 12 backend could not start: " + error);
    m_device->shutdown();
    return false;
  }
  m_initialized = true;
  return true;
}

void
D3D12Backend::Shutdown()
{
  if (m_device) {
    m_device->shutdown();
  }
  m_initialized = false;
}

void
D3D12Backend::BeginFrame()
{
  m_device->resetFrameStats();
  m_rejectionsAtFrameStart = m_commandQueue->GetTotalRejected();
  m_device->resetFrameError();
  m_device->beginFrame();
}

void
D3D12Backend::EndFrame()
{
  ILLUMO_PROFILE_ZONE("D3D12Backend.EndFrame");
  const D3D12FrameStats& stats = m_device->frameStats();
  ILLUMO_PROFILE_PLOT("DX12.Submits", stats.submits);
  ILLUMO_PROFILE_PLOT("DX12.Commands", stats.commands);
  ILLUMO_PROFILE_PLOT("DX12.RecordedLists", stats.recordedLists);
  ILLUMO_PROFILE_PLOT("DX12.RecordedCommands", stats.recordedCommands);
  ILLUMO_PROFILE_PLOT("DX12.DrawCalls", stats.drawCalls);
  ILLUMO_PROFILE_PLOT("DX12.UploadBytes", stats.uploadBytes);
  ILLUMO_PROFILE_PLOT("DX12.Barriers", stats.barriers);
  ILLUMO_PROFILE_PLOT("DX12.PipelinesCreated", stats.pipelinesCreated);
  ILLUMO_PROFILE_PLOT("DX12.CommandQueueHighWater",
                      m_commandQueue->GetHighWaterMark());
  {
    ILLUMO_PROFILE_ZONE("D3D12Backend.present");
    // The window keeps the vsync preference the swapchain follows.
    if (m_window != nullptr) {
      m_window->swapBuffers();
    }
    m_device->endFrame();
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
D3D12Backend::SubmitCommandQueue()
{
  ILLUMO_PROFILE_ZONE("D3D12Backend.SubmitCommandQueue");
  m_device->executeQueue(*m_commandQueue);
}

void
D3D12Backend::PushToCommandQueue(RenderCommand command)
{
  m_commandQueue->Submit(command);
}

void
D3D12Backend::ClearCommandQueue()
{
  m_commandQueue->Reset();
}

size_t
D3D12Backend::rejectedCommandCount() const
{
  return m_commandQueue->GetTotalRejected();
}

size_t
D3D12Backend::commandHighWaterMark() const
{
  return m_commandQueue->GetHighWaterMark();
}

std::string
D3D12Backend::submissionError() const
{
  return m_device->frameError();
}

FrameReadback
D3D12Backend::readBackbuffer(int width, int height)
{
  ILLUMO_PROFILE_ZONE("D3D12Backend.readBackbuffer");
  FrameReadback result;
  if (width < 1 || height < 1 || width > 4096 || height > 4096) {
    result.error = "Readback dimensions must be within 1..4096";
    return result;
  }
  if (m_commandQueue->GetTotalRejected() != m_rejectionsAtFrameStart) {
    result.error = "Frame rejected commands at the queue safety ceiling";
    return result;
  }
  if (!m_device->frameError().empty()) {
    result.error = m_device->frameError();
    return result;
  }
  return m_device->readBackbuffer(width, height);
}

bool
D3D12Backend::requestFramebufferReadback(std::uint32_t stream,
                                         FramebufferHandle framebuffer,
                                         int width,
                                         int height)
{
  return m_device->requestFramebufferReadback(
    stream, framebuffer, width, height);
}

bool
D3D12Backend::takeFramebufferReadback(std::uint32_t stream,
                                      bool wait,
                                      FrameReadback& out)
{
  return m_device->takeFramebufferReadback(stream, wait, out);
}

void
D3D12Backend::releaseReadbackStream(std::uint32_t stream)
{
  m_device->releaseReadbackStream(stream);
}

MeshHandle
D3D12Backend::CreateMesh(const void* vertices,
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
D3D12Backend::CreateMesh(const void* vertices,
                         size_t vertexSize,
                         const void* indices,
                         size_t indexSize,
                         MeshVertexLayout layout,
                         bool dynamic)
{
  return m_device->createMesh(
    vertices, vertexSize, indices, indexSize, layout, dynamic);
}

bool
D3D12Backend::ReplaceMesh(MeshHandle handle,
                          const void* vertices,
                          size_t vertexSize,
                          const void* indices,
                          size_t indexSize,
                          MeshVertexLayout layout,
                          bool dynamic)
{
  return m_device->replaceMesh(
    handle, vertices, vertexSize, indices, indexSize, layout, dynamic);
}

bool
D3D12Backend::DestroyMesh(MeshHandle handle)
{
  return m_device->destroyMesh(handle);
}

bool
D3D12Backend::IsMeshValid(MeshHandle handle) const
{
  return m_device->isMeshValid(handle);
}

bool
D3D12Backend::preprocess(const ShaderSources& sources,
                         ShaderSources* output) const
{
  PreprocessOptions vertexOptions;
  vertexOptions.defines = sources.defines;
  const PreprocessResult vertex =
    ShaderPreprocessor::Process(sources.vertexSource, vertexOptions);
  if (!vertex.success) {
    Logger::LogError("D3D12Backend: Vertex preprocessor failed: " +
                     vertex.errorMessage);
    return false;
  }
  PreprocessOptions fragmentOptions;
  fragmentOptions.defines = sources.defines;
  const PreprocessResult fragment =
    ShaderPreprocessor::Process(sources.fragmentSource, fragmentOptions);
  if (!fragment.success) {
    Logger::LogError("D3D12Backend: Fragment preprocessor failed: " +
                     fragment.errorMessage);
    return false;
  }
  output->vertexSource = vertex.source;
  output->fragmentSource = fragment.source;
  output->defines.clear();
  return true;
}

bool
D3D12Backend::preprocess(const ShaderPaths& paths, ShaderSources* output) const
{
  PreprocessOptions vertexOptions;
  vertexOptions.defines = paths.defines;
  vertexOptions.sourcePath = paths.vertexPath;
  const PreprocessResult vertex =
    ShaderPreprocessor::ProcessFile(paths.vertexPath, vertexOptions);
  if (!vertex.success) {
    Logger::LogError("D3D12Backend: Vertex preprocessor failed for " +
                     paths.vertexPath + ": " + vertex.errorMessage);
    return false;
  }
  PreprocessOptions fragmentOptions;
  fragmentOptions.defines = paths.defines;
  fragmentOptions.sourcePath = paths.fragmentPath;
  const PreprocessResult fragment =
    ShaderPreprocessor::ProcessFile(paths.fragmentPath, fragmentOptions);
  if (!fragment.success) {
    Logger::LogError("D3D12Backend: Fragment preprocessor failed for " +
                     paths.fragmentPath + ": " + fragment.errorMessage);
    return false;
  }
  output->vertexSource = vertex.source;
  output->fragmentSource = fragment.source;
  output->defines.clear();
  return true;
}

ShaderHandle
D3D12Backend::CreateShaderProgram(const ShaderPaths& paths)
{
  ILLUMO_PROFILE_ZONE("D3D12Backend.CreateShaderProgram");
  ShaderSources sources;
  std::string error;
  ShaderHandle handle{};
  if (preprocess(paths, &sources)) {
    handle = m_device->createShader(sources, &error);
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
D3D12Backend::CreateShaderProgram(const ShaderSources& sources)
{
  ILLUMO_PROFILE_ZONE("D3D12Backend.CreateShaderProgram");
  ShaderSources prepared;
  if (!preprocess(sources, &prepared)) {
    return {};
  }
  std::string error;
  const ShaderHandle handle = m_device->createShader(prepared, &error);
  if (!handle.isValid() && !error.empty()) {
    Logger::LogError(error);
  }
  return handle;
}

bool
D3D12Backend::ReplaceShaderProgram(ShaderHandle handle,
                                   const ShaderSources& sources)
{
  ILLUMO_PROFILE_ZONE("D3D12Backend.ReplaceShaderProgram");
  if (!m_device->isShaderValid(handle)) {
    Logger::LogWarning("ReplaceShaderProgram: stale shader handle ignored");
    return false;
  }
  ShaderSources prepared;
  std::string error;
  if (!preprocess(sources, &prepared) ||
      !m_device->replaceShader(handle, prepared, &error)) {
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
D3D12Backend::DestroyShaderProgram(ShaderHandle handle)
{
  return m_device->destroyShader(handle);
}

bool
D3D12Backend::IsShaderValid(ShaderHandle handle) const
{
  return m_device->isShaderValid(handle);
}

TextureHandle
D3D12Backend::CreateTexture(const unsigned char* data,
                            const int width,
                            const int height)
{
  return CreateTexture(data, width, height, 4, TextureOptions{});
}

TextureHandle
D3D12Backend::CreateTexture(const unsigned char* data,
                            const int width,
                            const int height,
                            int channels,
                            const TextureOptions& options)
{
  return m_device->createTexture(data, width, height, channels, options);
}

TextureHandle
D3D12Backend::CreateCubemap(
  const std::array<const unsigned char*, 6>& facesData,
  int width,
  int height,
  int channels)
{
  return m_device->createCubemap(facesData, width, height, channels);
}

bool
D3D12Backend::ReplaceTexture(TextureHandle handle,
                             const unsigned char* data,
                             int width,
                             int height,
                             int channels,
                             const TextureOptions& options)
{
  return m_device->replaceTexture(
    handle, data, width, height, channels, options);
}

bool
D3D12Backend::ReplaceCubemap(TextureHandle handle,
                             const std::array<const unsigned char*, 6>& faces,
                             int width,
                             int height,
                             int channels)
{
  return m_device->replaceCubemap(handle, faces, width, height, channels);
}

bool
D3D12Backend::DestroyTexture(TextureHandle handle)
{
  return m_device->destroyTexture(handle);
}

bool
D3D12Backend::IsTextureValid(TextureHandle handle) const
{
  return m_device->isTextureValid(handle);
}

TextureInfo
D3D12Backend::GetTextureInfo(TextureHandle handle) const
{
  return m_device->textureInfo(handle);
}

FramebufferHandle
D3D12Backend::CreateFramebuffer(const FramebufferDesc& desc,
                                FramebufferAttachments* outAttachments)
{
  return m_device->createFramebuffer(desc, outAttachments);
}

FramebufferHandle
D3D12Backend::CreateDepthFramebuffer(int width,
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
D3D12Backend::DestroyFramebuffer(FramebufferHandle handle)
{
  return m_device->destroyFramebuffer(handle);
}

bool
D3D12Backend::IsFramebufferValid(FramebufferHandle handle) const
{
  return m_device->isFramebufferValid(handle);
}

BufferHandle
D3D12Backend::CreateBuffer(BufferUsage usage, size_t capacityBytes)
{
  return m_device->createBuffer(usage, capacityBytes);
}

bool
D3D12Backend::DestroyBuffer(BufferHandle handle)
{
  return m_device->destroyBuffer(handle);
}

bool
D3D12Backend::IsBufferValid(BufferHandle handle) const
{
  return m_device->isBufferValid(handle);
}
