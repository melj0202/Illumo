#include "VulkanBackend.h"
#include "VulkanDevice.h"
#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/Logger.h>

VulkanBackend::VulkanBackend(IRenderWindow* window)
  : device(new VulkanDevice())
  , commandQueue(new CommandQueue())
  , window(window)
{
}

VulkanBackend::~VulkanBackend()
{
  Shutdown();
}

bool
VulkanBackend::Initialize()
{
  Logger::LogError(
    "Vulkan backend is a stub: no instance, device, or swapchain exists yet.");
  return false;
}

void
VulkanBackend::Shutdown()
{
  delete device;
  device = nullptr;
  delete commandQueue;
  commandQueue = nullptr;
}

void
VulkanBackend::BeginFrame()
{
}

void
VulkanBackend::EndFrame()
{
}

void
VulkanBackend::SubmitCommandQueue()
{
  device->ExecuteCommandQueue(*commandQueue);
}

void
VulkanBackend::PushToCommandQueue(RenderCommand command)
{
  commandQueue->Submit(command);
}

void
VulkanBackend::ClearCommandQueue()
{
  commandQueue->Reset();
}

size_t
VulkanBackend::rejectedCommandCount() const
{
  return commandQueue->GetTotalRejected();
}

size_t
VulkanBackend::commandHighWaterMark() const
{
  return commandQueue->GetHighWaterMark();
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
  (void)vertices;
  (void)vertexSize;
  (void)indices;
  (void)indexSize;
  (void)layout;
  (void)dynamic;
  return {};
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
  (void)handle;
  (void)vertices;
  (void)vertexSize;
  (void)indices;
  (void)indexSize;
  (void)layout;
  (void)dynamic;
  return false;
}

bool
VulkanBackend::DestroyMesh(MeshHandle handle)
{
  (void)handle;
  return false;
}

bool
VulkanBackend::IsMeshValid(MeshHandle handle) const
{
  (void)handle;
  return false;
}

ShaderHandle
VulkanBackend::CreateShaderProgram(const ShaderPaths& paths)
{
  (void)paths;
  return {};
}

ShaderHandle
VulkanBackend::CreateShaderProgram(const ShaderSources& sources)
{
  (void)sources;
  return {};
}

bool
VulkanBackend::ReplaceShaderProgram(ShaderHandle handle,
                                    const ShaderSources& sources)
{
  (void)handle;
  (void)sources;
  return false;
}

bool
VulkanBackend::DestroyShaderProgram(ShaderHandle handle)
{
  (void)handle;
  return false;
}

bool
VulkanBackend::IsShaderValid(ShaderHandle handle) const
{
  (void)handle;
  return false;
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
  (void)data;
  (void)width;
  (void)height;
  (void)channels;
  (void)options;
  return {};
}

TextureHandle
VulkanBackend::CreateCubemap(
  const std::array<const unsigned char*, 6>& facesData,
  int width,
  int height,
  int channels)
{
  (void)facesData;
  (void)width;
  (void)height;
  (void)channels;
  return {};
}

bool
VulkanBackend::ReplaceTexture(TextureHandle handle,
                              const unsigned char* data,
                              int width,
                              int height,
                              int channels,
                              const TextureOptions& options)
{
  (void)handle;
  (void)data;
  (void)width;
  (void)height;
  (void)channels;
  (void)options;
  return false;
}

bool
VulkanBackend::DestroyTexture(TextureHandle handle)
{
  (void)handle;
  return false;
}

bool
VulkanBackend::IsTextureValid(TextureHandle handle) const
{
  (void)handle;
  return false;
}

TextureInfo
VulkanBackend::GetTextureInfo(TextureHandle handle) const
{
  (void)handle;
  return {};
}

FramebufferHandle
VulkanBackend::CreateFramebuffer(const FramebufferDesc& desc,
                                 FramebufferAttachments* outAttachments)
{
  (void)desc;
  (void)outAttachments;
  return {};
}

FramebufferHandle
VulkanBackend::CreateDepthFramebuffer(int width,
                                      int height,
                                      TextureHandle* outDepthTexture)
{
  (void)width;
  (void)height;
  (void)outDepthTexture;
  return {};
}

bool
VulkanBackend::DestroyFramebuffer(FramebufferHandle handle)
{
  (void)handle;
  return false;
}

bool
VulkanBackend::IsFramebufferValid(FramebufferHandle handle) const
{
  (void)handle;
  return false;
}
