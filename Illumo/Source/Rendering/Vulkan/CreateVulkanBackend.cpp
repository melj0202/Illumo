#include "CreateVulkanBackend.h"
#include "Rendering/Gpu/GpuBackend.h"
#include "VulkanDriver.h"

std::unique_ptr<IBackend>
CreateVulkanBackend(IRenderWindow* window, bool present)
{
  return std::make_unique<GpuBackend>(
    std::make_unique<VulkanDriver>(), window, present);
}
