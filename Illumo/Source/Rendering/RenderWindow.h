#pragma once
#include "Rendering/BackendConfig.h"
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/IEnvVars.h>
#include <array>
#include <chrono>
#include <memory>
#include <string>

struct GLFWwindow;

// GLFW window for one graphics API. For OpenGL it also hosts the context;
// GLEW/backend initialization is owned by the backend factory after the
// context exists. For Vulkan and Direct3D 12 the window has no client API:
// the backend creates the surface or swapchain and presents, and swapBuffers
// only follows the vsync setting.
class RenderWindow : public IRenderWindow
{
public:
  RenderWindow(const int width,
               const int height,
               const std::string& title,
               IEnvVars* envVars,
               bool captureOnly = false,
               BackendDef graphicsApi = BackendDef::OPENGL);
  ~RenderWindow();
  void reinitializeWindow(const int width,
                          const int height,
                          const std::string& title) override;
  void reinitializeWindow() override;
  void toggleFullscreen() override;
  GLFWwindow* getWindowInstance() override { return window; }
  void updateWindow() override;
  std::array<double, 2> getMouseCoords() override;
  std::array<int, 2> getWindowDimensions() override
  {
    return std::array<int, 2>{ windowWidth, windowHeight };
  }
  void handleResize(int width, int height) override;
  bool shouldWindowClose() override;
  bool isFramePaced() const override { return vsyncEnabled; }
  int getRefreshRate() const override;
  void swapBuffers() override;
  void requestClose() override;
  void cancelCloseRequest() override;
  void setTitle(const std::string& title) override;
  void setIcon(const std::vector<WindowIconImage>& images) override;
  void setSystemCursorHidden(bool hidden) override;
  // Hidden capture windows report unknown: they never show a product's MSAA.
  int getMsaaSamples() const override
  {
    return m_captureOnly ? -1 : m_createdSamples;
  }
  std::string graphicsApi() const override
  {
    return TokenToString(m_graphicsApi);
  }
  // The samples the backbuffer was requested with; 0 for capture windows,
  // which getMsaaSamples reports as unknown.
  int requestedSamples() const { return m_createdSamples; }
  bool isCaptureWindow() const { return m_captureOnly; }

private:
  int m_createdSamples = -1;
  // glfwGetVideoMode asks the OS each call; the rate is re-read at most once
  // a second.
  mutable int m_refreshRate = 0;
  mutable std::chrono::steady_clock::time_point m_refreshRateRead{};
  friend std::unique_ptr<IRenderWindow>
  CreateCaptureWindowFor(int width, int height, BackendDef graphicsApi);
  bool m_captureOnly = false;
  BackendDef m_graphicsApi = BackendDef::OPENGL;
  friend std::unique_ptr<IRenderWindow> CreateRenderWindowFor(
    int width,
    int height,
    const std::string& title,
    IEnvVars* envVars,
    BackendDef graphicsApi);

  std::array<double, 2> mouseCoords;
  GLFWwindow* window;
  IEnvVars* envVars;
  int windowWidth;
  int windowHeight;
  int windowedX;
  int windowedY;
  int windowedWidth;
  int windowedHeight;
  std::string windowTitle;
  bool isFullScreen;
  bool vsyncEnabled;
  bool swapIntervalInitialized;
  bool glfwInitialized;
  bool initialize();
  void centerWindow();
  void syncPresentationMode();
};

// Records a window size as the persisted WinX/WinY. A minimized window
// reports 0x0, and persisting that would make the next launch fail to create
// its window, so a non-positive or iconified size keeps the last restored
// size. Returns whether the size was recorded.
bool
recordWindowSize(IEnvVars* envVars, int width, int height, bool iconified);

// An OpenGL window.
std::unique_ptr<IRenderWindow>
CreateRenderWindow(int width,
                   int height,
                   const std::string& title,
                   IEnvVars* envVars);

std::unique_ptr<IRenderWindow>
CreateRenderWindowFor(int width,
                      int height,
                      const std::string& title,
                      IEnvVars* envVars,
                      BackendDef graphicsApi);

// A hidden OpenGL window for capture.
std::unique_ptr<IRenderWindow>
CreateCaptureWindow(int width, int height);

std::unique_ptr<IRenderWindow>
CreateCaptureWindowFor(int width, int height, BackendDef graphicsApi);
