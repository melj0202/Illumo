#include <Illumo/Services/WorkerPool.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

struct WorkerPool::Impl
{
  std::vector<std::thread> workers;
  std::mutex mutex;
  std::condition_variable ready;
  std::condition_variable complete;
  std::atomic<size_t> next{ 0 };
  size_t generation = 0;
  size_t finished = 0;
  size_t count = 0;
  size_t grain = 1;
  RangeFunction function = nullptr;
  void* context = nullptr;
  bool stopping = false;
  bool active = false;

  void execute()
  {
    const size_t ranges = count / grain + (count % grain != 0 ? 1u : 0u);
    for (;;) {
      const size_t range = next.fetch_add(1, std::memory_order_relaxed);
      if (range >= ranges) {
        return;
      }
      const size_t begin = range * grain;
      function(context, begin, begin + std::min(grain, count - begin));
    }
  }

  void workerLoop()
  {
    size_t observed = 0;
    for (;;) {
      {
        std::unique_lock<std::mutex> lock(mutex);
        ready.wait(lock, [this, &observed]() {
          return stopping || generation != observed;
        });
        if (stopping) {
          return;
        }
        observed = generation;
      }
      execute();
      {
        std::lock_guard<std::mutex> lock(mutex);
        ++finished;
        if (finished == workers.size()) {
          complete.notify_one();
        }
      }
    }
  }
};

WorkerPool::WorkerPool()
  : m_impl(std::make_unique<Impl>())
{
}
WorkerPool::~WorkerPool()
{
  stop();
}

bool
WorkerPool::start(size_t workerCount)
{
  stop();
  m_impl->stopping = false;
  m_impl->generation = 0;
  try {
    m_impl->workers.reserve(workerCount);
    for (size_t i = 0; i < workerCount; ++i) {
      m_impl->workers.emplace_back(&Impl::workerLoop, m_impl.get());
    }
  } catch (...) {
    stop();
    return false;
  }
  return true;
}

void
WorkerPool::stop()
{
  join();
  {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->stopping = true;
  }
  m_impl->ready.notify_all();
  for (std::thread& worker : m_impl->workers) {
    worker.join();
  }
  m_impl->workers.clear();
}

bool
WorkerPool::submitRange(size_t count,
                        size_t grain,
                        RangeFunction function,
                        void* context)
{
  if (m_impl->active || function == nullptr || grain == 0) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->count = count;
    m_impl->grain = grain;
    m_impl->function = function;
    m_impl->context = context;
    m_impl->next.store(0, std::memory_order_relaxed);
    m_impl->finished = 0;
    m_impl->active = true;
    ++m_impl->generation;
  }
  m_impl->ready.notify_all();
  return true;
}

void
WorkerPool::join()
{
  if (!m_impl->active) {
    return;
  }
  m_impl->execute();
  std::unique_lock<std::mutex> lock(m_impl->mutex);
  m_impl->complete.wait(
    lock, [this]() { return m_impl->finished == m_impl->workers.size(); });
  m_impl->active = false;
}

size_t
WorkerPool::getWorkerCount() const
{
  return m_impl->workers.size();
}
