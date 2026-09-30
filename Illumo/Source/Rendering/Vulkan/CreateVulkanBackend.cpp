#include "CreateVulkanBackend.h"
#include "VulkanBackend.h"

std::unique_ptr<IBackend>
CreateVulkanBackend(IRenderWindow* window)
{
  return std::make_unique<VulkanBackend>(window);
}
