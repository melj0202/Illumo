#pragma once

#include <Illumo/Foundation/AxisAlignedBounds3.h>
#include <Illumo/Foundation/MathTypes.h>
#include <Illumo/Rendering/ISceneRenderAttachment.h>
#include <Illumo/Scene/SceneNodeHandle.h>
#include <Illumo/Scene/SceneSnapshot.h>
#include <Illumo/Scene/Transform3D.h>
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

// Value descriptors for persistent, main-thread-affine scene state.
struct SceneNodeDesc
{
  SceneNodeHandle parent;
  Transform3D transform;
  std::string_view name;
  uint64_t userData = 0;
  bool enabled = true;
  bool visible = true;
};

enum class SceneChangeKind
{
  Created,
  Destroyed,
  Reparented,
  Transform,
  State,
  Name,
  UserData,
  Attachments
};
struct SceneChange
{
  uint64_t sequence = 0;
  SceneChangeKind kind = SceneChangeKind::Created;
  SceneNodeHandle node;
};
struct SceneRayHit
{
  SceneNodeHandle node;
  float distance = 0.0f; // Ray parameter: origin + direction * distance.
};
struct SceneGraphStatistics
{
  size_t compiledNodes = 0;
  size_t recomputedTransforms = 0;
  size_t boundsQueries = 0;
  size_t extractionVisits = 0;
  uint64_t compilations = 0;
  uint64_t extractions = 0;
};

// Owns nodes, compiled state, and snapshot buffers; borrows attachments. Frame
// lists contain SceneGraphDrawable. Mutation is rejected during extraction;
// snapshots isolate subsequent emission from ordinary node edits.
class SceneGraph
{
public:
  SceneGraph();
  ~SceneGraph();

  SceneGraph(const SceneGraph&) = delete;
  SceneGraph& operator=(const SceneGraph&) = delete;
  SceneGraph(SceneGraph&&) = delete;
  SceneGraph& operator=(SceneGraph&&) = delete;

  SceneNodeHandle createNode(SceneNodeHandle parent = SceneNodeHandle{});
  SceneNodeHandle createNode(const SceneNodeDesc& description);
  bool destroyNode(SceneNodeHandle node);
  void clear();

  bool isNodeValid(SceneNodeHandle node) const;
  size_t getNodeCount() const;
  size_t getRootCount() const;
  SceneNodeHandle getRoot(size_t index) const;

  SceneNodeHandle getParent(SceneNodeHandle node) const;
  SceneNodeHandle getNextSibling(SceneNodeHandle node) const;
  size_t getChildCount(SceneNodeHandle node) const;
  SceneNodeHandle getChild(SceneNodeHandle node, size_t index) const;
  bool setParent(SceneNodeHandle node, SceneNodeHandle parent);
  bool canSetParent(SceneNodeHandle node, SceneNodeHandle parent) const;

  // Matrix input decomposes to TRS; shear/projective matrices are lossy.
  bool setLocalTransform(SceneNodeHandle node, const Matrix4& transform);
  bool setLocalTransform(SceneNodeHandle node, const Transform3D& transform);
  bool getLocalTransform(SceneNodeHandle node, Matrix4* transform) const;
  bool getLocalTransform(SceneNodeHandle node, Transform3D* transform) const;
  bool setLocalTransforms(const SceneNodeHandle* nodes,
                          const Transform3D* transforms,
                          size_t count);
  // O(depth), independent of compiled state and unrelated graph nodes.
  bool getWorldTransform(SceneNodeHandle node, Matrix4* transform) const;
  bool getWorldBounds(SceneNodeHandle node, AxisAlignedBounds3* bounds) const;
  void updateWorldTransforms();

  bool setEnabled(SceneNodeHandle node, bool enabled);
  bool getEnabled(SceneNodeHandle node, bool* enabled) const;
  bool setVisible(SceneNodeHandle node, bool nodeVisible);
  bool getVisible(SceneNodeHandle node, bool* nodeVisible) const;
  bool isEffectivelyVisible(SceneNodeHandle node) const;

  bool setName(SceneNodeHandle node, std::string_view name);
  std::string_view getName(SceneNodeHandle node) const;
  SceneNodeHandle findByName(std::string_view name) const;
  bool setUserData(SceneNodeHandle node, uint64_t data);
  bool getUserData(SceneNodeHandle node, uint64_t* data) const;
  SceneNodeHandle firstNode() const;
  SceneNodeHandle nextNode(SceneNodeHandle node) const;
  bool addAttachment(SceneNodeHandle node, ISceneRenderAttachment* attachment);
  bool removeAttachment(SceneNodeHandle node,
                        ISceneRenderAttachment* attachment);
  size_t getAttachmentCount(SceneNodeHandle node) const;
  ISceneRenderAttachment* getAttachment(SceneNodeHandle node,
                                        size_t index) const;

  bool setRenderAttachment(SceneNodeHandle node,
                           ISceneRenderAttachment* attachment);
  ISceneRenderAttachment* getRenderAttachment(SceneNodeHandle node) const;

  // Queries exclude hidden/disabled subtrees; unknown bounds cannot hit.
  // Ties use preorder. Linear mode is the index's correctness fallback/oracle.
  bool raycast(const Vector3& origin,
               const Vector3& direction,
               SceneRayHit* hit,
               bool useIndex = true) const;
  // Ordered broad-phase candidates for consumer-owned exact picking policy.
  void raycastCandidates(const Vector3& origin,
                         const Vector3& direction,
                         std::vector<SceneRayHit>* hits,
                         bool useIndex = true) const;
  void queryBounds(const AxisAlignedBounds3& bounds,
                   std::vector<SceneNodeHandle>* results,
                   bool useIndex = true) const;
  uint64_t getStructuralRevision() const;
  uint64_t getChangeSequence() const;
  // False means the cursor expired: results are empty and full resync is due.
  bool readChanges(uint64_t after, std::vector<SceneChange>* changes) const;
  SceneGraphStatistics getStatistics() const;
  SceneSnapshotView extract(Renderer* renderer);
  // Before externally reconfiguring borrowed content. Already emitted token
  // payloads must still live until synchronous submission returns.
  void invalidateSnapshots();
  bool notifyAttachmentChanged(SceneNodeHandle node);

private:
  friend class SceneGraphDrawable;
  void emitDirect(Renderer* renderer, unsigned pass);
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
