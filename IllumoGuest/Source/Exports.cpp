#include <Illumo/Foundation/Profile.h>
#include <IllumoGuest/Application.h>
#include <cstdlib>

static std::unique_ptr<GuestApplication> application;
// Retained between calls so steady frames reuse their capacity: the result
// the host reads, the call payload, the recorded frame and service messages.
static GuestWireWriter response;
static GuestWireWriter payload;
static GuestFrame recorded;
static std::vector<std::byte> messages;
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
  return static_cast<std::int32_t>(response.data().size());
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
  ILLUMO_PROFILE_ZONE("Guest.manifest");
  if (application) {
    __builtin_trap();
  }
  application = CreateGuestApplication();
  if (!application) {
    __builtin_trap();
  }
  response.clear();
  application->describe().write(response);
  return response.data().data();
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
  payload.clear();
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
    application->grantCapabilities(grants);
    started = application->start(startup.bytes(startup.remaining()));
    payload.u32(started ? 1 : 0);
  } else {
    if (!started) {
      __builtin_trap();
    }
    if (expected == GuestCall::Update) {
      GuestInput input;
      {
        ILLUMO_PROFILE_ZONE("Guest.decodeInput");
        if (!GuestInput::read(request.payload, input)) {
          __builtin_trap();
        }
      }
      {
        ILLUMO_PROFILE_ZONE("Guest.applicationUpdate");
        application->update(input);
      }
      const bool closing = application->closeRequested();
      payload.u32((closing ? GuestUpdateFlags::RequestClose : 0u) |
                  (closing && application->restartRequested()
                     ? GuestUpdateFlags::RequestRestart
                     : 0u) |
                  (application->services().hasOutgoing()
                     ? GuestUpdateFlags::ServicesPending
                     : 0u));
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
      {
        ILLUMO_PROFILE_ZONE("Guest.recordFrame");
        application->recordFrame(recorded);
      }
      {
        ILLUMO_PROFILE_ZONE("GuestFrame.write");
        recorded.write(payload);
      }
      ILLUMO_PROFILE_PLOT("Guest frame bytes", payload.data().size());
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
      recorded = GuestFrame{};
      started = false;
    } else if (expected == GuestCall::Services) {
      {
        ILLUMO_PROFILE_ZONE("GuestServices.exchange");
        if (!application->services().exchange(request.payload, messages)) {
          __builtin_trap();
        }
      }
      ILLUMO_PROFILE_PLOT("Guest service completion bytes",
                          request.payload.size());
      ILLUMO_PROFILE_PLOT("Guest service request bytes", messages.size());
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
  ILLUMO_PROFILE_ZONE("Guest.writeEnvelope");
  request.payload = payload.data();
  response.clear();
  request.write(response);
  return response.data().data();
}

extern "C" const void*
illumo_guest_init(const void* p, std::uint32_t n)
{
  ILLUMO_PROFILE_ZONE("Guest.init");
  return dispatch(p, n, GuestCall::Init);
}
extern "C" const void*
illumo_guest_update(const void* p, std::uint32_t n)
{
  ILLUMO_PROFILE_ZONE("Guest.update");
  return dispatch(p, n, GuestCall::Update);
}
extern "C" const void*
illumo_guest_frame(const void* p, std::uint32_t n)
{
  ILLUMO_PROFILE_ZONE("Guest.frame");
  return dispatch(p, n, GuestCall::Frame);
}
extern "C" const void*
illumo_guest_close(const void* p, std::uint32_t n)
{
  ILLUMO_PROFILE_ZONE("Guest.close");
  return dispatch(p, n, GuestCall::Close);
}
extern "C" const void*
illumo_guest_shutdown(const void* p, std::uint32_t n)
{
  ILLUMO_PROFILE_ZONE("Guest.shutdown");
  return dispatch(p, n, GuestCall::Shutdown);
}

extern "C" const void*
illumo_guest_receive(const void* p, std::uint32_t n)
{
  ILLUMO_PROFILE_ZONE("Guest.receive");
  return dispatch(p, n, GuestCall::Receive);
}

extern "C" const void*
illumo_guest_services(const void* p, std::uint32_t n)
{
  ILLUMO_PROFILE_ZONE("Guest.services");
  return dispatch(p, n, GuestCall::Services);
}
