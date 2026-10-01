#include <Illumo/Wasm/WasmGuest.h>

#include <Illumo/Foundation/Profile.h>
#include <algorithm>
#include <array>
#include <atomic>

static std::atomic<std::uint64_t> nextSession{ 1 };

WasmGuest::WasmGuest(WasmLimits limits, std::uint32_t messageLimit)
  : m_limits(limits)
  , m_messageLimit(messageLimit)
  , m_request(messageLimit)
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
  m_transfer = 0;
  m_transferCapacity = 0;
  return false;
}

bool
WasmGuest::transfer(std::span<const std::byte> request,
                    std::int32_t& address,
                    bool& retained)
{
  ILLUMO_PROFILE_ZONE("WasmGuest.transfer");
  const std::uint32_t size = static_cast<std::uint32_t>(request.size());
  const std::uint32_t previousCapacity = m_transferCapacity;
  retained = size <= RetainedTransferBytes;
  if (retained && size > m_transferCapacity && m_transfer != 0) {
    const std::array<std::int32_t, 1> release{ m_transfer };
    std::int32_t ignored = 0;
    m_transfer = 0;
    m_transferCapacity = 0;
    if (!m_instance->call("illumo_guest_free", release, ignored)) {
      return fail(m_instance->error());
    }
  }
  if (!retained || m_transfer == 0) {
    // The retained buffer grows geometrically so a slowly growing request
    // does not reallocate on every call; a one-off buffer is exact.
    const std::uint32_t capacity =
      retained ? std::min(RetainedTransferBytes,
                          std::max({ size, 4096u, previousCapacity * 2u }))
               : size;
    const std::array<std::int32_t, 1> allocation{ static_cast<std::int32_t>(
      capacity) };
    std::int32_t allocated = 0;
    if (!m_instance->call("illumo_guest_alloc", allocation, allocated)) {
      return fail(m_instance->error());
    }
    if (allocated == 0) {
      return fail("Guest transfer allocation failed");
    }
    if (retained) {
      m_transfer = allocated;
      m_transferCapacity = capacity;
    }
    address = allocated;
  } else {
    address = m_transfer;
  }
  if (!m_instance->copyToMemory(static_cast<std::uint32_t>(address), request)) {
    return fail(m_instance->error());
  }
  return true;
}

bool
WasmGuest::readResult(std::int32_t address,
                      std::vector<std::byte>& output,
                      std::uint32_t maximum)
{
  ILLUMO_PROFILE_ZONE("WasmGuest.readResult");
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
{
  ILLUMO_PROFILE_ZONE("WasmGuest.start");
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
  {
    ILLUMO_PROFILE_ZONE("WasmGuest.load");
    if (!m_instance->load(module) ||
        !m_instance->call("illumo_guest_describe", {}, abi)) {
      return fail(m_instance->error());
    }
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
  if (initial.failed()) {
    return fail("Guest startup data exceeds the message limit");
  }
  if (!exchange(GuestCall::Init, initial.data(), response)) {
    return false;
  }
  m_started = true;
  return true;
}

bool
WasmGuest::exchange(GuestCall call,
                    std::span<const std::byte> input,
                    std::vector<std::byte>& output)
{
  output.clear();
  std::span<const std::byte> payload;
  if (!exchangeView(call, input, payload)) {
    return false;
  }
  ILLUMO_PROFILE_ZONE("WasmGuest.copyReply");
  output.assign(payload.begin(), payload.end());
  return true;
}

bool
WasmGuest::exchangeView(GuestCall call,
                        std::span<const std::byte> input,
                        std::span<const std::byte>& output)
{
  ILLUMO_PROFILE_ZONE("WasmGuest.exchange");
  output = {};
  if (++m_sequence == 0) {
    return fail("Guest call sequence exhausted");
  }
  m_request.clear();
  GuestEnvelope{ call, m_session, m_sequence, input }.write(m_request);
  if (m_request.failed()) {
    return fail(m_request.failure());
  }
  std::int32_t address = 0;
  bool retained = false;
  if (!transfer(m_request.data(), address, retained)) {
    return false;
  }
  constexpr std::array<const char*, 7> exports{
    "illumo_guest_init",    "illumo_guest_update",   "illumo_guest_frame",
    "illumo_guest_close",   "illumo_guest_shutdown", "illumo_guest_receive",
    "illumo_guest_services"
  };
  const std::array<std::int32_t, 2> arguments{
    address, static_cast<std::int32_t>(m_request.data().size())
  };
  std::int32_t result = 0;
  {
    ILLUMO_PROFILE_ZONE("WasmGuest.call");
    if (!m_instance->call(
          exports[static_cast<std::size_t>(call) - 1], arguments, result)) {
      return fail(m_instance->error());
    }
  }
  std::int32_t size = 0;
  if (!m_instance->call("illumo_guest_result_size", {}, size)) {
    return fail(m_instance->error());
  }
  // The request buffer is released before the reply is viewed, so no call
  // into the guest follows the view (only a call can change guest memory).
  if (!retained) {
    const std::array<std::int32_t, 1> release{ address };
    std::int32_t ignored = 0;
    if (!m_instance->call("illumo_guest_free", release, ignored)) {
      return fail(m_instance->error());
    }
  }
  if (size < 0 || static_cast<std::uint32_t>(size) > m_messageLimit ||
      (size != 0 && result == 0)) {
    return fail("Guest result exceeds the message contract");
  }
  std::span<const std::byte> replyBytes;
  if (!m_instance->viewMemory(static_cast<std::uint32_t>(result),
                              static_cast<std::size_t>(size),
                              replyBytes)) {
    return fail(m_instance->error());
  }
  GuestEnvelope reply;
  if (!GuestEnvelope::read(replyBytes, reply) || reply.call != call ||
      reply.session != m_session || reply.sequence != m_sequence) {
    return fail("Guest response envelope does not match its invocation");
  }
  output = reply.payload;
  return true;
}

bool
WasmGuest::invoke(GuestCall call,
                  std::span<const std::byte> input,
                  std::vector<std::byte>& output)
{
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
}

bool
WasmGuest::invokeView(GuestCall call,
                      std::span<const std::byte> input,
                      std::span<const std::byte>& output)
{
  output = {};
  if (!isAlive()) {
    return false;
  }
  if (call != GuestCall::Frame) {
    return fail("Only frames are read in place");
  }
  if ((m_capabilities & static_cast<std::uint32_t>(GuestCapability::Render)) ==
      0) {
    return fail("Guest lifecycle operation lacks a capability grant");
  }
  return exchangeView(call, input, output);
}

void
WasmGuest::retire(std::string reason)
{
  fail(std::move(reason));
}

void
WasmGuest::shutdown()
{
  ILLUMO_PROFILE_ZONE("WasmGuest.shutdown");
  if (isAlive()) {
    std::vector<std::byte> ignored;
    exchange(GuestCall::Shutdown, {}, ignored);
  }
  m_started = false;
  m_capabilities = 0;
  m_instance.reset();
  m_transfer = 0;
  m_transferCapacity = 0;
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
