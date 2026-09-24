#include "WasmInputMapping.h"
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/Logger.h>
#include <Illumo/Wasm/WasmFrameRenderer.h>
#include <Illumo/Wasm/WasmPanelWindows.h>
#include <algorithm>

// Tool panels draw their own background; this only shows through gaps.
static const std::array<float, 4> kSurfaceClear{ 0.11f, 0.12f, 0.14f, 1.0f };

// Readback streams of panel windows: the high bit keeps them apart from any
// other stream user; surface ids fit in the low 31 bits.
static std::uint32_t
readbackStream(std::uint32_t surface)
{
  return 0x80000000u | surface;
}

struct WasmPanelWindows::Window
{
  std::uint32_t surface = 0;
  std::unique_ptr<ISurfaceWindow> window;
  FramebufferHandle target{};
  int targetWidth = 0;
  int targetHeight = 0;
  std::uint64_t replayedRevision = 0;
  int replayedWidth = 0;
  int replayedHeight = 0;
  std::size_t pendingReadbacks = 0;
  std::vector<std::uint8_t> pixels;
  int pixelWidth = 0;
  int pixelHeight = 0;
  // Input state carried between frames.
  std::uint32_t buttons = 0;
  double scroll = 0.0;
  bool closeReported = false;
  int reportedWidth = 0;
  int reportedHeight = 0;
  int reportedX = 0;
  int reportedY = 0;
  bool reportedFocus = false;
};

WasmPanelWindows::WasmPanelWindows(Renderer& renderer,
                                   IRenderWindow& mainWindow,
                                   ISurfaceWindowFactory& factory)
  : m_renderer(renderer)
  , m_mainWindow(mainWindow)
  , m_factory(factory)
{
}

WasmPanelWindows::~WasmPanelWindows()
{
  closeAll();
}

bool
WasmPanelWindows::available() const
{
  return m_factory.available();
}

WasmPanelWindows::Window*
WasmPanelWindows::find(std::uint32_t surface)
{
  for (const std::unique_ptr<Window>& window : m_windows) {
    if (window->surface == surface) {
      return window.get();
    }
  }
  return nullptr;
}

void
WasmPanelWindows::release(Window& window)
{
  IBackend* backend = m_renderer.getBackend();
  if (backend != nullptr) {
    backend->releaseReadbackStream(readbackStream(window.surface));
    if (window.target.isValid()) {
      backend->DestroyFramebuffer(window.target);
    }
  }
  window.target = FramebufferHandle{};
  window.window.reset();
}

void
WasmPanelWindows::closeAll()
{
  for (const std::unique_ptr<Window>& window : m_windows) {
    release(*window);
  }
  m_windows.clear();
}

// Guest window requests are answered with a bare rejection; the reason only
// reaches the host log.
static bool
refuseWindow(const GuestWindowRequest& request,
             std::string* reason,
             const std::string& text)
{
  *reason = text;
  const std::string message = "Panel window request for surface " +
                              std::to_string(request.surface) +
                              " refused: " + text;
  if (request.action == GuestWindowAction::Open) {
    Logger::LogWarning(message);
  } else {
    Logger::LogTrace(message);
  }
  return false;
}

