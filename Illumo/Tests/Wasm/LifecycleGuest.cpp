#include <IllumoGuest/Protocol.h>
#include <cstdlib>

static std::vector<std::byte> response;
static std::uint32_t updates = 0;

extern "C" std::int32_t
illumo_guest_describe()
{
  return GuestEnvelope::Version;
}
extern "C" std::int32_t
illumo_guest_result_size()
{
  return static_cast<std::int32_t>(response.size());
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

extern "C" const void*
illumo_guest_manifest()
{
  GuestWireWriter writer;
  GuestDescriptor{ GuestRole::Game,
                   static_cast<std::uint32_t>(GuestCapability::Render),
                   "lifecycle-fixture" }
    .write(writer);
  response = writer.take();
  return response.data();
}

static const void*
reply(const void* pointer, std::uint32_t length, GuestCall expected)
{
  GuestEnvelope request;
  if (!GuestEnvelope::read({ static_cast<const std::byte*>(pointer), length },
                           request) ||
      request.call != expected) {
    __builtin_trap();
  }
  GuestWireWriter payload;
  if (expected == GuestCall::Init) {
    GuestWireReader startup(request.payload);
    if (startup.u32() != static_cast<std::uint32_t>(GuestCapability::Render) ||
        startup.text() != "startup" || !startup.finished()) {
      __builtin_trap();
    }
    payload.u32(42);
  } else if (expected == GuestCall::Update) {
    GuestWireReader input(request.payload);
    const std::uint32_t mode = input.u32();
    if (!input.finished()) {
      __builtin_trap();
    }
    if (mode == 1) {
      __builtin_trap();
    }
    if (mode == 2) {
      ++request.sequence;
    }
    if (mode == 3) {
      ++request.session;
    }
    payload.u32(++updates);
  } else if (expected == GuestCall::Frame) {
    payload.u32(updates);
  } else if (expected == GuestCall::Close) {
    payload.u32(updates > 0 ? 1 : 0);
  }
  request.payload = payload.data();
  GuestWireWriter envelope;
  request.write(envelope);
  response = envelope.take();
  return response.data();
}

extern "C" const void*
illumo_guest_init(const void* p, std::uint32_t n)
{
  return reply(p, n, GuestCall::Init);
}
extern "C" const void*
illumo_guest_update(const void* p, std::uint32_t n)
{
  return reply(p, n, GuestCall::Update);
}
extern "C" const void*
illumo_guest_frame(const void* p, std::uint32_t n)
{
  return reply(p, n, GuestCall::Frame);
}
extern "C" const void*
illumo_guest_close(const void* p, std::uint32_t n)
{
  return reply(p, n, GuestCall::Close);
}
extern "C" const void*
illumo_guest_shutdown(const void* p, std::uint32_t n)
{
  return reply(p, n, GuestCall::Shutdown);
}
extern "C" const void*
illumo_guest_receive(const void* p, std::uint32_t n)
{
  return reply(p, n, GuestCall::Receive);
}
