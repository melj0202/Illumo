#pragma once

#include <Illumo/Foundation/MathTypes.h>

class Renderer;

// Borrowed, token-only rendering seam for one persistent scene node. The
// owning SceneGraph resolves hierarchy state and supplies the world transform;
// implementations retain ownership of their resources and append commands to
// the provided backend-neutral Renderer.
class ISceneRenderAttachment
{
public:
  virtual ~ISceneRenderAttachment() = default;

  virtual void appendSceneCommands(Renderer* renderer,
                                   const Matrix4& worldTransform) = 0;
};
