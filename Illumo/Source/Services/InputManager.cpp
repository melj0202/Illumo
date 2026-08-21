#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>

#ifdef TRACY_ENABLE
#include "tracy/Tracy.hpp"
#else
#define ZoneNamed(varname, name)
#endif

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

KeyCode
InputManager::TranslateKeyCodeToGLFW(int glfwKey)
{
  switch (glfwKey) {
    case GLFW_KEY_SPACE:
      return KeyCode::Space;
    case GLFW_KEY_A:
      return KeyCode::A;
    case GLFW_KEY_B:
      return KeyCode::B;
    case GLFW_KEY_C:
      return KeyCode::C;
    case GLFW_KEY_D:
      return KeyCode::D;
    case GLFW_KEY_E:
      return KeyCode::E;
    case GLFW_KEY_F:
      return KeyCode::F;
    case GLFW_KEY_G:
      return KeyCode::G;
    case GLFW_KEY_H:
      return KeyCode::H;
    case GLFW_KEY_I:
      return KeyCode::I;
    case GLFW_KEY_J:
      return KeyCode::J;
    case GLFW_KEY_K:
      return KeyCode::K;
    case GLFW_KEY_L:
      return KeyCode::L;
    case GLFW_KEY_M:
      return KeyCode::M;
    case GLFW_KEY_N:
      return KeyCode::N;
    case GLFW_KEY_O:
      return KeyCode::O;
    case GLFW_KEY_P:
      return KeyCode::P;
    case GLFW_KEY_Q:
      return KeyCode::Q;
    case GLFW_KEY_R:
      return KeyCode::R;
    case GLFW_KEY_S:
      return KeyCode::S;
    case GLFW_KEY_T:
      return KeyCode::T;
    case GLFW_KEY_U:
      return KeyCode::U;
    case GLFW_KEY_V:
      return KeyCode::V;
    case GLFW_KEY_W:
      return KeyCode::W;
    case GLFW_KEY_X:
      return KeyCode::X;
    case GLFW_KEY_Y:
      return KeyCode::Y;
    case GLFW_KEY_Z:
      return KeyCode::Z;
    case GLFW_KEY_0:
      return KeyCode::Num0;
    case GLFW_KEY_1:
      return KeyCode::Num1;
    case GLFW_KEY_2:
      return KeyCode::Num2;
    case GLFW_KEY_3:
      return KeyCode::Num3;
    case GLFW_KEY_4:
      return KeyCode::Num4;
    case GLFW_KEY_5:
      return KeyCode::Num5;
    case GLFW_KEY_6:
      return KeyCode::Num6;
    case GLFW_KEY_7:
      return KeyCode::Num7;
    case GLFW_KEY_8:
      return KeyCode::Num8;
    case GLFW_KEY_9:
      return KeyCode::Num9;
    case GLFW_KEY_ESCAPE:
      return KeyCode::Escape;
    case GLFW_KEY_ENTER:
      return KeyCode::Enter;
    case GLFW_KEY_TAB:
      return KeyCode::Tab;
    case GLFW_KEY_BACKSPACE:
      return KeyCode::Backspace;
    case GLFW_KEY_INSERT:
      return KeyCode::Insert;
    case GLFW_KEY_DELETE:
      return KeyCode::Delete;
    case GLFW_KEY_RIGHT:
      return KeyCode::Right;
    case GLFW_KEY_GRAVE_ACCENT:
      return KeyCode::Grave;
    case GLFW_KEY_LEFT:
      return KeyCode::Left;
    case GLFW_KEY_DOWN:
      return KeyCode::Down;
    case GLFW_KEY_UP:
      return KeyCode::Up;
    case GLFW_KEY_PAGE_UP:
      return KeyCode::PageUp;
    case GLFW_KEY_PAGE_DOWN:
      return KeyCode::PageDown;
    case GLFW_KEY_HOME:
      return KeyCode::Home;
    case GLFW_KEY_END:
      return KeyCode::End;
    case GLFW_KEY_F1:
      return KeyCode::F1;
    case GLFW_KEY_F2:
      return KeyCode::F2;
    case GLFW_KEY_F3:
      return KeyCode::F3;
    case GLFW_KEY_F4:
      return KeyCode::F4;
    case GLFW_KEY_F5:
      return KeyCode::F5;
    case GLFW_KEY_F6:
      return KeyCode::F6;
    case GLFW_KEY_F7:
      return KeyCode::F7;
    case GLFW_KEY_F8:
      return KeyCode::F8;
    case GLFW_KEY_F9:
      return KeyCode::F9;
    case GLFW_KEY_F10:
      return KeyCode::F10;
    case GLFW_KEY_F11:
      return KeyCode::F11;
    case GLFW_KEY_F12:
      return KeyCode::F12;
    case GLFW_MOUSE_BUTTON_LEFT:
      return KeyCode::MouseLeft;
    case GLFW_MOUSE_BUTTON_RIGHT:
      return KeyCode::MouseRight;
    case GLFW_MOUSE_BUTTON_MIDDLE:
      return KeyCode::MouseMiddle;
    case GLFW_MOUSE_BUTTON_4:
      return KeyCode::MouseButton4;
    case GLFW_MOUSE_BUTTON_5:
      return KeyCode::MouseButton5;
    case GLFW_MOUSE_BUTTON_6:
      return KeyCode::MouseButton6;
    case GLFW_MOUSE_BUTTON_7:
      return KeyCode::MouseButton7;
    case GLFW_MOUSE_BUTTON_8:
      return KeyCode::MouseButton8;
    default:
      return KeyCode::None;
  }
}

