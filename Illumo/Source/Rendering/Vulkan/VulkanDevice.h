#pragma once
#include <Illumo/Rendering/CommandQueue.h>
#include <Illumo/Rendering/RecordedCommandList.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <string>

// Stub device: owns no Vulkan instance, physical/logical device, or
// swapchain yet. ExecuteCommandQueue and executeList do not touch any GPU
// resources; VulkanBackend::Initialize() fails before either can run.
class VulkanDevice
{
public:
  VulkanDevice() = default;

  void resetFrameError() { m_frameError.clear(); }
  const std::string& frameError() const { return m_frameError; }

  void ExecuteCommandQueue(CommandQueue& commandQueue);
  void executeCommand(const RenderCommand& cmd);
  void executeList(const RecordedCommandList* list);

private:
  std::string m_frameError;
};
