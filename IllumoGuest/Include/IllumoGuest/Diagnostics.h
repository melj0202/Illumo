#pragma once
#include <IllumoGuest/Services.h>

class GuestDiagnostics
{
public:
  explicit GuestDiagnostics(GuestServiceQueue& queue);
  ~GuestDiagnostics();
  GuestDiagnostics(const GuestDiagnostics&) = delete;
  GuestDiagnostics& operator=(const GuestDiagnostics&) = delete;
  GuestDiagnostics(GuestDiagnostics&&) = delete;
  GuestDiagnostics& operator=(GuestDiagnostics&&) = delete;
  static std::uint64_t droppedMessages();
};
