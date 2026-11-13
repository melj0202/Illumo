#include <Illumo/Rendering/Primitives/GroundPlaneVisual.h>

#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/SkyboxVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/WorldLook.h>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>

GroundPlaneVisual::GroundPlaneVisual() = default;

GroundPlaneVisual::~GroundPlaneVisual()
{
  releaseMesh();
}

void
GroundPlaneVisual::setColor0(ColorRgba color)
{
  m_color0 = glm::vec4(static_cast<float>(color.r) / 255.0f,
                       static_cast<float>(color.g) / 255.0f,
                       static_cast<float>(color.b) / 255.0f,
                       static_cast<float>(color.a) / 255.0f);
}

void
GroundPlaneVisual::setColor1(ColorRgba color)
{
  m_color1 = glm::vec4(static_cast<float>(color.r) / 255.0f,
                       static_cast<float>(color.g) / 255.0f,
                       static_cast<float>(color.b) / 255.0f,
                       static_cast<float>(color.a) / 255.0f);
}

void
GroundPlaneVisual::prepare(Renderer* renderer)
{
  if (renderer == nullptr) {
    return;
  }
  if (m_renderer != nullptr && m_renderer != renderer) {
    releaseMesh();
  }
  m_renderer = renderer;
  if (!m_cubeMesh.isValid()) {
    const std::array<float, 24>& vertices = SkyboxVisual::cubeVertices();
    const std::array<unsigned int, 36>& indices = SkyboxVisual::cubeIndices();
    m_cubeMesh = m_renderer->enrollMesh(vertices.data(),
                                        sizeof(vertices),
                                        indices.data(),
                                        sizeof(indices),
                                        MeshVertexLayout::Pos3,
                                        false);
  }
}

void
GroundPlaneVisual::releaseMesh()
{
  if (m_renderer != nullptr && m_cubeMesh.isValid()) {
    m_renderer->destroyMesh(m_cubeMesh);
    m_cubeMesh = MeshHandle{};
  }
}

bool
GroundPlaneVisual::AppendCommands(Renderer* renderer)
{
  if (!isVisible()) {
    return true;
  }
  if (renderer == nullptr) {
    return false;
  }
  ILLUMO_PROFILE_ZONE("GroundPlaneVisual.AppendCommands");
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
  const glm::mat4 fullVp = projection * view;
  const glm::vec3 cameraPos = activeCamera->getEye();

  const Renderer::ShadowFrameContext& shadow =
    renderer->getShadowFrameContext();
  const bool shadowMapBound = m_lightingEnabled && m_shadowsEnabled &&
                              shadow.active && shadow.depthTexture.isValid();
  glm::mat4 lightSpaceMatrix(1.0f);
  glm::vec3 activeLightDir = m_lightDirection;
  if (shadowMapBound) {
    std::memcpy(glm::value_ptr(lightSpaceMatrix),
                shadow.lightSpaceMatrix.data(),
                shadow.lightSpaceMatrix.size() * sizeof(float));
    activeLightDir = glm::vec3(shadow.lightDirection[0],
                               shadow.lightDirection[1],
                               shadow.lightDirection[2]);
  }

  if (!renderer->bindStyle(RenderStyleId::GroundPlane)) {
    return false;
  }

  renderer->pushUniformMat4("uViewProjection", glm::value_ptr(skyboxMvp));
  renderer->pushUniformMat4("uFullViewProjection", glm::value_ptr(fullVp));
  renderer->pushUniformVec3(
    "uCameraPos", cameraPos.x, cameraPos.y, cameraPos.z);
  renderer->pushUniformFloat("uPlaneY", m_planeY);
  renderer->pushUniformFloat("uCheckSize", m_checkSize);
  renderer->pushUniformVec4(
    "uColor0", m_color0.x, m_color0.y, m_color0.z, m_color0.w);
  renderer->pushUniformVec4(
    "uColor1", m_color1.x, m_color1.y, m_color1.z, m_color1.w);

  renderer->pushUniformVec3(
    "uLightDir", activeLightDir.x, activeLightDir.y, activeLightDir.z);
  renderer->pushUniformVec3(
    "uLightColor", m_lightColor.x, m_lightColor.y, m_lightColor.z);
  renderer->pushUniformVec3(
    "uAmbientColor", m_ambientColor.x, m_ambientColor.y, m_ambientColor.z);
  renderer->pushUniformInt("uShadowsEnabled", shadowMapBound ? 1 : 0);
  renderer->pushUniformMat4("uLightSpaceMatrix",
                            glm::value_ptr(lightSpaceMatrix));
  renderer->pushUniformFloat("uShadowBias", m_shadowBias);
  renderer->pushUniformFloat("uShadowSlopeScale", m_shadowSlopeScale);
  renderer->pushUniformFloat("uShadowNormalOffset", m_shadowNormalOffset);
  renderer->pushUniformInt("uShadowPcf", m_shadowPcfEnabled ? 1 : 0);

  if (shadowMapBound) {
    renderer->pushSetTexture(shadow.depthTexture,
                             WorldLook::kShadowTextureUnit);
    renderer->pushUniformInt("uShadowMap", WorldLook::kShadowTextureUnit);
  }

  renderer->pushSetMesh(m_cubeMesh);
  renderer->pushDrawIndexed(36, 0);

  return true;
}

void
GroundPlaneVisual::appendSceneCommands(Renderer* renderer,
                                       const Matrix4& worldTransform)
{
  m_planeY = worldTransform[3].y;
  (void)AppendCommands(renderer);
}
