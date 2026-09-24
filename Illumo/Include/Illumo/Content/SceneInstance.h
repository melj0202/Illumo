#pragma once

#include <Illumo/Content/SceneDocument.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Scene/SceneGraphDrawable.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class AssetManager;
class Camera;
class MeshVisual;
class Renderer;
class SkyboxVisual;

struct SceneInstanceOptions
{
  // Adds an invisible 0.2-unit pick box to nodes that draw nothing (empty,
  // light and camera nodes) so editors can select them.
  bool pickProxies = false;
};

// The effective directional light after resolving light components against
// the environment sun: the first enabled, visible light in preorder wins.
struct SceneLighting
{
  bool lit = false;
  // Unit vector from the scene toward the light (renderer convention).
  Vector3 towardLight{ 0.0f, 1.0f, 0.0f };
  Vector3 color{ 1.0f, 1.0f, 1.0f };
  Vector3 ambient{ 1.0f, 1.0f, 1.0f };
  bool shadows = false;
  // Node id of the light component in use; empty for the environment sun.
  std::string sourceId;
};

// A live scene built from a SceneDocument: it owns the document state, one
// SceneGraph, the attachments that draw each node and the asset references
// they hold. Stable ids are graph node names. Every edit goes through this
// object so graph, attachments and document never disagree; edits touch only
// the affected nodes and invalidate graph snapshots before reconfiguring any
// attachment. Main-thread affine, like SceneGraph.
//
// Programs add drawable() (and skybox() when present) to their frame Scene,
// call update() once per frame before rendering, and read document() to
// save. Asset references resolve against packageRoot(); with no AssetManager
// every mesh, texture and cubemap draws a placeholder.
class SceneInstance
{
public:
  explicit SceneInstance(AssetManager* assets = nullptr,
                         SceneInstanceOptions options = SceneInstanceOptions{});
  ~SceneInstance();
  SceneInstance(const SceneInstance&) = delete;
  SceneInstance& operator=(const SceneInstance&) = delete;
  SceneInstance(SceneInstance&&) = delete;
  SceneInstance& operator=(SceneInstance&&) = delete;

  // Replaces everything after validating the document. On failure the
  // current scene is untouched.
  bool load(const SceneDocument& document,
            std::string_view packageRoot,
            std::string& error);
  void clear();

  // Binds the renderer used for attachment resources. Call before update()
  // or rendering; rebinding to another renderer rebuilds every attachment.
  void setRenderer(Renderer* renderer);
  // Resolves lighting (light components can move with their parents) and
  // pushes changes into every visual. Call once per frame.
  void update();

  // The current state as a document, rebuilt lazily in graph preorder.
  const SceneDocument& document() const;
  std::string_view packageRoot() const { return m_packageRoot; }
  SceneGraph& graph() { return m_graph; }
  const SceneGraph& graph() const { return m_graph; }
  SceneGraphDrawable& drawable() { return m_drawable; }
  // The skybox drawable when the environment names one, else nullptr.
  SkyboxVisual* skybox() { return m_skybox.get(); }
  const SceneLighting& lighting() const { return m_lighting; }
  // Messages about assets that failed to resolve or load since load().
  const std::vector<std::string>& warnings() const { return m_warnings; }
  // Increments on every accepted edit, load and clear.
  uint64_t revision() const { return m_revision; }

  size_t nodeCount() const { return m_records.size(); }
  // The node's record. Its parentId is always empty: the parent lives in the
  // graph, so ask parentOf (document() fills parentId in its copies).
  const SceneNode* findNode(std::string_view id) const;
  // The parent's id, empty for a root or an unknown id.
  std::string parentOf(std::string_view id) const;
  SceneNodeHandle handleOf(std::string_view id) const;
  std::string idOf(SceneNodeHandle node) const;
  // The id of a parent's child, root order when parentId is empty.
  std::vector<std::string> childIds(std::string_view parentId) const;
  // The next sibling's id (empty when last); used to restore positions.
  std::string nextSiblingId(std::string_view id) const;
  Matrix4 worldMatrix(std::string_view id) const;
  // Ids of the node and every descendant, in preorder.
  std::vector<std::string> subtreeIds(std::string_view id) const;
  // A fresh id "<prefix><n>" not used by any node.
  std::string uniqueId(std::string_view prefix = "n");

