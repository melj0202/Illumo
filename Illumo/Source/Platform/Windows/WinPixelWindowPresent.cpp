#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "../PixelWindow.h"

bool
PixelWindowPresentationSupported()
{
  return true;
}

// GDI blit of a top-down RGBA image into the window's client area. The window
// has no GL context, so nothing here touches the renderer's device state.
bool
PresentPixelWindowImage(GLFWwindow* window,
                        const std::uint8_t* rgba,
                        int width,
                        int height,
                        std::vector<std::uint8_t>& scratch)
{
  HWND handle = glfwGetWin32Window(window);
  if (handle == nullptr || rgba == nullptr || width <= 0 || height <= 0) {
    return false;
  }
  const std::size_t pixelCount =
    static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  scratch.resize(pixelCount * 4u);
  for (std::size_t i = 0; i < pixelCount; ++i) {
    scratch[i * 4u + 0u] = rgba[i * 4u + 2u];
    scratch[i * 4u + 1u] = rgba[i * 4u + 1u];
    scratch[i * 4u + 2u] = rgba[i * 4u + 0u];
    scratch[i * 4u + 3u] = 255;
  }
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height; // top-down rows
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  HDC context = GetDC(handle);
  if (context == nullptr) {
    return false;
  }
  const int copied = StretchDIBits(context,
                                   0,
                                   0,
                                   width,
                                   height,
                                   0,
                                   0,
                                   width,
                                   height,
                                   scratch.data(),
                                   &info,
                                   DIB_RGB_COLORS,
                                   SRCCOPY);
  ReleaseDC(handle, context);
  return copied != 0;
}
