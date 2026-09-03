#pragma once

#include <Illumo/Services/IEnvVars.h>
#include <array>
#include <string>

struct GLFWwindow;

// Platform window + GL context host. Draw submission is NOT done here;
// use Renderer / IBackend. GLFW types are required for input callbacks.
class IRenderWindow
{
public:
  IRenderWindow(const int width,
                const int height,
                const std::string& title,
                IEnvVars* envVars)
  {
    (void)width;
    (void)height;
    (void)title;
    (void)envVars;
  }
  virtual ~IRenderWindow() = default;
  virtual void updateWindow() = 0;
  virtual void toggleFullscreen() = 0;
  virtual void reinitializeWindow(const int width,
                                  const int height,
                                  const std::string& title) = 0;
  virtual void reinitializeWindow() = 0;
  virtual void handleResize(int width, int height) = 0;
  virtual std::array<double, 2> getMouseCoords() = 0;
  virtual GLFWwindow* getWindowInstance() = 0;
  virtual std::array<int, 2> getWindowDimensions() = 0;
  virtual bool shouldWindowClose() = 0;
  virtual bool isFramePaced() const = 0;
  virtual int getRefreshRate() const = 0;
  virtual void swapBuffers() = 0;
  virtual void requestClose() = 0;
};
