#pragma once

#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <array>

enum class ShapeKind : unsigned char
{
  FilledRect = 0,
  OutlineRect = 1,
  Line = 2,
  FilledEllipse = 3,
  FilledTriangle = 4,
  // Convex quad in fan order (0,1,2)+(2,3,0) with one color per vertex; the
  // backend interpolates colors across each of the two triangles.
  GradientQuad = 5
};

// Value-type shape description. Compose many on a GameVisual (or any host
// that owns a GameVisual) to build more complex objects — the primitive itself
// is not a Scene Drawable.
struct ShapePrimitive
{
  ShapeKind kind = ShapeKind::FilledRect;
  Rect2 rect;      // FilledRect / OutlineRect / FilledEllipse bounds
  float x0 = 0.0f; // Line endpoints or triangle vertex 0
  float y0 = 0.0f;
  float x1 = 0.0f;
  float y1 = 0.0f;
  float x2 = 0.0f; // FilledTriangle / GradientQuad vertex 2
  float y2 = 0.0f;
  float x3 = 0.0f; // GradientQuad vertex 3
  float y3 = 0.0f;
  float lineWidth = 1.0f; // OutlineRect border / Line thickness in space units
  ColorRgba color;        // GradientQuad: vertexColors[0]
  // GradientQuad per-vertex colors; other kinds use `color`.
  std::array<ColorRgba, 4> vertexColors{};
  Transform2D transform;
  RenderStyleHandle styleHandle{};
  int drawOrder = 0;
  bool visible = true;
};