int
InputManager::TranslateKeyCodeFromGLFW(KeyCode keyCode)
{
  switch (keyCode) {
    case KeyCode::Space:
      return GLFW_KEY_SPACE;
    case KeyCode::A:
      return GLFW_KEY_A;
    case KeyCode::B:
      return GLFW_KEY_B;
    case KeyCode::C:
      return GLFW_KEY_C;
    case KeyCode::D:
      return GLFW_KEY_D;
    case KeyCode::E:
      return GLFW_KEY_E;
    case KeyCode::F:
      return GLFW_KEY_F;
    case KeyCode::G:
      return GLFW_KEY_G;
    case KeyCode::H:
      return GLFW_KEY_H;
    case KeyCode::I:
      return GLFW_KEY_I;
    case KeyCode::J:
      return GLFW_KEY_J;
    case KeyCode::K:
      return GLFW_KEY_K;
    case KeyCode::L:
      return GLFW_KEY_L;
    case KeyCode::M:
      return GLFW_KEY_M;
    case KeyCode::N:
      return GLFW_KEY_N;
    case KeyCode::O:
      return GLFW_KEY_O;
    case KeyCode::P:
      return GLFW_KEY_P;
    case KeyCode::Q:
      return GLFW_KEY_Q;
    case KeyCode::R:
      return GLFW_KEY_R;
    case KeyCode::S:
      return GLFW_KEY_S;
    case KeyCode::T:
      return GLFW_KEY_T;
    case KeyCode::U:
      return GLFW_KEY_U;
    case KeyCode::V:
      return GLFW_KEY_V;
    case KeyCode::W:
      return GLFW_KEY_W;
    case KeyCode::X:
      return GLFW_KEY_X;
    case KeyCode::Y:
      return GLFW_KEY_Y;
    case KeyCode::Z:
      return GLFW_KEY_Z;
    case KeyCode::Num0:
      return GLFW_KEY_0;
    case KeyCode::Num1:
      return GLFW_KEY_1;
    case KeyCode::Num2:
      return GLFW_KEY_2;
    case KeyCode::Num3:
      return GLFW_KEY_3;
    case KeyCode::Num4:
      return GLFW_KEY_4;
    case KeyCode::Num5:
      return GLFW_KEY_5;
    case KeyCode::Num6:
      return GLFW_KEY_6;
    case KeyCode::Num7:
      return GLFW_KEY_7;
    case KeyCode::Num8:
      return GLFW_KEY_8;
    case KeyCode::Num9:
      return GLFW_KEY_9;
    case KeyCode::Escape:
      return GLFW_KEY_ESCAPE;
    case KeyCode::Enter:
      return GLFW_KEY_ENTER;
    case KeyCode::Tab:
      return GLFW_KEY_TAB;
    case KeyCode::Backspace:
      return GLFW_KEY_BACKSPACE;
    case KeyCode::Insert:
      return GLFW_KEY_INSERT;
    case KeyCode::Delete:
      return GLFW_KEY_DELETE;
    case KeyCode::Right:
      return GLFW_KEY_RIGHT;
    case KeyCode::Grave:
      return GLFW_KEY_GRAVE_ACCENT;
    case KeyCode::Left:
      return GLFW_KEY_LEFT;
    case KeyCode::Down:
      return GLFW_KEY_DOWN;
    case KeyCode::Up:
      return GLFW_KEY_UP;
    case KeyCode::PageUp:
      return GLFW_KEY_PAGE_UP;
    case KeyCode::PageDown:
      return GLFW_KEY_PAGE_DOWN;
    case KeyCode::Home:
      return GLFW_KEY_HOME;
    case KeyCode::End:
      return GLFW_KEY_END;
    case KeyCode::F1:
      return GLFW_KEY_F1;
    case KeyCode::F2:
      return GLFW_KEY_F2;
    case KeyCode::F3:
      return GLFW_KEY_F3;
    case KeyCode::F4:
      return GLFW_KEY_F4;
    case KeyCode::F5:
      return GLFW_KEY_F5;
    case KeyCode::F6:
      return GLFW_KEY_F6;
    case KeyCode::F7:
      return GLFW_KEY_F7;
    case KeyCode::F8:
      return GLFW_KEY_F8;
    case KeyCode::F9:
      return GLFW_KEY_F9;
    case KeyCode::F10:
      return GLFW_KEY_F10;
    case KeyCode::F11:
      return GLFW_KEY_F11;
    case KeyCode::F12:
      return GLFW_KEY_F12;
    case KeyCode::MouseLeft:
      return GLFW_MOUSE_BUTTON_LEFT;
    case KeyCode::MouseRight:
      return GLFW_MOUSE_BUTTON_RIGHT;
    case KeyCode::MouseMiddle:
      return GLFW_MOUSE_BUTTON_MIDDLE;
    case KeyCode::MouseButton4:
      return GLFW_MOUSE_BUTTON_4;
    case KeyCode::MouseButton5:
      return GLFW_MOUSE_BUTTON_5;
    case KeyCode::MouseButton6:
      return GLFW_MOUSE_BUTTON_6;
    case KeyCode::MouseButton7:
      return GLFW_MOUSE_BUTTON_7;
    case KeyCode::MouseButton8:
      return GLFW_MOUSE_BUTTON_8;
    default:
      return -1;
  }
}

