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
InputManager::isActionActive(std::string actionTag)
{
  if (!activeInputContext->getActions().contains(actionTag)) {
    return false;
  }
  InputEvent ie = activeInputContext->getActionTag(actionTag);
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
