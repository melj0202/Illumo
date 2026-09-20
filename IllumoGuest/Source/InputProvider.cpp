#include <IllumoGuest/InputProvider.h>
#include <stdexcept>

static InputAction
nativeAction(GuestKeyAction action)
{
  switch (action) {
    case GuestKeyAction::Press:
      return InputAction::Press;
    case GuestKeyAction::Release:
      return InputAction::Release;
    case GuestKeyAction::Hold:
      return InputAction::Hold;
    default:
      return InputAction::None;
  }
}
static KeyCode
nativeKey(GuestKey key)
{
  switch (key) {
#define ILLUMO_GUEST_KEY(name, number)                                         \
  case GuestKey::name:                                                         \
    return KeyCode::name;
#include <IllumoGuest/Keys.inc>
#undef ILLUMO_GUEST_KEY
    default:
      return KeyCode::None;
  }
}
InputManager::InputManager(GLFWwindow* requestedWindow)
  : window(nullptr)
  , scrollOffset(nullptr)
  , activeInputContext(&neutralContext)
  , m_modifierFlags(0)
{
  if (requestedWindow != nullptr) {
    throw std::invalid_argument("A guest cannot own a native window");
  }
  scrollOffset = new double(0);
  contextIds.fill(-1);
  for (KeyCode code : AllKeyCodes) {
    inputStatesCurrent[code] = InputAction::None;
    inputStatesPrevious[code] = InputAction::None;
  }
}
InputManager::~InputManager()
{
  delete scrollOffset;
}
void
InputManager::update()
{
  clearCharQueue();
  clearKeyQueue();
}
InputAction
InputManager::GetInputAction(KeyCode key)
{
  return frameAction(key);
}
bool
InputManager::isKeyPressed(KeyCode key)
{
  const InputAction action = frameAction(key);
  return action == InputAction::Press || action == InputAction::Hold;
}
bool
InputManager::isMouseButtonPressed(KeyCode key)
{
  return isKeyPressed(key);
}
bool
InputManager::isShiftPressed() const
{
  return (static_cast<unsigned int>(m_modifierFlags) & 1u) != 0;
}
bool
InputManager::isControlPressed() const
{
  return (static_cast<unsigned int>(m_modifierFlags) & 2u) != 0;
}
bool
InputManager::isAltPressed() const
{
  return (static_cast<unsigned int>(m_modifierFlags) & 4u) != 0;
}
std::array<double, 2>
InputManager::getMousePosition()
{
  return m_snapshotMouse;
}
void
GuestInputProvider::accept(InputManager& manager, const GuestInput& input)
{
  manager.clearCharQueue();
  manager.clearKeyQueue();
  manager.m_suppressedKeys.fill(false);
  manager.inputStatesPrevious = manager.inputStatesCurrent;
#define ILLUMO_GUEST_KEY(name, number)                                         \
  manager.inputStatesCurrent[KeyCode::name] = nativeAction(input.keys[number]);
#include <IllumoGuest/Keys.inc>
#undef ILLUMO_GUEST_KEY
  for (const GuestKeyEvent& event : input.events) {
    manager.keyQueue.push({ nativeKey(event.key),
                            nativeAction(event.action),
                            static_cast<int>(event.modifiers) });
  }
  for (std::uint32_t character : input.characters) {
    manager.charQueue.push(character);
  }
  manager.m_modifierFlags = static_cast<int>(input.modifiers);
  *manager.scrollOffset = input.scroll;
  manager.m_snapshotMouse = { input.mouseX, input.mouseY };
}
