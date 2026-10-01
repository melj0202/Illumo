#include "EditorHistory.h"

#include <Illumo/Content/SceneInstance.h>
#include <unordered_map>
#include <utility>

EditorNodeState
EditorHistory::capture(const SceneInstance& scene, std::string_view id)
{
  EditorNodeState state;
  state.id = std::string(id);
  const SceneNode* node = scene.findNode(id);
  if (node == nullptr) {
    return state;
  }
  state.present = true;
  state.node = *node;
  state.node.parentId = scene.idOf(scene.graph().getParent(scene.handleOf(id)));
  state.nextSibling = scene.nextSiblingId(id);
  return state;
}

EditorSceneSettings
EditorHistory::captureSettings(const SceneInstance& scene)
{
  const SceneDocument& document = scene.document();
  EditorSceneSettings settings;
  settings.worldMode = document.worldMode;
  settings.environment = document.environment;
  settings.assets = document.assets;
  settings.metadata = document.metadata;
  settings.extensions = document.extensions;
  return settings;
}

bool
EditorHistory::applyStates(SceneInstance& scene,
                           const std::vector<EditorNodeState>& target,
                           std::string& error)
{
  // Removals, deepest (last in preorder) first.
  for (size_t index = target.size(); index > 0; --index) {
    const EditorNodeState& state = target[index - 1];
    if (!state.present && scene.findNode(state.id) != nullptr) {
      scene.removeSubtree(state.id);
    }
  }
  // Creations, parents first; positions are fixed by the final pass.
  for (const EditorNodeState& state : target) {
    if (!state.present || scene.findNode(state.id) != nullptr) {
      continue;
    }
    const std::string before =
      scene.findNode(state.nextSibling) != nullptr ? state.nextSibling : "";
    if (!scene.insertNode(state.node, before, error)) {
      return false;
    }
  }
  // Updates of nodes that existed on both sides.
  for (const EditorNodeState& state : target) {
    if (!state.present) {
      continue;
    }
    const std::string parent =
      scene.idOf(scene.graph().getParent(scene.handleOf(state.id)));
    if (parent != state.node.parentId &&
        !scene.setParent(state.id, state.node.parentId, "", error)) {
      return false;
    }
    if (!scene.replaceNode(state.node, error)) {
      return false;
    }
  }
  // Sibling positions: later siblings are placed before earlier ones refer
  // to them, so a reverse pass restores every recorded position.
  for (size_t index = target.size(); index > 0; --index) {
    const EditorNodeState& state = target[index - 1];
    if (!state.present) {
      continue;
    }
    if (!state.nextSibling.empty() &&
        scene.findNode(state.nextSibling) == nullptr) {
      continue;
    }
    if (scene.nextSiblingId(state.id) != state.nextSibling &&
        !scene.setParent(
          state.id, state.node.parentId, state.nextSibling, error)) {
      return false;
    }
  }
  return true;
}

bool
EditorHistory::applySettings(SceneInstance& scene,
                             const EditorSceneSettings& settings,
                             std::string& error)
{
  if (!scene.setAssets(settings.assets, error) ||
      !scene.setEnvironment(settings.environment, error) ||
      !scene.setExtensions(settings.extensions, error)) {
    return false;
  }
  scene.setWorldMode(settings.worldMode);
  scene.setMetadata(settings.metadata);
  return true;
}

static size_t
stateBytes(const EditorNodeState& state)
{
  size_t bytes = sizeof(EditorNodeState) + state.id.size() +
                 state.nextSibling.size() + state.node.name.size() +
                 state.node.parentId.size();
  for (const std::string& tag : state.node.tags) {
    bytes += tag.size() + sizeof(std::string);
  }
  for (const SceneComponent& component : state.node.components) {
    bytes += sizeof(SceneComponent);
    const SceneOpaqueComponent* opaque =
      std::get_if<SceneOpaqueComponent>(&component.value);
    if (opaque != nullptr) {
      bytes += opaque->type.size() + opaque->data.size();
    }
  }
  return bytes;
}

size_t
EditorHistory::estimate(const EditorCommandRecord& record)
{
  size_t bytes =
    sizeof(EditorCommandRecord) + record.label.size() + record.mergeKey.size();
  for (const EditorNodeState& state : record.before) {
    bytes += stateBytes(state);
  }
  for (const EditorNodeState& state : record.after) {
    bytes += stateBytes(state);
  }
  if (record.hasSettings) {
    bytes += 2 * (sizeof(EditorSceneSettings) +
                  record.settingsBefore.assets.size() * sizeof(SceneAsset));
  }
  return bytes;
}

std::string
EditorHistory::undoLabel() const
{
  return canUndo() ? m_commands[m_cursor - 1].label : std::string();
}

