#pragma once

#include <Illumo/Foundation/AxisAlignedBounds3.h>
#include <Illumo/Foundation/MathTypes.h>
#include <cstdint>

class Renderer;

// Borrowed, token-only rendering seam for one persistent scene node. The
// owning SceneGraph resolves hierarchy state and supplies the world transform;
// implementations retain ownership of their resources and append commands to
// the provided backend-neutral Renderer.
class ISceneRenderAttachment
{
public:
  virtual ~ISceneRenderAttachment() = default;

  // Zero is uncacheable. Nonzero revisions change whenever bounds or validity
  // change. Calls stay on the owner thread.
  virtual uint64_t getSceneBoundsRevision() const { return 0; }

  // Returns conservative bounds in node-local space. Attachments without a
  // reliable finite bound remain unbounded and are never culled.
  virtual bool getSceneLocalBounds(AxisAlignedBounds3* bounds) const
  {
    (void)bounds;
    return false;
  }

  virtual void appendSceneCommands(Renderer* renderer,
                                   const Matrix4& worldTransform) = 0;

  virtual void collectSceneShadowCasters(Renderer* renderer,
                                         const Matrix4& worldTransform)
  {
    (void)renderer;
    (void)worldTransform;
  }

  virtual void appendSceneShadowCommands(Renderer* renderer,
                                         const Matrix4& worldTransform)
  {
    (void)renderer;
    (void)worldTransform;
  }
};
