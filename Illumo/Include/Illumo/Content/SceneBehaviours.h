#pragma once

#include <Illumo/Content/BehaviourSchema.h>
#include <Illumo/Content/SceneInstance.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class InputManager;
class SceneBehaviours;

// What a behaviour sees: its node, its typed values and the scene. Transform
// and flag edits apply at once through scene(); creating and destroying nodes
// is queued and applied after the update pass, so the pass never walks a
// half-edited graph. Never call SceneInstance's structural edits (insertNode,
// removeSubtree, setParent, load, clear) from a behaviour.
class SceneBehaviourContext
{
public:
  const std::string& nodeId() const { return m_nodeId; }
  // The behaviour's component type ("playground.spinner").
  const std::string& type() const { return m_type; }
  SceneInstance& scene() { return *m_scene; }
  const SceneInstance& scene() const { return *m_scene; }
  const BehaviourValues& values() const { return m_values; }
  // Seconds of updates since start.
  double time() const { return m_time; }
  // The program's input (keys and mouse), or nullptr when none was given.
  InputManager* input() const;
  // The node's record, or nullptr once the node is gone (in stop() after a
  // removal).
  const SceneNode* node() const;
  bool setTransform(const Transform3D& transform);

  // Queued until the end of the update pass. A created node needs a fresh id
  // (SceneInstance::uniqueId) and an existing parent (or none).
  void createNode(const SceneNode& node);
  void destroyNode(const std::string& id);

private:
  friend class SceneBehaviours;
  SceneBehaviours* m_owner = nullptr;
  SceneInstance* m_scene = nullptr;
  std::string m_nodeId;
  std::string m_type;
  BehaviourValues m_values;
  double m_time = 0.0;
};

// Game code attached to every node carrying a component of its type. Calls
// come on the main thread: start once before the first update, update once
// per frame in scene preorder, valuesChanged when the component's data
// changes (an editor or a script set it), and stop when the node or the
// component goes or the scene stops.
class SceneBehaviour
{
public:
  virtual ~SceneBehaviour() = default;
  virtual void start(SceneBehaviourContext& context) { (void)context; }
  virtual void update(SceneBehaviourContext& context, double elapsed)
  {
    (void)context;
    (void)elapsed;
  }
  virtual void valuesChanged(SceneBehaviourContext& context) { (void)context; }
  virtual void stop(SceneBehaviourContext& context) { (void)context; }
};

using SceneBehaviourFactory = std::function<std::unique_ptr<SceneBehaviour>()>;

// The behaviour types a program knows, by component type.
class BehaviourRegistry
{
public:
  // False for an invalid (non-namespaced) or already registered type.
  bool add(const std::string& type, SceneBehaviourFactory factory);
  // Registers T, which must be default-constructible.
  template<typename T>
  bool add(const std::string& type)
  {
    return add(type, []() { return std::make_unique<T>(); });
  }
  bool contains(std::string_view type) const;
  std::unique_ptr<SceneBehaviour> create(std::string_view type) const;
  std::vector<std::string> types() const;
  // Messages for registered types the schema does not describe and described
  // types with no factory; empty when they agree.
  std::vector<std::string> compare(const BehaviourSchema& schema) const;

private:
  std::vector<std::pair<std::string, SceneBehaviourFactory>> m_factories;
};

// Runs behaviours over one SceneInstance: attaches one instance per
// registered component on every node, follows nodes as they are added,
// removed or edited (ISceneContentObserver), and calls them each frame.
// Values come from the schema; a registered type the schema does not
// describe gets empty values (every lookup returns its fallback).
// Main-thread affine. The registry and schema must outlive this object.
class SceneBehaviours final : private ISceneContentObserver
{
public:
  SceneBehaviours(const BehaviourRegistry& registry,
                  const BehaviourSchema& schema);
  ~SceneBehaviours() override;
  SceneBehaviours(const SceneBehaviours&) = delete;
  SceneBehaviours& operator=(const SceneBehaviours&) = delete;
  SceneBehaviours(SceneBehaviours&&) = delete;
  SceneBehaviours& operator=(SceneBehaviours&&) = delete;

  // Follows `scene` (replacing any earlier scene, whose behaviours stop) and
  // attaches behaviours to its nodes; they start on the next update.
  void attach(SceneInstance& scene);
  // Stops every started behaviour while its node still exists and stops
  // following the scene.
  void detach();
  SceneInstance* scene() const { return m_scene; }
  // Input behaviours may read (SceneBehaviourContext::input); not owned.
  void setInput(InputManager* input) { m_input = input; }

  // One frame: stops behaviours whose node or component went, reports
  // changed values, starts new behaviours, updates the live ones in scene
  // preorder, then applies queued creations and destructions.
  void update(double elapsed);

  // Live behaviours (started or waiting to start).
  std::size_t count() const;
  SceneBehaviour* find(std::string_view nodeId, std::string_view type) const;
  // Messages since the last call: invalid component values and refused
  // queued edits.
  std::vector<std::string> takeWarnings();

private:
  friend class SceneBehaviourContext;
  struct Entry
  {
    std::unique_ptr<SceneBehaviour> behaviour;
    SceneBehaviourContext context;
    std::string data;
    bool started = false;
    bool removed = false;
    bool changed = false;
  };

  const BehaviourRegistry& m_registry;
  const BehaviourSchema& m_schema;
  SceneInstance* m_scene = nullptr;
  InputManager* m_input = nullptr;
  std::vector<std::unique_ptr<Entry>> m_entries;
  std::vector<std::string> m_warnings;
  std::vector<SceneNode> m_queuedCreations;
  std::vector<std::string> m_queuedDestructions;
  // Started entries in scene preorder, rebuilt only when the graph's
  // structure or the set of started entries changes.
  std::vector<Entry*> m_order;
  bool m_orderDirty = true;
  uint64_t m_orderStructure = 0;
  bool m_updating = false;

  // ISceneContentObserver.
  void contentReplaced() override;
  void nodeAdded(std::string_view id) override;
  void nodeRemoving(std::string_view id) override;
  void componentsChanged(std::string_view id) override;
  void sceneDestroyed() override;

  // Brings the node's entries in line with its registered components.
  void syncNode(std::string_view id);
  void syncAll();
  void markAllRemoved();
  void stopRemoved();
  void rebuildOrder();
  BehaviourValues decode(const std::string& type, const std::string& data);
  void applyQueued();
};
