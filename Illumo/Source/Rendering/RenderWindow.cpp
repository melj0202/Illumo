#include "RenderWindow.h"
#include "PresentationTiming.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <Illumo/Services/Logger.h>

void
windowSizeCallback(GLFWwindow* window, int width, int height) noexcept
{

  // 1. Grab our C++ instance pointer out of GLFW
  RenderWindow* myWindow =
    static_cast<RenderWindow*>(glfwGetWindowUserPointer(window));

  // 2. Use that pointer instead of 'this'
  if (myWindow) {
    myWindow->handleResize(width, height);
  }
  Logger::LogTrace("Window resized to " + std::to_string(width) + "x" +
                   std::to_string(height));
}

RenderWindow::RenderWindow(const int width,
                           const int height,
                           const std::string& title,
                           IEnvVars* envVars)
  : IRenderWindow(width, height, title, envVars)
{

  /*Init member variables*/

  this->mouseCoords = { 0.0, 0.0 };
  this->windowHeight = height;
  this->windowWidth = width;
  this->windowedX = 0;
  this->windowedY = 0;
  this->windowedWidth = width;
  this->windowedHeight = height;
  this->windowTitle = title;
  this->envVars = envVars;
  this->isFullScreen =
    envVars ? envVars->getVar("fullscreen").valueAsBool : false;
  this->vsyncEnabled = true;
  this->swapIntervalInitialized = false;
  this->glfwInitialized = false;
  this->window = nullptr;
}

bool
RenderWindow::initialize()
{
  if (!glfwInit()) {
    Logger::LogError("Failed to initialize GLFW");
    return false;
  }
  glfwInitialized = true;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  int samples = 4;
  if (envVars != nullptr) {
    const EnvVar& msaaVar = envVars->getVar("msaa");
    if (!msaaVar.value.empty()) {
      samples = static_cast<int>(msaaVar.valueAsLong);
    }
  }
  if (samples > 0) {
    glfwWindowHint(GLFW_SAMPLES, samples);
  }
  /* Create a windowed mode window and its OpenGL context */

  if (isFullScreen) {
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);
    if (primaryMonitor == nullptr || mode == nullptr) {
      Logger::LogError("Failed to query the primary monitor");
      glfwTerminate();
      glfwInitialized = false;
      return false;
    }
    window = glfwCreateWindow(
      mode->width, mode->height, windowTitle.c_str(), primaryMonitor, nullptr);
  } else {
    window = glfwCreateWindow(
      windowWidth, windowHeight, windowTitle.c_str(), nullptr, nullptr);
  }
  if (!window) {
    Logger::LogError("Failed to create window");
    glfwTerminate();
    glfwInitialized = false;
    return false;
  }
  Logger::LogTrace("Window created");
  glfwGetWindowSize(window, &windowWidth, &windowHeight);
  if (envVars) {
    envVars->setVar("WinX", std::to_string(windowWidth));
    envVars->setVar("WinY", std::to_string(windowHeight));
  }
  if (!isFullScreen) {
    centerWindow();
    glfwGetWindowPos(window, &windowedX, &windowedY);
    glfwGetWindowSize(window, &windowedWidth, &windowedHeight);
  }
  Logger::LogTrace("Window centered");

  /* Make the window's context current */
  glfwMakeContextCurrent(window);
  if (samples > 0) {
    glEnable(GL_MULTISAMPLE);
  }
  glfwSetWindowUserPointer(window, this);
  glfwSetWindowSizeCallback(window, windowSizeCallback);
  glfwSetWindowSizeLimits(window, 640, 360, GLFW_DONT_CARE, GLFW_DONT_CARE);
  // Set initial viewport size based on current framebuffer size
  int fbWidth = 0;
  int fbHeight = 0;
  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
  glViewport(0, 0, fbWidth, fbHeight);
  Logger::LogTrace("Viewport set");

  syncPresentationMode();
  return true;
}

