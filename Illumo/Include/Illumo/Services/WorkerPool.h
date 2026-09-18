#pragma once

#include <cstddef>
#include <memory>

// Owner-thread range dispatcher. Workers see only disjoint caller-owned ranges;
// callbacks must not throw, submit recursively, or call owner-thread methods.
// The caller keeps context alive until join. No allocation occurs on dispatch.
class WorkerPool
{
public:
  using RangeFunction = void (*)(void*, size_t, size_t) noexcept;

  WorkerPool();
  ~WorkerPool();
  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;
  WorkerPool(WorkerPool&&) = delete;
  WorkerPool& operator=(WorkerPool&&) = delete;

  bool start(size_t workerCount);
  void stop();
  bool submitRange(size_t count,
                   size_t grain,
                   RangeFunction function,
                   void* context);
  void join();
  size_t getWorkerCount() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
