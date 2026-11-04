#include <Illumo/Services/InputManager.h>
#include <Illumo/Services/Logger.h>
#include <limits>
#include <utility>

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
InputManager::suppressKeyForFrame(KeyCode key)
{
  if (key >= KeyCode::Space && key <= KeyCode::F12) {
    m_suppressedKeys[static_cast<size_t>(key)] = true;
  }
}

bool
InputManager::isKeySuppressed(KeyCode key) const
{
  return key >= KeyCode::Space && key <= KeyCode::F12 &&
         m_suppressedKeys[static_cast<size_t>(key)];
}

bool
InputManager::isKeyReleased(KeyCode key)
{
  if (isKeySuppressed(key)) {
    return false;
  }
  const std::unordered_map<KeyCode, InputAction>::const_iterator state =
    inputStatesCurrent.find(key);
  return state != inputStatesCurrent.end() &&
         state->second == InputAction::Release;
}

bool
InputManager::isMouseButtonReleased(KeyCode mouseButton)
{
  return isKeyReleased(mouseButton);
}

bool
InputManager::setActiveInputContext(long inputContext)
{
  if (inputContext < 0) {
    return false;
  }
  for (size_t i = 0; i < contextIds.size(); ++i) {
    if (contextIds[i] == inputContext) {
      activeInputContext = &inputContexts[i];
      return true;
    }
  }
  return false;
}

bool
InputManager::unregisterInputContext(long inputContext)
{
  if (inputContext < 0) {
    return false;
  }
  for (size_t i = 0; i < contextIds.size(); ++i) {
    if (contextIds[i] == inputContext) {
      if (activeInputContext == &inputContexts[i]) {
        activeInputContext = &neutralContext;
      }
      inputContexts[i] = InputContext{};
      contextIds[i] = -1;
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool
InputManager::isActionActive(std::string_view actionTag)
{
  const InputContext::ActionMap& actions = activeInputContext->getActions();
  const InputContext::ActionMap::const_iterator found = actions.find(actionTag);
  if (found == actions.end()) {
    return false;
  }
  const InputEvent& ie = found->second;
  if (isKeySuppressed(ie.keyCode)) {
    return false;
  }
  return inputStatesCurrent[ie.keyCode] == ie.inputAction;
}

[[nodiscard]] long
InputManager::registerInputContext(InputContext inputContext)
{
  if (nextContextId < std::numeric_limits<long>::max()) {
    for (size_t i = 0; i < contextIds.size(); ++i) {
      if (contextIds[i] == -1) {
        inputContexts[i] = std::move(inputContext);
        contextIds[i] = nextContextId++;
        return contextIds[i];
      }
    }
  }
  Logger::LogError("Input context capacity or identifier space exhausted");
  return -1;
}

void
InputManager::setGamepadState(const GamepadState& state)
{
  m_previousGamepadState = m_gamepadState;
  m_gamepadState = state;
}

float
InputManager::getGamepadAxis(GamepadAxis axis) const
{
  const size_t index = static_cast<size_t>(axis);
  return index < m_gamepadState.axes.size() ? m_gamepadState.axes[index] : 0.0f;
}

std::array<float, 2>
InputManager::getGamepadLeftStick() const
{
  return { m_gamepadState.axes[static_cast<size_t>(GamepadAxis::LeftX)],
           m_gamepadState.axes[static_cast<size_t>(GamepadAxis::LeftY)] };
}

std::array<float, 2>
InputManager::getGamepadRightStick() const
{
  return { m_gamepadState.axes[static_cast<size_t>(GamepadAxis::RightX)],
           m_gamepadState.axes[static_cast<size_t>(GamepadAxis::RightY)] };
}

float
InputManager::getGamepadLeftTrigger() const
{
  return m_gamepadState.axes[static_cast<size_t>(GamepadAxis::LeftTrigger)];
}

float
InputManager::getGamepadRightTrigger() const
{
  return m_gamepadState.axes[static_cast<size_t>(GamepadAxis::RightTrigger)];
}

InputAction
InputManager::getGamepadButtonAction(GamepadButton button) const
{
  const size_t index = static_cast<size_t>(button);
  return index < m_gamepadState.buttons.size() ? m_gamepadState.buttons[index]
                                               : InputAction::None;
}

bool
InputManager::isGamepadButtonPressed(GamepadButton button) const
{
  const InputAction action = getGamepadButtonAction(button);
  return action == InputAction::Press || action == InputAction::Hold;
}

bool
InputManager::isGamepadButtonDown(GamepadButton button) const
{
  return isGamepadButtonPressed(button);
}

bool
InputManager::isGamepadButtonJustPressed(GamepadButton button) const
{
  return getGamepadButtonAction(button) == InputAction::Press;
}

bool
InputManager::isGamepadButtonReleased(GamepadButton button) const
{
  return getGamepadButtonAction(button) == InputAction::Release;
}
