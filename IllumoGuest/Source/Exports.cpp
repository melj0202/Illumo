#include <IllumoGuest/Application.h>
#include <cstdlib>

static std::unique_ptr<GuestApplication> application;
static std::vector<std::byte> response;
static std::uint64_t session = 0;
static std::uint64_t sequence = 0;
static bool started = false;

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
  if (application) {
    __builtin_trap();
  }
  application = CreateGuestApplication();
  if (!application) {
    __builtin_trap();
  }
  GuestWireWriter writer;
  application->describe().write(writer);
  response = writer.take();
  return response.data();
}

static const void*
dispatch(const void* pointer, std::uint32_t length, GuestCall expected)
{
  GuestEnvelope request;
  if (!application ||
      !GuestEnvelope::read({ static_cast<const std::byte*>(pointer), length },
                           request) ||
      request.call != expected || request.sequence != sequence + 1 ||
      (session != 0 && request.session != session)) {
    __builtin_trap();
  }
  sequence = request.sequence;
  session = request.session;
  GuestWireWriter payload;
  if (expected == GuestCall::Init) {
    if (started) {
      __builtin_trap();
    }
    GuestWireReader startup(request.payload);
    const std::uint32_t grants = startup.u32();
    if (!startup.valid() ||
        (application->describe().requiredCapabilities & ~grants) != 0) {
      __builtin_trap();
    }
    started = application->start(startup.bytes(startup.remaining()));
    payload.u32(started ? 1 : 0);
  } else {
    if (!started) {
      __builtin_trap();
    }
    if (expected == GuestCall::Update) {
      GuestInput input;
      if (!GuestInput::read(request.payload, input)) {
        __builtin_trap();
      }
      application->update(input);
      payload.u32(application->closeRequested() ? GuestUpdateFlags::RequestClose
                                                : 0u);
      const std::vector<std::byte> message = application->extensionRequest();
      if (message.size() > 65536) {
        __builtin_trap();
      }
      payload.u32(static_cast<std::uint32_t>(message.size()));
      payload.bytes(message);
    } else if (expected == GuestCall::Frame) {
      if (!request.payload.empty()) {
        __builtin_trap();
      }
      application->frame().write(payload);
    } else if (expected == GuestCall::Close) {
      if (!request.payload.empty()) {
        __builtin_trap();
      }
      payload.u32(application->close() ? 1 : 0);
    } else if (expected == GuestCall::Shutdown) {
      if (!request.payload.empty()) {
        __builtin_trap();
      }
      application->shutdown();
      application.reset();
      started = false;
    } else if (expected == GuestCall::Services) {
      std::vector<std::byte> messages;
      if (!application->services().exchange(request.payload, messages)) {
        __builtin_trap();
      }
      payload.bytes(messages);
    } else if (expected == GuestCall::Receive) {
      if (request.payload.size() > 65536) {
        __builtin_trap();
      }
      const std::vector<std::byte> message =
        application->receive(request.payload);
      if (message.size() > 65536) {
        __builtin_trap();
      }
      payload.bytes(message);
    } else {
      __builtin_trap();
    }
  }
  request.payload = payload.data();
  GuestWireWriter packet;
  request.write(packet);
  response = packet.take();
  return response.data();
}

extern "C" const void*
illumo_guest_init(const void* p, std::uint32_t n)
{
  return dispatch(p, n, GuestCall::Init);
}
extern "C" const void*
illumo_guest_update(const void* p, std::uint32_t n)
{
  return dispatch(p, n, GuestCall::Update);
}
extern "C" const void*
illumo_guest_frame(const void* p, std::uint32_t n)
{
  return dispatch(p, n, GuestCall::Frame);
}
extern "C" const void*
illumo_guest_close(const void* p, std::uint32_t n)
{
  return dispatch(p, n, GuestCall::Close);
}
extern "C" const void*
illumo_guest_shutdown(const void* p, std::uint32_t n)
{
  return dispatch(p, n, GuestCall::Shutdown);
}

extern "C" const void*
illumo_guest_receive(const void* p, std::uint32_t n)
{
  return dispatch(p, n, GuestCall::Receive);
}

extern "C" const void*
illumo_guest_services(const void* p, std::uint32_t n)
{
  return dispatch(p, n, GuestCall::Services);
}
