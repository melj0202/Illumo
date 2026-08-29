#include "EditorAttachment.h"

#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Renderer.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

EditorAttachment::EditorAttachment()
  : m_kind(SceneNodeKind::Empty)
{
}

bool
EditorAttachment::configure(Renderer* renderer,
                            Camera* camera,
                            const IlscNode& node,
                            IlscWorldMode worldMode)
{
  m_kind = node.kind;
  m_draw3d.reset();
  m_visual2d.reset();
  if (renderer == nullptr || !IlscCodec::kindHasGeometry(node.kind)) {
    return false;
  }
  const Vector3 extent = node.primitive.extent;
  const ColorRgba color = node.primitive.color;
  const bool use3d =
    worldMode == IlscWorldMode::World3D || IlscCodec::kindIs3D(node.kind);
  if (use3d) {
    m_draw3d = std::make_unique<MeshVisual>();
    m_draw3d->prepare(renderer);
    if (node.kind == SceneNodeKind::SolidCube) {
      m_draw3d->addSolidCube(glm::vec3(0.0f), extent, color);
    } else if (node.kind == SceneNodeKind::SolidPyramid) {
      m_draw3d->addSolidPyramid(glm::vec3(0.0f), extent, color);
    } else if (node.kind == SceneNodeKind::WireSphere) {
      m_draw3d->addWireSphere(glm::vec3(0.0f),
                              std::max(extent.x, std::max(extent.y, extent.z)),
                              color);
    } else if (node.kind == SceneNodeKind::FilledTriangle) {
      m_draw3d->addSolidPyramid(glm::vec3(0.0f), extent, color);
    } else if (node.kind == SceneNodeKind::FilledEllipse) {
      m_draw3d->addWireSphere(glm::vec3(0.0f), extent.x, color);
    } else {
      m_draw3d->addSolidCube(
        glm::vec3(0.0f), glm::vec3(extent.x, extent.y, 0.02f), color);
    }
    return true;
  }

  m_visual2d = std::make_unique<GameVisual>(256u);
  m_visual2d->setRenderer(renderer);
  m_visual2d->setCamera(camera);
  m_visual2d->setSpace(PrimitiveSpace::World);
  m_visual2d->setLayerHint(RenderLayerId::World);
  m_visual2d->prepare(renderer);
  if (node.kind == SceneNodeKind::FilledRect) {
    m_visual2d->addFilledRect(
      -extent.x, -extent.y, extent.x * 2.0f, extent.y * 2.0f, color);
  } else if (node.kind == SceneNodeKind::FilledEllipse) {
    m_visual2d->addFilledEllipse(
      -extent.x, -extent.y, extent.x * 2.0f, extent.y * 2.0f, color);
  } else {
    m_visual2d->addFilledTriangle(
      0.0f, extent.y, -extent.x, -extent.y, extent.x, -extent.y, color);
  }
  return true;
}

void
EditorAttachment::appendSceneCommands(Renderer* renderer,
                                      const Matrix4& worldTransform)
{
  if (m_draw3d) {
    m_draw3d->appendSceneCommands(renderer, worldTransform);
    return;
  }
  if (!m_visual2d) {
    return;
  }
  Transform2D local;
  local.x = worldTransform[3][0];
  local.y = worldTransform[3][1];
  local.scaleX = glm::length(glm::vec3(worldTransform[0]));
  local.scaleY = glm::length(glm::vec3(worldTransform[1]));
  m_visual2d->setTransform(local);
  m_visual2d->setRenderer(renderer);
  (void)m_visual2d->AppendCommands(renderer);
}
