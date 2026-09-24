#pragma once

#include "EditorHistory.h"
#include "EditorSelection.h"
#include <Illumo/Content/SceneDocument.h>
#include <Illumo/Content/SceneInstance.h>
#include <Illumo/Scene/SceneGraph.h>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class AssetManager;
class Renderer;

// What the inspector and status bar show about the scene and selection.
struct EditorSceneDetail
{
  size_t nodeCount = 0;
  size_t selectionCount = 0;
  SceneWorldMode worldMode = SceneWorldMode::World2D;
  bool hasSelection = false;
  std::string selectedId;
  std::string selectedName;
  // "Empty", "Cube", "Mesh", "Light"... from the node's first component.
  std::string kindLabel = "Empty";
  Transform3D transform;
  bool hasPrimitive = false;
  Vector3 extent{ 0.5f, 0.5f, 0.5f };
  ColorRgba color{ 200, 200, 200, 255 };
};

// The editor's document: one SceneInstance (the live scene), its undo
// history and the save location. Every edit goes through this class, which
// records it as a history command; dirty means the history has moved since
// the last save, so view-only changes (camera, grid) never ask to save.
class EditorDocument
{
public:
  // Scenes opened as documents resolve package-relative assets here until a
  // project mount supplies a real package root.
  static constexpr const char* kLocalPackageRoot = "/local";

  EditorDocument();
  ~EditorDocument();

  EditorDocument(const EditorDocument&) = delete;
  EditorDocument& operator=(const EditorDocument&) = delete;
  EditorDocument(EditorDocument&&) = delete;
  EditorDocument& operator=(EditorDocument&&) = delete;

  // Rebuilds the live scene against an asset manager (nullptr draws
  // placeholders), keeping the document and its history.
  void setAssetManager(AssetManager* assets);
  void setRenderer(Renderer* renderer);

  // An empty, clean document with no location and no history.
  void clear();
  // Parses fully before replacing anything; history is cleared on success.
  // Relative asset references resolve against packageRoot (a mount such as
  // "/project"; empty keeps kLocalPackageRoot).
  bool loadFromText(const std::string& text,
                    std::string* error,
                    const std::string& packageRoot = {});
  std::string packageRoot() const
  {
    return std::string(m_scene->packageRoot());
  }
  // Rebuilds the instance with the same content under another package root
  // (for example after Save to Project); history is kept.
  void rebase(const std::string& packageRoot);
  // Canonical .ilsc text including the editor view state.
  std::string encode() const;

  bool isDirty() const;
  // The document's saved state now matches `location` (a completed save).
  void markSaved(const std::string& location, const std::string& label);
  // The save-in-place location: a path natively, an opaque grant in WASM.
  const std::string& path() const { return m_path; }
  void setPath(const std::string& path)
  {
    m_path = path;
    m_label.clear();
  }
  void setLocation(const std::string& location, const std::string& label)
  {
    m_path = location;
    m_label = label;
  }
  // What the UI calls the document: its label, else its location.
  const std::string& displayName() const
  {
    return m_label.empty() ? m_path : m_label;
  }

  const SceneEditorState& editorState() const;
  void setEditorState(const SceneEditorState& state);
  SceneWorldMode worldMode() const;
  bool setWorldMode(SceneWorldMode mode);

  SceneInstance& scene() { return *m_scene; }
  const SceneInstance& scene() const { return *m_scene; }
  SceneGraph& graph() { return m_scene->graph(); }
  const SceneGraph& graph() const { return m_scene->graph(); }
  size_t nodeCount() const { return m_scene->nodeCount(); }
  const SceneNode* findNode(const std::string& id) const;
  SceneNodeHandle nodeHandle(const std::string& id) const;
  uint64_t revision() const { return m_scene->revision(); }

  EditorHistory& history() { return m_history; }
  const EditorHistory& history() const { return m_history; }
  bool undo(std::string* label = nullptr);
  bool redo(std::string* label = nullptr);

  // --- Recorded edits. ---

