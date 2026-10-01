#include "EditorGizmo.h"

#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

static const float kPi = 3.14159265358979f;

static const ColorRgba kAxisColors[3] = { { 235, 55, 65, 255 },
                                          { 55, 215, 75, 255 },
                                          { 55, 125, 245, 255 } };
static const ColorRgba kHighlight{ 255, 245, 75, 255 };

static int
axisOf(GizmoPart part)
{
  switch (part) {
    case GizmoPart::AxisX:
    case GizmoPart::RingX:
    case GizmoPart::PlaneYZ:
      return 0;
    case GizmoPart::AxisY:
    case GizmoPart::RingY:
    case GizmoPart::PlaneXZ:
      return 1;
    case GizmoPart::AxisZ:
    case GizmoPart::RingZ:
    case GizmoPart::PlaneXY:
      return 2;
    default:
      return -1;
  }
}

static bool
intersectPlane(const Vector3& rayOrigin,
               const Vector3& rayDirection,
               const Vector3& planePoint,
               const Vector3& normal,
               Vector3* point,
               float* distance)
{
  const float denominator = glm::dot(rayDirection, normal);
  if (std::fabs(denominator) < 1.0e-6f) {
    return false;
  }
  const float t = glm::dot(planePoint - rayOrigin, normal) / denominator;
  if (t < 0.0f) {
    return false;
  }
  *point = rayOrigin + rayDirection * t;
  if (distance != nullptr) {
    *distance = t;
  }
  return true;
}

// Distance between a ray and a segment, with the segment parameter [0, 1].
static float
rayToSegment(const Vector3& rayOrigin,
             const Vector3& rayDirection,
             const Vector3& a,
             const Vector3& b)
{
  const Vector3 u = rayDirection;
  const Vector3 v = b - a;
  const Vector3 w = rayOrigin - a;
  const float aa = glm::dot(u, u);
  const float bb = glm::dot(u, v);
  const float cc = glm::dot(v, v);
  const float dd = glm::dot(u, w);
  const float ee = glm::dot(v, w);
  const float denominator = aa * cc - bb * bb;
  float s = 0.0f;
  float t = 0.0f;
  if (denominator < 1.0e-8f) {
    t = cc > 0.0f ? std::clamp(ee / cc, 0.0f, 1.0f) : 0.0f;
  } else {
    s = std::max(0.0f, (bb * ee - cc * dd) / denominator);
    t = std::clamp((aa * ee - bb * dd) / denominator, 0.0f, 1.0f);
  }
  return glm::length(w + u * s - v * t);
}

// Closest point on an infinite line to a ray.
static bool
closestOnLine(const Vector3& rayOrigin,
              const Vector3& rayDirection,
              const Vector3& lineOrigin,
              const Vector3& lineDirection,
              Vector3* point)
{
  const Vector3 w = rayOrigin - lineOrigin;
  const float aa = glm::dot(rayDirection, rayDirection);
  const float bb = glm::dot(rayDirection, lineDirection);
  const float cc = glm::dot(lineDirection, lineDirection);
  const float dd = glm::dot(rayDirection, w);
  const float ee = glm::dot(lineDirection, w);
  const float denominator = aa * cc - bb * bb;
  if (std::fabs(denominator) < 1.0e-6f) {
    return false;
  }
  const float t = (aa * ee - bb * dd) / denominator;
  *point = lineOrigin + lineDirection * t;
  return true;
}

static float
snapValue(float value, float step)
{
  return step > 0.0f ? std::round(value / step) * step : value;
}

