#include "SimulationLanes.h"
#include "SimulationProtocol.h"
#include <cstdlib>
#include <cstring>

// One worker store: either a whole-world CSW1 worker (parity tests) or one
// CSL1 simulation lane, selected by each request's magic.
static SimulationGuestWorker worker;
static SimulationLaneWorker lane;
static std::vector<std::byte> result;

extern "C" std::int32_t
illumo_guest_describe()
{
  return 1;
}
extern "C" void*
illumo_guest_alloc(std::uint32_t size)
{
  return std::malloc(size);
}
extern "C" std::int32_t
illumo_guest_free(void* pointer)
{
  std::free(pointer);
  return 0;
}
extern "C" std::int32_t
illumo_guest_result_size()
{
  return static_cast<std::int32_t>(result.size());
}
extern "C" const void*
illumo_guest_job(const void* pointer, std::uint32_t length)
{
  const std::span<const std::byte> request{
    static_cast<const std::byte*>(pointer), length
  };
  std::uint32_t magic = 0;
  if (length >= sizeof(magic)) {
    std::memcpy(&magic, pointer, sizeof(magic)); // wire values are LE
  }
  std::string error;
  const bool executed = magic == SimulationLaneRequest::Magic
                          ? lane.execute(request, result, error)
                          : worker.execute(request, result, error);
  if (!executed) {
    // The mutable worker must be retired after any failed generation/parse.
    __builtin_trap();
  }
  return result.data();
}
