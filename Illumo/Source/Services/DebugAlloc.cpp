#include <Illumo/Services/DebugAlloc.h>
#include <cstdlib>
#include <exception>
#include <new>
#include <tracy/TracyC.h>

void*
operator new(std::size_t size)
{
  void* ptr = std::malloc(size);
  if (!ptr) {
    throw std::bad_alloc();
  }
  TracyCAlloc(ptr, size);
  return ptr;
}

void
operator delete(void* ptr) noexcept
{
  TracyCFree(ptr);
  std::free(ptr);
}

void*
operator new[](std::size_t size)
{
  void* ptr = std::malloc(size);
  if (!ptr) {
    throw std::bad_alloc();
  }
  TracyCAlloc(ptr, size);
  return ptr;
}

void
operator delete[](void* ptr) noexcept
{
  TracyCFree(ptr);
  std::free(ptr);
}

void
operator delete(void* ptr, std::size_t) noexcept
{
  ::operator delete(ptr);
}

void
operator delete[](void* ptr, std::size_t) noexcept
{
  ::operator delete[](ptr);
}

void*
operator new(std::size_t size, const std::nothrow_t&) noexcept
{
  void* ptr = std::malloc(size == 0 ? 1 : size);
  if (ptr) {
    TracyCAlloc(ptr, size);
  }
  return ptr;
}

void*
operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
  return ::operator new(size, std::nothrow);
}

void
operator delete(void* ptr, const std::nothrow_t&) noexcept
{
  ::operator delete(ptr);
}

void
operator delete[](void* ptr, const std::nothrow_t&) noexcept
{
  ::operator delete[](ptr);
}
