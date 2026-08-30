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
  (void)camera;
  m_kind = node.kind;
  m_visual.reset();
  if (renderer == nullptr || !IlscCodec::kindHasGeometry(node.kind)) {
    return false;
  }
  const Vector3 extent = node.primitive.extent;
  const ColorRgba color = node.primitive.color;
  m_visual = std::make_unique<MeshVisual>();
  m_visual->prepare(renderer);

  const bool use3d =
    worldMode == IlscWorldMode::World3D || IlscCodec::kindIs3D(node.kind);
  if (use3d) {
    if (node.kind == SceneNodeKind::SolidCube) {
      m_visual->addSolidCube(glm::vec3(0.0f), extent, color);
    } else if (node.kind == SceneNodeKind::SolidPyramid) {
      m_visual->addSolidPyramid(glm::vec3(0.0f), extent, color);
    } else if (node.kind == SceneNodeKind::WireSphere) {
      m_visual->addWireSphere(glm::vec3(0.0f),
                              std::max(extent.x, std::max(extent.y, extent.z)),
                              color);
    } else if (node.kind == SceneNodeKind::FilledTriangle) {
      m_visual->addSolidPyramid(glm::vec3(0.0f), extent, color);
    } else if (node.kind == SceneNodeKind::FilledEllipse) {
      m_visual->addWireSphere(glm::vec3(0.0f), extent.x, color);
    } else {
      m_visual->addSolidCube(
        glm::vec3(0.0f), glm::vec3(extent.x, extent.y, 0.02f), color);
    }
  } else {
    if (node.kind == SceneNodeKind::FilledRect) {
      m_visual->addQuad(
        glm::vec3(0.0f), glm::vec2(extent.x * 2.0f, extent.y * 2.0f), color);
    } else if (node.kind == SceneNodeKind::FilledEllipse) {
      m_visual->addSolidEllipse(
        glm::vec3(0.0f), glm::vec2(extent.x, extent.y), color, 32);
    } else if (node.kind == SceneNodeKind::FilledTriangle) {
      m_visual->addSolidTriangle(glm::vec3(-extent.x, -extent.y, 0.0f),
                                 glm::vec3(extent.x, -extent.y, 0.0f),
                                 glm::vec3(0.0f, extent.y, 0.0f),
                                 color);
    } else if (node.kind == SceneNodeKind::SolidCube) {
      m_visual->addSolidCube(glm::vec3(0.0f), extent, color);
    } else if (node.kind == SceneNodeKind::SolidPyramid) {
      m_visual->addSolidPyramid(glm::vec3(0.0f), extent, color);
    } else if (node.kind == SceneNodeKind::WireSphere) {
      m_visual->addWireSphere(glm::vec3(0.0f),
                              std::max(extent.x, std::max(extent.y, extent.z)),
                              color);
    }
  }
  return true;
}

void
EditorAttachment::appendSceneCommands(Renderer* renderer,
                                      const Matrix4& worldTransform)
{
  if (m_visual) {
    m_visual->appendSceneCommands(renderer, worldTransform);
  }
}
