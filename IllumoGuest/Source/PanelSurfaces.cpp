#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <IllumoGuest/PanelSurfaces.h>
#include <IllumoGuest/RecordingBackend.h>
#include <IllumoGuest/SnapshotWindow.h>
#include <algorithm>
#include <stdexcept>

GuestPanelSurfaces::GuestPanelSurfaces(GuestServiceQueue& services,
                                       IRenderWindow& window,
                                       Camera& camera)
  : m_services(services)
  , m_window(window)
  , m_camera(camera)
{
}

GuestPanelSurfaces::~GuestPanelSurfaces() = default;

GuestPanelSurfaces::Surface*
GuestPanelSurfaces::find(std::uint32_t surface)
{
  for (const std::unique_ptr<Surface>& entry : m_surfaces) {
    if (entry->id == surface) {
      return entry.get();
    }
  }
  return nullptr;
}

const GuestPanelSurfaces::Surface*
GuestPanelSurfaces::find(std::uint32_t surface) const
{
  for (const std::unique_ptr<Surface>& entry : m_surfaces) {
    if (entry->id == surface) {
      return entry.get();
    }
  }
  return nullptr;
}

void
GuestPanelSurfaces::remove(std::uint32_t surface)
{
  for (std::size_t index = 0; index < m_surfaces.size(); ++index) {
    if (m_surfaces[index]->id == surface) {
      m_surfaces.erase(m_surfaces.begin() + static_cast<std::ptrdiff_t>(index));
      return;
    }
  }
}

std::string
GuestPanelSurfaces::sanitizedTitle(const std::string& title)
{
  std::string result;
  for (char character : title) {
    const unsigned char value = static_cast<unsigned char>(character);
    result.push_back(value < 0x20u || value == 0x7fu ? ' ' : character);
  }
  if (result.size() > GuestWindowRequest::MaximumTitleBytes) {
    result.resize(GuestWindowRequest::MaximumTitleBytes);
  }
  // Truncation may split a UTF-8 sequence; drop a partial tail.
  while (!result.empty() && !guestUtf8(result)) {
    result.pop_back();
  }
  return result;
}

void
GuestPanelSurfaces::enqueue(const GuestWindowRequest& request)
{
  GuestWireWriter bytes;
  request.write(bytes);
  const std::uint64_t id =
    m_services.enqueue(GuestService::Window, bytes.take());
  if (id != 0) {
    m_drain.push_back(id);
  }
}

bool
GuestPanelSurfaces::open(std::uint32_t surface,
                         const std::string& title,
                         int x,
                         int y,
                         int width,
                         int height)
{
  Surface* existing = find(surface);
  if (!m_granted || surface == kMainSurface || surface > kMaximumSurfaceId ||
      (existing != nullptr && existing->state != PanelSurfaceState::Failed)) {
    return false;
  }
  if (existing == nullptr && m_surfaces.size() >= kMaximumSurfaces) {
    return false;
  }
  GuestWindowRequest request;
  request.action = GuestWindowAction::Open;
  request.surface = surface;
  request.title = sanitizedTitle(title);
  request.x = std::clamp(
    x, -GuestWindowRequest::MaximumOffset, GuestWindowRequest::MaximumOffset);
  request.y = std::clamp(
    y, -GuestWindowRequest::MaximumOffset, GuestWindowRequest::MaximumOffset);
  request.width = static_cast<std::uint32_t>(
    std::clamp(width,
               static_cast<int>(GuestWindowRequest::MinimumWidth),
               static_cast<int>(GuestWindowRequest::MaximumSize)));
  request.height = static_cast<std::uint32_t>(
    std::clamp(height,
               static_cast<int>(GuestWindowRequest::MinimumHeight),
               static_cast<int>(GuestWindowRequest::MaximumSize)));
  GuestWireWriter bytes;
  request.write(bytes);
  const std::uint64_t id =
    m_services.enqueue(GuestService::Window, bytes.take());
  if (id == 0) {
    return false;
  }
  if (existing == nullptr) {
    std::unique_ptr<Surface> created = std::make_unique<Surface>();
    created->id = surface;
    created->scene = std::make_unique<Scene>(&m_window, &m_camera);
    existing = created.get();
    m_surfaces.push_back(std::move(created));
  }
  existing->state = PanelSurfaceState::Opening;
  existing->openRequest = id;
  existing->closeAfterOpen = false;
  existing->size = { static_cast<int>(request.width),
                     static_cast<int>(request.height) };
  existing->revision = 0;
  existing->recorded.clear();
  return true;
}

