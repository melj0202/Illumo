#pragma once

#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/IMesh.h>
#include <Illumo/Rendering/ISceneRenderAttachment.h>
#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <array>
#include <glm/glm.hpp>

class Renderer;

// Analytical unbounded implicit ground plane host: renders an infinite
// checkerboard plane at y = planeY via camera ray intersection in clip space.
// Writes perspective depth and receives directional lighting and shadows.
class GroundPlaneVisual
  : public DrawableBase
  , public ISceneRenderAttachment
{
public:
  GroundPlaneVisual();
  ~GroundPlaneVisual() override;

  GroundPlaneVisual(const GroundPlaneVisual&) = delete;
  GroundPlaneVisual& operator=(const GroundPlaneVisual&) = delete;
  GroundPlaneVisual(GroundPlaneVisual&&) = delete;
  GroundPlaneVisual& operator=(GroundPlaneVisual&&) = delete;

  void prepare(Renderer* renderer);

  void setColor0(ColorRgba color);
  void setColor0(const glm::vec4& color) { m_color0 = color; }
  const glm::vec4& getColor0() const { return m_color0; }

  void setColor1(ColorRgba color);
  void setColor1(const glm::vec4& color) { m_color1 = color; }
  const glm::vec4& getColor1() const { return m_color1; }

  void setCheckSize(float size) { m_checkSize = size; }
  float getCheckSize() const { return m_checkSize; }

  void setPlaneY(float y) { m_planeY = y; }
  float getPlaneY() const { return m_planeY; }

  void setLightingEnabled(bool enabled) { m_lightingEnabled = enabled; }
  bool isLightingEnabled() const { return m_lightingEnabled; }

  void setLightDirection(const glm::vec3& dir) { m_lightDirection = dir; }
  const glm::vec3& getLightDirection() const { return m_lightDirection; }

  void setLightColor(const glm::vec3& color) { m_lightColor = color; }
  const glm::vec3& getLightColor() const { return m_lightColor; }

  void setAmbientColor(const glm::vec3& color) { m_ambientColor = color; }
  const glm::vec3& getAmbientColor() const { return m_ambientColor; }

  void setShadowsEnabled(bool enabled) { m_shadowsEnabled = enabled; }
  bool isShadowsEnabled() const { return m_shadowsEnabled; }

  void setShadowBias(float bias) { m_shadowBias = bias; }
  float getShadowBias() const { return m_shadowBias; }

  void setShadowSlopeScale(float scale) { m_shadowSlopeScale = scale; }
  float getShadowSlopeScale() const { return m_shadowSlopeScale; }

  void setShadowNormalOffset(float offset) { m_shadowNormalOffset = offset; }
  float getShadowNormalOffset() const { return m_shadowNormalOffset; }

  void setShadowPcfEnabled(bool enabled) { m_shadowPcfEnabled = enabled; }
  bool isShadowPcfEnabled() const { return m_shadowPcfEnabled; }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;
  bool has3DWorldContent() const override { return isVisible(); }
  void appendSceneCommands(Renderer* renderer,
                           const Matrix4& worldTransform) override;
  bool getSceneLocalBounds(AxisAlignedBounds3* bounds) const override
  {
    (void)bounds;
    return false;
  }

private:
  Renderer* m_renderer = nullptr;
  MeshHandle m_cubeMesh{};
  glm::vec4 m_color0 =
    glm::vec4(235.0f / 255.0f, 120.0f / 255.0f, 24.0f / 255.0f, 1.0f);
  glm::vec4 m_color1 =
    glm::vec4(35.0f / 255.0f, 38.0f / 255.0f, 46.0f / 255.0f, 1.0f);
  float m_checkSize = 1.0f;
  float m_planeY = 0.0f;
  bool m_lightingEnabled = true;
  glm::vec3 m_lightDirection{ 0.6f, 1.0f, 0.8f };
  glm::vec3 m_lightColor{ 1.0f, 0.98f, 0.92f };
  glm::vec3 m_ambientColor{ 0.28f, 0.26f, 0.26f };
  bool m_shadowsEnabled = true;
  float m_shadowBias = 0.001f;
  float m_shadowSlopeScale = 0.002f;
  float m_shadowNormalOffset = 0.005f;
  bool m_shadowPcfEnabled = true;

  void releaseMesh();
};