std::string
EditorHistory::redoLabel() const
{
  return canRedo() ? m_commands[m_cursor].label : std::string();
}

uint64_t
EditorHistory::currentUid() const
{
  return m_cursor == 0 ? 0 : m_commands[m_cursor - 1].uid;
}

uint64_t
EditorHistory::push(EditorCommandRecord record)
{
  if (record.before.empty() && !record.hasSettings) {
    return 0;
  }
  while (m_commands.size() > m_cursor) {
    m_bytes -= m_commands.back().bytes;
    m_commands.pop_back();
  }
  if (!record.mergeKey.empty() && m_cursor > 0) {
    EditorCommandRecord& top = m_commands[m_cursor - 1];
    if (top.mergeKey == record.mergeKey && top.uid > m_mergeBarrier) {
      // Keep top's before for ids it already covers; adopt new ids' before.
      std::unordered_map<std::string, size_t> known;
      for (size_t index = 0; index < top.before.size(); ++index) {
        known.emplace(top.before[index].id, index);
      }
      for (EditorNodeState& state : record.before) {
        if (known.find(state.id) == known.end()) {
          top.before.push_back(std::move(state));
        }
      }
      top.after = std::move(record.after);
      if (record.hasSettings) {
        if (!top.hasSettings) {
          top.settingsBefore = std::move(record.settingsBefore);
        }
        top.hasSettings = true;
        top.settingsAfter = std::move(record.settingsAfter);
      }
      m_bytes -= top.bytes;
      top.bytes = estimate(top);
      m_bytes += top.bytes;
      trim();
      return top.uid;
    }
  }
  record.uid = m_nextUid++;
  record.bytes = estimate(record);
  m_bytes += record.bytes;
  m_commands.push_back(std::move(record));
  m_cursor = m_commands.size();
  trim();
  return m_commands.empty() ? 0 : m_commands.back().uid;
}

void
EditorHistory::trim()
{
  size_t drop = 0;
  size_t bytes = m_bytes;
  while (
    drop + 1 < m_commands.size() &&
    (m_commands.size() - drop > kMaximumCommands || bytes > kMaximumBytes)) {
    bytes -= m_commands[drop].bytes;
    ++drop;
  }
  if (drop == 0) {
    return;
  }
  m_commands.erase(m_commands.begin(),
                   m_commands.begin() + static_cast<std::ptrdiff_t>(drop));
  m_bytes = bytes;
  m_cursor = m_cursor > drop ? m_cursor - drop : 0;
}

// Brings the scene to a command's target. Nodes may reference assets the
// target adds (a paste) or drops (undoing one), so the asset table is the
// union of both while nodes change, then exactly the target's.
static bool
restore(SceneInstance& scene,
        const EditorSceneSettings* settings,
        const std::vector<EditorNodeState>& states)
{
  std::string error;
  if (settings == nullptr) {
    return EditorHistory::applyStates(scene, states, error);
  }
  EditorSceneSettings interim = *settings;
  for (const SceneAsset& asset : scene.document().assets) {
    bool listed = false;
    for (const SceneAsset& target : interim.assets) {
      listed = listed || target.id == asset.id;
    }
    if (!listed) {
      interim.assets.push_back(asset);
    }
  }
  if (interim.assets.size() == settings->assets.size()) {
    // Nothing is dropped, so one settings pass before the nodes suffices.
    return EditorHistory::applySettings(scene, *settings, error) &&
           EditorHistory::applyStates(scene, states, error);
  }
  return EditorHistory::applySettings(scene, interim, error) &&
         EditorHistory::applyStates(scene, states, error) &&
         EditorHistory::applySettings(scene, *settings, error);
}

bool
EditorHistory::undo(SceneInstance& scene, std::string* label)
{
  if (!canUndo()) {
    return false;
  }
  const EditorCommandRecord& command = m_commands[m_cursor - 1];
  if (!restore(scene,
               command.hasSettings ? &command.settingsBefore : nullptr,
               command.before)) {
    return false;
  }
  if (label != nullptr) {
    *label = command.label;
  }
  --m_cursor;
  return true;
}

bool
EditorHistory::redo(SceneInstance& scene, std::string* label)
{
  if (!canRedo()) {
    return false;
  }
  const EditorCommandRecord& command = m_commands[m_cursor];
  if (!restore(scene,
               command.hasSettings ? &command.settingsAfter : nullptr,
               command.after)) {
    return false;
  }
  if (label != nullptr) {
    *label = command.label;
  }
  ++m_cursor;
  return true;
}

void
EditorHistory::clear()
{
  m_commands.clear();
  m_cursor = 0;
  m_bytes = 0;
  m_mergeBarrier = 0;
}
