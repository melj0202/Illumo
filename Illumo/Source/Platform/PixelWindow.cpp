#include "PixelWindow.h"

#include <GLFW/glfw3.h>
#include <Illumo/Services/InputManager.h>

std::unique_ptr<PixelWindow>
PixelWindow::create(const std::string& title,
                    int x,
                    int y,
                    int width,
                    int height,
                    std::string* error)
{
  if (!PixelWindowPresentationSupported()) {
    if (error != nullptr) {
      *error = "Separate windows are not supported on this platform";
    }
    return nullptr;
  }
  // No client API: this window never owns or touches an OpenGL context, so
  // the main window's current context and backend state stay untouched.
  glfwDefaultWindowHints();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow* handle = glfwCreateWindow(width > 0 ? width : 800,
                                        height > 0 ? height : 480,
                                        title.c_str(),
                                        nullptr,
                                        nullptr);
  glfwDefaultWindowHints();
  if (handle == nullptr) {
    if (error != nullptr) {
      *error = "Could not create the window";
    }
    return nullptr;
  }
  std::unique_ptr<PixelWindow> result(new PixelWindow());
  result->m_window = handle;
  glfwSetWindowUserPointer(handle, result.get());
  glfwSetKeyCallback(handle, keyCallback);
  glfwSetCharCallback(handle, characterCallback);
  glfwSetMouseButtonCallback(handle, mouseButtonCallback);
  glfwSetCursorPosCallback(handle, cursorCallback);
  glfwSetScrollCallback(handle, scrollCallback);
  glfwSetWindowCloseCallback(handle, closeCallback);
  glfwSetWindowRefreshCallback(handle, refreshCallback);
  glfwSetWindowSizeLimits(handle, 360, 200, GLFW_DONT_CARE, GLFW_DONT_CARE);
  glfwSetWindowPos(handle, x, y);
  glfwShowWindow(handle);
  glfwFocusWindow(handle);
  return result;
}

bool
PixelWindow::isPresentationSupported()
{
  return PixelWindowPresentationSupported();
}

PixelWindow::~PixelWindow()
{
  if (m_window != nullptr) {
    glfwSetWindowUserPointer(m_window, nullptr);
    glfwDestroyWindow(m_window);
    m_window = nullptr;
  }
}

PixelWindow*
PixelWindow::owner(GLFWwindow* window)
{
  return static_cast<PixelWindow*>(glfwGetWindowUserPointer(window));
}

void
PixelWindow::push(const Event& event)
{
  if (m_events.size() < kMaxQueuedEvents) {
    m_events.push_back(event);
  }
}

void
PixelWindow::keyCallback(GLFWwindow* window,
                         int key,
                         int scancode,
                         int action,
                         int mods)
{
  (void)scancode;
  PixelWindow* self = owner(window);
  const KeyCode code = InputManager::keyCodeFromGlfw(key);
  if (self == nullptr || code == KeyCode::None) {
    return;
  }
  Event event;
  event.kind = EventKind::Key;
  event.key = code;
  event.action = InputManager::inputActionFromGlfw(action);
  event.modifiers = mods;
  self->push(event);
}

void
PixelWindow::characterCallback(GLFWwindow* window, unsigned int codepoint)
{
  PixelWindow* self = owner(window);
  if (self == nullptr) {
    return;
  }
  Event event;
  event.kind = EventKind::Character;
  event.codepoint = codepoint;
  self->push(event);
}

void
PixelWindow::mouseButtonCallback(GLFWwindow* window,
                                 int button,
                                 int action,
                                 int mods)
{
  PixelWindow* self = owner(window);
  if (self == nullptr) {
    return;
  }
  Event event;
  event.kind = EventKind::MouseButton;
  event.key = button == GLFW_MOUSE_BUTTON_LEFT    ? KeyCode::MouseLeft
              : button == GLFW_MOUSE_BUTTON_RIGHT ? KeyCode::MouseRight
                                                  : KeyCode::MouseMiddle;
  event.action = InputManager::inputActionFromGlfw(action);
  event.modifiers = mods;
  glfwGetCursorPos(window, &event.x, &event.y);
  self->push(event);
}

