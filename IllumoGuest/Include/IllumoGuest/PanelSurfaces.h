#pragma once

#include <Illumo/Gui/PanelSurfaces.h>
#include <IllumoGuest/Frame.h>
#include <IllumoGuest/Input.h>
#include <IllumoGuest/Services.h>
#include <IllumoGuest/Windows.h>
#include <memory>
#include <string>
#include <vector>

class Camera;
class GuestRecordingBackend;
class GuestSnapshotWindow;
class IRenderWindow;
class Renderer;
class Scene;

// IPanelSurfaces inside a guest (Windows capability): Open, Close and
// SetTitle travel as Window service requests; sizes, pointers, focus and
// window events arrive in input v2; each Open surface's scene records into
// the frame's surfaces section (frame schema v5). A surface whose recorded
// batches, and the textures and meshes they read, did not change is sent as
// `same` so the host neither replays nor presents it again.
class GuestPanelSurfaces final : public IPanelSurfaces
{
public:
  static constexpr std::uint32_t kMaximumSurfaces = GuestFrame::MaximumSurfaces;

  GuestPanelSurfaces(GuestServiceQueue& services,
                     IRenderWindow& window,
                     Camera& camera);
  ~GuestPanelSurfaces() override;
  GuestPanelSurfaces(const GuestPanelSurfaces&) = delete;
  GuestPanelSurfaces& operator=(const GuestPanelSurfaces&) = delete;
  GuestPanelSurfaces(GuestPanelSurfaces&&) = delete;
  GuestPanelSurfaces& operator=(GuestPanelSurfaces&&) = delete;

  // Whether the host granted the Windows capability.
  void setGranted(bool granted) { m_granted = granted; }
  // Before modules update: the main window and surfaces from input.
  void accept(const GuestInput& input);
  // Service completions (opens, closes, titles).
  void pump();
  // Before modules dispatch: empties every surface scene.
  void clearScenes();
  // After the main scene recorded: records every Open surface's scene.
  void record(Renderer& renderer,
              GuestRecordingBackend& backend,
              GuestSnapshotWindow& window);
  // Once the frame will be delivered: marks unchanged surfaces `same` and
  // advances the revisions of changed ones.
  void finish(GuestFrame& frame);

  bool available() const override { return m_granted; }
  bool open(std::uint32_t surface,
            const std::string& title,
            int x,
            int y,
            int width,
            int height) override;
  void close(std::uint32_t surface) override;
  void setTitle(std::uint32_t surface, const std::string& title) override;
  PanelSurfaceState state(std::uint32_t surface) const override;
  std::array<int, 2> size(std::uint32_t surface) const override;
  std::array<int, 2> origin(std::uint32_t surface) const override;
  PanelSurfacePointer pointer(std::uint32_t surface) const override;
  std::uint32_t focused() const override { return m_focused; }
  Scene* scene(std::uint32_t surface) override;
  std::vector<PanelSurfaceEvent> takeEvents() override;

private:
  struct Surface
  {
    std::uint32_t id = 0;
    PanelSurfaceState state = PanelSurfaceState::Closed;
    std::uint64_t openRequest = 0;
    bool closeAfterOpen = false;
    std::array<int, 2> size{ 0, 0 };
    std::array<int, 2> origin{ 0, 0 };
    PanelSurfacePointer pointer;
    std::unique_ptr<Scene> scene;
    std::uint64_t revision = 0;
    std::vector<std::byte> recorded;
  };
  Surface* find(std::uint32_t surface);
  const Surface* find(std::uint32_t surface) const;
  void remove(std::uint32_t surface);
  void enqueue(const GuestWindowRequest& request);
  static std::string sanitizedTitle(const std::string& title);

  GuestServiceQueue& m_services;
  IRenderWindow& m_window;
  Camera& m_camera;
  bool m_granted = false;
  std::vector<std::unique_ptr<Surface>> m_surfaces;
  // Close and SetTitle completions are only drained.
  std::vector<std::uint64_t> m_drain;
  std::vector<PanelSurfaceEvent> m_events;
  std::uint32_t m_focused = kMainSurface;
  std::array<int, 2> m_mainSize{ 1280, 720 };
  std::array<int, 2> m_mainOrigin{ 0, 0 };
  PanelSurfacePointer m_mainPointer;
};
