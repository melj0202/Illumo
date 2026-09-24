#pragma once

#include <string>
#include <string_view>
#include <vector>

class SceneInstance;

// Ordered set of selected node ids with one primary node (the last one
// chosen). The gizmo and inspector follow the primary; bulk edits apply to
// every member.
class EditorSelection
{
public:
  bool empty() const { return m_ids.empty(); }
  size_t size() const { return m_ids.size(); }
  const std::vector<std::string>& ids() const { return m_ids; }
  // Empty when nothing is selected.
  const std::string& primary() const;
  bool contains(std::string_view id) const;

  // Replaces the selection with one node (or clears it for an empty id).
  void set(std::string_view id);
  void set(const std::vector<std::string>& ids);
  // Adds a node, or makes it primary when already selected.
  void add(std::string_view id);
  // Adds or removes a node (Ctrl+click).
  void toggle(std::string_view id);
  void remove(std::string_view id);
  void clear();
  // Drops ids that no longer exist in the scene.
  void prune(const SceneInstance& scene);
  // The selected ids without any whose ancestor is also selected, in scene
  // preorder: the roots bulk moves, deletes and copies operate on.
  std::vector<std::string> topLevel(const SceneInstance& scene) const;

private:
  std::vector<std::string> m_ids;
};
