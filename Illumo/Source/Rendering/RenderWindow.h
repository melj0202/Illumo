#pragma once
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Services/IEnvVars.h>
#include <array>
#include <memory>
#include <string>

struct GLFWwindow;

// GLFW window and OpenGL context host. GLEW/backend initialization is owned by
// the backend factory after a context has been created successfully.
class RenderWindow : public IRenderWindow
{
public:
  RenderWindow(const int width,
               const int height,
               const std::string& title,
               IEnvVars* envVars);
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

private:
  friend std::unique_ptr<IRenderWindow> CreateRenderWindow(
    int width,
    int height,
    const std::string& title,
    IEnvVars* envVars);

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

std::unique_ptr<IRenderWindow>
CreateRenderWindow(int width,
                   int height,
                   const std::string& title,
                   IEnvVars* envVars);
