#pragma once

#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/IMesh.h>
#include <Illumo/Rendering/ISceneRenderAttachment.h>
#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <array>
#include <glm/glm.hpp>

class Renderer;

// 3D Cubemap Skybox host: renders a background cube with infinite depth (far
// plane) sampled from a cubemap texture using the active camera orientation.
class SkyboxVisual
  : public DrawableBase
  , public ISceneRenderAttachment
{
public:
  SkyboxVisual();
  explicit SkyboxVisual(TextureHandle cubemap);
  ~SkyboxVisual() override;

  SkyboxVisual(const SkyboxVisual&) = delete;
  SkyboxVisual& operator=(const SkyboxVisual&) = delete;
  SkyboxVisual(SkyboxVisual&&) = delete;
  SkyboxVisual& operator=(SkyboxVisual&&) = delete;

  void prepare(Renderer* renderer);

  void setCubemap(TextureHandle handle) { m_cubemap = handle; }
  TextureHandle getCubemap() const { return m_cubemap; }

  void setTint(ColorRgba tint);
  void setTint(const glm::vec4& tint) { m_tint = tint; }
  const glm::vec4& getTint() const { return m_tint; }

  // The unit cube (Pos3) every sky draws: SkyboxVisual and RenderWorld's.
  static const std::array<float, 24>& cubeVertices();
  static const std::array<unsigned int, 36>& cubeIndices();

  void Draw() override {}
  // Offers itself to IBackend::AppendSkybox first: a guest with HostRender
  // shows it as its host render world's sky instead of recording a cube.
  bool AppendCommands(Renderer* renderer) override;
  void appendSceneCommands(Renderer* renderer,
                           const Matrix4& worldTransform) override;

private:
  Renderer* m_renderer = nullptr;
  TextureHandle m_cubemap{};
  MeshHandle m_cubeMesh{};
  glm::vec4 m_tint = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

  void releaseMesh();
};
