#pragma once

#include <memory>

class IBackend;
class IRenderWindow;

// Constructs the Vulkan backend stub. Not yet composed into production
// backend selection (Engine/Illumo.cpp); the returned backend's Initialize()
// reports failure until a real instance/device/swapchain exists.
std::unique_ptr<IBackend>
CreateVulkanBackend(IRenderWindow* window);
