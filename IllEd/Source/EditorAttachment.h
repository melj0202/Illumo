#pragma once

#include "IlscCodec.h"
#include <Illumo/Rendering/ISceneRenderAttachment.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <memory>

class Camera;
class Renderer;

class EditorAttachment : public ISceneRenderAttachment
{
public:
  EditorAttachment();
  ~EditorAttachment() override = default;

  EditorAttachment(const EditorAttachment&) = delete;
  EditorAttachment& operator=(const EditorAttachment&) = delete;

  bool configure(Renderer* renderer,
                 Camera* camera,
                 const IlscNode& node,
                 IlscWorldMode worldMode);
  bool getSceneLocalBounds(AxisAlignedBounds3* bounds) const override;
  uint64_t getSceneBoundsRevision() const override { return m_boundsRevision; }
  void appendSceneCommands(Renderer* renderer,
                           const Matrix4& worldTransform) override;

private:
  SceneNodeKind m_kind;
  std::unique_ptr<MeshVisual> m_visual;
  uint64_t m_boundsRevision = 1;
};
