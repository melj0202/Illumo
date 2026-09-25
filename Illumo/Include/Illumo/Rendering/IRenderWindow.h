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
  // The window publishes the resulting fullscreen state to its environment.
  // A rejected toggle leaves that state unchanged.
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
  // Custom windows used with modules that defer close must clear their flag.
  virtual void cancelCloseRequest() {}
  // Close, then start the application again with the same command line
  // (settings read only at startup, such as MSAA, take effect). The runner
  // relaunches after a normal shutdown; a deferred close drops the restart.
  void requestRestart()
  {
    m_restartRequested = true;
    requestClose();
  }
  bool restartRequested() const { return m_restartRequested; }
  void clearRestartRequest() { m_restartRequested = false; }
  // Multisample count the framebuffer was created with, or -1 when unknown.
  virtual int getMsaaSamples() const { return -1; }
  // Hosts that learn the product name after creation (a package runtime)
  // retitle the window. Windows without a title bar ignore it.
  virtual void setTitle(const std::string& title) { (void)title; }
  // Products that draw their own (software) pointer hide the system cursor
  // while it is over the window's content area. Windows without a system
  // cursor ignore it.
  virtual void setSystemCursorHidden(bool hidden) { (void)hidden; }

private:
  bool m_restartRequested = false;
};