void
PixelWindow::cursorCallback(GLFWwindow* window, double x, double y)
{
  PixelWindow* self = owner(window);
  if (self == nullptr) {
    return;
  }
  // Coalesce motion so a fast drag cannot flood the bounded queue.
  if (!self->m_events.empty() &&
      self->m_events.back().kind == EventKind::MouseMove) {
    self->m_events.back().x = x;
    self->m_events.back().y = y;
    return;
  }
  Event event;
  event.kind = EventKind::MouseMove;
  event.x = x;
  event.y = y;
  self->push(event);
}

void
PixelWindow::scrollCallback(GLFWwindow* window, double x, double y)
{
  (void)x;
  PixelWindow* self = owner(window);
  if (self == nullptr) {
    return;
  }
  Event event;
  event.kind = EventKind::Scroll;
  event.scroll = y;
  self->push(event);
}

void
PixelWindow::closeCallback(GLFWwindow* window)
{
  PixelWindow* self = owner(window);
  if (self != nullptr) {
    self->m_closeRequested = true;
  }
  // The owner destroys the window; keep GLFW from treating it as closed.
  glfwSetWindowShouldClose(window, GLFW_FALSE);
}

void
PixelWindow::refreshCallback(GLFWwindow* window)
{
  PixelWindow* self = owner(window);
  if (self != nullptr) {
    self->m_repaintRequested = true;
  }
}

void
PixelWindow::clientSize(int* width, int* height) const
{
  int w = 0;
  int h = 0;
  if (m_window != nullptr) {
    glfwGetFramebufferSize(m_window, &w, &h);
  }
  if (width != nullptr) {
    *width = w;
  }
  if (height != nullptr) {
    *height = h;
  }
}

bool
PixelWindow::takeRepaintRequest()
{
  const bool requested = m_repaintRequested;
  m_repaintRequested = false;
  return requested;
}

std::vector<PixelWindow::Event>
PixelWindow::takeEvents()
{
  std::vector<Event> events;
  events.swap(m_events);
  return events;
}

bool
PixelWindow::present(const std::vector<std::uint8_t>& rgba,
                     int width,
                     int height)
{
  if (m_window == nullptr || width <= 0 || height <= 0 ||
      rgba.size() < static_cast<std::size_t>(width) *
                      static_cast<std::size_t>(height) * 4u) {
    return false;
  }
  return PresentPixelWindowImage(
    m_window, rgba.data(), width, height, m_scratch);
}

void
PixelWindow::focus()
{
  if (m_window != nullptr) {
    glfwFocusWindow(m_window);
  }
}

void
PixelWindow::clientOrigin(int* x, int* y) const
{
  int left = 0;
  int top = 0;
  if (m_window != nullptr) {
    glfwGetWindowPos(m_window, &left, &top);
  }
  if (x != nullptr) {
    *x = left;
  }
  if (y != nullptr) {
    *y = top;
  }
}

void
PixelWindow::setTitle(const std::string& title)
{
  if (m_window != nullptr) {
    glfwSetWindowTitle(m_window, title.c_str());
  }
}

bool
PixelWindow::isFocused() const
{
  return m_window != nullptr &&
         glfwGetWindowAttrib(m_window, GLFW_FOCUSED) == GLFW_TRUE;
}

void
PixelWindow::cursor(double* x, double* y) const
{
  double left = 0.0;
  double top = 0.0;
  if (m_window != nullptr) {
    glfwGetCursorPos(m_window, &left, &top);
  }
  if (x != nullptr) {
    *x = left;
  }
  if (y != nullptr) {
    *y = top;
  }
}

void
PixelWindow::setMinimumSize(int width, int height)
{
  if (m_window != nullptr) {
    glfwSetWindowSizeLimits(
      m_window, width, height, GLFW_DONT_CARE, GLFW_DONT_CARE);
  }
}
