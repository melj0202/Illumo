#pragma once

#include <Illumo/Foundation/MathTypes.h>
#include <cmath>
#include <cstdint>

// Shared value types for render primitives (D-R15).
// Top-left origin for Rect2. Primitives never own GPU objects.

struct ColorRgba
{
  unsigned char r = 255;
  unsigned char g = 255;
  unsigned char b = 255;
  unsigned char a = 255;
};

// Axis-aligned rect; origin is top-left, size is width/height.
struct Rect2
{
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
};

struct Transform2D
{
  float x = 0.0f;
  float y = 0.0f;
  float scaleX = 1.0f;
  float scaleY = 1.0f;
  float rotationRadians = 0.0f;
  // Normalized within the transformed primitive or GameVisual content bounds.
  float pivotX = 0.0f;
  float pivotY = 0.0f;

  Matrix4 toMatrix() const
  {
    const Matrix4 translationMatrix =
      glm::translate(Matrix4(1.0f), Vector3(x, y, 0.0f));
    const Matrix4 rotationMatrix =
      glm::rotate(Matrix4(1.0f), rotationRadians, Vector3(0.0f, 0.0f, 1.0f));
    const Matrix4 scaleMatrix =
      glm::scale(Matrix4(1.0f), Vector3(scaleX, scaleY, 1.0f));
    return translationMatrix * rotationMatrix * scaleMatrix;
  }

  static Transform2D fromMatrix(const Matrix4& matrix)
  {
    Transform2D transform;
    transform.x = matrix[3][0];
    transform.y = matrix[3][1];
    transform.scaleX = glm::length(Vector3(matrix[0]));
    transform.scaleY = glm::length(Vector3(matrix[1]));
    const float det2D =
      matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0];
    if (det2D < 0.0f) {
      transform.scaleY = -transform.scaleY;
    }
    if (transform.scaleX > 0.00001f) {
      transform.rotationRadians = std::atan2(matrix[0][1], matrix[0][0]);
    }
    return transform;
  }
};

struct TextureRegion
{
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 1.0f;
  float v1 = 1.0f;

  static TextureRegion gridCell(unsigned int columns,
                                unsigned int rows,
                                unsigned int column,
                                unsigned int row)
  {
    TextureRegion region;
    if (columns == 0 || rows == 0 || column >= columns || row >= rows) {
      return region;
    }
    const float cellWidth = 1.0f / static_cast<float>(columns);
    const float cellHeight = 1.0f / static_cast<float>(rows);
    region.u0 = static_cast<float>(column) * cellWidth;
    region.v0 = static_cast<float>(row) * cellHeight;
    region.u1 = region.u0 + cellWidth;
    region.v1 = region.v0 + cellHeight;
    return region;
  }
};

// How GameVisual interprets primitive coordinates when emitting tokens.
enum class PrimitiveSpace : unsigned char
{
  Pixels = 0, // window pixels (y down, top-left origin) — UI/debug default
  World = 1   // world units via camera MVP
};
