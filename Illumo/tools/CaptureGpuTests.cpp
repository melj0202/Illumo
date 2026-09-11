#include <Illumo/Rendering/FrameCapture.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/Logger.h>
#include <iostream>
#include <stdexcept>

int
main()
{
  Logger::setConsoleToStderr(true);
  FrameCaptureOptions options;
  options.width = 32;
  options.height = 32;
  int failures = 0;
  const FrameCaptureResult overflow = FrameCapture::render(
    options, [](Renderer& renderer, Camera&, std::string&) {
      for (int i = 0; i < 65537; ++i) {
        renderer.pushClearColor(0, 0, 0, 1);
      }
      renderer.SubmitOnly();
      renderer.getBackend()->ClearCommandQueue();
      return true;
    });
  if (overflow.success() ||
      overflow.error.find("safety ceiling") == std::string::npos) {
    std::cerr << "Overflow did not survive queue reset: " << overflow.error
              << '\n';
    ++failures;
  }
  const FrameCaptureResult invalid = FrameCapture::render(
    options, [](Renderer& renderer, Camera&, std::string&) {
      renderer.pushSetMesh(MeshHandle{});
      renderer.SubmitOnly();
      renderer.getBackend()->ClearCommandQueue();
      return true;
    });
  if (invalid.success() ||
      invalid.error.find("unknown mesh") == std::string::npos) {
    std::cerr << "Invalid handle failure lost: " << invalid.error << '\n';
    ++failures;
  }
  const FrameCaptureResult exception =
    FrameCapture::render(options, [](Renderer&, Camera&, std::string&) -> bool {
      throw std::runtime_error("producer exception");
    });
  if (exception.success() || exception.error != "producer exception") {
    ++failures;
  }
  const FrameCaptureResult strict = FrameCapture::render(
    options, [](Renderer& renderer, Camera&, std::string&) {
      renderer.reportFrameError("required attachment failed");
      return false;
    });
  if (strict.success() || strict.error != "required attachment failed") {
    ++failures;
  }
  const FrameCaptureResult recovered = FrameCapture::render(
    options, [](Renderer& renderer, Camera&, std::string&) {
      renderer.pushClearScreen(1, 0, 0, 1);
      renderer.SubmitOnly();
      return true;
    });
  if (!recovered.success() || recovered.image.pixels[0] != 255 ||
      recovered.image.pixels[1] != 0 || recovered.elapsedMilliseconds <= 0) {
    std::cerr << "Context cleanup/recreation failed: " << recovered.error
              << '\n';
    ++failures;
  }
  std::cout << "GPU backend failure and lifecycle checks: " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}
