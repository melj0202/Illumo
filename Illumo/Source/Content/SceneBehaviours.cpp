#include <Illumo/Content/SceneBehaviours.h>

#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <utility>

// ---------------------------------------------------------------------------
// Context.

const SceneNode*
SceneBehaviourContext::node() const
{
  return m_scene != nullptr ? m_scene->findNode(m_nodeId) : nullptr;
}

bool
SceneBehaviourContext::setTransform(const Transform3D& transform)
{
  return m_scene != nullptr && m_scene->setTransform(m_nodeId, transform);
}

void
SceneBehaviourContext::createNode(const SceneNode& node)
{
  if (m_owner != nullptr) {
    m_owner->m_queuedCreations.push_back(node);
  }
}

void
SceneBehaviourContext::destroyNode(const std::string& id)
{
  if (m_owner != nullptr) {
    m_owner->m_queuedDestructions.push_back(id);
  }
}

// ---------------------------------------------------------------------------
// Registry.

bool
BehaviourRegistry::add(const std::string& type, SceneBehaviourFactory factory)
{
  if (!validSceneNamespace(type) || !factory || contains(type)) {
    return false;
  }
  m_factories.emplace_back(type, std::move(factory));
  return true;
}

bool
BehaviourRegistry::contains(std::string_view type) const
{
  for (const std::pair<std::string, SceneBehaviourFactory>& entry :
       m_factories) {
    if (entry.first == type) {
      return true;
    }
  }
  return false;
}

std::unique_ptr<SceneBehaviour>
BehaviourRegistry::create(std::string_view type) const
{
  for (const std::pair<std::string, SceneBehaviourFactory>& entry :
       m_factories) {
    if (entry.first == type) {
      return entry.second();
    }
  }
  return nullptr;
}

std::vector<std::string>
BehaviourRegistry::types() const
{
  std::vector<std::string> names;
  names.reserve(m_factories.size());
  for (const std::pair<std::string, SceneBehaviourFactory>& entry :
       m_factories) {
    names.push_back(entry.first);
  }
  return names;
}

std::vector<std::string>
BehaviourRegistry::compare(const BehaviourSchema& schema) const
{
  std::vector<std::string> messages;
  for (const std::pair<std::string, SceneBehaviourFactory>& entry :
       m_factories) {
    if (schema.find(entry.first) == nullptr) {
      messages.push_back("behaviour \"" + entry.first +
                         "\" is registered but behaviours.json does not "
                         "describe it");
    }
  }
  for (const BehaviourType& type : schema.types()) {
    if (!contains(type.type)) {
      messages.push_back("behaviours.json describes \"" + type.type +
                         "\" but no code is registered for it");
    }
  }
  return messages;
}

// ---------------------------------------------------------------------------
// Driver.

SceneBehaviours::SceneBehaviours(const BehaviourRegistry& registry,
                                 const BehaviourSchema& schema)
  : m_registry(registry)
  , m_schema(schema)
{
}

SceneBehaviours::~SceneBehaviours()
{
  detach();
}

void
SceneBehaviours::attach(SceneInstance& scene)
{
  if (m_scene == &scene) {
    return;
  }
  detach();
  m_scene = &scene;
  scene.setContentObserver(this);
  syncAll();
}

void
SceneBehaviours::detach()
{
  if (m_scene != nullptr && m_scene->contentObserver() == this) {
    m_scene->setContentObserver(nullptr);
  }
  // Nodes still exist here, so stop() can read them.
  for (std::unique_ptr<Entry>& entry : m_entries) {
    if (entry->started) {
      entry->behaviour->stop(entry->context);
    }
  }
  m_entries.clear();
  m_order.clear();
  m_orderDirty = true;
  m_queuedCreations.clear();
  m_queuedDestructions.clear();
  m_scene = nullptr;
}

