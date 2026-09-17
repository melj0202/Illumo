#include "OpenGL/CreateOpenGLBackend.h"
#include "RenderWindow.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/FrameCapture.h>
#include <Illumo/Rendering/Renderer.h>
#include <chrono>
#include <cmath>
#include <exception>

std::string
FrameCapture::validate(const FrameCaptureOptions& options)
{
  if (options.width < 1 || options.height < 1 || options.width > 4096 ||
      options.height > 4096) {
    return "Capture dimensions must be within 1..4096";
  }
  for (size_t axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(options.eye[axis]) ||
        !std::isfinite(options.target[axis]) ||
        std::abs(options.eye[axis]) > 1000000.0f ||
        std::abs(options.target[axis]) > 1000000.0f) {
      return "Camera coordinates must be finite and within +/-1000000";
    }
  }
  const float dx = options.target[0] - options.eye[0];
  const float dz = options.target[2] - options.eye[2];
  if (dx * dx + dz * dz < 0.000001f) {
    return "Camera direction must not be parallel to the Y-up vector";
  }
  if (!std::isfinite(options.verticalFovDegrees) ||
      options.verticalFovDegrees < 1.0f ||
      options.verticalFovDegrees > 175.0f) {
    return "Camera FOV must be within 1..175 degrees";
  }
  return {};
}

static FrameCaptureResult
renderCapture(const FrameCaptureOptions& options,
              const FrameCaptureProducer& producer)
{
  FrameCaptureResult result;
  result.stage = "validation";
  result.error = FrameCapture::validate(options);
  if (!result.error.empty()) {
    return result;
  }
  if (!producer) {
    result.error = "Capture requires a synchronous frame producer";
    return result;
  }
  try {
    result.stage = "context";
    std::unique_ptr<IRenderWindow> window =
      CreateCaptureWindow(options.width, options.height);
    if (!window) {
      result.error = "Unable to create the requested hidden OpenGL context";
      return result;
    }
    result.stage = "backend";
    std::unique_ptr<IBackend> backend = CreateOpenGLBackend(window.get());
    if (!backend || !backend->Initialize()) {
      result.error = "Unable to initialize OpenGL backend";
      return result;
    }
    Camera camera;
    camera.setProjectionType(ProjectionType::Perspective);
    camera.setPerspective(options.verticalFovDegrees, 0.1f, 1000.0f);
    camera.lookAt(
      glm::vec3(options.eye[0], options.eye[1], options.eye[2]),
      glm::vec3(options.target[0], options.target[1], options.target[2]),
      glm::vec3(0.0f, 1.0f, 0.0f));
    Renderer renderer(window.get(), nullptr, &camera, std::move(backend));
    renderer.setStrictSubmission(true);
    renderer.BeginFrame();
    result.stage = "submission";
    if (!producer(renderer, camera, result.error)) {
      if (result.error.empty()) {
        result.error = renderer.frameError().empty() ? "Frame producer failed"
                                                     : renderer.frameError();
      }
      return result;
    }
    if (!result.error.empty()) {
      return result;
    }
    if (!renderer.frameError().empty()) {
      result.error = renderer.frameError();
      return result;
    }
    result.stage = "readback";
    result.image =
      renderer.getBackend()->readBackbuffer(options.width, options.height);
    if (!result.image.success()) {
      result.error = result.image.error;
      return result;
    }
    result.stage = "complete";
  } catch (const std::exception& exception) {
    result.error = exception.what();
    result.image = {};
  } catch (...) {
    result.error = "Unknown capture failure";
    result.image = {};
  }
  return result;
}

FrameCaptureResult
FrameCapture::render(const FrameCaptureOptions& options,
                     const FrameCaptureProducer& producer)
{
  const std::chrono::steady_clock::time_point start =
    std::chrono::steady_clock::now();
  FrameCaptureResult result = renderCapture(options, producer);
  result.elapsedMilliseconds = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
  return result;
}
