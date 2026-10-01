#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/Primitives/VisualStore.h>
#include <Illumo/Rendering/Renderer.h>

VisualStore::VisualStore(Limits limits)
  : m_limits(limits)
{
}

VisualStore::VisualStore()
  : VisualStore(Limits{})
{
}

VisualStore::~VisualStore() = default;

void
VisualStore::setRenderer(Renderer* renderer)
{
  m_renderer = renderer;
  for (auto& [id, entry] : m_entries) {
    entry->visual->setRenderer(renderer);
  }
}

void
VisualStore::setWindow(IRenderWindow* window)
{
  m_window = window;
  for (auto& [id, entry] : m_entries) {
    entry->visual->setWindow(window);
  }
}

void
VisualStore::setCamera(Camera* camera)
{
  m_camera = camera;
  for (auto& [id, entry] : m_entries) {
    entry->visual->setCamera(camera);
  }
}

VisualStore::Entry*
VisualStore::find(VisualId id)
{
  const auto found = m_entries.find(id);
  return found == m_entries.end() ? nullptr : found->second.get();
}

const VisualStore::Entry*
VisualStore::find(VisualId id) const
{
  const auto found = m_entries.find(id);
  return found == m_entries.end() ? nullptr : found->second.get();
}

VisualStore::Result
VisualStore::create(VisualId id)
{
  if (m_entries.contains(id)) {
    return Result::DuplicateId;
  }
  if (m_entries.size() >= m_limits.visuals) {
    return Result::OverBudget;
  }
  std::unique_ptr<Entry> entry = std::make_unique<Entry>();
  entry->visual = std::make_unique<GameVisual>();
  entry->visual->setWindow(m_window);
  entry->visual->setCamera(m_camera);
  if (m_renderer != nullptr) {
    entry->visual->setRenderer(m_renderer);
  }
  m_entries.emplace(id, std::move(entry));
  m_stats.visuals = m_entries.size();
  return Result::Ok;
}

VisualStore::Result
VisualStore::destroy(VisualId id)
{
  if (m_entries.erase(id) == 0) {
    return Result::UnknownId;
  }
  m_stats.visuals = m_entries.size();
  return Result::Ok;
}

VisualStore::Result
VisualStore::setProperties(VisualId id, const VisualProperties& properties)
{
  Entry* entry = find(id);
  if (entry == nullptr) {
    return Result::UnknownId;
  }
  GameVisual& visual = *entry->visual;
  visual.setSpace(properties.space);
  visual.setLayerHint(properties.layer);
  const VisualProperties& previous = entry->properties;
  const Transform2D& a = previous.transform;
  const Transform2D& b = properties.transform;
  // setTransform always re-tessellates, so only a real change reaches it.
  if (a.x != b.x || a.y != b.y || a.scaleX != b.scaleX ||
      a.scaleY != b.scaleY || a.rotationRadians != b.rotationRadians ||
      a.pivotX != b.pivotX || a.pivotY != b.pivotY) {
    visual.setTransform(b);
  }
  visual.setOpacity(properties.opacity);
  if (properties.clipEnabled) {
    visual.setPixelClipRect(properties.clipRect);
  } else {
    visual.clearPixelClipRect();
  }
  visual.setVisible(properties.visible);
  entry->properties = properties;
  entry->properties.opacity = visual.getOpacity();
  return Result::Ok;
}

template<typename Item>
VisualStore::Result
VisualStore::placeItem(VisualId id, size_t index, const Item& item)
{
  Entry* entry = find(id);
  if (entry == nullptr) {
    return Result::UnknownId;
  }
  if (index >= m_limits.itemsPerVisual) {
    return Result::OverBudget;
  }
  return entry->visual->setItem(index, item) ? Result::Ok : Result::BadIndex;
}

VisualStore::Result
VisualStore::setItem(VisualId id, size_t index, const GameVisualItem& item)
{
  return placeItem(id, index, item);
}

VisualStore::Result
VisualStore::setItem(VisualId id, size_t index, const ShapePrimitive& item)
{
  return placeItem(id, index, item);
}

VisualStore::Result
VisualStore::setItem(VisualId id, size_t index, const SpritePrimitive& item)
{
  return placeItem(id, index, item);
}

VisualStore::Result
VisualStore::setItem(VisualId id, size_t index, const TextPrimitive& item)
{
  return placeItem(id, index, item);
}

VisualStore::Result
VisualStore::removeItems(VisualId id, size_t first, size_t count)
{
  Entry* entry = find(id);
  if (entry == nullptr) {
    return Result::UnknownId;
  }
  if (first > entry->visual->itemCount() ||
      count > entry->visual->itemCount() - first) {
    return Result::BadIndex;
  }
  entry->visual->removeItems(first, count);
  return Result::Ok;
}

VisualStore::Result
VisualStore::insertItems(VisualId id, size_t index, size_t count)
{
  Entry* entry = find(id);
  if (entry == nullptr) {
    return Result::UnknownId;
  }
  const size_t items = entry->visual->itemCount();
  if (index > items || count == 0) {
    return Result::BadIndex;
  }
  if (count > m_limits.itemsPerVisual - items) {
    return Result::OverBudget;
  }
  entry->visual->insertItems(index, count);
  return Result::Ok;
}

VisualStore::Result
VisualStore::clearItems(VisualId id)
{
  Entry* entry = find(id);
  if (entry == nullptr) {
    return Result::UnknownId;
  }
  entry->visual->clearPrimitives();
  return Result::Ok;
}

bool
VisualStore::contains(VisualId id) const
{
  return find(id) != nullptr;
}

const VisualProperties*
VisualStore::properties(VisualId id) const
{
  const Entry* entry = find(id);
  return entry == nullptr ? nullptr : &entry->properties;
}

const GameVisual*
VisualStore::visual(VisualId id) const
{
  const Entry* entry = find(id);
  return entry == nullptr ? nullptr : entry->visual.get();
}

bool
VisualStore::append(Renderer* renderer,
                    VisualId id,
                    const GameVisual::FrameOverride* frame)
{
  Entry* entry = find(id);
  if (entry == nullptr || renderer == nullptr) {
    return false;
  }
  GameVisual& visual = *entry->visual;
  if (!visual.isVisible()) {
    return true;
  }
  GameVisual::FrameState state;
  if (!visual.prepareFrame(renderer, &state, frame)) {
    return false;
  }
  // Inside someone else's recording the draws go straight into it.
  if (renderer->isRecording()) {
    return visual.emitDraws(renderer, state);
  }
  if (!entry->recorded || entry->list.failed() ||
      !(state == entry->recordedState)) {
    ILLUMO_PROFILE_ZONE("VisualStore.record");
    entry->list.clear();
    renderer->beginRecording(&entry->list);
    const bool emitted = visual.emitDraws(renderer, state);
    renderer->endRecording();
    entry->recorded = emitted && !entry->list.failed();
    entry->recordedState = state;
    m_stats.recordings += 1;
    if (!entry->recorded) {
      renderer->reportFrameError("VisualStore could not record a visual");
      return false;
    }
  } else {
    m_stats.replays += 1;
  }
  if (entry->list.size() == 0) {
    return true;
  }
  return renderer->pushExecuteList(&entry->list);
}

void
VisualStore::releaseResources()
{
  m_entries.clear();
  m_stats.visuals = 0;
}
