#pragma once

// Pure input service: no Game includes (D-E2).

#include <Illumo/Foundation/ArrayQueue.h>
#include <Illumo/Services/InputContext.h>
#include <Illumo/Services/KeyCode.h>

struct GLFWwindow;
#include <array>
#include <queue>
#include <string>
#include <unordered_map>

#define MAX_INPUT_EVENTS 256
#define NUM_INPUT_CONTEXTS 32

class InputManager
{
  friend class InputManagerTestAccess;

public:
  struct KeyPressEvent
  {
    KeyCode key;
    InputAction action;
    int modifiers;
  };

private:
  // I need this to iterate with for loop
  static inline constexpr KeyCode AllKeyCodes[] = {
    KeyCode::None,
    KeyCode::Space,
    KeyCode::A,
    KeyCode::B,
    KeyCode::C,
    KeyCode::D,
    KeyCode::E,
    KeyCode::F,
    KeyCode::G,
    KeyCode::H,
    KeyCode::I,
    KeyCode::J,
    KeyCode::K,
    KeyCode::L,
    KeyCode::M,
    KeyCode::N,
    KeyCode::O,
    KeyCode::P,
    KeyCode::Q,
    KeyCode::R,
    KeyCode::S,
    KeyCode::T,
    KeyCode::U,
    KeyCode::V,
    KeyCode::W,
    KeyCode::X,
    KeyCode::Y,
    KeyCode::Z,
    KeyCode::Num0,
    KeyCode::Num1,
    KeyCode::Num2,
    KeyCode::Num3,
    KeyCode::Num4,
    KeyCode::Num5,
    KeyCode::Num6,
    KeyCode::Num7,
    KeyCode::Num8,
    KeyCode::Num9,
    KeyCode::Escape,
    KeyCode::Enter,
    KeyCode::Tab,
    KeyCode::Backspace,
    KeyCode::Insert,
    KeyCode::Delete,
    KeyCode::Right,
    KeyCode::Grave,
    KeyCode::Left,
    KeyCode::Down,
    KeyCode::Up,
    KeyCode::PageUp,
    KeyCode::PageDown,
    KeyCode::Home,
    KeyCode::End,
    KeyCode::F1,
    KeyCode::F2,
    KeyCode::F3,
    KeyCode::F4,
    KeyCode::F5,
    KeyCode::F6,
    KeyCode::F7,
    KeyCode::F8,
    KeyCode::F9,
    KeyCode::F10,
    KeyCode::F11,
    KeyCode::F12,
    KeyCode::MouseLeft,
    KeyCode::MouseRight,
    KeyCode::MouseMiddle,
    KeyCode::MouseButton4,
    KeyCode::MouseButton5,
    KeyCode::MouseButton6,
    KeyCode::MouseButton7,
    KeyCode::MouseButton8,
  };
  GLFWwindow* window;
  static inline InputManager* s_Instance = nullptr;

  double* scrollOffset;
  std::unordered_map<KeyCode, InputAction> inputStatesCurrent;
  std::unordered_map<KeyCode, InputAction> inputStatesPrevious;
  InputContext inputContexts[NUM_INPUT_CONTEXTS];
  InputContext* activeInputContext;
  std::queue<unsigned int> charQueue;
  std::queue<KeyPressEvent> keyQueue;

  long numInputContexts;
  int m_modifierFlags;

  KeyCode TranslateKeyCodeToGLFW(int glfwKey);

  int TranslateKeyCodeFromGLFW(KeyCode keyCode);

  [[nodiscard]] InputAction TranslateInputActionGLFW(int glfwAction);

public:
  InputManager(GLFWwindow* window);
  ~InputManager();

  std::queue<unsigned int>& getCharQueue() { return charQueue; }
  std::queue<KeyPressEvent>& getKeyQueue() { return keyQueue; }

  void clearCharQueue();
  void clearKeyQueue();

  void update();

  InputAction GetInputAction(KeyCode keyCode);

  bool isKeyPressed(KeyCode key);

  bool isKeyReleased(KeyCode key);

  bool isMouseButtonPressed(KeyCode mouseButton);

  bool isShiftPressed() const;

  bool isControlPressed() const;

  bool isMouseButtonReleased(KeyCode mouseButton);

  void setActiveInputContext(long inputContext);

  InputContext* getActiveInputContext() { return activeInputContext; }

  [[nodiscard]] bool isActionActive(std::string actionTag);

  [[nodiscard]] long registerInputContext(InputContext inputContext);

  double* getMouseScrollOffset() { return scrollOffset; }

  std::array<double, 2> getMousePosition();

  static void characterCallback(GLFWwindow* /*window*/, unsigned int codepoint);

  static void scrollCallback(GLFWwindow* /*window*/,
                             double /*xoffset*/,
                             double yoffset);

  static void normalKeyCallback(GLFWwindow* /*window*/,
                                const int key,
                                int /*scancode*/,
                                const int action,
                                const int mods);
};
