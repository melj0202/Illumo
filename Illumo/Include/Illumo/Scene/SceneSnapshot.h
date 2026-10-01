#pragma once

#include <Illumo/Foundation/AxisAlignedBounds3.h>
#include <Illumo/Scene/SceneNodeHandle.h>
#include <cstdint>
#include <memory>
#include <vector>

class ISceneRenderAttachment;
class Renderer;
class SceneGraph;
struct SceneSnapshotLifetime;

struct SceneRenderItem
{
  Matrix4 worldTransform{ 1.0f };
  AxisAlignedBounds3 worldBounds;
  ISceneRenderAttachment* attachment = nullptr;
  SceneNodeHandle node;
  bool boundsValid = false;
  bool cameraVisible = true;
};

struct SceneSnapshot
{
  uint64_t graphId = 0;
  uint64_t frameSerial = 0;
  uint64_t structuralRevision = 0;
  uint64_t publication = 0;
  std::vector<SceneRenderItem> items;
};

// Views expire on graph destruction, detach, explicit invalidation, or reuse
// of their ring slot. Check get() before each callback. Ordinary transform and
// hierarchy edits preserve already captured values for the current frame.
class SceneSnapshotView
{
public:
  const SceneSnapshot* get() const;

private:
  friend class SceneGraph;
  std::shared_ptr<SceneSnapshot> m_snapshot;
  std::shared_ptr<SceneSnapshotLifetime> m_lifetime;
  uint64_t m_epoch = 0;
  uint64_t m_publication = 0;
};
