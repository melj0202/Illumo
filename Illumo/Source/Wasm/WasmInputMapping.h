#pragma once

#include <Illumo/Services/KeyCode.h>
#include <IllumoGuest/Input.h>

// Host key and action codes to their guest wire values. GuestKey::Count
// marks a key the guest ABI does not carry.
inline GuestKeyAction
wasmGuestAction(InputAction action)
{
  switch (action) {
    case InputAction::Press:
      return GuestKeyAction::Press;
    case InputAction::Release:
      return GuestKeyAction::Release;
    case InputAction::Hold:
      return GuestKeyAction::Hold;
    default:
      return GuestKeyAction::None;
  }
}

inline GuestKey
wasmGuestKey(KeyCode key)
{
  switch (key) {
#define ILLUMO_GUEST_KEY(name, number)                                         \
  case KeyCode::name:                                                          \
    return GuestKey::name;
#include <IllumoGuest/Keys.inc>
#undef ILLUMO_GUEST_KEY
    default:
      return GuestKey::Count;
  }
}
