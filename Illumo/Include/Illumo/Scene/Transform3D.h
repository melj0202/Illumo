#pragma once

#include <Illumo/Foundation/MathTypes.h>
#include <cmath>
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

  static Transform3D fromMatrix(const Matrix4& matrix)
  {
    Transform3D transform;
    transform.position = Vector3(matrix[3][0], matrix[3][1], matrix[3][2]);
    transform.scale.x = glm::length(Vector3(matrix[0]));
    transform.scale.y = glm::length(Vector3(matrix[1]));
    transform.scale.z = glm::length(Vector3(matrix[2]));

    const Vector3 crossXy = glm::cross(Vector3(matrix[0]), Vector3(matrix[1]));
    if (glm::dot(crossXy, Vector3(matrix[2])) < 0.0f) {
      transform.scale.z = -transform.scale.z;
    }

    Matrix3 rotMat(1.0f);
    if (std::abs(transform.scale.x) > 0.00001f) {
      rotMat[0] = Vector3(matrix[0]) / transform.scale.x;
    }
    if (std::abs(transform.scale.y) > 0.00001f) {
      rotMat[1] = Vector3(matrix[1]) / transform.scale.y;
    }
    if (std::abs(transform.scale.z) > 0.00001f) {
      rotMat[2] = Vector3(matrix[2]) / transform.scale.z;
    }
    transform.rotation = glm::quat_cast(rotMat);
    return transform;
  }
};
