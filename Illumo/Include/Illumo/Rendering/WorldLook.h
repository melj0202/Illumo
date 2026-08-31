#pragma once

#include <Illumo/Foundation/MathTypes.h>
#include <Illumo/Scene/Transform3D.h>
#include <glm/gtc/matrix_transform.hpp>

// Canonical world/overlay look (D-R21). Shape and Sprite programs position
// vertices with uMVP only. Overlay chrome uses a Y-down screen ortho; world
// objects use camera view-projection times node world (billboard optional).
struct WorldLook
{
  static constexpr const char* kMvpUniform = "uMVP";
  static constexpr const char* kModelUniform = "uModel";
  static constexpr const char* kLightSpaceMatrixUniform = "uLightSpaceMatrix";
  static constexpr const char* kLightDirUniform = "uLightDir";
  static constexpr const char* kLightColorUniform = "uLightColor";
  static constexpr const char* kAmbientColorUniform = "uAmbientColor";
  static constexpr const char* kShadowMapUniform = "uShadowMap";
  static constexpr const char* kTextureUniform = "uTexture";
  static constexpr const char* kResolutionUniform = "u_resolution";
  static constexpr int kTextureUnit = 0;
  static constexpr int kShadowTextureUnit = 1;
  static constexpr int kPositionLocation = 0;
  static constexpr int kColorLocation = 1;
  static constexpr int kUvLocation = 2;
  static constexpr int kNormalLocation = 3;
  static constexpr float kOverlayNear = -1.0f;
  static constexpr float kOverlayFar = 1.0f;

  static Matrix4 overlayProjection(float width, float height)
  {
    const float safeWidth = width > 0.0f ? width : 1.0f;
    const float safeHeight = height > 0.0f ? height : 1.0f;
    return glm::ortho(
      0.0f, safeWidth, safeHeight, 0.0f, kOverlayNear, kOverlayFar);
  }

  static Matrix4 billboardWorld(const Matrix4& nodeWorld, const Matrix4& view)
  {
    const Transform3D decomp = Transform3D::fromMatrix(nodeWorld);
    const Vector3& position = decomp.position;
    const Vector3& scale = decomp.scale;
    const Matrix4 invView = glm::inverse(view);
    Vector3 right(invView[0][0], invView[0][1], invView[0][2]);
    Vector3 up(invView[1][0], invView[1][1], invView[1][2]);
    Vector3 forward(invView[2][0], invView[2][1], invView[2][2]);
    const float rightLength = glm::length(right);
    const float upLength = glm::length(up);
    const float forwardLength = glm::length(forward);
    if (rightLength <= 0.0001f || upLength <= 0.0001f ||
        forwardLength <= 0.0001f) {
      return nodeWorld;
    }
    right = right / rightLength;
    up = up / upLength;
    forward = forward / forwardLength;

    Matrix4 result(1.0f);
    result[0] = Vector4(right * scale.x, 0.0f);
    result[1] = Vector4(up * scale.y, 0.0f);
    result[2] = Vector4(forward * scale.z, 0.0f);
    result[3] = Vector4(position, 1.0f);
    return result;
  }
};
