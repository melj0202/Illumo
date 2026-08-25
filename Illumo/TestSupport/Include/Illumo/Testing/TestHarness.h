#pragma once
// Shared null window for headless renderer and service tests.

#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/MockBackend.h>
#include <array>
#include <string>

class NullRenderWindow : public IRenderWindow
{
public:
  int width;
  int height;
  int fullscreenToggleCount;
  bool closeRequested;

  NullRenderWindow()
    : NullRenderWindow(1280, 720)
  {
  }

  NullRenderWindow(int w, int h)
    : IRenderWindow(w, h, "test", nullptr)
    , width(w)
    , height(h)
    , fullscreenToggleCount(0)
    , closeRequested(false)
  {
  }

  void updateWindow() override {}
  void toggleFullscreen() override { fullscreenToggleCount += 1; }
  void reinitializeWindow(const int, const int, const std::string&) override {}
  void reinitializeWindow() override {}
  void handleResize(int w, int h) override
  {
    width = w;
    height = h;
  }
  std::array<double, 2> getMouseCoords() override
  {
    return std::array<double, 2>{ 0.0, 0.0 };
  }
  GLFWwindow* getWindowInstance() override { return nullptr; }
  std::array<int, 2> getWindowDimensions() override
  {
    return std::array<int, 2>{ width, height };
  }
  bool shouldWindowClose() override { return closeRequested; }
  bool isFramePaced() const override { return false; }
  void swapBuffers() override {}
  void requestClose() override { closeRequested = true; }
};

struct HeadlessRenderFixture
{
  NullRenderWindow window;
  EnvVars env;
  Camera camera;
  MockBackend mock;
  Renderer renderer;

  HeadlessRenderFixture(int width = 1280, int height = 720)
    : window(width, height)
    , env()
    , camera(glm::vec2(0.0f, 0.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
  {
    env.setVar("WinX", width);
    env.setVar("WinY", height);
    mock.Initialize();
  }
};
