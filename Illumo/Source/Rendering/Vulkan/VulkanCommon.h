#pragma once

// The only header that includes the Vulkan API. Vulkan types stay inside
// Source/Rendering/Vulkan; the rest of the engine sees IBackend.
#include <vk_mem_alloc.h>
#include <volk.h>

#include <string>

std::string
vulkanResultText(VkResult result);
