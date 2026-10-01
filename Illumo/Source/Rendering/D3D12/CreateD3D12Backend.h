#pragma once

#include <memory>

class IBackend;
class IRenderWindow;

// Constructs the Direct3D 12 backend. Illumo owns its one Initialize call, as
// for CreateOpenGLBackend. present=false renders offscreen only (capture
// windows and GPU tests); the window then only supplies the backbuffer size.
std::unique_ptr<IBackend>
CreateD3D12Backend(IRenderWindow* window, bool present = true);
