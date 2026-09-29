#include <Illumo/Content/SceneDirector.h>
#include <Illumo/Foundation/Profile.h>
#include <Illumo/Services/Logger.h>

SceneDirector::SceneDirector(IllumoContext& context, ISceneWorlds* worlds)
  : m_context(context)
  , m_worlds(worlds)
  , m_initialCamera(context.camera != nullptr ? *context.camera : Camera())
  , m_sharedWorld(context.renderWorld)
{
}

SceneDirector::~SceneDirector()
{
  stopAll();
  // Scenes are destroyed newest first, after every one has stopped.
  while (!m_entries.empty()) {
    m_entries.pop_back();
  }
}

bool
SceneDirector::add(std::string name, std::unique_ptr<ProgramScene> scene)
{
  if (name.empty() || scene == nullptr || entry(name) != nullptr) {
    Logger::LogWarning("Scene not added: empty name, no scene, or the name '" +
                       name + "' is taken");
    return false;
  }
  std::unique_ptr<Entry> added = std::make_unique<Entry>();
  added->name = std::move(name);
  added->scene = std::move(scene);
  m_entries.push_back(std::move(added));
  return true;
}

bool
SceneDirector::has(std::string_view name) const
{
  return entry(name) != nullptr;
}

ProgramScene*
SceneDirector::find(std::string_view name)
{
  Entry* found = entry(name);
  return found != nullptr ? found->scene.get() : nullptr;
}

ProgramScene*
SceneDirector::active()
{
  return m_active != nullptr ? m_active->scene.get() : nullptr;
}

const std::string&
SceneDirector::activeName() const
{
  static const std::string none;
  return m_active != nullptr ? m_active->name : none;
}

bool
SceneDirector::switchTo(std::string_view name, SceneSwitch how)
{
  Entry* target = entry(name);
  if (target == nullptr || target == m_active) {
    Logger::LogWarning(
      "Scene switch refused: '" + std::string(name) + "' " +
      (target == nullptr ? "is unknown" : "is already active"));
    return false;
  }
  m_pending = target;
  m_pendingHow = how;
  m_covered = false;
  return true;
}

bool
SceneDirector::release(std::string_view name)
{
  for (std::vector<std::unique_ptr<Entry>>::iterator it = m_entries.begin();
       it != m_entries.end();
       ++it) {
    if ((*it)->name != name) {
      continue;
    }
    if (it->get() == m_active || it->get() == m_pending) {
      Logger::LogWarning("Scene '" + std::string(name) +
                         "' is active or about to be; not released");
      return false;
    }
    stopEntry(**it);
    m_entries.erase(it);
    return true;
  }
  return false;
}

bool
SceneDirector::applyPending()
{
  if (m_pending == nullptr ||
      (m_pendingHow == SceneSwitch::Cover && !m_covered)) {
    return true;
  }
  ILLUMO_PROFILE_ZONE("SceneDirector.applyPending");
  Entry* target = m_pending;
  m_pending = nullptr;
  m_covered = false;
  resetFrameInput();
  Entry* previous = m_active;
  if (previous != nullptr) {
    leaveEntry(*previous);
  }
  if (!target->started && !startEntry(*target)) {
    Logger::LogError("Scene '" + target->name + "' failed to start");
    if (previous == nullptr) {
      return false;
    }
    enterEntry(*previous);
    return true;
  }
  Logger::LogTrace("Scene '" + target->name + "' is active");
  enterEntry(*target);
  return true;
}

void
SceneDirector::update(double elapsed)
{
  ILLUMO_PROFILE_ZONE("SceneDirector.update");
  if (m_active != nullptr) {
    m_active->scene->update(elapsed);
  }
}

void
SceneDirector::dispatch(DrawList& frame)
{
  ILLUMO_PROFILE_ZONE("SceneDirector.dispatch");
  if (m_active != nullptr) {
    m_active->scene->dispatch(frame);
  }
}

bool
SceneDirector::closeRequested()
{
  return m_active == nullptr || m_active->scene->closeRequested();
}

