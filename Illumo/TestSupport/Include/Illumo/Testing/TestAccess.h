#pragma once

#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/KeyCode.h>

class InputManagerTestAccess
{
public:
  static void setAction(InputManager& input, KeyCode key, InputAction action)
  {
    input.inputStatesCurrent[key] = action;
  }

  static void setPreviousAction(InputManager& input,
                                KeyCode key,
                                InputAction action)
  {
    input.inputStatesPrevious[key] = action;
  }

  static void setModifierFlags(InputManager& input, int modifiers)
  {
    input.m_modifierFlags = modifiers;
  }

  static int toGlfw(InputManager& input, KeyCode key)
  {
    return input.TranslateKeyCodeFromGLFW(key);
  }

  static KeyCode fromGlfw(InputManager& input, int key)
  {
    return input.TranslateKeyCodeToGLFW(key);
  }

  static InputAction actionFromGlfw(InputManager& input, int action)
  {
    return input.TranslateInputActionGLFW(action);
  }
};