  // Creates a node from a template (its id is replaced by a fresh one) under
  // parentId, before insertBeforeId or last. Returns the new id.
  std::string createNode(const SceneNode& node,
                         const std::string& parentId,
                         const std::string& insertBeforeId = {});
  // A node with one primitive component (none for an empty node) named after
  // its shape.
  std::string createPrimitive(bool empty,
                              ScenePrimitiveShape shape,
                              const std::string& parentId,
                              const Transform3D& transform);
  bool destroySubtree(const std::string& id);
  // Deletes several subtrees as one command.
  bool destroyNodes(const std::vector<std::string>& ids);
  bool canSetParent(const std::string& id, const std::string& parentId) const;
  bool setParent(const std::string& id,
                 const std::string& parentId,
                 const std::string& insertBeforeId = {});
  bool setTransform(const std::string& id,
                    const Transform3D& transform,
                    const std::string& mergeKey = {});
  bool setName(const std::string& id, const std::string& name);
  bool setEnabled(const std::string& id, bool enabled);
  bool setVisible(const std::string& id, bool visible);
  bool setComponents(const std::string& id,
                     const std::vector<SceneComponent>& components);
  // Primitive extent and color of the node's primitive component.
  bool setExtent(const std::string& id, const Vector3& extent);
  bool setColor(const std::string& id, ColorRgba color);
  // Moves nodes by a world-space delta (converted through each parent), as
  // one command; commands sharing a merge key collapse into one.
  bool translate(const std::vector<std::string>& ids,
                 const Vector3& deltaWorld,
                 const std::string& mergeKey = {});
  bool translate(const std::string& id, const Vector3& deltaWorld);
  // Sets local transforms of several nodes as one (mergeable) command.
  bool setTransforms(const std::vector<std::string>& ids,
                     const std::vector<Transform3D>& transforms,
                     const std::string& mergeKey = {});
  // Applies a mutation to a copy of each node (ids must exist; the parent and
  // id must stay unchanged) as one command. The mutator returns false to skip
  // a node. Returns false when nothing changed or the scene rejected an edit,
  // in which case every node is restored.
  bool editNodes(const std::vector<std::string>& ids,
                 const std::string& label,
                 const std::string& mergeKey,
                 const std::function<bool(SceneNode&)>& mutate);
  bool setEnvironment(const SceneEnvironment& environment,
                      const std::string& mergeKey = {});
  bool setMetadata(const SceneMetadata& metadata);
  // Copies each subtree next to its original; returns the new root ids.
  std::vector<std::string> duplicate(const std::vector<std::string>& ids);
  // Adds a mesh (.obj) or texture file from the virtual file tree as an asset
  // entry plus a node showing it (a mesh renderer or a sprite) at the given
  // world transform, as one command. The reference is package-relative when
  // the file lies in this document's package. Returns the new node id.
  std::string placeAsset(const std::string& virtualPath,
                         const Transform3D& transform);
  // Inserts a clipboard fragment (EditorClipboard::read) under parentId,
  // before insertBeforeId or last, as one command. Every node gets a fresh
  // id; fragment assets are added, reused when identical, or renamed when
  // their id is taken by a different asset. Roots keep their world pose.
  // Returns the new root ids (empty when nothing was pasted).
  std::vector<std::string> paste(const SceneDocument& fragment,
                                 const std::string& parentId,
                                 const std::string& insertBeforeId = {});

  // --- Queries. ---

  bool pickRay(const Vector3& origin,
               const Vector3& direction,
               std::string* id) const;
  Matrix4 worldMatrix(const std::string& id) const;
  // A transform at a point of the edit plane (XY at z=0 in 2D, XZ at y=0 in
  // 3D).
  Transform3D makeEditPlaneTransform(float planeX, float planeY) const;
  EditorSceneDetail sceneDetail(const EditorSelection& selection) const;

private:
  AssetManager* m_assets = nullptr;
  Renderer* m_renderer = nullptr;
  std::unique_ptr<SceneInstance> m_scene;
  EditorHistory m_history;
  std::string m_path;
  std::string m_label;
  uint64_t m_savedUid = 0;

  std::unique_ptr<SceneInstance> makeScene() const;
  std::vector<EditorNodeState> captureAll(
    const std::vector<std::string>& ids) const;
  // Records the change between before and the current state of the same ids,
  // dropping it when nothing changed.
  bool recordSettings(const std::string& label,
                      const std::string& mergeKey,
                      const EditorSceneSettings& before);
  bool record(const std::string& label,
              const std::string& mergeKey,
              const std::vector<EditorNodeState>& before);
};
