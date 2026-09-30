#include "VulkanBackend.h"
#include "VulkanDevice.h"

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Services/Logger.h>

VulkanBackend::VulkanBackend(IRenderWindow* window, bool present)
  : m_device(std::make_unique<VulkanDevice>())
  , m_commandQueue(std::make_unique<CommandQueue>())
  , m_window(window)
  , m_present(present)
  , m_fpsStart(std::chrono::steady_clock::now())
{
}

VulkanBackend::~VulkanBackend()
{
  Shutdown();
}

bool
VulkanBackend::Initialize()
{
  ILLUMO_PROFILE_ZONE("VulkanBackend.Initialize");
  if (!m_device) {
    return false;
  }
  std::string error;
  if (!m_device->initialize(m_window, m_present, &error)) {
    Logger::LogError("Vulkan backend could not start: " + error);
    m_device->shutdown();
    return false;
  }
  m_initialized = true;
  return true;
}

void
VulkanBackend::Shutdown()
{
  if (m_device) {
    m_device->shutdown();
  }
  m_initialized = false;
}

void
VulkanBackend::BeginFrame()
{
  m_device->resetFrameStats();
  m_rejectionsAtFrameStart = m_commandQueue->GetTotalRejected();
  m_device->resetFrameError();
  m_device->beginFrame();
}