std::size_t
SceneBehaviours::count() const
{
  std::size_t live = 0;
  for (const std::unique_ptr<Entry>& entry : m_entries) {
    live += entry->removed ? 0u : 1u;
  }
  return live;
}

SceneBehaviour*
SceneBehaviours::find(std::string_view nodeId, std::string_view type) const
{
  for (const std::unique_ptr<Entry>& entry : m_entries) {
    if (!entry->removed && entry->context.m_nodeId == nodeId &&
        entry->context.m_type == type) {
      return entry->behaviour.get();
    }
  }
  return nullptr;
}

std::vector<std::string>
SceneBehaviours::takeWarnings()
{
  std::vector<std::string> warnings = std::move(m_warnings);
  m_warnings.clear();
  return warnings;
}

BehaviourValues
SceneBehaviours::decode(const std::string& type, const std::string& data)
{
  const BehaviourType* described = m_schema.find(type);
  if (described == nullptr) {
    return BehaviourValues{};
  }
  std::vector<std::string> warnings;
  BehaviourValues values = BehaviourSchema::decode(*described, data, &warnings);
  for (const std::string& warning : warnings) {
    Logger::LogWarning("Scene behaviour " + warning);
    m_warnings.push_back(warning);
  }
  return values;
}

void
SceneBehaviours::syncNode(std::string_view id)
{
  if (m_scene == nullptr) {
    return;
  }
  const SceneNode* node = m_scene->findNode(id);
  // Existing entries: gone with their component, or given new values.
  for (std::unique_ptr<Entry>& entry : m_entries) {
    if (entry->removed || entry->context.m_nodeId != id) {
      continue;
    }
    const SceneComponent* component =
      node != nullptr ? node->findOpaque(entry->context.m_type) : nullptr;
    if (component == nullptr) {
      entry->removed = true;
      continue;
    }
    const std::string& data =
      std::get<SceneOpaqueComponent>(component->value).data;
    if (data != entry->data) {
      entry->data = data;
      entry->context.m_values = decode(entry->context.m_type, data);
      entry->changed = true;
    }
  }
  if (node == nullptr) {
    return;
  }
  // New registered components get an entry; they start on the next update.
  for (const SceneComponent& component : node->components) {
    const SceneOpaqueComponent* opaque =
      std::get_if<SceneOpaqueComponent>(&component.value);
    if (opaque == nullptr || !m_registry.contains(opaque->type) ||
        find(id, opaque->type) != nullptr) {
      continue;
    }
    std::unique_ptr<Entry> entry = std::make_unique<Entry>();
    entry->behaviour = m_registry.create(opaque->type);
    if (!entry->behaviour) {
      continue;
    }
    entry->context.m_owner = this;
    entry->context.m_scene = m_scene;
    entry->context.m_nodeId = std::string(id);
    entry->context.m_type = opaque->type;
    entry->data = opaque->data;
    entry->context.m_values = decode(opaque->type, opaque->data);
    m_entries.push_back(std::move(entry));
  }
}

void
SceneBehaviours::syncAll()
{
  if (m_scene == nullptr) {
    return;
  }
  const SceneGraph& graph = m_scene->graph();
  for (SceneNodeHandle handle = graph.firstNode(); !handle.isNull();
       handle = graph.nextNode(handle)) {
    syncNode(graph.getName(handle));
  }
}

void
SceneBehaviours::markAllRemoved()
{
  for (std::unique_ptr<Entry>& entry : m_entries) {
    entry->removed = true;
  }
}

void
SceneBehaviours::stopRemoved()
{
  // Erasing only here keeps entry pointers valid through the update pass.
  for (std::unique_ptr<Entry>& entry : m_entries) {
    if (entry->removed && entry->started) {
      entry->started = false;
      entry->behaviour->stop(entry->context);
    }
  }
  const std::size_t before = m_entries.size();
  std::erase_if(m_entries, [](const std::unique_ptr<Entry>& entry) {
    return entry->removed;
  });
  m_orderDirty = m_orderDirty || m_entries.size() != before;
}

