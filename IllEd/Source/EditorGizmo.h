#pragma once

#include <Illumo/Foundation/MathTypes.h>
#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>

class MeshVisual;

enum class GizmoMode
{
  Translate,
  Rotate,
  Scale
};

enum class GizmoSpace
{
  World,
  Local
};

enum class GizmoPart
{
  None,
  Center,
  AxisX,
  AxisY,
  AxisZ,
  PlaneXY,
  PlaneXZ,
  PlaneYZ,
  RingX,
  RingY,
  RingZ
};

// Where the gizmo sits and how it is oriented: world axes, or the primary
// node's rotation in Local space. scale keeps a constant on-screen size.
struct GizmoFrame
{
  Vector3 origin{ 0.0f };
  // Unit world directions of the gizmo's X, Y and Z handles.
  Vector3 axes[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };
  float scale = 1.0f;
  // 2D worlds only offer X, Y, the XY plane and the Z ring.
  bool is3D = false;
};

struct GizmoSnap
{
  bool enabled = false;
  float translate = 0.5f;
  float angleDegrees = 15.0f;
  float scale = 0.1f;
};

// The drag result, cumulative from the press. Apply it to the transforms
// captured at the press, never to the previous frame's.
struct GizmoDelta
{
  bool valid = false;
  // World-space translation.
  Vector3 translation{ 0.0f };
  // World-space rotation about the gizmo origin.
  Quaternion rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
  // Scale factors along the gizmo's axes.
  Vector3 scale{ 1.0f };
};

// Transform gizmo geometry and drag math. It knows rays, frames and handles,
// not documents or input: the module turns mouse positions into rays and
// deltas into node transforms.
class EditorGizmo
{
public:
  static GizmoPart hitTest(const GizmoFrame& frame,
                           GizmoMode mode,
                           const Vector3& rayOrigin,
                           const Vector3& rayDirection);

  // Starts a drag on a handle; false when the ray cannot grab it (for
  // example an axis seen end-on).
  bool begin(const GizmoFrame& frame,
             GizmoMode mode,
             GizmoPart part,
             const Vector3& rayOrigin,
             const Vector3& rayDirection);
  GizmoDelta drag(const Vector3& rayOrigin,
                  const Vector3& rayDirection,
                  const GizmoSnap& snap) const;
  void end() { m_part = GizmoPart::None; }
  bool active() const { return m_part != GizmoPart::None; }
  GizmoPart part() const { return m_part; }
  GizmoMode mode() const { return m_mode; }
  const GizmoFrame& frame() const { return m_frame; }

  // Draws the handles for a mode; the hovered or active part is highlighted.
  static void draw(MeshVisual& visual,
                   const GizmoFrame& frame,
                   GizmoMode mode,
                   GizmoPart highlighted);

  // Ring radius and handle lengths in gizmo scale units.
  static constexpr float kAxisLength = 0.85f;
  static constexpr float kPlaneSize = 0.28f;
  static constexpr float kRingRadius = 0.75f;

private:
  GizmoFrame m_frame;
  GizmoMode m_mode = GizmoMode::Translate;
  GizmoPart m_part = GizmoPart::None;
  // The grabbed point (translate, scale) or the ring start vector (rotate).
  Vector3 m_start{ 0.0f };
  float m_startDistance = 1.0f;

  // Where a ray meets the constraint of the grabbed part.
  bool constrain(const Vector3& rayOrigin,
                 const Vector3& rayDirection,
                 Vector3* point) const;
};
