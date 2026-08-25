#pragma once

#include <Illumo/Foundation/MathTypes.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

struct Transform3D
{
  Vector3 position{ 0.0f, 0.0f, 0.0f };
  Quaternion rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
  Vector3 scale{ 1.0f, 1.0f, 1.0f };

  Matrix4 toMatrix() const
  {
    const Matrix4 translationMatrix = glm::translate(Matrix4(1.0f), position);
    const Matrix4 rotationMatrix = glm::mat4_cast(rotation);
    const Matrix4 scaleMatrix = glm::scale(Matrix4(1.0f), scale);
    return translationMatrix * rotationMatrix * scaleMatrix;
  }

  static Transform3D fromPosition(const Vector3& pos)
  {
    Transform3D transform;
    transform.position = pos;
    return transform;
  }

  static Transform3D fromEuler(float pitchRadians,
                               float yawRadians,
                               float rollRadians)
  {
    Transform3D transform;
    transform.rotation =
      Quaternion(Vector3(pitchRadians, yawRadians, rollRadians));
    return transform;
  }
};
