#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class Scene;

enum class PanelSurfaceState
{
  Closed,
  Opening,
  Open,
  Failed
};

// One window's pointer in its own client coordinates. While a button is held
// the position may lie outside the client area (the drag keeps its window).
struct PanelSurfacePointer
{
  double x = 0.0;
  double y = 0.0;
  bool left = false;
  bool right = false;
  bool middle = false;
  double scroll = 0.0;
};

struct PanelSurfaceEvent
{
  enum class Kind
  {
    Opened,
    Failed,
    CloseRequested, // the user closed the window; the product decides
    Resized,        // x, y: the new client size
    Moved,          // x, y: the new client origin in screen coordinates
    FocusGained,
    FocusLost
  };
  Kind kind = Kind::Opened;
  std::uint32_t surface = 0;
  int x = 0;
  int y = 0;
};

// Extra top-level windows that show product-drawn surfaces (detached tool
// panels, D-E27). Surface 0 is the main window: its size, origin and pointer
// are reported for uniform code, and it is never opened or closed. A product
// opens a surface, draws into its scene each frame while it is Open, and
// reads that window's pointer. Key and character input stays in the one
// InputManager queue; focused() says which window it came from.
// Published as IllumoContext::panelSurfaces; absent where no host can show
// extra windows, so products must work fully docked.
class IPanelSurfaces
{
public:
  static constexpr std::uint32_t kMainSurface = 0;
  static constexpr std::uint32_t kMaximumSurfaceId = 0x7fffffffu;

  IPanelSurfaces() = default;
  virtual ~IPanelSurfaces() = default;
  IPanelSurfaces(const IPanelSurfaces&) = delete;
  IPanelSurfaces& operator=(const IPanelSurfaces&) = delete;
  IPanelSurfaces(IPanelSurfaces&&) = delete;
  IPanelSurfaces& operator=(IPanelSurfaces&&) = delete;

  virtual bool available() const = 0;
  // Asks for a window: x, y relative to the main window's client origin,
  // client size width x height, all in pixels. The answer arrives later as
  // an Opened or Failed event. False when refused at once.
  virtual bool open(std::uint32_t surface,
                    const std::string& title,
                    int x,
                    int y,
                    int width,
                    int height) = 0;
  virtual void close(std::uint32_t surface) = 0;
  virtual void setTitle(std::uint32_t surface, const std::string& title) = 0;
  virtual PanelSurfaceState state(std::uint32_t surface) const = 0;
  // Client size in pixels.
  virtual std::array<int, 2> size(std::uint32_t surface) const = 0;
  // Client origin in screen coordinates.
  virtual std::array<int, 2> origin(std::uint32_t surface) const = 0;
  virtual PanelSurfacePointer pointer(std::uint32_t surface) const = 0;
  // The window with keyboard focus (kMainSurface when none of the others).
  virtual std::uint32_t focused() const = 0;
  // Drawables for an Open surface this frame; null otherwise.
  virtual Scene* scene(std::uint32_t surface) = 0;
  virtual std::vector<PanelSurfaceEvent> takeEvents() = 0;
};
