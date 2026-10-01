#include "../PixelWindow.h"

// Linux is not a supported platform; detaching the console reports an error
// there instead of presenting through X11.
bool
PixelWindowPresentationSupported()
{
  return false;
}

bool
PresentPixelWindowImage(GLFWwindow* window,
                        const std::uint8_t* rgba,
                        int width,
                        int height,
                        std::vector<std::uint8_t>& scratch)
{
  (void)window;
  (void)rgba;
  (void)width;
  (void)height;
  (void)scratch;
  return false;
}