void
GuestPanelSurfaces::close(std::uint32_t surface)
{
  Surface* existing = find(surface);
  if (existing == nullptr) {
    return;
  }
  if (existing->state == PanelSurfaceState::Opening) {
    existing->closeAfterOpen = true;
    return;
  }
  if (existing->state == PanelSurfaceState::Open) {
    GuestWindowRequest request;
    request.action = GuestWindowAction::Close;
    request.surface = surface;
    enqueue(request);
  }
  remove(surface);
}

void
GuestPanelSurfaces::setTitle(std::uint32_t surface, const std::string& title)
{
  const Surface* existing = find(surface);
  if (existing == nullptr || existing->state != PanelSurfaceState::Open) {
    return;
  }
  GuestWindowRequest request;
  request.action = GuestWindowAction::SetTitle;
  request.surface = surface;
  request.title = sanitizedTitle(title);
  enqueue(request);
}

PanelSurfaceState
GuestPanelSurfaces::state(std::uint32_t surface) const
{
  if (surface == kMainSurface) {
    return PanelSurfaceState::Open;
  }
  const Surface* existing = find(surface);
  return existing != nullptr ? existing->state : PanelSurfaceState::Closed;
}

std::array<int, 2>
GuestPanelSurfaces::size(std::uint32_t surface) const
{
  if (surface == kMainSurface) {
    return m_mainSize;
  }
  const Surface* existing = find(surface);
  return existing != nullptr ? existing->size : std::array<int, 2>{ 0, 0 };
}

std::array<int, 2>
GuestPanelSurfaces::origin(std::uint32_t surface) const
{
  if (surface == kMainSurface) {
    return m_mainOrigin;
  }
  const Surface* existing = find(surface);
  return existing != nullptr ? existing->origin : std::array<int, 2>{ 0, 0 };
}

PanelSurfacePointer
GuestPanelSurfaces::pointer(std::uint32_t surface) const
{
  if (surface == kMainSurface) {
    return m_mainPointer;
  }
  const Surface* existing = find(surface);
  return existing != nullptr ? existing->pointer : PanelSurfacePointer{};
}

Scene*
GuestPanelSurfaces::scene(std::uint32_t surface)
{
  Surface* existing = find(surface);
  return existing != nullptr && existing->state == PanelSurfaceState::Open
           ? existing->scene.get()
           : nullptr;
}

std::vector<PanelSurfaceEvent>
GuestPanelSurfaces::takeEvents()
{
  std::vector<PanelSurfaceEvent> events;
  events.swap(m_events);
  return events;
}

void
GuestPanelSurfaces::accept(const GuestInput& input)
{
  m_mainSize = { static_cast<int>(input.width),
                 static_cast<int>(input.height) };
  m_mainPointer.x = input.mouseX;
  m_mainPointer.y = input.mouseY;
  m_mainPointer.left = input.held(GuestKey::MouseLeft);
  m_mainPointer.right = input.held(GuestKey::MouseRight);
  m_mainPointer.middle = input.held(GuestKey::MouseMiddle);
  m_mainPointer.scroll = input.scroll;
  if (input.version < 2) {
    m_focused = kMainSurface;
    return;
  }
  m_mainOrigin = { input.originX, input.originY };
  m_focused = input.focusedSurface;
  for (const GuestSurfaceInput& value : input.surfaces) {
    Surface* existing = find(value.surface);
    if (existing == nullptr || existing->state != PanelSurfaceState::Open) {
      continue;
    }
    existing->size = { static_cast<int>(value.width),
                       static_cast<int>(value.height) };
    existing->origin = { value.originX, value.originY };
    existing->pointer.x = value.mouseX;
    existing->pointer.y = value.mouseY;
    existing->pointer.left =
      (value.buttons & GuestSurfaceInput::LeftButton) != 0;
    existing->pointer.right =
      (value.buttons & GuestSurfaceInput::RightButton) != 0;
    existing->pointer.middle =
      (value.buttons & GuestSurfaceInput::MiddleButton) != 0;
    existing->pointer.scroll = value.scroll;
  }
  if (find(m_focused) == nullptr) {
    m_focused = kMainSurface;
  }
  for (const GuestWindowEvent& event : input.windowEvents) {
    if (find(event.surface) == nullptr) {
      continue; // closed here already
    }
    PanelSurfaceEvent translated;
    translated.surface = event.surface;
    translated.x = event.x;
    translated.y = event.y;
    switch (event.kind) {
      case GuestWindowEventKind::Closed:
        translated.kind = PanelSurfaceEvent::Kind::CloseRequested;
        break;
      case GuestWindowEventKind::Resized:
        translated.kind = PanelSurfaceEvent::Kind::Resized;
        break;
      case GuestWindowEventKind::Moved:
        translated.kind = PanelSurfaceEvent::Kind::Moved;
        break;
      case GuestWindowEventKind::FocusGained:
        translated.kind = PanelSurfaceEvent::Kind::FocusGained;
        break;
      case GuestWindowEventKind::FocusLost:
        translated.kind = PanelSurfaceEvent::Kind::FocusLost;
        break;
    }
    m_events.push_back(translated);
  }
}

