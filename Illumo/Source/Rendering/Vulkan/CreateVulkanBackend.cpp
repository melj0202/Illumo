#include "CreateVulkanBackend.h"
#include "Rendering/Gpu/GpuBackend.h"
#include "VulkanDevice.h"

std::unique_ptr<IBackend>
CreateVulkanBackend(IRenderWindow* window, bool present)
{
  return std::make_unique<GpuBackend>(
    std::make_unique<VulkanDevice>(), window, present);
}