GizmoPart
EditorGizmo::hitTest(const GizmoFrame& frame,
                     GizmoMode mode,
                     const Vector3& rayOrigin,
                     const Vector3& rayDirection)
{
  const float scale = std::max(0.01f, frame.scale);
  const Vector3& origin = frame.origin;
  if (mode == GizmoMode::Rotate) {
    GizmoPart best = GizmoPart::None;
    float bestDistance = 1.0e30f;
    const GizmoPart rings[3] = { GizmoPart::RingX,
                                 GizmoPart::RingY,
                                 GizmoPart::RingZ };
    for (int axis = frame.is3D ? 0 : 2; axis < 3; ++axis) {
      Vector3 point;
      float distance = 0.0f;
      if (!intersectPlane(rayOrigin,
                          rayDirection,
                          origin,
                          frame.axes[axis],
                          &point,
                          &distance)) {
        continue;
      }
      const float radius = glm::length(point - origin);
      if (std::fabs(radius - kRingRadius * scale) <= 0.09f * scale &&
          distance < bestDistance) {
        best = rings[axis];
        bestDistance = distance;
      }
    }
    return best;
  }
  // Center handle: a small sphere at the origin.
  const float along = glm::dot(origin - rayOrigin, rayDirection);
  if (along >= 0.0f && glm::length(rayOrigin + rayDirection * along - origin) <=
                         (mode == GizmoMode::Scale ? 0.11f : 0.08f) * scale) {
    return GizmoPart::Center;
  }
  if (mode == GizmoMode::Translate) {
    struct Plane
    {
      GizmoPart part;
      int u;
      int v;
      int normal;
      bool needs3D;
    };
    const Plane planes[3] = { { GizmoPart::PlaneXY, 0, 1, 2, false },
                              { GizmoPart::PlaneXZ, 0, 2, 1, true },
                              { GizmoPart::PlaneYZ, 1, 2, 0, true } };
    GizmoPart best = GizmoPart::None;
    float bestDistance = 1.0e30f;
    for (const Plane& plane : planes) {
      if (plane.needs3D && !frame.is3D) {
        continue;
      }
      Vector3 point;
      float distance = 0.0f;
      if (!intersectPlane(rayOrigin,
                          rayDirection,
                          origin,
                          frame.axes[plane.normal],
                          &point,
                          &distance)) {
        continue;
      }
      const float a = glm::dot(point - origin, frame.axes[plane.u]);
      const float b = glm::dot(point - origin, frame.axes[plane.v]);
      const float size = kPlaneSize * scale;
      if (a >= 0.0f && a <= size && b >= 0.0f && b <= size &&
          distance < bestDistance) {
        best = plane.part;
        bestDistance = distance;
      }
    }
    if (best != GizmoPart::None) {
      return best;
    }
  }
  const GizmoPart axes[3] = { GizmoPart::AxisX,
                              GizmoPart::AxisY,
                              GizmoPart::AxisZ };
  GizmoPart best = GizmoPart::None;
  float bestDistance = 0.10f * scale;
  for (int axis = 0; axis < (frame.is3D ? 3 : 2); ++axis) {
    const float distance =
      rayToSegment(rayOrigin,
                   rayDirection,
                   origin,
                   origin + frame.axes[axis] * (kAxisLength * scale));
    if (distance < bestDistance) {
      best = axes[axis];
      bestDistance = distance;
    }
  }
  return best;
}

bool
EditorGizmo::constrain(const Vector3& rayOrigin,
                       const Vector3& rayDirection,
                       Vector3* point) const
{
  const int axis = axisOf(m_part);
  switch (m_part) {
    case GizmoPart::AxisX:
    case GizmoPart::AxisY:
    case GizmoPart::AxisZ:
      return closestOnLine(
        rayOrigin, rayDirection, m_frame.origin, m_frame.axes[axis], point);
    case GizmoPart::PlaneXY:
    case GizmoPart::PlaneXZ:
    case GizmoPart::PlaneYZ:
    case GizmoPart::RingX:
    case GizmoPart::RingY:
    case GizmoPart::RingZ:
      return intersectPlane(rayOrigin,
                            rayDirection,
                            m_frame.origin,
                            m_frame.axes[axis],
                            point,
                            nullptr);
    case GizmoPart::Center: {
      // Translate moves on the ground (3D) or the XY plane (2D); uniform
      // scale measures on the plane the grab started on.
      const Vector3 normal = m_mode == GizmoMode::Scale
                               ? m_start
                               : (m_frame.is3D ? Vector3(0.0f, 1.0f, 0.0f)
                                               : Vector3(0.0f, 0.0f, 1.0f));
      return intersectPlane(
        rayOrigin, rayDirection, m_frame.origin, normal, point, nullptr);
    }
    case GizmoPart::None:
      return false;
  }
  return false;
}

