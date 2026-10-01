#pragma once

#include <Illumo/Foundation/MathTypes.h>
#include <cmath>
#include <limits>

struct AxisAlignedBounds3
{
  Vector3 minimum = Vector3(0.0f);
  Vector3 maximum = Vector3(0.0f);

  bool isValid() const
  {
    for (int axis = 0; axis < 3; ++axis) {
      if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) ||
          minimum[axis] > maximum[axis]) {
        return false;
      }
    }
    return true;
  }

  bool intersects(const AxisAlignedBounds3& other) const
  {
    if (!isValid() || !other.isValid()) {
      return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
      if (maximum[axis] < other.minimum[axis] ||
          minimum[axis] > other.maximum[axis]) {
        return false;
      }
    }
    return true;
  }

  void include(const Vector3& point)
  {
    minimum = glm::min(minimum, point);
    maximum = glm::max(maximum, point);
  }

  void include(const AxisAlignedBounds3& other)
  {
    if (!other.isValid()) {
      return;
    }
    minimum = glm::min(minimum, other.minimum);
    maximum = glm::max(maximum, other.maximum);
  }

  bool transformed(const Matrix4& transform,
                   AxisAlignedBounds3* transformedBounds) const
  {
    if (transformedBounds == nullptr || !isValid()) {
      return false;
    }

    const float epsilon = 0.00001f;
    if (!std::isfinite(transform[0][3]) || !std::isfinite(transform[1][3]) ||
        !std::isfinite(transform[2][3]) || !std::isfinite(transform[3][3]) ||
        std::abs(transform[0][3]) > epsilon ||
        std::abs(transform[1][3]) > epsilon ||
        std::abs(transform[2][3]) > epsilon ||
        std::abs(transform[3][3] - 1.0f) > epsilon) {
      return false;
    }

    Vector3 nextMinimum(std::numeric_limits<float>::max());
    Vector3 nextMaximum(std::numeric_limits<float>::lowest());
    for (int x = 0; x < 2; ++x) {
      for (int y = 0; y < 2; ++y) {
        for (int z = 0; z < 2; ++z) {
          const Vector3 corner(x == 0 ? minimum.x : maximum.x,
                               y == 0 ? minimum.y : maximum.y,
                               z == 0 ? minimum.z : maximum.z);
          const Vector4 transformedCorner = transform * Vector4(corner, 1.0f);
          const Vector3 point(
            transformedCorner.x, transformedCorner.y, transformedCorner.z);
          if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
              !std::isfinite(point.z)) {
            return false;
          }
          nextMinimum = glm::min(nextMinimum, point);
          nextMaximum = glm::max(nextMaximum, point);
        }
      }
    }

    *transformedBounds = AxisAlignedBounds3{ nextMinimum, nextMaximum };
    return true;
  }
};
