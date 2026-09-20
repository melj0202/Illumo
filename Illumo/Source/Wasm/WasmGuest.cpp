#include <Illumo/Wasm/WasmGuest.h>

#include <array>
#include <atomic>
#include <exception>

static std::atomic<std::uint64_t> nextSession{ 1 };

WasmGuest::WasmGuest(WasmLimits limits, std::uint32_t messageLimit)
  : m_limits(limits)
  , m_messageLimit(messageLimit)
{
}

WasmGuest::~WasmGuest() = default;

bool
WasmGuest::fail(std::string reason)
{
  if (m_error.empty()) {
    m_error = std::move(reason);
  }
  m_started = false;
  m_capabilities = 0;
  m_instance.reset();
  return false;
}

bool
WasmGuest::readResult(std::int32_t address,
                      std::vector<std::byte>& output,
                      std::uint32_t maximum)
{
  std::int32_t size = 0;
  if (!m_instance->call("illumo_guest_result_size", {}, size)) {
    return fail(m_instance->error());
  }
  if (size < 0 || static_cast<std::uint32_t>(size) > maximum ||
      (size != 0 && address == 0)) {
    return fail("Guest result exceeds the message contract");
  }
  output.resize(static_cast<std::size_t>(size));
  if (!m_instance->copyFromMemory(static_cast<std::uint32_t>(address),
                                  output)) {
    return fail(m_instance->error());
  }
  return true;
}

bool
WasmGuest::start(std::span<const std::byte> module,
                 GuestRole role,
                 std::uint32_t grants,
                 std::span<const std::byte> startup,
                 std::vector<std::byte>& response)
try {
  response.clear();
  if (m_attempted) {
    return false;
  }
  m_attempted = true;
  if (m_messageLimit < GuestEnvelope::HeaderBytes ||
      m_messageLimit > INT32_MAX ||
      (grants & ~GuestEnvelope::KnownCapabilities) != 0 ||
      (role != GuestRole::Game && role != GuestRole::Mod)) {
    return fail("Invalid host guest policy");
  }
  m_session = nextSession.load(std::memory_order_relaxed);
  do {
    if (m_session == UINT64_MAX) {
      return fail("Guest session IDs exhausted");
    }
  } while (!nextSession.compare_exchange_weak(
    m_session, m_session + 1, std::memory_order_relaxed));
  m_instance = std::make_unique<WasmInstance>(m_limits);
  std::int32_t abi = 0;
  if (!m_instance->load(module) ||
      !m_instance->call("illumo_guest_describe", {}, abi)) {
    return fail(m_instance->error());
  }
  if (abi != GuestEnvelope::Version) {
    return fail("Unsupported guest ABI");
  }
  std::int32_t address = 0;
  if (!m_instance->call("illumo_guest_manifest", {}, address)) {
    return fail(m_instance->error());
  }
  std::vector<std::byte> description;
  if (!readResult(address, description, 512)) {
    return false;
  }
  if (!GuestDescriptor::read(description, m_descriptor) ||
      m_descriptor.role != role ||
      (m_descriptor.requiredCapabilities & ~grants) != 0) {
    return fail("Unsupported guest role, descriptor or required capability");
  }
  m_capabilities = grants;
  GuestWireWriter initial(m_messageLimit);
  initial.u32(grants);
  initial.bytes(startup);
  if (!exchange(GuestCall::Init, initial.data(), response)) {
    return false;
  }
  m_started = true;
  return true;
} catch (const std::exception& exception) {
  response.clear();
  return fail(exception.what());
}

bool
WasmGuest::exchange(GuestCall call,
                    std::span<const std::byte> input,
                    std::vector<std::byte>& output)
{
  output.clear();
  if (++m_sequence == 0) {
    return fail("Guest call sequence exhausted");
  }
  GuestWireWriter request(m_messageLimit);
  GuestEnvelope{ call, m_session, m_sequence, input }.write(request);
  const std::array<std::int32_t, 1> allocation{ static_cast<std::int32_t>(
    request.data().size()) };
  std::int32_t address = 0;
  if (!m_instance->call("illumo_guest_alloc", allocation, address)) {
    return fail(m_instance->error());
  }
  if (address == 0) {
    return fail("Guest transfer allocation failed");
  }
  if (!m_instance->copyToMemory(static_cast<std::uint32_t>(address),
                                request.data())) {
    return fail(m_instance->error());
  }
  constexpr std::array<const char*, 7> exports{
    "illumo_guest_init",    "illumo_guest_update",   "illumo_guest_frame",
    "illumo_guest_close",   "illumo_guest_shutdown", "illumo_guest_receive",
    "illumo_guest_services"
  };
  const std::array<std::int32_t, 2> arguments{ address, allocation[0] };
  std::int32_t result = 0;
  if (!m_instance->call(
        exports[static_cast<std::size_t>(call) - 1], arguments, result)) {
    return fail(m_instance->error());
  }
  std::vector<std::byte> copied;
  if (!readResult(result, copied, m_messageLimit)) {
    return false;
  }
  const std::array<std::int32_t, 1> release{ address };
  if (!m_instance->call("illumo_guest_free", release, result)) {
    return fail(m_instance->error());
  }
  GuestEnvelope reply;
  if (!GuestEnvelope::read(copied, reply) || reply.call != call ||
      reply.session != m_session || reply.sequence != m_sequence) {
    return fail("Guest response envelope does not match its invocation");
  }
  output.assign(reply.payload.begin(), reply.payload.end());
  return true;
}

bool
WasmGuest::invoke(GuestCall call,
                  std::span<const std::byte> input,
                  std::vector<std::byte>& output)
try {
  output.clear();
  if (!isAlive()) {
    return false;
  }
  if (call != GuestCall::Update && call != GuestCall::Frame &&
      call != GuestCall::Close && call != GuestCall::Receive &&
      call != GuestCall::Services) {
    return fail("Invalid guest lifecycle operation");
  }
  if (((call == GuestCall::Frame || call == GuestCall::Services) &&
       (m_capabilities & static_cast<std::uint32_t>(GuestCapability::Render)) ==
         0) ||
      (call == GuestCall::Receive &&
       (m_capabilities &
        static_cast<std::uint32_t>(GuestCapability::Messages)) == 0)) {
    return fail("Guest lifecycle operation lacks a capability grant");
  }
  return exchange(call, input, output);
} catch (const std::exception& exception) {
  output.clear();
  return fail(exception.what());
}

void
WasmGuest::retire(std::string reason)
{
  fail(std::move(reason));
}

void
WasmGuest::shutdown()
{
  if (isAlive()) {
    try {
      std::vector<std::byte> ignored;
      exchange(GuestCall::Shutdown, {}, ignored);
    } catch (const std::exception& exception) {
      fail(exception.what());
    }
  }
  m_started = false;
  m_capabilities = 0;
  m_instance.reset();
}

bool
WasmGuest::isAlive() const
{
  return m_started && m_instance && m_instance->isAlive();
}
std::uint64_t
WasmGuest::session() const
{
  return m_session;
}
std::uint32_t
WasmGuest::capabilities() const
{
  return m_capabilities;
}
const GuestDescriptor&
WasmGuest::descriptor() const
{
  return m_descriptor;
}
const std::string&
WasmGuest::error() const
{
  return m_error;
}