bool
EditorGizmo::begin(const GizmoFrame& frame,
                   GizmoMode mode,
                   GizmoPart part,
                   const Vector3& rayOrigin,
                   const Vector3& rayDirection)
{
  m_frame = frame;
  m_mode = mode;
  m_part = part;
  if (part == GizmoPart::None) {
    return false;
  }
  if (mode == GizmoMode::Scale && part == GizmoPart::Center) {
    // Measure uniform scale on the plane facing the camera.
    m_start =
      frame.is3D ? -glm::normalize(rayDirection) : Vector3(0.0f, 0.0f, 1.0f);
  }
  Vector3 point;
  if (!constrain(rayOrigin, rayDirection, &point)) {
    m_part = GizmoPart::None;
    return false;
  }
  if (mode == GizmoMode::Rotate) {
    const Vector3 offset = point - frame.origin;
    if (glm::length(offset) < 1.0e-6f) {
      m_part = GizmoPart::None;
      return false;
    }
    m_start = glm::normalize(offset);
  } else if (mode == GizmoMode::Scale) {
    const int axis = axisOf(part);
    m_startDistance = axis >= 0
                        ? glm::dot(point - frame.origin, frame.axes[axis])
                        : glm::length(point - frame.origin);
    if (std::fabs(m_startDistance) < 0.01f * std::max(0.01f, frame.scale)) {
      m_part = GizmoPart::None;
      return false;
    }
    if (axis >= 0) {
      m_start = point;
    }
  } else {
    m_start = point;
  }
  return true;
}

GizmoDelta
EditorGizmo::drag(const Vector3& rayOrigin,
                  const Vector3& rayDirection,
                  const GizmoSnap& snap) const
{
  GizmoDelta delta;
  Vector3 point;
  if (m_part == GizmoPart::None ||
      !constrain(rayOrigin, rayDirection, &point)) {
    return delta;
  }
  if (m_mode == GizmoMode::Translate) {
    const Vector3 moved = point - m_start;
    for (int axis = 0; axis < 3; ++axis) {
      float amount = glm::dot(moved, m_frame.axes[axis]);
      if (snap.enabled) {
        amount = snapValue(amount, snap.translate);
      }
      delta.translation += m_frame.axes[axis] * amount;
    }
    delta.valid = true;
    return delta;
  }
  if (m_mode == GizmoMode::Rotate) {
    const int axis = axisOf(m_part);
    const Vector3 offset = point - m_frame.origin;
    if (glm::length(offset) < 1.0e-6f) {
      return delta;
    }
    const Vector3 current = glm::normalize(offset);
    float angle =
      std::atan2(glm::dot(glm::cross(m_start, current), m_frame.axes[axis]),
                 glm::dot(m_start, current));
    if (snap.enabled && snap.angleDegrees > 0.0f) {
      angle = glm::radians(snapValue(glm::degrees(angle), snap.angleDegrees));
    }
    delta.rotation = glm::angleAxis(angle, m_frame.axes[axis]);
    delta.valid = true;
    return delta;
  }
  const int axis = axisOf(m_part);
  const float distance =
    axis >= 0 ? glm::dot(point - m_frame.origin, m_frame.axes[axis])
              : glm::length(point - m_frame.origin);
  float factor = distance / m_startDistance;
  if (snap.enabled && snap.scale > 0.0f) {
    factor = 1.0f + snapValue(factor - 1.0f, snap.scale);
  }
  factor = std::max(0.01f, factor);
  if (axis >= 0) {
    delta.scale[axis] = factor;
  } else {
    delta.scale = Vector3(factor);
  }
  delta.valid = true;
  return delta;
}

