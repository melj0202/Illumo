#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>

// Bump-pointer arena with chained fixed-size chunks. Individual free is not
// supported; Clear() / Deallocate() bulk-reset the arena (destructors are not
// invoked — use POD or manage lifetimes yourself).
//
// Fit: frame scratch, transient command data, parsers, load buffers.
class ArenaAlloc
{
public:
  static constexpr size_t kMaxChunks = 4;

private:
  struct Chunk
  {
    size_t alignment;
    char* data;
    explicit Chunk(size_t size,
                   size_t requestedAlignment = alignof(std::max_align_t))
      : alignment(requestedAlignment)
      , data(
          static_cast<char*>(::operator new(size, std::align_val_t(alignment))))
    {
    }
    ~Chunk() { ::operator delete(data, std::align_val_t(alignment)); }
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;
    Chunk(Chunk&&) = delete;
    Chunk& operator=(Chunk&&) = delete;
    size_t wastedBytes = 0;
    Chunk* next = nullptr;
  };

  Chunk* chunk = nullptr;
  Chunk* chunkHead = nullptr;
  size_t offset = 0;
  size_t chunkSize = 0;
  size_t numChunks = 0;

  void* alignedAddress(size_t size, size_t alignment)
  {
    void* address = chunk->data + offset;
    size_t available = chunkSize - offset;
    if (std::align(alignment, size, address, available) != nullptr) {
      return address;
    }
    if (offset == 0 && alignment > chunk->alignment) {
      // No live allocation moves when an empty chunk gains stronger alignment.
      char* replacement = static_cast<char*>(
        ::operator new(chunkSize, std::align_val_t(alignment)));
      ::operator delete(chunk->data, std::align_val_t(chunk->alignment));
      chunk->data = replacement;
      chunk->alignment = alignment;
      return replacement;
    }
    return nullptr;
  }

  bool growChunk(size_t alignment)
  {
    if (numChunks >= kMaxChunks) {
      return false;
    }
    Chunk* nextChunk = new Chunk(chunkSize, alignment);
    nextChunk->next = nullptr;
    nextChunk->wastedBytes = 0;
    chunk->wastedBytes = chunkSize - offset;
    chunk->next = nextChunk;
    chunk = nextChunk;
    offset = 0;
    numChunks += 1;
    return true;
  }

  void* allocateRaw(size_t size, size_t alignment)
  {
    if (size == 0 || size > chunkSize) {
      return nullptr;
    }
    if (alignment == 0) {
      alignment = 1;
    }
    if ((alignment & (alignment - 1)) != 0) {
      return nullptr;
    }

    void* address = alignedAddress(size, alignment);
    if (address == nullptr) {
      if (!growChunk(alignment)) {
        return nullptr;
      }
      address = chunk->data;
    }
    offset =
      static_cast<size_t>(static_cast<char*>(address) - chunk->data) + size;
    return address;
  }

public:
  explicit ArenaAlloc(size_t bytesPerChunk)
    : chunkSize(bytesPerChunk)
  {
    if (chunkSize == 0) {
      chunkSize = 1;
    }
    chunk = new Chunk(chunkSize);
    chunk->next = nullptr;
    chunk->wastedBytes = 0;
    chunkHead = chunk;
    numChunks = 1;
    offset = 0;
  }

  ~ArenaAlloc()
  {
    Chunk* currentChunk = chunkHead;
    while (currentChunk != nullptr) {
      Chunk* next = currentChunk->next;
      delete currentChunk;
      currentChunk = next;
    }
  }

  ArenaAlloc(const ArenaAlloc&) = delete;
  ArenaAlloc& operator=(const ArenaAlloc&) = delete;
  ArenaAlloc(ArenaAlloc&&) = delete;
  ArenaAlloc& operator=(ArenaAlloc&&) = delete;

  template<typename T, typename... Args>
  T* Allocate(Args&&... args)
  {
    void* address = allocateRaw(sizeof(T), alignof(T));
    if (address == nullptr) {
      return nullptr;
    }
    return ::new (address) T(std::forward<Args>(args)...);
  }

  // Uninitialized aligned bytes (or nullptr on failure / zero size).
  void* AllocateBytes(size_t size, size_t alignment = alignof(std::max_align_t))
  {
    return allocateRaw(size, alignment);
  }

  // Null-terminated copy of src, or nullptr on failure.
  char* AllocateCString(const char* src, size_t length)
  {
    if (length == std::numeric_limits<size_t>::max()) {
      return nullptr;
    }
    char* dest = static_cast<char*>(allocateRaw(length + 1, alignof(char)));
    if (dest == nullptr) {
      return nullptr;
    }
    if (length > 0 && src != nullptr) {
      std::memcpy(dest, src, length);
    }
    dest[length] = '\0';
    return dest;
  }

  char* AllocateCString(const std::string& text)
  {
    return AllocateCString(text.data(), text.size());
  }

  // Bulk free; synonym for Clear(). Does not run destructors.
  void Deallocate() { Clear(); }

  void Clear()
  {
    offset = 0;
    numChunks = 1;

    Chunk* currentChunk = chunkHead->next;
    while (currentChunk != nullptr) {
      Chunk* nextChunk = currentChunk->next;
      delete currentChunk;
      currentChunk = nextChunk;
    }

    chunkHead->next = nullptr;
    chunkHead->wastedBytes = 0;
    chunk = chunkHead;
  }

  size_t getChunkSize() const { return chunkSize; }
  size_t getNumChunks() const { return numChunks; }
  size_t getOffset() const { return offset; }
};