[[nodiscard]] InputAction
InputManager::TranslateInputActionGLFW(int glfwAction)
{
  switch (glfwAction) {
    case GLFW_PRESS:
      return InputAction::Press;
    case GLFW_RELEASE:
      return InputAction::Release;
    case GLFW_REPEAT:
      return InputAction::Hold;
    default:
      return InputAction::Release;
  }
}

InputManager::InputManager(GLFWwindow* window)
  : window(window)
  , activeInputContext(&inputContexts[0])
  , numInputContexts(0)
  , m_modifierFlags(0)
{
  s_Instance = this;
  if (window != nullptr) {
    glfwSetKeyCallback(window, normalKeyCallback);
    glfwSetCharCallback(window, characterCallback);
    glfwSetScrollCallback(window, scrollCallback);
  }
  scrollOffset = new double(0.0);

  for (KeyCode keyCode : AllKeyCodes) {
    inputStatesCurrent[keyCode] = InputAction::None;
    inputStatesPrevious[keyCode] = InputAction::None;
  }

  for (int i = 0; i < NUM_INPUT_CONTEXTS; i++) {
    inputContexts[i] = InputContext();
  }
}

InputManager::~InputManager()
{
  delete scrollOffset;
  if (s_Instance == this) {
    s_Instance = nullptr;
  }
}

void
InputManager::clearCharQueue()
{
  std::queue<unsigned int> empty;
  std::swap(charQueue, empty);
}

void
InputManager::clearKeyQueue()
{
  std::queue<KeyPressEvent> empty;
  std::swap(keyQueue, empty);
}

void
InputManager::update()
{
  ZoneNamed(InputManagerUpdateZone, "InputManager Update");
  *scrollOffset = 0.0;
  if (window != nullptr) {
    glfwPollEvents();
  }
  for (KeyCode keyCode : AllKeyCodes) {
    inputStatesCurrent[keyCode] = GetInputAction(keyCode);

    if (inputStatesCurrent[keyCode] == InputAction::Press &&
        inputStatesPrevious[keyCode] == InputAction::Press) {
      inputStatesCurrent[keyCode] = InputAction::Hold;
    }

    else if (inputStatesCurrent[keyCode] == InputAction::Press &&
             inputStatesPrevious[keyCode] == InputAction::Hold) {
      inputStatesCurrent[keyCode] = InputAction::Hold;
    }

    else if (inputStatesCurrent[keyCode] == InputAction::Release &&
             inputStatesPrevious[keyCode] == InputAction::Release) {
      inputStatesCurrent[keyCode] = InputAction::None;
    }

    inputStatesPrevious[keyCode] = inputStatesCurrent[keyCode];
  }
}

