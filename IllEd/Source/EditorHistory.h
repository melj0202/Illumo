#pragma once

#include <Illumo/Content/SceneDocument.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class SceneInstance;

// One node's full state at a point in time: absent, or its record, parent and
// position among its siblings (the next sibling's id, empty when last).
struct EditorNodeState
{
  std::string id;
  bool present = false;
  SceneNode node;
  std::string nextSibling;
};

// Document-level state that some commands change (world mode, environment,
// asset table, metadata, extensions).
struct EditorSceneSettings
{
  SceneWorldMode worldMode = SceneWorldMode::World2D;
  SceneEnvironment environment;
  std::vector<SceneAsset> assets;
  SceneMetadata metadata;
  std::vector<SceneExtension> extensions;
};

// A reversible edit stored as patches: the before and after state of every
// node it touched (parents before children), plus optional settings.
struct EditorCommandRecord
{
  std::string label;
  // Consecutive commands with the same non-empty key merge (a drag, a slider
  // scrub): the merged command keeps the first before and the last after.
  std::string mergeKey;
  uint64_t uid = 0;
  std::vector<EditorNodeState> before;
  std::vector<EditorNodeState> after;
  bool hasSettings = false;
  EditorSceneSettings settingsBefore;
  EditorSceneSettings settingsAfter;
  size_t bytes = 0;
};

// Undo/redo over SceneInstance patches. The cursor sits after the last
// applied command; pushing discards any redo tail. History is bounded by
// command count and estimated bytes; the oldest commands fall off first.
class EditorHistory
{
public:
  static constexpr size_t kMaximumCommands = 512;
  static constexpr size_t kMaximumBytes = 64u * 1024u * 1024u;

  static EditorNodeState capture(const SceneInstance& scene,
                                 std::string_view id);
  static EditorSceneSettings captureSettings(const SceneInstance& scene);
  // Brings every listed node to its target state. Removals run first (deepest
  // first), then creations (parents first), then updates, then one reverse
  // pass restores sibling positions.
  static bool applyStates(SceneInstance& scene,
                          const std::vector<EditorNodeState>& target,
                          std::string& error);
  static bool applySettings(SceneInstance& scene,
                            const EditorSceneSettings& settings,
                            std::string& error);

  // Records an already-applied command. Returns its uid (0 when the command
  // changed nothing and was dropped).
  uint64_t push(EditorCommandRecord record);
  bool canUndo() const { return m_cursor > 0; }
  bool canRedo() const { return m_cursor < m_commands.size(); }
  std::string undoLabel() const;
  std::string redoLabel() const;
  bool undo(SceneInstance& scene, std::string* label);
  bool redo(SceneInstance& scene, std::string* label);
  void clear();

  // The uid of the last applied command (0 at the start of history). A
  // document is clean when this equals the uid recorded at its last save.
  uint64_t currentUid() const;
  // Commands at or before this uid are never merged into, so edits after a
  // save always move the uid.
  void setMergeBarrier(uint64_t uid) { m_mergeBarrier = uid; }

  size_t size() const { return m_commands.size(); }
  size_t cursor() const { return m_cursor; }
  size_t bytes() const { return m_bytes; }

private:
  std::vector<EditorCommandRecord> m_commands;
  size_t m_cursor = 0;
  uint64_t m_nextUid = 1;
  uint64_t m_mergeBarrier = 0;
  size_t m_bytes = 0;

  static size_t estimate(const EditorCommandRecord& record);
  void trim();
};