bool
WasmPanelWindows::handle(const GuestWindowRequest& request,
                         GuestWindowOpened* opened,
                         std::string* reason)
{
  Window* existing = find(request.surface);
  if (request.action == GuestWindowAction::Close) {
    if (existing == nullptr) {
      return refuseWindow(request, reason, "No such window");
    }
    release(*existing);
    for (std::size_t index = 0; index < m_windows.size(); ++index) {
      if (m_windows[index].get() == existing) {
        m_windows.erase(m_windows.begin() + static_cast<std::ptrdiff_t>(index));
        break;
      }
    }
    Logger::LogTrace("Closed the panel window for surface " +
                     std::to_string(request.surface) + " (" +
                     std::to_string(m_windows.size()) + " still open)");
    return true;
  }
  if (request.action == GuestWindowAction::SetTitle) {
    if (existing == nullptr) {
      return refuseWindow(request, reason, "No such window");
    }
    existing->window->setTitle(request.title);
    return true;
  }
  if (!m_factory.available()) {
    return refuseWindow(
      request, reason, "Windows are unavailable on this host");
  }
  if (existing != nullptr) {
    return refuseWindow(request, reason, "The window is already open");
  }
  if (m_windows.size() >= kMaximumWindows) {
    return refuseWindow(request, reason, "Too many windows are open");
  }
  int originX = 0;
  int originY = 0;
  m_factory.mainOrigin(m_mainWindow, &originX, &originY);
  std::string error;
  std::unique_ptr<ISurfaceWindow> created =
    m_factory.create(request.title,
                     originX + request.x,
                     originY + request.y,
                     static_cast<int>(request.width),
                     static_cast<int>(request.height),
                     static_cast<int>(GuestWindowRequest::MinimumWidth),
                     static_cast<int>(GuestWindowRequest::MinimumHeight),
                     &error);
  if (!created) {
    return refuseWindow(
      request,
      reason,
      error.empty() ? std::string("The window could not be created") : error);
  }
  std::unique_ptr<Window> window = std::make_unique<Window>();
  window->surface = request.surface;
  window->window = std::move(created);
  int width = 0;
  int height = 0;
  window->window->clientSize(&width, &height);
  window->window->clientOrigin(&window->reportedX, &window->reportedY);
  window->reportedWidth = width;
  window->reportedHeight = height;
  window->reportedFocus = window->window->focused();
  opened->width = static_cast<std::uint32_t>(
    std::clamp(width, 1, static_cast<int>(GuestWindowRequest::MaximumSize)));
  opened->height = static_cast<std::uint32_t>(
    std::clamp(height, 1, static_cast<int>(GuestWindowRequest::MaximumSize)));
  m_windows.push_back(std::move(window));
  Logger::LogTrace("Opened panel window '" + request.title + "' for surface " +
                   std::to_string(request.surface) + " at " +
                   std::to_string(width) + "x" + std::to_string(height));
  return true;
}

void
WasmPanelWindows::collectInput(GuestInput& input)
{
  input.version = 2;
  input.eventSurfaces.assign(input.events.size(), 0u);
  input.characterSurfaces.assign(input.characters.size(), 0u);
  m_factory.mainOrigin(m_mainWindow, &input.originX, &input.originY);
  input.focusedSurface = 0;
  input.surfaces.clear();
  input.windowEvents.clear();
  for (const std::unique_ptr<Window>& window : m_windows) {
    ISurfaceWindow& os = *window->window;
    for (const SurfaceWindowEvent& event : os.takeEvents()) {
      if (event.kind == SurfaceWindowEvent::Kind::Key) {
        const GuestKey key = wasmGuestKey(event.key);
        if (key != GuestKey::Count &&
            input.events.size() < GuestInput::MaximumEvents) {
          input.events.push_back(
            { key,
              wasmGuestAction(event.action),
              static_cast<std::uint32_t>(event.modifiers) & 15u });
          input.eventSurfaces.push_back(window->surface);
        }
      } else if (event.kind == SurfaceWindowEvent::Kind::Character) {
        if (event.codepoint <= 0x10ffffu &&
            !(event.codepoint >= 0xd800u && event.codepoint <= 0xdfffu) &&
            input.characters.size() < GuestInput::MaximumEvents) {
          input.characters.push_back(event.codepoint);
          input.characterSurfaces.push_back(window->surface);
        }
      } else if (event.kind == SurfaceWindowEvent::Kind::MouseButton) {
        const std::uint32_t bit =
          event.key == KeyCode::MouseLeft    ? GuestSurfaceInput::LeftButton
          : event.key == KeyCode::MouseRight ? GuestSurfaceInput::RightButton
                                             : GuestSurfaceInput::MiddleButton;
        if (event.action == InputAction::Release) {
          window->buttons &= ~bit;
        } else {
          window->buttons |= bit;
        }
      } else if (event.kind == SurfaceWindowEvent::Kind::Scroll) {
        window->scroll += event.scroll;
      }
    }
    int width = 0;
    int height = 0;
    os.clientSize(&width, &height);
    int x = 0;
    int y = 0;
    os.clientOrigin(&x, &y);
    const bool focused = os.focused();
    if (os.closeRequested() && !window->closeReported &&
        input.windowEvents.size() < GuestInput::MaximumWindowEvents) {
      window->closeReported = true;
      input.windowEvents.push_back(
        { GuestWindowEventKind::Closed, window->surface, 0, 0 });
    }
    // A minimized window reports no size; keep the last one.
    if (width > 0 && height > 0 &&
        (width != window->reportedWidth || height != window->reportedHeight) &&
        input.windowEvents.size() < GuestInput::MaximumWindowEvents) {
      window->reportedWidth = width;
      window->reportedHeight = height;
      input.windowEvents.push_back(
        { GuestWindowEventKind::Resized, window->surface, width, height });
    }
    if ((x != window->reportedX || y != window->reportedY) &&
        input.windowEvents.size() < GuestInput::MaximumWindowEvents) {
      window->reportedX = x;
      window->reportedY = y;
      input.windowEvents.push_back(
        { GuestWindowEventKind::Moved, window->surface, x, y });
    }
    if (focused != window->reportedFocus &&
        input.windowEvents.size() < GuestInput::MaximumWindowEvents) {
      window->reportedFocus = focused;
      input.windowEvents.push_back({ focused ? GuestWindowEventKind::FocusGained
                                             : GuestWindowEventKind::FocusLost,
                                     window->surface,
                                     0,
                                     0 });
    }
    GuestSurfaceInput surface;
    surface.surface = window->surface;
    surface.width =
      static_cast<std::uint32_t>(std::clamp(window->reportedWidth, 1, 65536));
    surface.height =
      static_cast<std::uint32_t>(std::clamp(window->reportedHeight, 1, 65536));
    surface.originX = window->reportedX;
    surface.originY = window->reportedY;
    os.cursor(&surface.mouseX, &surface.mouseY);
    surface.buttons = window->buttons;
    surface.scroll = window->scroll;
    surface.focused = focused;
    window->scroll = 0.0;
    input.surfaces.push_back(surface);
    if (focused) {
      input.focusedSurface = window->surface;
    }
  }
}

