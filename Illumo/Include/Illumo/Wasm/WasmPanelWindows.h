#pragma once

#include <Illumo/Platform/SurfaceWindow.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <IllumoGuest/Input.h>
#include <IllumoGuest/Windows.h>
#include <memory>
#include <string>
#include <vector>

class IRenderWindow;
class Renderer;
class WasmFrameRenderer;

// The host side of guest surface windows (Windows capability, D-E27). It
// validates Window service requests, owns one ISurfaceWindow per open
// surface, adds their input to the guest's input v2, and presents each
// surface: the frame renderer replays its batches into an offscreen target
// of the window's size, the pixels come back through a two-deep asynchronous
// readback (one frame of latency) and the window shows them. A surface is
// replayed only when its revision or window size changes. Product-agnostic:
// it never learns what a surface shows. Main-thread only.
class WasmPanelWindows
{
public:
  static constexpr std::uint32_t kMaximumWindows =
    GuestWindowRequest::MaximumSurfaces;

  WasmPanelWindows(Renderer& renderer,
                   IRenderWindow& mainWindow,
                   ISurfaceWindowFactory& factory);
  ~WasmPanelWindows();
  WasmPanelWindows(const WasmPanelWindows&) = delete;
  WasmPanelWindows& operator=(const WasmPanelWindows&) = delete;
  WasmPanelWindows(WasmPanelWindows&&) = delete;
  WasmPanelWindows& operator=(WasmPanelWindows&&) = delete;

  bool available() const;
  // Applies one validated request. Open fills *opened with the client size.
  // False with *reason when refused.
  bool handle(const GuestWindowRequest& request,
              GuestWindowOpened* opened,
              std::string* reason);
  // Pumps the windows and turns input into version 2: screen origins,
  // surfaces, window events, the focused window and key and character
  // events tagged by window. input already holds the main window's v1 part.
  void collectInput(GuestInput& input);
  // Replays changed surfaces of the last accepted frame and presents
  // completed readbacks. Call between frames (not inside RenderScene).
  void present(WasmFrameRenderer& frames);
  void closeAll();
  std::size_t openCount() const { return m_windows.size(); }
  // Diagnostics: surface replays and presented images so far.
  std::uint64_t replays() const { return m_replays; }
  std::uint64_t presents() const { return m_presents; }

private:
  struct Window;
  Window* find(std::uint32_t surface);
  void release(Window& window);

  Renderer& m_renderer;
  IRenderWindow& m_mainWindow;
  ISurfaceWindowFactory& m_factory;
  std::vector<std::unique_ptr<Window>> m_windows;
  std::uint64_t m_replays = 0;
  std::uint64_t m_presents = 0;
};
