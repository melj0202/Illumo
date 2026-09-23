#pragma once

#include <Illumo/Services/KeyCode.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;

// A secondary top-level OS window with no GPU context. GLFW (created with
// GLFW_NO_API) supplies the native title bar, move/resize, close, focus, and
// input callbacks; content is a CPU RGBA image the platform presents (Windows:
// GDI). The main loop's glfwPollEvents pumps it. Main-thread only; destroy it
// before GLFW terminates. Used by the detached developer console (D-UI5).
class PixelWindow
{
public:
  enum class EventKind : unsigned char
  {
    Key,
    Character,
    MouseButton,
    MouseMove,
    Scroll
  };

  struct Event
  {
    EventKind kind = EventKind::Key;
    KeyCode key = KeyCode::None;
    InputAction action = InputAction::None;
    int modifiers = 0;
    unsigned int codepoint = 0;
    double x = 0.0;
    double y = 0.0;
    double scroll = 0.0;
  };

  // Client-area origin in screen coordinates. Returns null with *error set
  // when the platform cannot present CPU images or window creation fails.
  static std::unique_ptr<PixelWindow> create(const std::string& title,
                                             int x,
                                             int y,
                                             int width,
                                             int height,
                                             std::string* error);
  static bool isPresentationSupported();

  ~PixelWindow();
  PixelWindow(const PixelWindow&) = delete;
  PixelWindow& operator=(const PixelWindow&) = delete;
  PixelWindow(PixelWindow&&) = delete;
  PixelWindow& operator=(PixelWindow&&) = delete;

  bool isCloseRequested() const { return m_closeRequested; }
  void clientSize(int* width, int* height) const;
  // The OS asked for the content again (exposed after being covered).
  bool takeRepaintRequest();
  std::vector<Event> takeEvents();
  bool present(const std::vector<std::uint8_t>& rgba, int width, int height);
  void focus();

private:
  PixelWindow() = default;

  static void keyCallback(GLFWwindow* window,
                          int key,
                          int scancode,
                          int action,
                          int mods);
  static void characterCallback(GLFWwindow* window, unsigned int codepoint);
  static void mouseButtonCallback(GLFWwindow* window,
                                  int button,
                                  int action,
                                  int mods);
  static void cursorCallback(GLFWwindow* window, double x, double y);
  static void scrollCallback(GLFWwindow* window, double x, double y);
  static void closeCallback(GLFWwindow* window);
  static void refreshCallback(GLFWwindow* window);
  static PixelWindow* owner(GLFWwindow* window);
  void push(const Event& event);

  // Bounds queued input when nothing drains it (a stalled frame).
  static constexpr std::size_t kMaxQueuedEvents = 512;

  GLFWwindow* m_window = nullptr;
  std::vector<Event> m_events;
  std::vector<std::uint8_t> m_scratch;
  bool m_closeRequested = false;
  bool m_repaintRequested = true;
};

// Platform presenter (PixelWindowPresent per OS). Converts top-down RGBA to
// the native format using `scratch`; false when unsupported or failed.
bool
PresentPixelWindowImage(GLFWwindow* window,
                        const std::uint8_t* rgba,
                        int width,
                        int height,
                        std::vector<std::uint8_t>& scratch);
bool
PixelWindowPresentationSupported();