InputAction
InputManager::GetInputAction(KeyCode keyCode)
{
  if (keyCode == KeyCode::None || window == nullptr) {
    return InputAction::None;
  }
  int glfwInputAction;
  if (keyCode >= KeyCode::MouseLeft) {
    glfwInputAction =
      glfwGetMouseButton(window, TranslateKeyCodeFromGLFW(keyCode));
  } else {
    glfwInputAction = glfwGetKey(window, TranslateKeyCodeFromGLFW(keyCode));
  }
  return TranslateInputActionGLFW(glfwInputAction);
}

bool
InputManager::isKeyPressed(KeyCode key)
{
  if (window == nullptr) {
    std::unordered_map<KeyCode, InputAction>::const_iterator it =
      inputStatesCurrent.find(key);
    if (it == inputStatesCurrent.end()) {
      return false;
    }
    return it->second == InputAction::Press || it->second == InputAction::Hold;
  }
  int glfwInputAction = glfwGetKey(window, TranslateKeyCodeFromGLFW(key));

  return TranslateInputActionGLFW(glfwInputAction) == InputAction::Press;
}

bool
InputManager::isKeyReleased(KeyCode key)
{
  if (window == nullptr) {
    return false;
  }
  int glfwInputAction = glfwGetKey(window, TranslateKeyCodeFromGLFW(key));

  return TranslateInputActionGLFW(glfwInputAction) == InputAction::Release;
}

bool
InputManager::isMouseButtonPressed(KeyCode mouseButton)
{
  if (window == nullptr) {
    std::unordered_map<KeyCode, InputAction>::const_iterator it =
      inputStatesCurrent.find(mouseButton);
    if (it == inputStatesCurrent.end()) {
      return false;
    }
    return it->second == InputAction::Press || it->second == InputAction::Hold;
  }
  int glfwInputAction =
    glfwGetMouseButton(window, TranslateKeyCodeFromGLFW(mouseButton));

  return TranslateInputActionGLFW(glfwInputAction) == InputAction::Press;
}

bool
InputManager::isShiftPressed() const
{
  if (window != nullptr) {
    return glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
           glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
  }
  return (m_modifierFlags & GLFW_MOD_SHIFT) != 0;
}

bool
InputManager::isControlPressed() const
{
  if (window != nullptr) {
    return glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
           glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
  }
  return (m_modifierFlags & GLFW_MOD_CONTROL) != 0;
}

bool
InputManager::isMouseButtonReleased(KeyCode mouseButton)
{
  if (window == nullptr) {
    return false;
  }
  int glfwInputAction =
    glfwGetMouseButton(window, TranslateKeyCodeFromGLFW(mouseButton));

  return TranslateInputActionGLFW(glfwInputAction) == InputAction::Release;
}

void
InputManager::setActiveInputContext(long inputContext)
{
  activeInputContext = &inputContexts[inputContext];
}

[[nodiscard]] bool
InputManager::isActionActive(std::string actionTag)
{
  InputEvent ie = activeInputContext->getActionTag(actionTag);
  return inputStatesCurrent[ie.keyCode] == ie.inputAction;
}

[[nodiscard]] long
InputManager::registerInputContext(InputContext inputContext)
{
  if (numInputContexts >= NUM_INPUT_CONTEXTS) {
    Logger::LogError("Too many input contexts registered");
    return -1;
  }
  inputContexts[numInputContexts] = inputContext;
  numInputContexts++;
  return numInputContexts - 1;
}

std::array<double, 2>
InputManager::getMousePosition()
{
  if (window == nullptr) {
    return { 0.0, 0.0 };
  }
  double x, y;
  glfwGetCursorPos(window, &x, &y);
  return { x, y };
}

void
InputManager::characterCallback(GLFWwindow* /*window*/, unsigned int codepoint)
{
  if (s_Instance) {
    s_Instance->charQueue.push(codepoint);
  }
}

void
InputManager::scrollCallback(GLFWwindow* /*window*/,
                             double /*xoffset*/,
                             double yoffset)
{
  if (s_Instance && s_Instance->scrollOffset) {
    *(s_Instance->scrollOffset) = yoffset;
  }
}

void
InputManager::normalKeyCallback(GLFWwindow* /*window*/,
                                const int key,
                                int /*scancode*/,
                                const int action,
                                const int mods)
{
  if (s_Instance) {
    KeyCode localKey = s_Instance->TranslateKeyCodeToGLFW(key);
    InputAction localAction = s_Instance->TranslateInputActionGLFW(action);
    s_Instance->m_modifierFlags = mods;
    s_Instance->keyQueue.push({ localKey, localAction, mods });
  }
}