std::unique_ptr<IRenderWindow>
CreateRenderWindow(int width,
                   int height,
                   const std::string& title,
                   IEnvVars* envVars)
{
  std::unique_ptr<RenderWindow> window =
    std::make_unique<RenderWindow>(width, height, title, envVars);
  if (!window->initialize()) {
    return nullptr;
  }
  return window;
}

void
RenderWindow::syncPresentationMode()
{
  const bool requestedVsync = isVsyncRequested(envVars);
  if (swapIntervalInitialized && requestedVsync == vsyncEnabled) {
    return;
  }

  glfwSwapInterval(requestedVsync ? 1 : 0);
  vsyncEnabled = requestedVsync;
  swapIntervalInitialized = true;
}

void
RenderWindow::updateWindow()
{
  // Frame clear/draw/swap is owned by Renderer + Illumo::render.
  // Kept as a no-op hook for any legacy call sites.
}

std::array<double, 2>
RenderWindow::getMouseCoords()
{
  glfwGetCursorPos(window, &mouseCoords[0], &mouseCoords[1]);
  return mouseCoords;
}

void
RenderWindow::centerWindow()
{
  // Get monitor dimensions
  GLFWmonitor* monitor = glfwGetPrimaryMonitor();
  if (monitor) {
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (mode) {
      int monitorWidth = mode->width;
      int monitorHeight = mode->height;
      glfwSetWindowPos(window,
                       (monitorWidth - windowWidth) / 2,
                       (monitorHeight - windowHeight) / 2);
    }
  }
}

void
RenderWindow::handleResize(int width, int height)
{
  windowWidth = width;
  windowHeight = height;

  int fbWidth = 0;
  int fbHeight = 0;
  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
  glViewport(0, 0, fbWidth, fbHeight);
  if (envVars) {
    envVars->setVar("WinX", std::to_string(width));
    envVars->setVar("WinY", std::to_string(height));
  }
}

void
RenderWindow::reinitializeWindow(const int width,
                                 const int height,
                                 const std::string& title)
{
  (void)width;
  (void)height;
  (void)title;
  // ServiceLocator::provide<IRenderWindow, RenderWindow>(width, height, title);
}

void
RenderWindow::reinitializeWindow()
{
  // glfwDestroyWindow(window);
  // ServiceLocator::provide<IRenderWindow, RenderWindow>(windowWidth,
  // windowHeight, windowTitle);
}

void
RenderWindow::toggleFullscreen()
{
  if (isFullScreen) {
    glfwSetWindowMonitor(
      window, nullptr, windowedX, windowedY, windowedWidth, windowedHeight, 0);
    isFullScreen = false;
  } else {
    glfwGetWindowPos(window, &windowedX, &windowedY);
    glfwGetWindowSize(window, &windowedWidth, &windowedHeight);
    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode =
      primaryMonitor ? glfwGetVideoMode(primaryMonitor) : nullptr;
    if (primaryMonitor == nullptr || mode == nullptr) {
      Logger::LogError(
        "Cannot enter fullscreen: primary monitor is unavailable");
      return;
    }
    glfwSetWindowMonitor(window,
                         primaryMonitor,
                         0,
                         0,
                         mode->width,
                         mode->height,
                         mode->refreshRate);
    isFullScreen = true;
  }
  if (envVars != nullptr) {
    envVars->setVar("fullscreen", isFullScreen);
  }
}

bool
RenderWindow::shouldWindowClose()
{
  return glfwWindowShouldClose(window);
}

void
RenderWindow::requestClose()
{
  glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void
RenderWindow::swapBuffers()
{
  if (window != nullptr) {
    syncPresentationMode();
    glfwSwapBuffers(window);
  }
}

RenderWindow::~RenderWindow()
{
  if (window != nullptr) {
    glfwDestroyWindow(window);
    window = nullptr;
  }
  if (glfwInitialized) {
    glfwTerminate();
    glfwInitialized = false;
  }
}
