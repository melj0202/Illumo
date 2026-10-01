#include "CreateVulkanBackend.h"
#include "VulkanBackend.h"

std::unique_ptr<IBackend>
CreateVulkanBackend(IRenderWindow* window, bool present)
{
  return std::make_unique<VulkanBackend>(window, present);
}
