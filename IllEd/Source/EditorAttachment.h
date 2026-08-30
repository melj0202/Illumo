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
  void appendSceneCommands(Renderer* renderer,
                           const Matrix4& worldTransform) override;

private:
  SceneNodeKind m_kind;
  std::unique_ptr<MeshVisual> m_visual;
};
