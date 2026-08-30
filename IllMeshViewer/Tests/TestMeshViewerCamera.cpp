#include "MeshViewerCamera.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

static TestCounters g;

static void
testCameraOrbitAndClamping()
{
  testSection("MeshViewerCamera: orbit and pitch clamping");
  MeshViewerCamera cam;
  const float initialYaw = cam.yaw();
  const float initialPitch = cam.pitch();

  cam.orbit(0.5f, 0.3f);
  cam.update(1.0f);

  testTrue(g,
           std::abs(cam.yaw() - (initialYaw + 0.5f)) < 0.001f,
           "yaw updated by delta");
  testTrue(g,
           std::abs(cam.pitch() - (initialPitch + 0.3f)) < 0.001f,
           "pitch updated by delta");

  // Extreme vertical orbit clamps to safe pitch range
  cam.orbit(0.0f, 10.0f);
  cam.update(1.0f);
  testTrue(g, cam.pitch() <= 1.50f, "pitch clamped to upper limit");

  cam.orbit(0.0f, -20.0f);
  cam.update(1.0f);
  testTrue(g, cam.pitch() >= -1.50f, "pitch clamped to lower limit");
}

static void
testCameraPanAndZoom()
{
  testSection("MeshViewerCamera: pan and zoom operations");
  MeshViewerCamera cam;
  cam.setTarget(glm::vec3(0.0f, 0.0f, 0.0f));
  cam.setDistance(4.0f);
  cam.update(1.0f);

  cam.zoom(0.5f);
  cam.update(1.0f);
  testTrue(
    g, std::abs(cam.distance() - 2.0f) < 0.001f, "zoom factor halved distance");

  cam.zoom(0.0001f);
  cam.update(1.0f);
  testTrue(g, cam.distance() >= 0.05f, "zoom clamped to min distance");

  cam.pan(1.0f, 0.5f);
  cam.update(1.0f);
  testTrue(g, glm::length(cam.target()) > 0.01f, "pan modified camera target");
}

static void
testCameraFramingAndMatrices()
{
  testSection("MeshViewerCamera: frameBounds, roll, and matrix calculations");
  MeshViewerCamera cam;
  const glm::vec3 minB(-5.0f, -2.0f, -3.0f);
  const glm::vec3 maxB(5.0f, 2.0f, 3.0f);

  cam.frameBounds(minB, maxB);
  cam.update(1.0f);

  testTrue(g,
           glm::length(cam.target() - glm::vec3(0.0f, 0.0f, 0.0f)) < 0.001f,
           "framed center is at origin");
  testTrue(g, cam.distance() >= 10.0f, "distance scaled to framed extent");

  cam.rotate(0.25f);
  cam.update(1.0f);
  testTrue(g, std::abs(cam.roll() - 0.25f) < 0.001f, "roll updated correctly");

  const glm::mat4 view = cam.getViewMatrix();
  const glm::mat4 proj = cam.getProjectionMatrix(16.0f / 9.0f);
  testTrue(g, view[3][3] == 1.0f, "valid view matrix");
  testTrue(g, proj[3][2] != 0.0f, "valid perspective projection matrix");
}

static int
runCameraCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerMeshViewerCameraTests(IllumoTestRegistry& registry)
{
  registry.add("IllMeshViewer.Camera.OrbitAndClamping",
               []() { return runCameraCase(testCameraOrbitAndClamping); });
  registry.add("IllMeshViewer.Camera.PanAndZoom",
               []() { return runCameraCase(testCameraPanAndZoom); });
  registry.add("IllMeshViewer.Camera.FramingAndMatrices",
               []() { return runCameraCase(testCameraFramingAndMatrices); });
}
