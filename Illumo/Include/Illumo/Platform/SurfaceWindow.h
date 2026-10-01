#pragma once

#include <Illumo/Services/KeyCode.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class IRenderWindow;

struct SurfaceWindowEvent
{
  enum class Kind : unsigned char
  {
    Key,
    Character,
    MouseButton,
    MouseMove,
    Scroll
  };
  Kind kind = Kind::Key;
  KeyCode key = KeyCode::None; // Key and MouseButton
  InputAction action = InputAction::None;
  int modifiers = 0;
  unsigned int codepoint = 0;
  double x = 0.0; // MouseButton and MouseMove, client coordinates
  double y = 0.0;
  double scroll = 0.0;
};

// A secondary top-level OS window that shows CPU images supplied by its owner
// and has no GPU context (detached tool panels). The platform supplies the
// title bar, moving, resizing, closing, focus and input. The main loop's
// event pump drives it. Main-thread only; destroy before the platform
// shuts down.
class ISurfaceWindow
{
public:
  ISurfaceWindow() = default;
  virtual ~ISurfaceWindow() = default;
  ISurfaceWindow(const ISurfaceWindow&) = delete;
  ISurfaceWindow& operator=(const ISurfaceWindow&) = delete;
  ISurfaceWindow(ISurfaceWindow&&) = delete;
  ISurfaceWindow& operator=(ISurfaceWindow&&) = delete;

  // The user asked to close it; the owner decides and destroys it.
  virtual bool closeRequested() const = 0;
  virtual void clientSize(int* width, int* height) const = 0;
  // Client-area origin in screen coordinates.
  virtual void clientOrigin(int* x, int* y) const = 0;
  // The platform needs the content presented again (it was exposed).
  virtual bool takeRepaintRequest() = 0;
  virtual std::vector<SurfaceWindowEvent> takeEvents() = 0;
  // Top-down RGBA8 at the client size.
  virtual bool present(const std::vector<std::uint8_t>& rgba,
                       int width,
                       int height) = 0;
  virtual void setTitle(const std::string& title) = 0;
  virtual void focus() = 0;
  virtual bool focused() const = 0;
  // Current cursor position in client coordinates; it may lie outside the
  // client area while a button is held.
  virtual void cursor(double* x, double* y) const = 0;
};

class ISurfaceWindowFactory
{
public:
  ISurfaceWindowFactory() = default;
  virtual ~ISurfaceWindowFactory() = default;
  ISurfaceWindowFactory(const ISurfaceWindowFactory&) = delete;
  ISurfaceWindowFactory& operator=(const ISurfaceWindowFactory&) = delete;
  ISurfaceWindowFactory(ISurfaceWindowFactory&&) = delete;
  ISurfaceWindowFactory& operator=(ISurfaceWindowFactory&&) = delete;

  virtual bool available() const = 0;
  // Client rectangle in screen coordinates. Null with *error set on failure.
  virtual std::unique_ptr<ISurfaceWindow> create(const std::string& title,
                                                 int x,
                                                 int y,
                                                 int width,
                                                 int height,
                                                 int minimumWidth,
                                                 int minimumHeight,
                                                 std::string* error) = 0;
  // The main window's client-area origin in screen coordinates.
  virtual bool mainOrigin(IRenderWindow& window, int* x, int* y) const = 0;
  // The main window has input focus.
  virtual bool mainFocused(IRenderWindow& window) const = 0;
};

// The platform's windows: GLFW windows presented through the OS image path
// (Windows GDI). available() is false where CPU presentation is unsupported.
ISurfaceWindowFactory&
PlatformSurfaceWindows();
