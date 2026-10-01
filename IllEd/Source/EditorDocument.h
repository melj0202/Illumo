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
//
// In the editor the instance is the program scene's content (D-E31), which
// the document borrows; elsewhere (tools, tests, reloading a saved file) the
// document owns one. Loads, clears and rebases replace the content in place.
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

  // Edits `scene` from now on (the editor scene's content), moving the
  // current document and history into it. `scene` must outlive the document
  // and keeps its own asset manager and bindings.
  void attach(SceneInstance& scene);
  // Rebuilds an owned scene against an asset manager (nullptr draws
  // placeholders), keeping the document and its history. A borrowed scene
  // keeps the asset manager its owner gave it.
  void setAssetManager(AssetManager* assets);
  void setRenderer(Renderer* renderer);
  // Scene meshes draw through `world` when the host keeps one (nullptr
  // draws them as MeshVisuals); kept across scene rebuilds.
  void setRenderWorld(IRenderWorld* world);

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
  // Reloads the same content under another package root (for example after
  // Save to Project); history is kept.
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
  // Advances whenever the scene's content is replaced (load, clear, package
  // root change, attach), so node handles from before are stale.
  uint64_t sceneGeneration() const { return m_sceneGeneration; }

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
  // Moves each node by its own world-space delta, as one command.
  bool translateEach(const std::vector<std::string>& ids,
                     const std::vector<Vector3>& deltasWorld,
                     const std::string& label,
                     const std::string& mergeKey = {});
  // Moves top-level nodes so their subtree bounds line up on one world axis
  // (0-2) at the selection's combined minimum (side < 0), centre (0) or
  // maximum (side > 0), as one command.
  bool alignNodes(const std::vector<std::string>& ids, int axis, int side);
  // Spaces the subtree bounds centres of three or more top-level nodes evenly
  // along one world axis between the two outermost, as one command.
  bool distributeNodes(const std::vector<std::string>& ids, int axis);
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
  // Replaces the asset table as one settings command (commands sharing a
  // merge key collapse into one). Returns false when nothing changed or the
  // scene rejected the table, which then stays as it was.
  bool setAssets(const std::vector<SceneAsset>& assets,
                 const std::string& label,
                 const std::string& mergeKey = {});
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
  // World bounds of a node and its descendants; a node that draws nothing
  // counts as its origin. False for an unknown id.
  bool subtreeWorldBounds(const std::string& id,
                          AxisAlignedBounds3* bounds) const;
  // A transform at a point of the edit plane (XY at z=0 in 2D, XZ at y=0 in
  // 3D).
  Transform3D makeEditPlaneTransform(float planeX, float planeY) const;
  EditorSceneDetail sceneDetail(const EditorSelection& selection) const;

private:
  AssetManager* m_assets = nullptr;
  Renderer* m_renderer = nullptr;
  IRenderWorld* m_renderWorld = nullptr;
  // Null once the document borrows a scene.
  std::unique_ptr<SceneInstance> m_owned;
  // The scene every edit goes to: m_owned or the borrowed one.
  SceneInstance* m_scene = nullptr;
  uint64_t m_sceneGeneration = 0;
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