  // --- Edits. Each validates first and changes nothing on failure. ---

  // Inserts a node (its parentId names the parent) before insertBeforeId
  // among its siblings, or last when insertBeforeId is empty.
  bool insertNode(const SceneNode& node,
                  std::string_view insertBeforeId,
                  std::string& error);
  // Removes the node and all descendants.
  bool removeSubtree(std::string_view id);
  bool setParent(std::string_view id,
                 std::string_view parentId,
                 std::string_view insertBeforeId,
                 std::string& error);
  bool setTransform(std::string_view id, const Transform3D& transform);
  bool setTransforms(const std::vector<std::string>& ids,
                     const std::vector<Transform3D>& transforms);
  bool setName(std::string_view id, std::string_view name);
  bool setEnabled(std::string_view id, bool enabled);
  bool setVisible(std::string_view id, bool visible);
  bool setTags(std::string_view id,
               const std::vector<std::string>& tags,
               std::string& error);
  bool setComponents(std::string_view id,
                     const std::vector<SceneComponent>& components,
                     std::string& error);
  // Replaces the whole node record (name, flags, transform, tags,
  // components); its id and parent must not change.
  bool replaceNode(const SceneNode& node, std::string& error);

  bool setAssets(const std::vector<SceneAsset>& assets, std::string& error);
  bool setEnvironment(const SceneEnvironment& environment, std::string& error);
  void setWorldMode(SceneWorldMode mode);
  void setMetadata(const SceneMetadata& metadata);
  bool setExtensions(const std::vector<SceneExtension>& extensions,
                     std::string& error);
  // Editor view state never counts as an edit (revision is unchanged).
  void setEditorState(const SceneEditorState& state);

  // --- Queries. ---

  // Nearest node whose attachment local bounds a ray enters, respecting
  // rotation, scale and effective visibility. Equal distances prefer the
  // node later in preorder (drawn on top in 2D).
  bool pickRay(const Vector3& origin,
               const Vector3& direction,
               std::string* id) const;
  // Union of the node's attachment bounds in local space.
  bool localBounds(std::string_view id, AxisAlignedBounds3* bounds) const;
  // The primary camera component (else the first camera) in preorder.
  bool primaryCameraId(std::string* id) const;
  // Points a Camera along the node's -Z with its +Y up and applies the
  // camera component's projection.
  bool applyCamera(std::string_view id, Camera& camera) const;

private:
  struct Record;
  struct AssetSlot;
  class PickProxy;

  AssetManager* m_assets = nullptr;
  Renderer* m_renderer = nullptr;
  SceneInstanceOptions m_options;
  SceneGraph m_graph;
  SceneGraphDrawable m_drawable;
  std::string m_packageRoot;
  SceneDocument m_state;
  mutable SceneDocument m_cache;
  mutable bool m_cacheDirty = true;
  std::unordered_map<std::string, std::unique_ptr<Record>> m_records;
  std::unordered_map<std::string, std::unique_ptr<AssetSlot>> m_assetSlots;
  std::unique_ptr<SkyboxVisual> m_skybox;
  SceneLighting m_lighting;
  bool m_lightingDirty = true;
  std::vector<std::string> m_warnings;
  uint64_t m_revision = 1;
  uint64_t m_idCounter = 0;
  MeshHandle m_primitiveMeshes[6]{};
  mutable std::vector<SceneRayHit> m_candidates;

  void touch();
  Record* record(std::string_view id) const;
  bool buildNode(const SceneNode& node,
                 SceneNodeHandle parent,
                 SceneNodeHandle before,
                 std::string& error);
  void destroyAttachments(Record& record);
  void buildAttachments(Record& record);
  // Returns false (changing nothing) when the visual component kinds differ.
  bool reconfigureAttachments(Record& record,
                              const std::vector<SceneComponent>& before);
  bool configureVisual(MeshVisual& visual,
                       const SceneComponent& component,
                       bool* castShadows);
  void applyLightingTo(MeshVisual& visual, bool castShadows) const;
  bool validateNode(const SceneNode& node, std::string& error) const;
  void releaseAssets();
  AssetSlot* assetSlot(std::string_view id);
  void rebuildEnvironment();
  void rebuildAllAttachments();
  MeshHandle primitiveMesh(ScenePrimitiveShape shape);
  SceneLighting resolveLighting() const;
};
