#include "EditorGizmo.h"
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

static bool
near(float a, float b, float tolerance = 1.0e-3f)
{
  return std::fabs(a - b) <= tolerance;
}

static GizmoFrame
worldFrame(bool is3D)
{
  GizmoFrame frame;
  frame.origin = Vector3(0.0f);
  frame.scale = 1.0f;
  frame.is3D = is3D;
  return frame;
}

static int
testTranslateAxisProjection()
{
  TestCounters counters;
  const GizmoFrame frame = worldFrame(true);
  // A ray from the front aimed at the X handle.
  const Vector3 origin(0.5f, 0.0f, 10.0f);
  const Vector3 down(0.0f, 0.0f, -1.0f);
  testTrue(counters,
           EditorGizmo::hitTest(frame, GizmoMode::Translate, origin, down) ==
             GizmoPart::AxisX,
           "the X handle is hit");
  testTrue(counters,
           EditorGizmo::hitTest(
             frame, GizmoMode::Translate, Vector3(0, 0, 10), down) ==
             GizmoPart::Center,
           "the center is hit through the origin");
  testTrue(counters,
           EditorGizmo::hitTest(
             frame, GizmoMode::Translate, Vector3(0.14f, 0.14f, 10), down) ==
             GizmoPart::PlaneXY,
           "the XY plane handle is hit");
  EditorGizmo gizmo;
  testTrue(
    counters,
    gizmo.begin(frame, GizmoMode::Translate, GizmoPart::AxisX, origin, down),
    "an X drag starts");
  const GizmoDelta moved =
    gizmo.drag(Vector3(2.5f, 1.0f, 10.0f), down, GizmoSnap{});
  testTrue(counters,
           moved.valid && near(moved.translation.x, 2.0f) &&
             near(moved.translation.y, 0.0f) && near(moved.translation.z, 0.0f),
           "an axis drag moves only along the axis");
  GizmoSnap snap;
  snap.enabled = true;
  snap.translate = 0.5f;
  const GizmoDelta snapped = gizmo.drag(Vector3(1.8f, 0.0f, 10.0f), down, snap);
  testTrue(counters,
           near(snapped.translation.x, 1.5f),
           "translation snaps to the grid step");
  return counters.failures;
}

static int
testRotateRingAngle()
{
  TestCounters counters;
  const GizmoFrame frame = worldFrame(false);
  const Vector3 down(0.0f, 0.0f, -1.0f);
  const float radius = EditorGizmo::kRingRadius;
  testTrue(counters,
           EditorGizmo::hitTest(
             frame, GizmoMode::Rotate, Vector3(radius, 0, 10), down) ==
             GizmoPart::RingZ,
           "the Z ring is hit on its radius");
  testTrue(counters,
           EditorGizmo::hitTest(
             frame, GizmoMode::Rotate, Vector3(0.1f, 0, 10), down) ==
             GizmoPart::None,
           "the ring's inside is not a handle");
  EditorGizmo gizmo;
  gizmo.begin(
    frame, GizmoMode::Rotate, GizmoPart::RingZ, Vector3(radius, 0, 10), down);
  const GizmoDelta quarter =
    gizmo.drag(Vector3(0, radius, 10), down, GizmoSnap{});
  const Vector3 turned = quarter.rotation * Vector3(1.0f, 0.0f, 0.0f);
  testTrue(counters,
           quarter.valid && near(turned.x, 0.0f) && near(turned.y, 1.0f),
           "dragging a quarter turn rotates 90 degrees about Z");
  GizmoSnap snap;
  snap.enabled = true;
  snap.angleDegrees = 45.0f;
  const float a = glm::radians(50.0f);
  const GizmoDelta snapped = gizmo.drag(
    Vector3(std::cos(a) * radius, std::sin(a) * radius, 10), down, snap);
  testTrue(counters,
           near(glm::degrees(glm::angle(snapped.rotation)), 45.0f, 0.05f),
           "rotation snaps to the angle step");
  return counters.failures;
}

