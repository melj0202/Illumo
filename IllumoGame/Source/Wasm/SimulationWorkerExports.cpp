#include "SimulationProtocol.h"
#include <cstdlib>

static SimulationGuestWorker worker;
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
  std::string error;
  if (!worker.execute(
        { static_cast<const std::byte*>(pointer), length }, result, error)) {
    // The mutable worker must be retired after any failed generation/parse.
    __builtin_trap();
  }
  return result.data();
}
