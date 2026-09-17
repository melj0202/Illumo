#pragma once
#include <Illumo/Rendering/FrameReadback.h>
#include <array>
#include <filesystem>
#include <functional>

class Renderer;
class Camera;

struct FrameCaptureOptions
{
  int width = 640;
  int height = 480;
  std::array<float, 3> eye{ 4.0f, 3.0f, 5.0f };
  std::array<float, 3> target{ 0.0f, 0.0f, 0.0f };
  float verticalFovDegrees = 45.0f;
};

struct FrameCaptureResult
{
  FrameReadback image;
  std::string stage;
  std::string error;
  double elapsedMilliseconds = 0.0;
  bool success() const { return error.empty() && image.success(); }
};

// Runs on the calling/main thread in a fresh hidden OpenGL context. Do not call
// while another Illumo window is live. The callback must create, synchronously
// submit, and destroy all renderer-bound content before returning. It must not
// swap, start a loop, retain service pointers, or mutate external simulation.
// Return false with a diagnostic for required content/asset failures. Scene is
// an optional transient frame list; persistent SceneGraph nodes are not
// required.
using FrameCaptureProducer =
  std::function<bool(Renderer&, Camera&, std::string&)>;

class FrameCapture
{
public:
  static std::string validate(const FrameCaptureOptions& options);
  static FrameCaptureResult render(const FrameCaptureOptions& options,
                                   const FrameCaptureProducer& producer);
  // CPU-only PNG output. Refuses existing files; publishes after complete
  // write.
  static bool savePng(const std::filesystem::path& path,
                      const FrameReadback& image,
                      std::string* error);
};