void
VulkanBackend::EndFrame()
{
  ILLUMO_PROFILE_ZONE("VulkanBackend.EndFrame");
  const VulkanFrameStats& stats = m_device->frameStats();
  ILLUMO_PROFILE_PLOT("VK.Submits", stats.submits);
  ILLUMO_PROFILE_PLOT("VK.Commands", stats.commands);
  ILLUMO_PROFILE_PLOT("VK.RecordedLists", stats.recordedLists);
  ILLUMO_PROFILE_PLOT("VK.RecordedCommands", stats.recordedCommands);
  ILLUMO_PROFILE_PLOT("VK.DrawCalls", stats.drawCalls);
  ILLUMO_PROFILE_PLOT("VK.UploadBytes", stats.uploadBytes);
  ILLUMO_PROFILE_PLOT("VK.RenderPassBreaks", stats.renderPassBreaks);
  ILLUMO_PROFILE_PLOT("VK.PipelinesCreated", stats.pipelinesCreated);
  ILLUMO_PROFILE_PLOT("VK.CommandQueueHighWater",
                      m_commandQueue->GetHighWaterMark());
  {
    ILLUMO_PROFILE_ZONE("VulkanBackend.present");
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
VulkanBackend::SubmitCommandQueue()
{
  ILLUMO_PROFILE_ZONE("VulkanBackend.SubmitCommandQueue");
  m_device->executeQueue(*m_commandQueue);
}

void
VulkanBackend::PushToCommandQueue(RenderCommand command)
{
  m_commandQueue->Submit(command);
}

void
VulkanBackend::ClearCommandQueue()
{
  m_commandQueue->Reset();
}

size_t
VulkanBackend::rejectedCommandCount() const
{
  return m_commandQueue->GetTotalRejected();
}

size_t
VulkanBackend::commandHighWaterMark() const
{
  return m_commandQueue->GetHighWaterMark();
}

std::string
VulkanBackend::submissionError() const
{
  return m_device->frameError();
}

FrameReadback
VulkanBackend::readBackbuffer(int width, int height)
{
  ILLUMO_PROFILE_ZONE("VulkanBackend.readBackbuffer");
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
VulkanBackend::requestFramebufferReadback(std::uint32_t stream,
                                          FramebufferHandle framebuffer,
                                          int width,
                                          int height)
{
  return m_device->requestFramebufferReadback(
    stream, framebuffer, width, height);
}

bool
VulkanBackend::takeFramebufferReadback(std::uint32_t stream,
                                       bool wait,
                                       FrameReadback& out)
{
  return m_device->takeFramebufferReadback(stream, wait, out);
}

void
VulkanBackend::releaseReadbackStream(std::uint32_t stream)
{
  m_device->releaseReadbackStream(stream);
}

MeshHandle
VulkanBackend::CreateMesh(const void* vertices,
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
VulkanBackend::CreateMesh(const void* vertices,
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
VulkanBackend::ReplaceMesh(MeshHandle handle,
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
VulkanBackend::DestroyMesh(MeshHandle handle)
{
  return m_device->destroyMesh(handle);
}

bool
VulkanBackend::IsMeshValid(MeshHandle handle) const
{
  return m_device->isMeshValid(handle);
}

bool
VulkanBackend::preprocess(const ShaderSources& sources,
                          ShaderSources* output) const
{
  PreprocessOptions vertexOptions;
  vertexOptions.defines = sources.defines;
  const PreprocessResult vertex =
    ShaderPreprocessor::Process(sources.vertexSource, vertexOptions);
  if (!vertex.success) {
    Logger::LogError("VulkanBackend: Vertex preprocessor failed: " +
                     vertex.errorMessage);
    return false;
  }
  PreprocessOptions fragmentOptions;
  fragmentOptions.defines = sources.defines;
  const PreprocessResult fragment =
    ShaderPreprocessor::Process(sources.fragmentSource, fragmentOptions);
  if (!fragment.success) {
    Logger::LogError("VulkanBackend: Fragment preprocessor failed: " +
                     fragment.errorMessage);
    return false;
  }
  output->vertexSource = vertex.source;
  output->fragmentSource = fragment.source;
  output->defines.clear();
  return true;
}

bool
VulkanBackend::preprocess(const ShaderPaths& paths, ShaderSources* output) const
{
  PreprocessOptions vertexOptions;
  vertexOptions.defines = paths.defines;
  vertexOptions.sourcePath = paths.vertexPath;
  const PreprocessResult vertex =
    ShaderPreprocessor::ProcessFile(paths.vertexPath, vertexOptions);
  if (!vertex.success) {
    Logger::LogError("VulkanBackend: Vertex preprocessor failed for " +
                     paths.vertexPath + ": " + vertex.errorMessage);
    return false;
  }
  PreprocessOptions fragmentOptions;
  fragmentOptions.defines = paths.defines;
  fragmentOptions.sourcePath = paths.fragmentPath;
  const PreprocessResult fragment =
    ShaderPreprocessor::ProcessFile(paths.fragmentPath, fragmentOptions);
  if (!fragment.success) {
    Logger::LogError("VulkanBackend: Fragment preprocessor failed for " +
                     paths.fragmentPath + ": " + fragment.errorMessage);
    return false;
  }
  output->vertexSource = vertex.source;
  output->fragmentSource = fragment.source;
  output->defines.clear();
  return true;
}

ShaderHandle
VulkanBackend::CreateShaderProgram(const ShaderPaths& paths)
{
  ILLUMO_PROFILE_ZONE("VulkanBackend.CreateShaderProgram");
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
VulkanBackend::CreateShaderProgram(const ShaderSources& sources)
{
  ILLUMO_PROFILE_ZONE("VulkanBackend.CreateShaderProgram");
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
VulkanBackend::ReplaceShaderProgram(ShaderHandle handle,
                                    const ShaderSources& sources)
{
  ILLUMO_PROFILE_ZONE("VulkanBackend.ReplaceShaderProgram");
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
VulkanBackend::DestroyShaderProgram(ShaderHandle handle)
{
  return m_device->destroyShader(handle);
}

bool
VulkanBackend::IsShaderValid(ShaderHandle handle) const
{
  return m_device->isShaderValid(handle);
}

TextureHandle
VulkanBackend::CreateTexture(const unsigned char* data,
                             const int width,
                             const int height)
{
  return CreateTexture(data, width, height, 4, TextureOptions{});
}

TextureHandle
VulkanBackend::CreateTexture(const unsigned char* data,
                             const int width,
                             const int height,
                             int channels,
                             const TextureOptions& options)
{
  return m_device->createTexture(data, width, height, channels, options);
}

TextureHandle
VulkanBackend::CreateCubemap(
  const std::array<const unsigned char*, 6>& facesData,
  int width,
  int height,
  int channels)
{
  return m_device->createCubemap(facesData, width, height, channels);
}

bool
VulkanBackend::ReplaceTexture(TextureHandle handle,
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
VulkanBackend::ReplaceCubemap(TextureHandle handle,
                              const std::array<const unsigned char*, 6>& faces,
                              int width,
                              int height,
                              int channels)
{
  return m_device->replaceCubemap(handle, faces, width, height, channels);
}

bool
VulkanBackend::DestroyTexture(TextureHandle handle)
{
  return m_device->destroyTexture(handle);
}

bool
VulkanBackend::IsTextureValid(TextureHandle handle) const
{
  return m_device->isTextureValid(handle);
}

TextureInfo
VulkanBackend::GetTextureInfo(TextureHandle handle) const
{
  return m_device->textureInfo(handle);
}

FramebufferHandle
VulkanBackend::CreateFramebuffer(const FramebufferDesc& desc,
                                 FramebufferAttachments* outAttachments)
{
  return m_device->createFramebuffer(desc, outAttachments);
}

FramebufferHandle
VulkanBackend::CreateDepthFramebuffer(int width,
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
VulkanBackend::DestroyFramebuffer(FramebufferHandle handle)
{
  return m_device->destroyFramebuffer(handle);
}

bool
VulkanBackend::IsFramebufferValid(FramebufferHandle handle) const
{
  return m_device->isFramebufferValid(handle);
}

BufferHandle
VulkanBackend::CreateBuffer(BufferUsage usage, size_t capacityBytes)
{
  return m_device->createBuffer(usage, capacityBytes);
}

bool
VulkanBackend::DestroyBuffer(BufferHandle handle)
{
  return m_device->destroyBuffer(handle);
}

bool
VulkanBackend::IsBufferValid(BufferHandle handle) const
{
  return m_device->isBufferValid(handle);
}
