#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

// Bump-pointer stack allocator with chained fixed-size chunks. Free is LIFO
// only: Deallocate / FreeTop must release the most recently allocated block.
// Typed Deallocate runs the destructor; FreeTop does not.
// Clear() bulk-resets without running destructors.
//
// Fit: nested temporary lifetimes that free in reverse order of allocation.
class ChainedStackAlloc
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

  // Restore point captured before each successful allocation (handles padding
  // and chunk growth so LIFO free is exact).
  struct Mark
  {
    Chunk* chunk = nullptr;
    size_t offset = 0;
    size_t numChunks = 0;
    void* pointer = nullptr;
  };

  Chunk* chunk = nullptr;
  Chunk* chunkHead = nullptr;
  size_t offset = 0;
  size_t chunkSize = 0;
  size_t numChunks = 0;
  std::vector<Mark> marks;

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

  void releaseChunksAfter(Chunk* keep)
  {
    if (keep == nullptr) {
      return;
    }
    Chunk* current = keep->next;
    keep->next = nullptr;
    keep->wastedBytes = 0;
    while (current != nullptr) {
      Chunk* next = current->next;
      delete current;
      current = next;
    }
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

    Mark mark;
    mark.chunk = chunk;
    mark.offset = offset;
    mark.numChunks = numChunks;

    void* address = alignedAddress(size, alignment);
    if (address == nullptr) {
      if (!growChunk(alignment)) {
        return nullptr;
      }
      address = chunk->data;
    }
    mark.pointer = address;
    try {
      marks.push_back(mark);
    } catch (...) {
      releaseChunksAfter(mark.chunk);
      chunk = mark.chunk;
      offset = mark.offset;
      numChunks = mark.numChunks;
      throw;
    }
    offset =
      static_cast<size_t>(static_cast<char*>(address) - chunk->data) + size;
    return address;
  }

  bool freeTopRaw(void* pointer, bool destroyAsUnknown)
  {
    (void)destroyAsUnknown;
    if (pointer == nullptr || marks.empty()) {
      return false;
    }
    const Mark& top = marks.back();
    if (top.pointer != pointer) {
      return false;
    }
    releaseChunksAfter(top.chunk);
    chunk = top.chunk;
    offset = top.offset;
    numChunks = top.numChunks;
    marks.pop_back();
    return true;
  }

public:
  explicit ChainedStackAlloc(size_t bytesPerChunk)
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

  ~ChainedStackAlloc()
  {
    Chunk* currentChunk = chunkHead;
    while (currentChunk != nullptr) {
      Chunk* next = currentChunk->next;
      delete currentChunk;
      currentChunk = next;
    }
  }

  ChainedStackAlloc(const ChainedStackAlloc&) = delete;
  ChainedStackAlloc& operator=(const ChainedStackAlloc&) = delete;
  ChainedStackAlloc(ChainedStackAlloc&&) = delete;
  ChainedStackAlloc& operator=(ChainedStackAlloc&&) = delete;

  template<typename T, typename... Args>
  T* Allocate(Args&&... args)
  {
    void* address = allocateRaw(sizeof(T), alignof(T));
    if (address == nullptr) {
      return nullptr;
    }
    // allocateRaw already pushed a mark with `address`; placement-new there.
    // Rewind mark pointer is already correct.
    return ::new (address) T(std::forward<Args>(args)...);
  }

  void* AllocateBytes(size_t size, size_t alignment = alignof(std::max_align_t))
  {
    return allocateRaw(size, alignment);
  }

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

  // LIFO free of the most recently allocated T. Calls the destructor.
  template<typename T>
  bool Deallocate(T* data)
  {
    if (data == nullptr || marks.empty()) {
      return false;
    }
    const Mark& top = marks.back();
    if (top.pointer != data) {
      return false;
    }
    data->~T();
    releaseChunksAfter(top.chunk);
    chunk = top.chunk;
    offset = top.offset;
    numChunks = top.numChunks;
    marks.pop_back();
    return true;
  }

  // LIFO free of the top raw/byte allocation (no destructor).
  bool FreeTop(void* pointer) { return freeTopRaw(pointer, false); }

  void Clear()
  {
    marks.clear();
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
  size_t getDepth() const { return marks.size(); }
};