void
SceneBehaviours::rebuildOrder()
{
  // Scene preorder, so parents update before their children.
  std::unordered_map<std::string_view, std::vector<Entry*>> byNode;
  for (std::unique_ptr<Entry>& entry : m_entries) {
    if (entry->started && !entry->removed) {
      byNode[entry->context.m_nodeId].push_back(entry.get());
    }
  }
  m_order.clear();
  const SceneGraph& graph = m_scene->graph();
  for (SceneNodeHandle handle = graph.firstNode(); !handle.isNull();
       handle = graph.nextNode(handle)) {
    const std::unordered_map<std::string_view, std::vector<Entry*>>::iterator
      found = byNode.find(graph.getName(handle));
    if (found != byNode.end()) {
      m_order.insert(m_order.end(), found->second.begin(), found->second.end());
    }
  }
  m_orderStructure = graph.getStructuralRevision();
  m_orderDirty = false;
}

void
SceneBehaviours::applyQueued()
{
  std::vector<SceneNode> creations = std::move(m_queuedCreations);
  std::vector<std::string> destructions = std::move(m_queuedDestructions);
  m_queuedCreations.clear();
  m_queuedDestructions.clear();
  for (const SceneNode& node : creations) {
    std::string error;
    if (!m_scene->insertNode(node, {}, error)) {
      m_warnings.push_back("a behaviour's new node \"" + node.id +
                           "\" was refused: " + error);
      Logger::LogWarning("Scene behaviour " + m_warnings.back());
    }
  }
  for (const std::string& id : destructions) {
    m_scene->removeSubtree(id);
  }
}

void
SceneBehaviours::update(double elapsed)
{
  if (m_scene == nullptr || m_updating) {
    return;
  }
  m_updating = true;
  stopRemoved();
  for (std::unique_ptr<Entry>& entry : m_entries) {
    if (entry->changed && entry->started) {
      entry->changed = false;
      entry->behaviour->valuesChanged(entry->context);
    }
  }
  for (std::size_t index = 0; index < m_entries.size(); ++index) {
    Entry& entry = *m_entries[index];
    if (!entry.started && !entry.removed) {
      entry.started = true;
      entry.changed = false;
      entry.context.m_time = 0.0;
      m_orderDirty = true;
      entry.behaviour->start(entry.context);
    }
  }
  if (m_orderDirty ||
      m_orderStructure != m_scene->graph().getStructuralRevision()) {
    rebuildOrder();
  }
  for (Entry* entry : m_order) {
    if (!entry->removed) {
      entry->context.m_time += elapsed;
      entry->behaviour->update(entry->context, elapsed);
    }
  }
  applyQueued();
  m_updating = false;
}

// ---------------------------------------------------------------------------
// Scene notifications: they only record what changed; behaviour code runs in
// update(), never from inside a SceneInstance edit.

void
SceneBehaviours::contentReplaced()
{
  markAllRemoved();
  syncAll();
}

void
SceneBehaviours::nodeAdded(std::string_view id)
{
  syncNode(id);
}

void
SceneBehaviours::nodeRemoving(std::string_view id)
{
  for (std::unique_ptr<Entry>& entry : m_entries) {
    if (entry->context.m_nodeId == id) {
      entry->removed = true;
    }
  }
}

void
SceneBehaviours::componentsChanged(std::string_view id)
{
  syncNode(id);
}

void
SceneBehaviours::sceneDestroyed()
{
  // The scene is still whole here: stop everything while nodes exist.
  for (std::unique_ptr<Entry>& entry : m_entries) {
    if (entry->started) {
      entry->started = false;
      entry->behaviour->stop(entry->context);
    }
  }
  m_entries.clear();
  m_order.clear();
  m_orderDirty = true;
  m_queuedCreations.clear();
  m_queuedDestructions.clear();
  m_scene = nullptr;
}
