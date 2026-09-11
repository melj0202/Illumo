#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/SkyboxVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <array>
#include <glm/gtc/type_ptr.hpp>

static const float kSkyboxUnitCubeVertices[24] = {
  -1.0f, -1.0f, -1.0f, // 0
  1.0f,  -1.0f, -1.0f, // 1
  1.0f,  1.0f,  -1.0f, // 2
  -1.0f, 1.0f,  -1.0f, // 3
  -1.0f, -1.0f, 1.0f,  // 4
  1.0f,  -1.0f, 1.0f,  // 5
  1.0f,  1.0f,  1.0f,  // 6
  -1.0f, 1.0f,  1.0f   // 7
};

static const unsigned int kSkyboxUnitCubeIndices[36] = {
  // Front (-Z)
  0,
  2,
  1,
  0,
  3,
  2,
  // Back (+Z)
  4,
  5,
  6,
  4,
  6,
  7,
  // Left (-X)
  0,
  7,
  3,
  0,
  4,
  7,
  // Right (+X)
  1,
  6,
  2,
  1,
  5,
  6,
  // Top (+Y)
  3,
  6,
  2,
  3,
  7,
  6,
  // Bottom (-Y)
  0,
  1,
  5,
  0,
  5,
  4
};

SkyboxVisual::SkyboxVisual() = default;

SkyboxVisual::SkyboxVisual(TextureHandle cubemap)
  : m_cubemap(cubemap)
{
}

SkyboxVisual::~SkyboxVisual()
{
  releaseMesh();
}

void
SkyboxVisual::setTint(ColorRgba tint)
{
  m_tint = glm::vec4(static_cast<float>(tint.r) / 255.0f,
                     static_cast<float>(tint.g) / 255.0f,
                     static_cast<float>(tint.b) / 255.0f,
                     static_cast<float>(tint.a) / 255.0f);
}

void
SkyboxVisual::prepare(Renderer* renderer)
{
  if (renderer == nullptr) {
    return;
  }
  if (m_renderer != nullptr && m_renderer != renderer) {
    releaseMesh();
  }
  m_renderer = renderer;
  if (!m_cubeMesh.isValid()) {
    m_cubeMesh = m_renderer->enrollMesh(kSkyboxUnitCubeVertices,
                                        sizeof(kSkyboxUnitCubeVertices),
                                        kSkyboxUnitCubeIndices,
                                        sizeof(kSkyboxUnitCubeIndices),
                                        MeshVertexLayout::Pos3,
                                        false);
  }
}

void
SkyboxVisual::releaseMesh()
{
  if (m_renderer != nullptr && m_cubeMesh.isValid()) {
    m_renderer->destroyMesh(m_cubeMesh);
    m_cubeMesh = MeshHandle{};
  }
}

bool
SkyboxVisual::AppendCommands(Renderer* renderer)
{
  if (!isVisible() || !m_cubemap.isValid()) {
    return true;
  }
  if (renderer == nullptr) {
    return false;
  }
  prepare(renderer);
  if (!m_cubeMesh.isValid()) {
    return false;
  }

  Camera* activeCamera = nullptr;
  const Renderer::FrameContext& frameContext = renderer->getFrameContext();
  if (frameContext.active) {
    activeCamera = frameContext.worldCamera;
  }
  if (activeCamera == nullptr) {
    activeCamera = renderer->getCamera();
  }
  if (activeCamera == nullptr) {
    return false;
  }

  std::array<int, 2> dimensions{ 1280, 720 };
  if (frameContext.active) {
    dimensions = frameContext.windowDimensions;
  } else if (renderer->getWindow() != nullptr) {
    dimensions = renderer->getWindow()->getWindowDimensions();
  }

  const float aspect =
    static_cast<float>(dimensions[0]) /
    static_cast<float>(dimensions[1] > 0 ? dimensions[1] : 1);
  const glm::mat4 projection = activeCamera->GetProjectionMatrix(aspect);
  const glm::mat4 view = activeCamera->GetViewMatrix();
  const glm::mat4 skyboxView = glm::mat4(glm::mat3(view));
  const glm::mat4 skyboxMvp = projection * skyboxView;

  if (!renderer->bindStyle(RenderStyleId::Skybox)) {
    return false;
  }

  renderer->pushSetTexture(m_cubemap, 0);
  renderer->pushUniformInt("uSkybox", 0);
  renderer->pushUniformVec4("uTint", m_tint.x, m_tint.y, m_tint.z, m_tint.w);
  renderer->pushUniformMat4("uViewProjection", glm::value_ptr(skyboxMvp));
  renderer->pushSetMesh(m_cubeMesh);
  renderer->pushDrawIndexed(36, 0);

  return true;
}

void
SkyboxVisual::appendSceneCommands(Renderer* renderer,
                                  const Matrix4& worldTransform)
{
  (void)worldTransform;
  (void)AppendCommands(renderer);
}
