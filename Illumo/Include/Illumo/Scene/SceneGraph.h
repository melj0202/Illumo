#pragma once

#include <Illumo/Foundation/MathTypes.h>
#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/ISceneRenderAttachment.h>
#include <Illumo/Scene/SceneNodeHandle.h>
#include <Illumo/Scene/Transform3D.h>
#include <cstddef>
#include <memory>

// Persistent, main-thread-affine transform hierarchy. SceneGraph owns nodes
// and borrows render attachments. It is one token-only DrawableBase entry in
// the per-frame render list; enabled and visible nodes emit in hierarchy
// pre-order after dirty world transforms are resolved. Mutating graph storage,
// hierarchy, node state, or attachments during that traversal is rejected.
class SceneGraph : public DrawableBase
{
public:
  SceneGraph();
  ~SceneGraph() override;

  SceneGraph(const SceneGraph&) = delete;
  SceneGraph& operator=(const SceneGraph&) = delete;
  SceneGraph(SceneGraph&&) = delete;
  SceneGraph& operator=(SceneGraph&&) = delete;

  SceneNodeHandle createNode(SceneNodeHandle parent = SceneNodeHandle{});
  bool destroyNode(SceneNodeHandle node);
  void clear();

  bool isNodeValid(SceneNodeHandle node) const;
  size_t getNodeCount() const;
  size_t getRootCount() const;
  SceneNodeHandle getRoot(size_t index) const;

  SceneNodeHandle getParent(SceneNodeHandle node) const;
  size_t getChildCount(SceneNodeHandle node) const;
  SceneNodeHandle getChild(SceneNodeHandle node, size_t index) const;
  bool setParent(SceneNodeHandle node, SceneNodeHandle parent);

  bool setLocalTransform(SceneNodeHandle node, const Matrix4& transform);
  bool setLocalTransform(SceneNodeHandle node, const Transform3D& transform);
  bool getLocalTransform(SceneNodeHandle node, Matrix4* transform) const;
  bool getWorldTransform(SceneNodeHandle node, Matrix4* transform);
  void updateWorldTransforms();

  bool setEnabled(SceneNodeHandle node, bool enabled);
  bool getEnabled(SceneNodeHandle node, bool* enabled) const;
  using DrawableBase::setVisible;
  bool setVisible(SceneNodeHandle node, bool nodeVisible);
  bool getVisible(SceneNodeHandle node, bool* nodeVisible) const;

  bool setRenderAttachment(SceneNodeHandle node,
                           ISceneRenderAttachment* attachment);
  ISceneRenderAttachment* getRenderAttachment(SceneNodeHandle node) const;

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
