#pragma once

#include <memory>

class IBackend;
class IRenderWindow;

// Constructs the Vulkan backend. Illumo owns its one Initialize call, as for
// CreateOpenGLBackend. present=false renders offscreen only (capture windows
// and GPU tests); the window then only supplies the backbuffer size.
std::unique_ptr<IBackend>
CreateVulkanBackend(IRenderWindow* window, bool present = true);
