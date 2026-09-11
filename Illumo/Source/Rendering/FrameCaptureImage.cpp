#include <Illumo/Rendering/FrameCapture.h>
#include <fstream>
#include <system_error>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

static void
writeCaptureBytes(void* context, void* data, int size)
{
  std::ofstream* output = static_cast<std::ofstream*>(context);
  output->write(static_cast<const char*>(data), size);
}

bool
FrameCapture::savePng(const std::filesystem::path& path,
                      const FrameReadback& image,
                      std::string* error)
{
  std::string failure;
  if (!image.success() || image.width < 1 || image.height < 1 ||
      image.width > 4096 || image.height > 4096 ||
      image.pixels.size() != static_cast<size_t>(image.width) *
                               static_cast<size_t>(image.height) * 4) {
    failure = "Invalid RGBA8 image";
  }
  std::error_code ioError;
  const std::filesystem::path partial = path.string() + ".partial";
  if (failure.empty() &&
      (path.empty() || std::filesystem::exists(path, ioError) ||
       std::filesystem::exists(partial, ioError) || ioError)) {
    failure = "Output or staging path already exists or is inaccessible";
  }
  if (failure.empty()) {
    std::ofstream output(partial, std::ios::binary | std::ios::noreplace);
    if (!output) {
      failure = "Cannot open capture output: " + path.string();
    } else {
      const int written = stbi_write_png_to_func(writeCaptureBytes,
                                                 &output,
                                                 image.width,
                                                 image.height,
                                                 4,
                                                 image.pixels.data(),
                                                 image.width * 4);
      output.close();
      if (written == 0 || output.fail()) {
        failure = "Unable to write complete PNG";
      } else {
        // Same-directory hard-link publication is atomic and cannot replace an
        // existing destination on either Windows or POSIX. Unsupported
        // filesystems fail explicitly rather than weakening the no-overwrite
        // contract.
        std::filesystem::create_hard_link(partial, path, ioError);
        if (ioError) {
          failure = "Unable to publish PNG: " + ioError.message();
        }
      }
      std::filesystem::remove(partial, ioError);
    }
  }
  if (error != nullptr) {
    *error = failure;
  }
  return failure.empty();
}
