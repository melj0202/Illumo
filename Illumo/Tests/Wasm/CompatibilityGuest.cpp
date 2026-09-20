#include <IllumoGuest/Wire.h>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <wasm_simd128.h>

static int s_count = 0;
static std::vector<int> s_initialized{ 3, 1, 2 };
static volatile v128_t s_simd;
static unsigned char* volatile s_allocation = nullptr;
static std::vector<std::byte> s_response;

extern "C" int
illumo_guest_describe()
{
  return 1;
}

extern "C" std::uint32_t
illumo_guest_alloc(int size)
{
  return reinterpret_cast<std::uint32_t>(new unsigned char[size]);
}

extern "C" int
illumo_guest_free(std::uint32_t address)
{
  delete[] reinterpret_cast<unsigned char*>(address);
  return 0;
}

extern "C" std::uint32_t
illumo_guest_job(const std::byte* input, int length)
{
  GuestWireReader request(std::span(input, static_cast<std::size_t>(length)));
  const std::uint32_t operation = request.u32();
  if (!request.valid() || operation == 1) {
    __builtin_trap();
  }
  GuestWireWriter response;
  response.u32(static_cast<std::uint32_t>(++s_count));
  const std::string text = request.text();
  if (!request.finished()) {
    __builtin_trap();
  }
  response.text(text);
  s_response = response.take();
  return reinterpret_cast<std::uint32_t>(s_response.data());
}

extern "C" int
illumo_guest_result_size()
{
  return static_cast<int>(s_response.size());
}

struct UnwindProbe
{
  explicit UnwindProbe(bool& destroyed)
    : m_destroyed(destroyed)
  {
  }
  ~UnwindProbe() { m_destroyed = true; }
  UnwindProbe(const UnwindProbe&) = delete;
  UnwindProbe& operator=(const UnwindProbe&) = delete;
  UnwindProbe(UnwindProbe&&) = delete;
  UnwindProbe& operator=(UnwindProbe&&) = delete;
  bool& m_destroyed;
};

extern "C" int
compatibility()
{
  std::vector<int> values = s_initialized;
  std::ranges::sort(values);
  std::unordered_map<std::string, int> map{ { "answer", values[2] } };
  bool unwound = false;
  try {
    UnwindProbe probe(unwound);
    throw std::runtime_error("guest exception");
  } catch (const std::exception& error) {
    if (std::string(error.what()) != "guest exception") {
      return -1;
    }
  }
  if (!unwound) {
    return -2;
  }
  s_simd = wasm_i32x4_splat(map.at("answer"));
  const v128_t lanes = s_simd;
  return wasm_i32x4_extract_lane(lanes, 0) + ++s_count;
}

extern "C" int
allocationFailure()
{
  try {
    s_allocation = new unsigned char[80u * 1024u * 1024u];
    delete[] s_allocation;
    s_allocation = nullptr;
    return 0;
  } catch (const std::bad_alloc&) {
    return 1;
  }
}

extern "C" int
counter()
{
  return ++s_count;
}

extern "C" int
spin()
{
  volatile unsigned int value = 0;
  while (true) {
    value = value + 1u;
  }
}

extern "C" int
crash()
{
  __builtin_trap();
}

extern "C" int
grow(int pages)
{
  return static_cast<int>(__builtin_wasm_memory_grow(0, pages));
}

extern "C" std::uint32_t
allocate(int size)
{
  return reinterpret_cast<std::uint32_t>(new unsigned char[size]);
}

extern "C" int
release(std::uint32_t pointer)
{
  delete[] reinterpret_cast<unsigned char*>(pointer);
  return 0;
}
