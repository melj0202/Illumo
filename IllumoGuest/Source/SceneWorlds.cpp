#include <Illumo/Foundation/Profile.h>
#include <IllumoGuest/SceneWorlds.h>

GuestSceneWorlds::GuestSceneWorlds(GuestRecordingBackend& backend)
  : m_backend(backend)
{
}

IRenderWorld*
GuestSceneWorlds::create()
{
  if (m_worlds.size() >= kWorlds) {
    return nullptr;
  }
  World added;
  added.id = m_nextId++;
  added.world = std::make_unique<GuestRenderWorld>(m_backend);
  m_worlds.push_back(std::move(added));
  return m_worlds.back().world.get();
}

void
GuestSceneWorlds::activate(IRenderWorld* world)
{
  m_active = nullptr;
  m_activeId = 0;
  for (const World& candidate : m_worlds) {
    if (candidate.world.get() == world) {
      m_active = candidate.world.get();
      m_activeId = candidate.id;
      break;
    }
  }
  m_backend.setRenderWorld(m_active);
}

void
GuestSceneWorlds::destroy(IRenderWorld* world)
{
  for (std::vector<World>::iterator it = m_worlds.begin(); it != m_worlds.end();
       ++it) {
    if (it->world.get() != world) {
      continue;
    }
    // A world the host never saw needs no message; its queue goes with it.
    if (it->announced) {
      m_destroyed.push_back(it->id);
    }
    if (m_active == world) {
      m_active = nullptr;
      m_activeId = 0;
      m_backend.setRenderWorld(nullptr);
    }
    m_worlds.erase(it);
    return;
  }
}

void
GuestSceneWorlds::beginFrame()
{
  if (m_active != nullptr) {
    m_active->beginFrame();
  }
}

void
GuestSceneWorlds::takeOperations(std::vector<GuestWorldOperation>& output,
                                 std::size_t limit)
{
  ILLUMO_PROFILE_ZONE("SceneWorlds.takeOperations");
  std::size_t sentDestroys = 0;
  while (sentDestroys < m_destroyed.size() && output.size() < limit) {
    GuestWorldOperation operation;
    operation.op = GuestWorldOp::DestroyWorld;
    operation.id = m_destroyed[sentDestroys++];
    output.push_back(operation);
  }
  m_destroyed.erase(m_destroyed.begin(), m_destroyed.begin() + sentDestroys);
  if (!m_destroyed.empty()) {
    return;
  }
  for (World& world : m_worlds) {
    if (output.size() >= limit) {
      return;
    }
    const std::size_t marker = output.size();
    GuestWorldOperation select;
    select.op = GuestWorldOp::SelectWorld;
    select.id = world.id;
    output.push_back(select);
    world.world->takeOperations(output, limit);
    if (output.size() == marker + 1 && world.announced) {
      // Nothing to send: steady frames carry no world operations at all.
      output.pop_back();
    } else {
      world.announced = true;
    }
  }
  // The shown world must exist on the host first.
  bool activeAnnounced = m_activeId == 0;
  for (const World& world : m_worlds) {
    activeAnnounced =
      activeAnnounced || (world.id == m_activeId && world.announced);
  }
  if (m_activeId != m_shownOnHost && activeAnnounced && output.size() < limit) {
    GuestWorldOperation show;
    show.op = GuestWorldOp::ShowWorld;
    show.id = m_activeId;
    output.push_back(show);
    m_shownOnHost = m_activeId;
  }
}