static ColorRgba
partColor(GizmoPart part, GizmoPart highlighted, ColorRgba base)
{
  return part == highlighted ? kHighlight : base;
}

void
EditorGizmo::draw(MeshVisual& visual,
                  const GizmoFrame& frame,
                  GizmoMode mode,
                  GizmoPart highlighted)
{
  const float scale = std::max(0.01f, frame.scale);
  const Vector3& origin = frame.origin;
  if (mode == GizmoMode::Rotate) {
    const GizmoPart rings[3] = { GizmoPart::RingX,
                                 GizmoPart::RingY,
                                 GizmoPart::RingZ };
    const int segments = 64;
    for (int axis = frame.is3D ? 0 : 2; axis < 3; ++axis) {
      const Vector3& u = frame.axes[(axis + 1) % 3];
      const Vector3& v = frame.axes[(axis + 2) % 3];
      const ColorRgba color =
        partColor(rings[axis], highlighted, kAxisColors[axis]);
      const float radius = kRingRadius * scale;
      for (int index = 0; index < segments; ++index) {
        const float a =
          2.0f * kPi * static_cast<float>(index) / static_cast<float>(segments);
        const float b = 2.0f * kPi * static_cast<float>(index + 1) /
                        static_cast<float>(segments);
        visual.addLine(origin + (u * std::cos(a) + v * std::sin(a)) * radius,
                       origin + (u * std::cos(b) + v * std::sin(b)) * radius,
                       color);
      }
    }
    visual.addSolidCube(
      origin, Vector3(scale * 0.03f), ColorRgba{ 245, 245, 245, 230 });
    return;
  }
  const ColorRgba centerColor =
    partColor(GizmoPart::Center, highlighted, ColorRgba{ 245, 245, 245, 230 });
  const float centerHalf = (mode == GizmoMode::Scale ? 0.06f : 0.035f) * scale;
  visual.addSolidCube(origin, Vector3(centerHalf), centerColor);
  visual.addWireCube(
    origin, Vector3(centerHalf * 1.1f), ColorRgba{ 30, 30, 30, 255 });
  if (mode == GizmoMode::Translate) {
    struct Plane
    {
      GizmoPart part;
      int u;
      int v;
      int normal;
      bool needs3D;
    };
    const Plane planes[3] = { { GizmoPart::PlaneXY, 0, 1, 2, false },
                              { GizmoPart::PlaneXZ, 0, 2, 1, true },
                              { GizmoPart::PlaneYZ, 1, 2, 0, true } };
    for (const Plane& plane : planes) {
      if (plane.needs3D && !frame.is3D) {
        continue;
      }
      const ColorRgba color =
        partColor(plane.part, highlighted, kAxisColors[plane.normal]);
      const float size = kPlaneSize * scale;
      const Vector3 a = origin + frame.axes[plane.u] * size;
      const Vector3 b = a + frame.axes[plane.v] * size;
      const Vector3 c = origin + frame.axes[plane.v] * size;
      visual.addLine(a, b, color);
      visual.addLine(c, b, color);
      const ColorRgba fill{ color.r, color.g, color.b, 65 };
      visual.addSolidTriangle(origin, a, b, fill);
      visual.addSolidTriangle(origin, b, c, fill);
    }
  }
  const GizmoPart axes[3] = { GizmoPart::AxisX,
                              GizmoPart::AxisY,
                              GizmoPart::AxisZ };
  const float capHalf = (mode == GizmoMode::Scale ? 0.05f : 0.035f) * scale;
  for (int axis = 0; axis < (frame.is3D ? 3 : 2); ++axis) {
    const ColorRgba color =
      partColor(axes[axis], highlighted, kAxisColors[axis]);
    const Vector3 tip = origin + frame.axes[axis] * (kAxisLength * scale);
    visual.addLine(origin, tip, color);
    visual.addSolidCube(tip, Vector3(capHalf), color);
  }
}
