#include <Illumo/Rendering/FrameCapture.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <fstream>
#include <limits>
#include <thread>

static TestCounters g;

static int
testCaptureValidation()
{
  g = {};
  FrameCaptureOptions options;
  testTrue(
    g, FrameCapture::validate(options).empty(), "default camera is valid");
  options.width = 0;
  bool called = false;
  FrameCaptureResult rejected =
    FrameCapture::render(options, [&](Renderer&, Camera&, std::string&) {
      called = true;
      return true;
    });
  testTrue(g,
           !rejected.success() && !called,
           "invalid dimensions never initialize producer");
  options.width = 4097;
  testTrue(
    g, !FrameCapture::validate(options).empty(), "allocation bound enforced");
  options.width = 640;
  options.eye = options.target;
  testTrue(
    g, !FrameCapture::validate(options).empty(), "degenerate camera rejected");
  options.eye = { 4.0f, 3.0f, 5.0f };
  options.verticalFovDegrees = std::numeric_limits<float>::quiet_NaN();
  testTrue(
    g, !FrameCapture::validate(options).empty(), "nonfinite camera rejected");
  MockBackend backend;
  testTrue(g,
           !backend.readBackbuffer(4, 4).success(),
           "unsupported backend reports failure");
  return g.failures;
}

static int
testCapturePng()
{
  g = {};
  const std::filesystem::path path = "capture-unit.png";
  std::filesystem::remove(path);
  FrameReadback image{ 2, 1, { 255, 0, 0, 255, 0, 0, 255, 255 }, {} };
  std::string error;
  testTrue(
    g, FrameCapture::savePng(path, image, &error), "RGBA image writes PNG");
  std::ifstream input(path, std::ios::binary);
  unsigned char header[24]{};
  input.read(reinterpret_cast<char*>(header), sizeof(header));
  testTrue(g,
           input.good() && header[0] == 137 && header[1] == 'P' &&
             header[19] == 2 && header[23] == 1,
           "PNG signature and dimensions preserved");
  input.close();
  testTrue(g,
           !FrameCapture::savePng(path, image, &error),
           "existing artifact is preserved");
  image.pixels.pop_back();
  testTrue(g,
           !FrameCapture::savePng("invalid-image.png", image, &error),
           "invalid byte count rejected");
  image.pixels.push_back(255);
  testTrue(g,
           !FrameCapture::savePng("absent-parent/capture.png", image, &error),
           "writer failure reported");
  std::filesystem::remove(path);
  bool first = false;
  bool second = false;
  std::thread writerOne(
    [&]() { first = FrameCapture::savePng(path, image, nullptr); });
  std::thread writerTwo(
    [&]() { second = FrameCapture::savePng(path, image, nullptr); });
  writerOne.join();
  writerTwo.join();
  testTrue(g, first != second, "exactly one concurrent writer publishes");
  testTrue(g,
           !std::filesystem::exists(path.string() + ".partial"),
           "exclusive staging cleans up");
  std::filesystem::remove(path);
  return g.failures;
}

class CaptureUnsupportedDrawable final : public DrawableBase
{
public:
  bool drawn = false;
  void Draw() override { drawn = true; }
};

static int
testCaptureStrictSubmission()
{
  g = {};
  MockBackend backend;
  backend.Initialize();
  Renderer renderer(nullptr, nullptr, nullptr, &backend, false);
  CaptureUnsupportedDrawable drawable;
  Scene scene;
  scene.AddDrawable(&drawable);
  renderer.setStrictSubmission(true);
  renderer.BeginFrame();
  renderer.RenderScene(&scene, nullptr);
  testTrue(g,
           !renderer.frameError().empty() && !drawable.drawn,
           "strict capture rejects immediate fallback");
  renderer.setStrictSubmission(false);
  renderer.BeginFrame();
  renderer.RenderScene(&scene, nullptr);
  testTrue(g,
           renderer.frameError().empty() && drawable.drawn,
           "normal immediate fallback remains compatible");
  return g.failures;
}

void
registerFrameCaptureTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Capture.Validation", testCaptureValidation);
  registry.add("Illumo.Capture.Png", testCapturePng);
  registry.add("Illumo.Capture.StrictSubmission", testCaptureStrictSubmission);
}