static int
testScaleUniformAndAxis()
{
  TestCounters counters;
  const GizmoFrame frame = worldFrame(true);
  const Vector3 down(0.0f, 0.0f, -1.0f);
  EditorGizmo gizmo;
  testTrue(counters,
           gizmo.begin(frame,
                       GizmoMode::Scale,
                       GizmoPart::AxisY,
                       Vector3(0.0f, 0.5f, 10.0f),
                       down),
           "a Y scale drag starts");
  const GizmoDelta doubled =
    gizmo.drag(Vector3(0.0f, 1.0f, 10.0f), down, GizmoSnap{});
  testTrue(counters,
           doubled.valid && near(doubled.scale.y, 2.0f) &&
             near(doubled.scale.x, 1.0f) && near(doubled.scale.z, 1.0f),
           "an axis handle scales one axis by the drag ratio");
  const GizmoDelta tiny =
    gizmo.drag(Vector3(0.0f, -3.0f, 10.0f), down, GizmoSnap{});
  testTrue(counters, tiny.scale.y >= 0.01f, "scale never inverts or collapses");
  EditorGizmo uniform;
  testTrue(counters,
           uniform.begin(frame,
                         GizmoMode::Scale,
                         GizmoPart::Center,
                         Vector3(0.3f, 0.4f, 10.0f),
                         down),
           "a uniform scale drag starts");
  const GizmoDelta grown =
    uniform.drag(Vector3(0.6f, 0.8f, 10.0f), down, GizmoSnap{});
  testTrue(counters,
           near(grown.scale.x, 2.0f) && near(grown.scale.y, 2.0f) &&
             near(grown.scale.z, 2.0f),
           "the center scales uniformly");
  GizmoSnap snap;
  snap.enabled = true;
  snap.scale = 0.25f;
  const GizmoDelta snapped =
    uniform.drag(Vector3(0.39f, 0.52f, 10.0f), down, snap);
  testTrue(
    counters, near(snapped.scale.x, 1.25f), "scale factors snap to the step");
  return counters.failures;
}

static int
testLocalSpaceAxes()
{
  TestCounters counters;
  GizmoFrame frame = worldFrame(true);
  const Quaternion turn =
    glm::angleAxis(glm::radians(90.0f), Vector3(0.0f, 0.0f, 1.0f));
  for (int axis = 0; axis < 3; ++axis) {
    frame.axes[axis] = turn * frame.axes[axis];
  }
  // Local X now points along world +Y.
  const Vector3 down(0.0f, 0.0f, -1.0f);
  testTrue(counters,
           EditorGizmo::hitTest(
             frame, GizmoMode::Translate, Vector3(0, 0.5f, 10), down) ==
             GizmoPart::AxisX,
           "local handles follow the node's rotation");
  EditorGizmo gizmo;
  gizmo.begin(
    frame, GizmoMode::Translate, GizmoPart::AxisX, Vector3(0, 0.5f, 10), down);
  const GizmoDelta moved =
    gizmo.drag(Vector3(3.0f, 2.5f, 10.0f), down, GizmoSnap{});
  testTrue(counters,
           near(moved.translation.x, 0.0f) && near(moved.translation.y, 2.0f),
           "a local-axis drag moves along the rotated axis only");
  testTrue(
    counters,
    !gizmo.begin(
      frame, GizmoMode::Translate, GizmoPart::AxisZ, Vector3(0, 0, 10), down),
    "an axis seen end-on cannot be grabbed");
  return counters.failures;
}

void
registerEditorGizmoTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Gizmo.TranslateAxisProjection",
               []() { return testTranslateAxisProjection(); });
  registry.add("IllEd.Gizmo.RotateRingAngle",
               []() { return testRotateRingAngle(); });
  registry.add("IllEd.Gizmo.ScaleUniformAndAxis",
               []() { return testScaleUniformAndAxis(); });
  registry.add("IllEd.Gizmo.LocalSpaceAxes",
               []() { return testLocalSpaceAxes(); });
}
