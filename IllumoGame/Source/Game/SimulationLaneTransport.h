#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Opaque request/reply channels to isolated simulation compute lanes, one
// outstanding request per lane. The package implements it over host job
// services; native tests loop it back in-process. Game code never learns how
// a lane executes, only which bytes it returned.
class SimulationLaneTransport
{
public:
  SimulationLaneTransport() = default;
  virtual ~SimulationLaneTransport() = default;
  SimulationLaneTransport(const SimulationLaneTransport&) = delete;
  SimulationLaneTransport& operator=(const SimulationLaneTransport&) = delete;
  SimulationLaneTransport(SimulationLaneTransport&&) = delete;
  SimulationLaneTransport& operator=(SimulationLaneTransport&&) = delete;

  // Granted lanes; 0 while the grant is unknown or when none are available.
  virtual std::uint32_t laneCount() = 0;
  // False while the grant is still being requested.
  virtual bool laneCountKnown() const = 0;
  // Queues one request and consumes it; false (request untouched) when the
  // lane is busy or cannot accept it now.
  virtual bool submit(std::uint32_t lane, std::vector<std::byte>&& request) = 0;
  // 1 with the reply once the lane answered, 0 while pending or idle, -1
  // when the lane failed (a failed lane never answers again).
  virtual int poll(std::uint32_t lane, std::vector<std::byte>& reply) = 0;
  virtual bool busy(std::uint32_t lane) const = 0;
};