void
SceneDirector::stopAll() noexcept
{
  m_pending = nullptr;
  m_covered = false;
  if (m_active != nullptr) {
    Entry* current = m_active;
    leaveEntry(*current);
    stopEntry(*current);
  }
  for (std::vector<std::unique_ptr<Entry>>::reverse_iterator it =
         m_entries.rbegin();
       it != m_entries.rend();
       ++it) {
    stopEntry(**it);
  }
}

SceneDirector::Entry*
SceneDirector::entry(std::string_view name)
{
  for (const std::unique_ptr<Entry>& candidate : m_entries) {
    if (candidate->name == name) {
      return candidate.get();
    }
  }
  return nullptr;
}

const SceneDirector::Entry*
SceneDirector::entry(std::string_view name) const
{
  for (const std::unique_ptr<Entry>& candidate : m_entries) {
    if (candidate->name == name) {
      return candidate.get();
    }
  }
  return nullptr;
}

bool
SceneDirector::startEntry(Entry& target)
{
  ILLUMO_PROFILE_ZONE("SceneDirector.startEntry");
  ProgramScene& scene = *target.scene;
  scene.m_context = &m_context;
  if (!scene.m_content) {
    scene.m_content = std::make_unique<SceneInstance>(m_context.assetManager,
                                                      scene.contentOptions());
  }
  scene.m_content->setRenderer(m_context.renderer);
  // With per-scene worlds a scene never falls back to another's world.
  IRenderWorld* world = m_sharedWorld;
  if (m_worlds != nullptr) {
    target.ownWorld = m_worlds->create();
    world = target.ownWorld;
    m_worlds->activate(world);
  }
  scene.m_world = world;
  scene.m_content->setRenderWorld(world);
  m_context.renderWorld = world;
  if (m_context.camera != nullptr) {
    *m_context.camera = m_initialCamera;
  }

  if (scene.start(m_context)) {
    target.started = true;
    return true;
  }
  scene.withdrawCommands();
  scene.m_content->setRenderWorld(nullptr);
  scene.m_world = nullptr;
  if (target.ownWorld != nullptr) {
    m_worlds->destroy(target.ownWorld);
    target.ownWorld = nullptr;
  }
  return false;
}

void
SceneDirector::enterEntry(Entry& target)
{
  ILLUMO_PROFILE_ZONE("SceneDirector.enterEntry");
  if (m_context.camera != nullptr && target.hasCamera) {
    *m_context.camera = target.camera;
  }
  m_context.renderWorld = target.scene->m_world;
  if (m_worlds != nullptr) {
    m_worlds->activate(target.scene->m_world);
  }
  m_active = &target;
  target.scene->enter();
}

void
SceneDirector::leaveEntry(Entry& current)
{
  ILLUMO_PROFILE_ZONE("SceneDirector.leaveEntry");
  current.scene->leave();
  current.scene->withdrawCommands();
  if (m_context.camera != nullptr) {
    current.camera = *m_context.camera;
    current.hasCamera = true;
  }
  if (m_active == &current) {
    m_active = nullptr;
  }
}

void
SceneDirector::stopEntry(Entry& current) noexcept
{
  if (!current.started) {
    return;
  }
  ILLUMO_PROFILE_ZONE("SceneDirector.stopEntry");
  current.started = false;
  current.scene->stop();
  current.scene->withdrawCommands();
  if (current.scene->m_content) {
    current.scene->m_content->setRenderWorld(nullptr);
  }
  current.scene->m_world = nullptr;
  if (current.ownWorld != nullptr) {
    m_worlds->destroy(current.ownWorld);
    current.ownWorld = nullptr;
  }
}

void
SceneDirector::resetFrameInput()
{
  if (m_context.inputManager != nullptr) {
    m_context.inputManager->clearKeyQueue();
    m_context.inputManager->clearCharQueue();
  }
  if (m_context.scene != nullptr) {
    m_context.scene->ResetDefaultPasses();
    m_context.scene->ClearDrawables();
  }
}