void
GuestPanelSurfaces::pump()
{
  for (std::size_t index = 0; index < m_drain.size();) {
    GuestServiceRecord ignored;
    if (m_services.take(m_drain[index], ignored)) {
      m_drain.erase(m_drain.begin() + static_cast<std::ptrdiff_t>(index));
    } else {
      ++index;
    }
  }
  std::vector<std::uint32_t> removed;
  for (const std::unique_ptr<Surface>& surface : m_surfaces) {
    if (surface->state != PanelSurfaceState::Opening ||
        surface->openRequest == 0) {
      continue;
    }
    GuestServiceRecord result;
    if (!m_services.take(surface->openRequest, result)) {
      continue;
    }
    surface->openRequest = 0;
    GuestWindowOpened opened;
    if (result.status == GuestServiceStatus::Complete &&
        !GuestWindowOpened::read(result.payload, opened)) {
      throw std::runtime_error("Invalid window completion");
    }
    if (result.status != GuestServiceStatus::Complete) {
      surface->state = PanelSurfaceState::Failed;
      if (surface->closeAfterOpen) {
        removed.push_back(surface->id);
      } else {
        m_events.push_back(
          { PanelSurfaceEvent::Kind::Failed, surface->id, 0, 0 });
      }
      continue;
    }
    surface->state = PanelSurfaceState::Open;
    surface->size = { static_cast<int>(opened.width),
                      static_cast<int>(opened.height) };
    if (surface->closeAfterOpen) {
      GuestWindowRequest request;
      request.action = GuestWindowAction::Close;
      request.surface = surface->id;
      enqueue(request);
      removed.push_back(surface->id);
      continue;
    }
    m_events.push_back({ PanelSurfaceEvent::Kind::Opened,
                         surface->id,
                         surface->size[0],
                         surface->size[1] });
  }
  for (std::uint32_t id : removed) {
    remove(id);
  }
}

void
GuestPanelSurfaces::clearScenes()
{
  for (const std::unique_ptr<Surface>& surface : m_surfaces) {
    surface->scene->ClearDrawables();
  }
}

void
GuestPanelSurfaces::record(Renderer& renderer,
                           GuestRecordingBackend& backend,
                           GuestSnapshotWindow& window)
{
  for (const std::unique_ptr<Surface>& surface : m_surfaces) {
    if (surface->state != PanelSurfaceState::Open || surface->size[0] < 1 ||
        surface->size[1] < 1) {
      continue;
    }
    window.overrideDimensions(surface->size[0], surface->size[1]);
    backend.beginSurface(surface->id,
                         static_cast<float>(surface->size[0]),
                         static_cast<float>(surface->size[1]));
    try {
      renderer.RenderScene(surface->scene.get(), &m_camera);
    } catch (...) {
      backend.endSurface();
      window.clearOverride();
      throw;
    }
    backend.endSurface();
    window.clearOverride();
  }
}

static bool
sameId(const GuestResourceId& left, const GuestResourceId& right)
{
  return left.owner == right.owner && left.slot == right.slot &&
         left.generation == right.generation && left.kind == right.kind;
}

void
GuestPanelSurfaces::finish(GuestFrame& frame)
{
  for (GuestSurfaceFrame& content : frame.surfaces) {
    Surface* surface = find(content.surface);
    if (surface == nullptr) {
      continue;
    }
    // The size belongs to the content: batches are laid out in that space.
    GuestWireWriter bytes;
    bytes.f32(content.width);
    bytes.f32(content.height);
    for (const GuestBatch& batch : content.batches) {
      GuestFrame::writeBatch(bytes, batch);
    }
    // Content may also change through the meshes and textures it reads.
    bool touched = false;
    for (const GuestBatch& batch : content.batches) {
      for (const GuestFrameMeshWrite& write : frame.meshWrites) {
        touched =
          touched || (batch.retained() && sameId(batch.mesh, write.mesh));
      }
      for (const GuestTextureWrite& write : frame.textureWrites) {
        touched = touched || (batch.texture.owner != 0 &&
                              sameId(batch.texture, write.texture));
      }
    }
    if (surface->revision != 0 && !touched &&
        bytes.data() == surface->recorded) {
      content.same = true;
      content.batches.clear();
      content.revision = surface->revision;
      continue;
    }
    surface->revision += 1;
    surface->recorded = bytes.take();
    content.revision = surface->revision;
  }
}
