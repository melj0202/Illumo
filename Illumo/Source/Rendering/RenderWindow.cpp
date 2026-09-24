#include "RenderWindow.h"
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <Illumo/Engine/PresentationTiming.h>
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

static void
glfwErrorCallback(int code, const char* description) noexcept
{
  try {
    Logger::LogWarning("GLFW error " + std::to_string(code) + ": " +
                       (description != nullptr ? description : "unknown"));
  } catch (...) {
  }
}

RenderWindow::RenderWindow(const int width,
                           const int height,
                           const std::string& title,
                           IEnvVars* envVars,
                           bool captureOnly)
  : IRenderWindow(width, height, title, envVars)
  , m_captureOnly(captureOnly)
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
  glfwSetErrorCallback(glfwErrorCallback);
  if (!glfwInit()) {
    const char* description = nullptr;
    glfwGetError(&description);
    Logger::LogError(std::string("Failed to initialize GLFW") +
                     (description != nullptr ? std::string(": ") + description
                                             : std::string()));
    return false;
  }
  glfwInitialized = true;
  Logger::LogTrace(std::string("GLFW ") + glfwGetVersionString());
  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_VISIBLE, m_captureOnly ? GLFW_FALSE : GLFW_TRUE);
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
  if (m_captureOnly) {
    samples = 0;
  }
  glfwWindowHint(GLFW_SAMPLES, samples);
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
    const char* description = nullptr;
    glfwGetError(&description);
    Logger::LogError(std::string("Failed to create an OpenGL 3.3 core window") +
                     (description != nullptr ? std::string(": ") + description
                                             : std::string()));
    glfwTerminate();
    glfwInitialized = false;
    return false;
  }
  glfwGetWindowSize(window, &windowWidth, &windowHeight);
  if (!m_captureOnly) {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode =
      monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
    const char* monitorName =
      monitor != nullptr ? glfwGetMonitorName(monitor) : nullptr;
    if (mode != nullptr) {
      Logger::LogInfo(
        std::string("Display: ") +
        (monitorName != nullptr ? monitorName : "primary monitor") + ", " +
        std::to_string(mode->width) + "x" + std::to_string(mode->height) +
        " at " + std::to_string(mode->refreshRate) + " Hz");
    }
  }
  Logger::LogTrace(std::string("Window created: ") +
                   std::to_string(windowWidth) + "x" +
                   std::to_string(windowHeight) +
                   (isFullScreen ? ", fullscreen" : ", windowed") +
                   (m_captureOnly ? ", hidden for capture" : "") + ", " +
                   std::to_string(samples) + "x MSAA");
  if (envVars) {
    envVars->setVar("WinX", std::to_string(windowWidth));
    envVars->setVar("WinY", std::to_string(windowHeight));
  }
  if (!isFullScreen) {
    centerWindow();
    glfwGetWindowPos(window, &windowedX, &windowedY);
    glfwGetWindowSize(window, &windowedWidth, &windowedHeight);
  }

  /* Make the window's context current. Do not issue GL calls here: GLEW has
     not loaded function pointers yet, and core-profile GLX can crash. */
  glfwMakeContextCurrent(window);
  glfwSetWindowUserPointer(window, this);
  glfwSetWindowSizeCallback(window, windowSizeCallback);
  if (!m_captureOnly) {
    glfwSetWindowSizeLimits(window, 640, 360, GLFW_DONT_CARE, GLFW_DONT_CARE);
  }
  int fbWidth = 0;
  int fbHeight = 0;
  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
  if (m_captureOnly && (fbWidth != windowWidth || fbHeight != windowHeight)) {
    Logger::LogError(
      "Capture framebuffer size differs from requested dimensions");
    return false;
  }
  Logger::LogTrace("Framebuffer: " + std::to_string(fbWidth) + "x" +
                   std::to_string(fbHeight));

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
  const bool requestedVsync = !m_captureOnly && isVsyncRequested(envVars);
  if (swapIntervalInitialized && requestedVsync == vsyncEnabled) {
    return;
  }

  glfwSwapInterval(requestedVsync ? 1 : 0);
  vsyncEnabled = requestedVsync;
  swapIntervalInitialized = true;
  if (!m_captureOnly) {
    Logger::LogInfo(requestedVsync ? "VSync on: presenting at the monitor rate"
                                   : "VSync off: presentation is uncapped");
  }
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
RenderWindow::setTitle(const std::string& title)
{
  windowTitle = title;
  if (window != nullptr && !m_captureOnly) {
    glfwSetWindowTitle(window, windowTitle.c_str());
  }
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
  // Callers (the F11 hotkey, the display service) report the change to the
  // user; this records it.
  Logger::LogTrace(isFullScreen ? "Window entered fullscreen"
                                : "Window returned to windowed mode");
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
RenderWindow::cancelCloseRequest()
{
  glfwSetWindowShouldClose(window, GLFW_FALSE);
}

int
RenderWindow::getRefreshRate() const
{
  GLFWmonitor* monitor = glfwGetPrimaryMonitor();
  if (monitor != nullptr) {
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    if (mode != nullptr && mode->refreshRate > 0) {
      return mode->refreshRate;
    }
  }
  return 60;
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
    try {
      Logger::LogTrace("Render window destroyed");
    } catch (...) {
    }
  }
  if (glfwInitialized) {
    glfwTerminate();
    glfwInitialized = false;
  }
}

std::unique_ptr<IRenderWindow>
CreateCaptureWindow(int width, int height)
{
  std::unique_ptr<RenderWindow> window = std::make_unique<RenderWindow>(
    width, height, "Illumo capture", nullptr, true);
  if (!window->initialize()) {
    return nullptr;
  }
  return window;
}
