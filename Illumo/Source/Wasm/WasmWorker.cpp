#include <Illumo/Wasm/WasmWorker.h>

#include <array>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <thread>

struct WasmWorker::State
{
  State(std::vector<std::byte> bytes, WasmLimits value, std::uint32_t maximum)
    : module(std::move(bytes))
    , limits(value)
    , messageLimit(maximum)
  {
  }
  ~State()
  {
    stop();
    if (thread.joinable()) {
      thread.join();
    }
  }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;

  void stop()
  {
    std::lock_guard<std::mutex> lock(mutex);
    stopping = true;
    wake.notify_one();
  }

  bool execute(WasmInstance& instance,
               std::span<const std::byte> request,
               std::vector<std::byte>& output)
  {
    std::int32_t address = 0;
    const std::array<std::int32_t, 1> size{ static_cast<std::int32_t>(
      request.size()) };
    if (!instance.call("illumo_guest_alloc", size, address) || address == 0 ||
        !instance.copyToMemory(static_cast<std::uint32_t>(address), request)) {
      return false;
    }
    std::int32_t response = 0;
    const std::array<std::int32_t, 2> args{ address, size[0] };
    std::int32_t responseSize = 0;
    if (!instance.call("illumo_guest_job", args, response) ||
        !instance.call("illumo_guest_result_size", {}, responseSize) ||
        responseSize < 0 ||
        static_cast<std::uint32_t>(responseSize) > messageLimit) {
      return false;
    }
    output.resize(static_cast<std::size_t>(responseSize));
    if (!instance.copyFromMemory(static_cast<std::uint32_t>(response),
                                 output)) {
      return false;
    }
    const std::array<std::int32_t, 1> release{ address };
    std::int32_t ignored = 0;
    return instance.call("illumo_guest_free", release, ignored);
  }

  void run()
  {
    try {
      WasmInstance instance(limits);
      std::int32_t abi = 0;
      if (messageLimit == 0 || messageLimit > INT32_MAX ||
          !instance.load(module) ||
          !instance.call("illumo_guest_describe", {}, abi) || abi != 1) {
        fail(instance.error().empty() ? "Unsupported worker ABI or limits"
                                      : instance.error());
        return;
      }
      module.clear();
      module.shrink_to_fit();
      {
        std::lock_guard<std::mutex> lock(mutex);
        current = WasmWorkerStatus::Idle;
      }
      for (;;) {
        std::vector<std::byte> request;
        std::uint64_t id = 0;
        {
          std::unique_lock<std::mutex> lock(mutex);
          wake.wait(lock, [this]() {
            return stopping || current == WasmWorkerStatus::Working;
          });
          if (stopping) {
            return;
          }
          request.swap(input);
          id = activeId;
        }
        WasmJobResult completed;
        completed.requestId = id;
        if (!execute(instance, request, completed.bytes)) {
          fail(instance.error().empty() ? "Invalid worker response"
                                        : instance.error(),
               id);
          return;
        }
        {
          std::lock_guard<std::mutex> lock(mutex);
          if (stopping) {
            return;
          }
          result = std::move(completed);
          current = WasmWorkerStatus::Completed;
        }
      }
    } catch (const std::exception& exception) {
      fail(exception.what(), activeId);
    } catch (...) {
      fail("Unknown worker failure", activeId);
    }
  }

  void fail(std::string error, std::uint64_t id = 0)
  {
    std::lock_guard<std::mutex> lock(mutex);
    result = { id, {}, std::move(error) };
    current = WasmWorkerStatus::Failed;
    failureDelivered = false;
  }

  mutable std::mutex mutex;
  std::condition_variable wake;
  std::vector<std::byte> module;
  WasmLimits limits;
  std::uint32_t messageLimit;
  std::vector<std::byte> input;
  WasmJobResult result;
  WasmWorkerStatus current = WasmWorkerStatus::Loading;
  std::uint64_t nextId = 1;
  std::uint64_t activeId = 0;
  bool stopping = false;
  bool failureDelivered = false;
  std::thread thread;
};

WasmWorker::WasmWorker(std::vector<std::byte> module,
                       WasmLimits limits,
                       std::uint32_t messageLimit)
  : m_state(std::make_unique<State>(std::move(module), limits, messageLimit))
{
  m_state->thread = std::thread(&State::run, m_state.get());
}

WasmWorker::~WasmWorker() = default;

WasmWorkerStatus
WasmWorker::status() const
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  return m_state->current;
}

bool
WasmWorker::submit(std::span<const std::byte> bytes, std::uint64_t& requestId)
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  if (m_state->stopping || m_state->current != WasmWorkerStatus::Idle ||
      bytes.size() > m_state->messageLimit || m_state->nextId == 0) {
    return false;
  }
  m_state->input.assign(bytes.begin(), bytes.end());
  requestId = m_state->nextId++;
  m_state->activeId = requestId;
  m_state->current = WasmWorkerStatus::Working;
  m_state->wake.notify_one();
  return true;
}

bool
WasmWorker::poll(WasmJobResult& result)
{
  std::lock_guard<std::mutex> lock(m_state->mutex);
  if (m_state->stopping) {
    return false;
  }
  if (m_state->current == WasmWorkerStatus::Failed &&
      !m_state->failureDelivered) {
    result = std::move(m_state->result);
    m_state->failureDelivered = true;
    return true;
  }
  if (m_state->current != WasmWorkerStatus::Completed) {
    return false;
  }
  result = std::move(m_state->result);
  m_state->current = WasmWorkerStatus::Idle;
  return true;
}

void
WasmWorker::requestStop()
{
  m_state->stop();
}
