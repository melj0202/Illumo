#pragma once
#include <IllumoGuest/Services.h>

// Routes the guest Logger to the host as fire-and-forget Log requests. Lines
// the full service queue refuses (a scene change saturates it) wait in a
// bounded backlog, in order, and flush when requests free up; overflow drops
// the oldest and reports how many were lost once there is room.
class GuestDiagnostics
{
public:
  static constexpr std::size_t MaximumBacklog = 64;
  explicit GuestDiagnostics(GuestServiceQueue& queue);
  ~GuestDiagnostics();
  GuestDiagnostics(const GuestDiagnostics&) = delete;
  GuestDiagnostics& operator=(const GuestDiagnostics&) = delete;
  GuestDiagnostics(GuestDiagnostics&&) = delete;
  GuestDiagnostics& operator=(GuestDiagnostics&&) = delete;
  // Lines that never reached the queue: no route, or pushed out of a full
  // backlog.
  static std::uint64_t droppedMessages();
  // Moves waiting lines into the queue while it has room. Pumped first each
  // update, after the services exchange freed requests and before new work;
  // each new line also flushes the backlog ahead of itself.
  void pump();
};
