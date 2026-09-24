#pragma once

#include <Illumo/Gui/PanelSurfaces.h>
#include <Illumo/Rendering/Scene.h>
#include <map>
#include <memory>

// Deterministic IPanelSurfaces for native product tests. open() answers on
// the next step() (as a real host does, one exchange later); tests move
// pointers, change focus, resize and ask to close windows by hand.
class FakePanelSurfaces final : public IPanelSurfaces
{
public:
  struct Window
  {
    std::string title;
    PanelSurfaceState state = PanelSurfaceState::Opening;
    std::array<int, 2> size{ 0, 0 };
    std::array<int, 2> origin{ 0, 0 };
    PanelSurfacePointer pointer;
    std::unique_ptr<Scene> scene;
    int requestedX = 0;
    int requestedY = 0;
  };

  FakePanelSurfaces(IRenderWindow* window, Camera* camera)
    : m_window(window)
    , m_camera(camera)
  {
  }

  bool supported = true;
  // The next open is refused by the "host".
  bool refuseNextOpen = false;
  std::array<int, 2> mainSize{ 1280, 720 };
  std::array<int, 2> mainOrigin{ 100, 50 };
  PanelSurfacePointer mainPointer;
  std::uint32_t focusedWindow = kMainSurface;

  // Completes pending opens: Opened (or Failed) events for each.
  void step()
  {
    for (std::pair<const std::uint32_t, Window>& entry : m_windows) {
      Window& window = entry.second;
      if (window.state != PanelSurfaceState::Opening) {
        continue;
      }
      if (refuseNextOpen) {
        refuseNextOpen = false;
        window.state = PanelSurfaceState::Failed;
        m_events.push_back(
          { PanelSurfaceEvent::Kind::Failed, entry.first, 0, 0 });
        continue;
      }
      window.state = PanelSurfaceState::Open;
      window.origin = { mainOrigin[0] + window.requestedX,
                        mainOrigin[1] + window.requestedY };
      m_events.push_back({ PanelSurfaceEvent::Kind::Opened,
                           entry.first,
                           window.size[0],
                           window.size[1] });
    }
  }
  Window* window(std::uint32_t surface)
  {
    std::map<std::uint32_t, Window>::iterator found = m_windows.find(surface);
    return found == m_windows.end() ? nullptr : &found->second;
  }
  std::size_t openCount() const
  {
    std::size_t count = 0;
    for (const std::pair<const std::uint32_t, Window>& entry : m_windows) {
      count += entry.second.state == PanelSurfaceState::Open ? 1u : 0u;
    }
    return count;
  }
  void userClose(std::uint32_t surface)
  {
    m_events.push_back(
      { PanelSurfaceEvent::Kind::CloseRequested, surface, 0, 0 });
  }
  void resize(std::uint32_t surface, int width, int height)
  {
    Window* found = window(surface);
    if (found != nullptr) {
      found->size = { width, height };
      m_events.push_back(
        { PanelSurfaceEvent::Kind::Resized, surface, width, height });
    }
  }
  void move(std::uint32_t surface, int x, int y)
  {
    Window* found = window(surface);
    if (found != nullptr) {
      found->origin = { x, y };
      m_events.push_back({ PanelSurfaceEvent::Kind::Moved, surface, x, y });
    }
  }
  void clearScenes()
  {
    for (std::pair<const std::uint32_t, Window>& entry : m_windows) {
      entry.second.scene->ClearDrawables();
    }
  }

  bool available() const override { return supported; }
  bool open(std::uint32_t surface,
            const std::string& title,
            int x,
            int y,
            int width,
            int height) override
  {
    Window* existing = window(surface);
    if (!supported || surface == kMainSurface || surface > kMaximumSurfaceId ||
        (existing != nullptr && existing->state != PanelSurfaceState::Failed) ||
        (existing == nullptr && m_windows.size() >= 8)) {
      return false;
    }
    Window& created = m_windows[surface];
    created.title = title;
    created.state = PanelSurfaceState::Opening;
    created.size = { width, height };
    created.requestedX = x;
    created.requestedY = y;
    if (!created.scene) {
      created.scene = std::make_unique<Scene>(m_window, m_camera);
    }
    return true;
  }
  void close(std::uint32_t surface) override { m_windows.erase(surface); }
  void setTitle(std::uint32_t surface, const std::string& title) override
  {
    Window* found = window(surface);
    if (found != nullptr) {
      found->title = title;
    }
  }
  PanelSurfaceState state(std::uint32_t surface) const override
  {
    if (surface == kMainSurface) {
      return PanelSurfaceState::Open;
    }
    std::map<std::uint32_t, Window>::const_iterator found =
      m_windows.find(surface);
    return found == m_windows.end() ? PanelSurfaceState::Closed
                                    : found->second.state;
  }
  std::array<int, 2> size(std::uint32_t surface) const override
  {
    if (surface == kMainSurface) {
      return mainSize;
    }
    std::map<std::uint32_t, Window>::const_iterator found =
      m_windows.find(surface);
    return found == m_windows.end() ? std::array<int, 2>{ 0, 0 }
                                    : found->second.size;
  }
  std::array<int, 2> origin(std::uint32_t surface) const override
  {
    if (surface == kMainSurface) {
      return mainOrigin;
    }
    std::map<std::uint32_t, Window>::const_iterator found =
      m_windows.find(surface);
    return found == m_windows.end() ? std::array<int, 2>{ 0, 0 }
                                    : found->second.origin;
  }
  PanelSurfacePointer pointer(std::uint32_t surface) const override
  {
    if (surface == kMainSurface) {
      return mainPointer;
    }
    std::map<std::uint32_t, Window>::const_iterator found =
      m_windows.find(surface);
    return found == m_windows.end() ? PanelSurfacePointer{}
                                    : found->second.pointer;
  }
  std::uint32_t focused() const override { return focusedWindow; }
  Scene* scene(std::uint32_t surface) override
  {
    Window* found = window(surface);
    return found != nullptr && found->state == PanelSurfaceState::Open
             ? found->scene.get()
             : nullptr;
  }
  std::vector<PanelSurfaceEvent> takeEvents() override
  {
    std::vector<PanelSurfaceEvent> events;
    events.swap(m_events);
    return events;
  }

private:
  IRenderWindow* m_window;
  Camera* m_camera;
  std::map<std::uint32_t, Window> m_windows;
  std::vector<PanelSurfaceEvent> m_events;
};
