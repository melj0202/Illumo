#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <vector>

// Fixed-size object pool with chained chunks. Each block is sizeof(T) storage;
// Allocate returns raw storage (placement-new is the caller's job unless T is
// trivially constructible). Deallocate returns a block to the free list.
//
// Fit: many same-sized objects with stable slots / frequent recycle.
template<typename T>
class ChainedPoolAlloc
{
public:
  static constexpr size_t kMaxChunks = 4;

private:
  struct Chunk
  {
    size_t count;
    T* data;
    explicit Chunk(size_t blocks)
      : count(blocks)
      , data(std::allocator<T>{}.allocate(blocks))
    {
    }
    ~Chunk() { std::allocator<T>{}.deallocate(data, count); }
    Chunk(const Chunk&) = delete;
    Chunk& operator=(const Chunk&) = delete;
    Chunk(Chunk&&) = delete;
    Chunk& operator=(Chunk&&) = delete;
    Chunk* next = nullptr;
  };

  Chunk* chunk = nullptr;
  Chunk* chunkHead = nullptr;
  size_t blockSize = 0;
  size_t numBlocks = 0;
  size_t numChunks = 0;
  std::vector<T*> freeList;

  void populateFreeList(Chunk* targetChunk)
  {
    for (size_t i = 0; i < numBlocks; ++i) {
      freeList.push_back(targetChunk->data + i);
    }
  }

  bool growChunk()
  {
    if (numChunks >= kMaxChunks) {
      return false;
    }
    Chunk* nextChunk = new Chunk(numBlocks);
    nextChunk->next = nullptr;
    chunk->next = nextChunk;
    chunk = nextChunk;
    populateFreeList(chunk);
    numChunks += 1;
    return true;
  }

public:
  // numBlocks: blocks per chunk (at least 1).
  explicit ChainedPoolAlloc(size_t blocksPerChunk)
    : blockSize(sizeof(T))
    , numBlocks(blocksPerChunk == 0 ? 1 : blocksPerChunk)
  {
    if (numBlocks > std::numeric_limits<size_t>::max() / blockSize ||
        numBlocks > std::numeric_limits<size_t>::max() / kMaxChunks) {
      throw std::bad_array_new_length();
    }
    // Growth only populates an empty list; reserve before owning any chunks.
    freeList.reserve(numBlocks);
    chunk = new Chunk(numBlocks);
    chunk->next = nullptr;
    chunkHead = chunk;
    numChunks = 1;
    populateFreeList(chunk);
  }

  ~ChainedPoolAlloc()
  {
    Chunk* current = chunkHead;
    while (current != nullptr) {
      Chunk* next = current->next;
      delete current;
      current = next;
    }
  }

  ChainedPoolAlloc(const ChainedPoolAlloc&) = delete;
  ChainedPoolAlloc& operator=(const ChainedPoolAlloc&) = delete;
  ChainedPoolAlloc(ChainedPoolAlloc&&) = delete;
  ChainedPoolAlloc& operator=(ChainedPoolAlloc&&) = delete;

  // Returns uninitialized storage for one T, or nullptr when exhausted.
  T* Allocate()
  {
    if (freeList.empty()) {
      if (!growChunk()) {
        return nullptr;
      }
    }
    T* block = freeList.back();
    freeList.pop_back();
    return block;
  }

  void Deallocate(T* block)
  {
    if (block == nullptr) {
      return;
    }
    freeList.push_back(block);
  }

  // Drops extra chunks and rebuilds a single full free list (does not run
  // destructors on live objects).
  void Clear()
  {
    freeList.clear();

    Chunk* current = chunkHead->next;
    while (current != nullptr) {
      Chunk* next = current->next;
      delete current;
      current = next;
    }

    chunkHead->next = nullptr;
    chunk = chunkHead;
    numChunks = 1;
    populateFreeList(chunkHead);
  }

  size_t getBlockSize() const { return blockSize; }
  size_t getNumBlocks() const { return numBlocks; }
  size_t getNumChunks() const { return numChunks; }
  size_t getBlocksFree() const { return freeList.size(); }
  size_t getCapacity() const { return numChunks * numBlocks; }
};
