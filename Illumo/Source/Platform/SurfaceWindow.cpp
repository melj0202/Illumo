#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include "PixelWindow.h"
#include <GLFW/glfw3.h>
#include <Illumo/Platform/SurfaceWindow.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <algorithm>

// ISurfaceWindow over PixelWindow (the D-UI5 presenter).
class PixelSurfaceWindow final : public ISurfaceWindow
{
public:
  explicit PixelSurfaceWindow(std::unique_ptr<PixelWindow> window)
    : m_window(std::move(window))
  {
  }
  bool closeRequested() const override { return m_window->isCloseRequested(); }
  void clientSize(int* width, int* height) const override
  {
    m_window->clientSize(width, height);
  }
  void clientOrigin(int* x, int* y) const override
  {
    m_window->clientOrigin(x, y);
  }
  bool takeRepaintRequest() override { return m_window->takeRepaintRequest(); }
  std::vector<SurfaceWindowEvent> takeEvents() override
  {
    std::vector<SurfaceWindowEvent> events;
    for (const PixelWindow::Event& source : m_window->takeEvents()) {
      SurfaceWindowEvent event;
      switch (source.kind) {
        case PixelWindow::EventKind::Key:
          event.kind = SurfaceWindowEvent::Kind::Key;
          break;
        case PixelWindow::EventKind::Character:
          event.kind = SurfaceWindowEvent::Kind::Character;
          break;
        case PixelWindow::EventKind::MouseButton:
          event.kind = SurfaceWindowEvent::Kind::MouseButton;
          break;
        case PixelWindow::EventKind::MouseMove:
          event.kind = SurfaceWindowEvent::Kind::MouseMove;
          break;
        case PixelWindow::EventKind::Scroll:
          event.kind = SurfaceWindowEvent::Kind::Scroll;
          break;
      }
      event.key = source.key;
      event.action = source.action;
      event.modifiers = source.modifiers;
      event.codepoint = source.codepoint;
      event.x = source.x;
      event.y = source.y;
      event.scroll = source.scroll;
      events.push_back(event);
    }
    return events;
  }
  bool present(const std::vector<std::uint8_t>& rgba,
               int width,
               int height) override
  {
    return m_window->present(rgba, width, height);
  }
  void setTitle(const std::string& title) override
  {
    m_window->setTitle(title);
  }
  void focus() override { m_window->focus(); }
  bool focused() const override { return m_window->isFocused(); }
  void cursor(double* x, double* y) const override { m_window->cursor(x, y); }

private:
  std::unique_ptr<PixelWindow> m_window;
};

// Keeps a requested client rectangle on a monitor: the work area that holds
// its centre (else the primary one), with room above for the title bar.
static void
clampToMonitor(int width, int height, int* x, int* y)
{
  int count = 0;
  GLFWmonitor** monitors = glfwGetMonitors(&count);
  if (monitors == nullptr || count <= 0) {
    return;
  }
  const int centreX = *x + width / 2;
  const int centreY = *y + height / 2;
  int left = 0;
  int top = 0;
  int areaWidth = 0;
  int areaHeight = 0;
  glfwGetMonitorWorkarea(monitors[0], &left, &top, &areaWidth, &areaHeight);
  for (int index = 0; index < count; ++index) {
    int candidateLeft = 0;
    int candidateTop = 0;
    int candidateWidth = 0;
    int candidateHeight = 0;
    glfwGetMonitorWorkarea(monitors[index],
                           &candidateLeft,
                           &candidateTop,
                           &candidateWidth,
                           &candidateHeight);
    if (centreX >= candidateLeft && centreX < candidateLeft + candidateWidth &&
        centreY >= candidateTop && centreY < candidateTop + candidateHeight) {
      left = candidateLeft;
      top = candidateTop;
      areaWidth = candidateWidth;
      areaHeight = candidateHeight;
      break;
    }
  }
  constexpr int kTitleBar = 32;
  if (areaWidth <= 0 || areaHeight <= 0) {
    return;
  }
  *x = std::clamp(*x, left, std::max(left, left + areaWidth - width));
  *y = std::clamp(
    *y, top + kTitleBar, std::max(top + kTitleBar, top + areaHeight - height));
}

class PixelSurfaceWindowFactory final : public ISurfaceWindowFactory
{
public:
  bool available() const override
  {
    return PixelWindow::isPresentationSupported();
  }
  std::unique_ptr<ISurfaceWindow> create(const std::string& title,
                                         int x,
                                         int y,
                                         int width,
                                         int height,
                                         int minimumWidth,
                                         int minimumHeight,
                                         std::string* error) override
  {
    clampToMonitor(width, height, &x, &y);
    std::unique_ptr<PixelWindow> window =
      PixelWindow::create(title, x, y, width, height, error);
    if (!window) {
      return nullptr;
    }
    window->setMinimumSize(minimumWidth, minimumHeight);
    return std::make_unique<PixelSurfaceWindow>(std::move(window));
  }
  bool mainOrigin(IRenderWindow& window, int* x, int* y) const override
  {
    GLFWwindow* handle = window.getWindowInstance();
    if (handle == nullptr) {
      return false;
    }
    glfwGetWindowPos(handle, x, y);
    return true;
  }
  bool mainFocused(IRenderWindow& window) const override
  {
    GLFWwindow* handle = window.getWindowInstance();
    return handle != nullptr &&
           glfwGetWindowAttrib(handle, GLFW_FOCUSED) == GLFW_TRUE;
  }
};

ISurfaceWindowFactory&
PlatformSurfaceWindows()
{
  static PixelSurfaceWindowFactory factory;
  return factory;
}