void
WasmPanelWindows::present(WasmFrameRenderer& frames)
{
  IBackend* backend = m_renderer.getBackend();
  if (backend == nullptr) {
    return;
  }
  const std::vector<WasmSurfaceContent> contents = frames.surfaces();
  for (const std::unique_ptr<Window>& window : m_windows) {
    int width = 0;
    int height = 0;
    window->window->clientSize(&width, &height);
    const WasmSurfaceContent* content = nullptr;
    for (const WasmSurfaceContent& candidate : contents) {
      if (candidate.surface == window->surface) {
        content = &candidate;
      }
    }
    // Minimized windows, and surfaces the guest has not drawn yet, wait.
    if (content != nullptr && width > 0 && height > 0 &&
        (content->revision != window->replayedRevision ||
         width != window->replayedWidth || height != window->replayedHeight)) {
      if (!window->target.isValid() || width != window->targetWidth ||
          height != window->targetHeight) {
        if (window->target.isValid()) {
          backend->releaseReadbackStream(readbackStream(window->surface));
          backend->DestroyFramebuffer(window->target);
          window->pendingReadbacks = 0;
        }
        FramebufferDesc desc;
        desc.width = width;
        desc.height = height;
        desc.colorAttachments.push_back(FramebufferAttachmentDesc{});
        window->target = backend->CreateFramebuffer(desc);
        window->targetWidth = window->target.isValid() ? width : 0;
        window->targetHeight = window->target.isValid() ? height : 0;
      }
      DrawableBase* drawable = frames.surfaceDrawable(window->surface);
      if (window->target.isValid() && drawable != nullptr &&
          window->pendingReadbacks < 2 &&
          m_renderer.renderOffscreen(
            window->target, width, height, { drawable }, kSurfaceClear, 1.0f) &&
          backend->requestFramebufferReadback(
            readbackStream(window->surface), window->target, width, height)) {
        ++window->pendingReadbacks;
        ++m_replays;
        window->replayedRevision = content->revision;
        window->replayedWidth = width;
        window->replayedHeight = height;
      }
    }
    // Present the newest completed copy; older ones are superseded.
    bool fresh = false;
    FrameReadback readback;
    while (window->pendingReadbacks > 0 &&
           backend->takeFramebufferReadback(
             readbackStream(window->surface), false, readback)) {
      --window->pendingReadbacks;
      window->pixels = std::move(readback.pixels);
      window->pixelWidth = readback.width;
      window->pixelHeight = readback.height;
      fresh = true;
    }
    const bool repaint = window->window->takeRepaintRequest();
    // A copy of an older size cannot be shown in the resized window.
    if ((fresh || repaint) && !window->pixels.empty() &&
        window->pixelWidth == width && window->pixelHeight == height &&
        window->window->present(
          window->pixels, window->pixelWidth, window->pixelHeight)) {
      ++m_presents;
    }
  }
}
