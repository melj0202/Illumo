#pragma once
#include <Illumo/Services/InputManager.h>
#include <IllumoGuest/Input.h>

class GuestInputProvider
{
public:
  static void accept(InputManager& manager, const GuestInput& input);
};
