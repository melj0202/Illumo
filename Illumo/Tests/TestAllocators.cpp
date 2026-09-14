#include <Illumo/Services/ArenaAlloc.h>
#include <Illumo/Services/ChainedStackAlloc.h>
#include <Illumo/Services/IAllocator.h>
#include <Illumo/Services/MallocAlloc.h>
#include <Illumo/Services/PoolAlloc.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

static TestCounters g;

struct Tracked
{
  static int liveCount;
  int value;

  explicit Tracked(int v = 0)
    : value(v)
  {
    liveCount += 1;
  }

  ~Tracked() { liveCount -= 1; }
};

int Tracked::liveCount = 0;

struct alignas(16) Aligned16
{
  double a;
  double b;
};

static void
testArenaBasicAndReuse()
{
  testSection("ArenaAlloc: construct, allocate, clear, reuse");
  Tracked::liveCount = 0;

  ArenaAlloc arena(256);
  testEqSize(g, arena.getChunkSize(), 256, "chunk size stored");
  testEqSize(g, arena.getNumChunks(), 1, "starts with one chunk");
  testEqSize(g, arena.getOffset(), 0, "starts empty");

  Tracked* a = arena.Allocate<Tracked>(11);
  Tracked* b = arena.Allocate<Tracked>(22);
  testTrue(g, a != nullptr && b != nullptr, "two allocations succeed");
  testTrue(g, a != b, "distinct addresses");
  testEqInt(g, a->value, 11, "placement args a");
  testEqInt(g, b->value, 22, "placement args b");
  testEqInt(g, Tracked::liveCount, 2, "constructors ran");
  testTrue(g, arena.getOffset() > 0, "offset advanced");

  // Bulk clear does not run destructors (arena contract).
  arena.Clear();
  testEqSize(g, arena.getNumChunks(), 1, "clear keeps one chunk");
  testEqSize(g, arena.getOffset(), 0, "clear resets offset");
  testEqInt(g, Tracked::liveCount, 2, "clear does not destroy objects");
  Tracked::liveCount = 0; // abandon tracked objects after bulk free

  Tracked* c = arena.Allocate<Tracked>(33);
  testTrue(g, c != nullptr, "reuse after clear");
  testEqInt(g, c->value, 33, "new object after clear");
  testEqInt(g, Tracked::liveCount, 1, "constructor after clear");
  arena.Deallocate();
  Tracked::liveCount = 0;
}

static void
testArenaChunkGrowthAndCap()
{
  testSection("ArenaAlloc: multi-chunk growth and hard cap");

  // Each int is 4 bytes; 16-byte chunks hold 4 ints before growth.
  ArenaAlloc arena(16);
  std::vector<int*> pointers;
  for (int i = 0; i < 16; ++i) {
    int* p = arena.Allocate<int>(i);
    if (p == nullptr) {
      break;
    }
    testEqInt(g, *p, i, "stored value survives");
    pointers.push_back(p);
  }

  testTrue(g, pointers.size() >= 4, "fills first chunk");
  testTrue(g, arena.getNumChunks() > 1, "grows additional chunks");
  testTrue(g,
           arena.getNumChunks() <= ArenaAlloc::kMaxChunks,
           "never exceeds max chunks");

  // Force exhaustion: allocate until nullptr.
  int nulls = 0;
  for (int i = 0; i < 64; ++i) {
    if (arena.Allocate<int>(0) == nullptr) {
      nulls += 1;
    }
  }
  testTrue(g, nulls > 0, "returns nullptr when capacity exhausted");
  testEqSize(
    g, arena.getNumChunks(), ArenaAlloc::kMaxChunks, "sits at max chunks");

  // Object larger than a chunk always fails.
  struct Big
  {
    char bytes[32];
  };
  testTrue(g, arena.Allocate<Big>() == nullptr, "oversize object rejected");

  arena.Clear();
  testEqSize(g, arena.getNumChunks(), 1, "clear drops extra chunks");
  testTrue(g, arena.Allocate<int>(7) != nullptr, "usable after clear");
}

static void
testArenaAlignment()
{
  testSection("ArenaAlloc: alignment for over-aligned types");
  ArenaAlloc arena(128);

  // Force an awkward offset with a 1-byte alloc, then request alignas(16).
  char* c = arena.Allocate<char>('x');
  testTrue(g, c != nullptr, "byte alloc");
  Aligned16* a = arena.Allocate<Aligned16>();
  testTrue(g, a != nullptr, "aligned alloc");
  const std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(a);
  testTrue(g, (addr % 16) == 0, "address is 16-byte aligned");
}

static void
testStackLifoAndDestructors()
{
  testSection("ChainedStackAlloc: LIFO free and destructor");
  Tracked::liveCount = 0;

  ChainedStackAlloc stack(256);
  Tracked* a = stack.Allocate<Tracked>(1);
  Tracked* b = stack.Allocate<Tracked>(2);
  Tracked* c = stack.Allocate<Tracked>(3);
  testTrue(g, a && b && c, "three stack allocs");
  testEqInt(g, Tracked::liveCount, 3, "three live objects");
  testEqSize(g, stack.getDepth(), 3, "depth tracks allocs");

  testTrue(g, !stack.Deallocate(a), "out-of-order free of bottom fails");
  testEqInt(g, Tracked::liveCount, 3, "failed free leaves objects live");

  testTrue(g, stack.Deallocate(c), "free top succeeds");
  testEqInt(g, Tracked::liveCount, 2, "destructor ran for top");
  testEqSize(g, stack.getDepth(), 2, "depth decremented");
  testTrue(g, stack.Deallocate(b), "free next succeeds");
  testTrue(g, stack.Deallocate(a), "free last succeeds");
  testEqInt(g, Tracked::liveCount, 0, "all destructors ran");
  testEqSize(g, stack.getOffset(), 0, "stack empty");
  testEqSize(g, stack.getNumChunks(), 1, "back to one chunk");
  testEqSize(g, stack.getDepth(), 0, "depth empty");

  testTrue(g,
           !stack.Deallocate(static_cast<Tracked*>(nullptr)),
           "null deallocate fails");

  // Alignment padding must not break LIFO restore.
  char* byte = stack.Allocate<char>('z');
  Aligned16* aligned = stack.Allocate<Aligned16>();
  testTrue(g, byte != nullptr && aligned != nullptr, "mixed align allocs");
  testTrue(g, stack.Deallocate(aligned), "free aligned top");
  testTrue(g, stack.Deallocate(byte), "free byte under padding");
  testEqSize(g, stack.getOffset(), 0, "empty after padded LIFO");
}

static void
testStackChunkCrossing()
{
  testSection("ChainedStackAlloc: LIFO across chunk boundaries");
  Tracked::liveCount = 0;

  // 12-byte chunks hold two ints (4+4) then grow on the third.
  ChainedStackAlloc stack(12);
  int* i0 = stack.Allocate<int>(10);
  int* i1 = stack.Allocate<int>(20);
  int* i2 = stack.Allocate<int>(30);
  int* i3 = stack.Allocate<int>(40);
  int* i4 = stack.Allocate<int>(50);
  testTrue(g, i0 && i1 && i2 && i3 && i4, "enough ints allocated");
  testTrue(g, stack.getNumChunks() >= 2, "stack grew chunks");
  testEqSize(g, stack.getDepth(), 5, "depth is five");

  testTrue(g, stack.Deallocate(i4), "pop across high chunk");
  testTrue(g, stack.Deallocate(i3), "pop");
  testTrue(g, stack.Deallocate(i2), "pop");
  testTrue(g, stack.Deallocate(i1), "pop");
  testTrue(g, stack.Deallocate(i0), "pop last");
  testEqSize(g, stack.getOffset(), 0, "empty after full LIFO unwind");
  testEqSize(g, stack.getNumChunks(), 1, "extra chunks released on pop");
  testEqSize(g, stack.getDepth(), 0, "depth cleared");

  // Cap behavior mirrors arena.
  ChainedStackAlloc small(16);
  int allocated = 0;
  for (int n = 0; n < 64; ++n) {
    if (small.Allocate<int>(n) != nullptr) {
      allocated += 1;
    }
  }
  testTrue(g, allocated > 0, "small stack allocates something");
  testTrue(g,
           small.Allocate<int>(0) == nullptr ||
             small.getNumChunks() == ChainedStackAlloc::kMaxChunks,
           "cap reached or full");
  testTrue(g,
           small.getNumChunks() <= ChainedStackAlloc::kMaxChunks,
           "never exceeds max");

  struct Big
  {
    char bytes[64];
  };
  testTrue(g, small.Allocate<Big>() == nullptr, "oversize rejected");
}

static void
testPoolAllocateRecycleAndCap()
{
  testSection("ChainedPoolAlloc: allocate, recycle, clear, cap");

  ChainedPoolAlloc<int> pool(4);
  testEqSize(g, pool.getNumBlocks(), 4, "blocks per chunk");
  testEqSize(g, pool.getBlockSize(), sizeof(int), "block size is sizeof(T)");
  testEqSize(g, pool.getNumChunks(), 1, "one chunk");
  testEqSize(g, pool.getBlocksFree(), 4, "full free list");
  testEqSize(g, pool.getCapacity(), 4, "capacity");

  int* a = pool.Allocate();
  int* b = pool.Allocate();
  testTrue(g, a != nullptr && b != nullptr && a != b, "two distinct blocks");
  testEqSize(g, pool.getBlocksFree(), 2, "free list decreased");

  ::new (a) int(100);
  ::new (b) int(200);
  testEqInt(g, *a, 100, "placement write a");
  testEqInt(g, *b, 200, "placement write b");

  pool.Deallocate(a);
  testEqSize(g, pool.getBlocksFree(), 3, "recycled block free");

  int* c = pool.Allocate();
  testTrue(g, c == a, "LIFO free list reuses last free");
  ::new (c) int(300);
  testEqInt(g, *c, 300, "reuse storage");

  // Exhaust first chunk and force growth.
  std::vector<int*> held;
  held.push_back(b);
  held.push_back(c);
  while (pool.getBlocksFree() > 0 || pool.getNumChunks() < 2) {
    int* p = pool.Allocate();
    if (p == nullptr) {
      break;
    }
    held.push_back(p);
    if (held.size() > 32) {
      break;
    }
  }
  testTrue(g, pool.getNumChunks() >= 2 || held.size() >= 4, "grew or full");

  // Drain remaining capacity until cap.
  while (true) {
    int* p = pool.Allocate();
    if (p == nullptr) {
      break;
    }
    held.push_back(p);
  }
  testEqSize(
    g, pool.getNumChunks(), ChainedPoolAlloc<int>::kMaxChunks, "at max chunks");
  testTrue(g, pool.Allocate() == nullptr, "nullptr when exhausted");
  testEqSize(g,
             held.size(),
             ChainedPoolAlloc<int>::kMaxChunks * pool.getNumBlocks(),
             "capacity = chunks * blocks");

  pool.Clear();
  testEqSize(g, pool.getNumChunks(), 1, "clear collapses to one chunk");
  testEqSize(g, pool.getBlocksFree(), pool.getNumBlocks(), "free list rebuilt");
  testTrue(g, pool.Allocate() != nullptr, "allocate works after clear");

  pool.Deallocate(nullptr); // must not crash
}

static void
testMallocAllocViaInterface()
{
  testSection("MallocAlloc: IAllocator malloc/free");

  MallocAlloc mallocAlloc;
  IAllocator* allocator = &mallocAlloc;

  void* zero = allocator->Allocate(0);
  testTrue(g, zero == nullptr, "zero-size returns nullptr");

  void* mem = allocator->Allocate(128);
  testTrue(g, mem != nullptr, "allocate 128 bytes");
  unsigned char* bytes = static_cast<unsigned char*>(mem);
  for (int i = 0; i < 128; ++i) {
    bytes[i] = static_cast<unsigned char>(i);
  }
  testEqInt(g, static_cast<int>(bytes[0]), 0, "writable[0]");
  testEqInt(g, static_cast<int>(bytes[127]), 127, "writable[127]");

  allocator->Free(mem);
  allocator->Free(nullptr); // free(nullptr) is well-defined

  // Several small allocations should succeed independently.
  std::vector<void*> blocks;
  for (int i = 0; i < 8; ++i) {
    void* p = allocator->Allocate(32);
    testTrue(g, p != nullptr, "small malloc");
    blocks.push_back(p);
  }
  for (void* p : blocks) {
    allocator->Free(p);
  }
}

static void
testArenaBytesAndCString()
{
  testSection("ArenaAlloc: AllocateBytes and AllocateCString");
  ArenaAlloc arena(256);
  void* raw = arena.AllocateBytes(64);
  testTrue(g, raw != nullptr, "raw bytes");
  std::memset(raw, 0xAB, 64);

  char* hello = arena.AllocateCString("hello", 5);
  testTrue(g, hello != nullptr, "cstring");
  testTrue(g, std::strcmp(hello, "hello") == 0, "cstring content");

  std::string owned = "world";
  char* world = arena.AllocateCString(owned);
  testTrue(
    g, world != nullptr && std::strcmp(world, "world") == 0, "string copy");

  arena.Clear();
  testEqSize(g, arena.getOffset(), 0, "clear after bytes");
}

static void
testStackNestedCStringFrames()
{
  testSection("ChainedStackAlloc: nested CString LIFO frames");
  ChainedStackAlloc stack(512);
  char* outer = stack.AllocateCString("outer-command", 13);
  char* inner = stack.AllocateCString("inner-expand", 12);
  testTrue(g, outer != nullptr && inner != nullptr, "nested strings");
  testEqSize(g, stack.getDepth(), 2, "depth 2");
  testTrue(g, stack.FreeTop(inner), "free inner first");
  testEqSize(g, stack.getDepth(), 1, "depth 1");
  testTrue(g, !stack.FreeTop(inner), "double free fails");
  testTrue(g, stack.FreeTop(outer), "free outer");
  testEqSize(g, stack.getDepth(), 0, "depth 0");
}

template<size_t Alignment>
static void
testOverAlignedStorage()
{
  struct alignas(Alignment) Value
  {
    std::array<int, Alignment / sizeof(int)> values{ 17 };
  };
  ArenaAlloc arena(sizeof(Value));
  ChainedStackAlloc stack(sizeof(Value));
  ChainedPoolAlloc<Value> pool(2);
  for (int cycle = 0; cycle < 3; ++cycle) {
    std::vector<Value*> stackValues;
    std::vector<Value*> poolValues;
    for (size_t i = 0; i < ArenaAlloc::kMaxChunks; ++i) {
      Value* a = arena.Allocate<Value>();
      Value* s = stack.Allocate<Value>();
      testTrue(
        g, a != nullptr && s != nullptr, "full-size aligned chunks allocate");
      testTrue(g,
               reinterpret_cast<std::uintptr_t>(a) % Alignment == 0 &&
                 reinterpret_cast<std::uintptr_t>(s) % Alignment == 0,
               "arena and stack honor over-alignment");
      if (a != nullptr) {
        testEqInt(g, a->values[0], 17, "arena object constructed");
      }
      if (s != nullptr) {
        stackValues.push_back(s);
      }
    }
    testTrue(g,
             arena.Allocate<Value>() == nullptr &&
               stack.Allocate<Value>() == nullptr,
             "aligned allocations preserve four-chunk cap");
    for (size_t i = 0; i < 8; ++i) {
      Value* p = pool.Allocate();
      testTrue(g,
               p != nullptr &&
                 reinterpret_cast<std::uintptr_t>(p) % Alignment == 0,
               "every pool slot and grown chunk is aligned");
      if (p != nullptr) {
        ::new (p) Value();
        poolValues.push_back(p);
      }
    }
    testTrue(g, pool.Allocate() == nullptr, "pool cap preserved");
    for (Value* p : poolValues) {
      p->~Value();
      pool.Deallocate(p);
    }
    Value* recycled = pool.Allocate();
    testTrue(g,
             !poolValues.empty() && recycled == poolValues.back(),
             "aligned pool slot recycled");
    while (!stackValues.empty()) {
      testTrue(g, stack.Deallocate(stackValues.back()), "aligned LIFO unwind");
      stackValues.pop_back();
    }
    testTrue(g,
             stack.getOffset() == 0 && stack.getNumChunks() == 1 &&
               stack.getDepth() == 0,
             "alignment padding and growth unwind exactly");
    arena.Clear();
    stack.Clear();
    pool.Clear();
  }
}

static void
testAllocatorOverAlignment()
{
  testOverAlignedStorage<256>();
  testOverAlignedStorage<4096>();
  ArenaAlloc arena(512);
  ChainedStackAlloc stack(512);
  char* a = arena.Allocate<char>('a');
  char* s = stack.Allocate<char>('s');
  void* alignedA = arena.AllocateBytes(256, 256);
  void* alignedS = stack.AllocateBytes(256, 256);
  testTrue(g,
           alignedA != nullptr && alignedS != nullptr &&
             reinterpret_cast<std::uintptr_t>(alignedA) % 256 == 0 &&
             reinterpret_cast<std::uintptr_t>(alignedS) % 256 == 0 &&
             *a == 'a' && *s == 's',
           "mixed alignment preserves earlier live storage");
  const size_t offset = stack.getOffset();
  const size_t depth = stack.getDepth();
  testTrue(g,
           arena.AllocateBytes(1, 3) == nullptr &&
             stack.AllocateBytes(1, 3) == nullptr &&
             stack.getOffset() == offset && stack.getDepth() == depth,
           "non-power-of-two alignment rejected without mutation");
  testTrue(
    g,
    arena.AllocateCString("x", std::numeric_limits<size_t>::max()) == nullptr &&
      stack.AllocateCString("x", std::numeric_limits<size_t>::max()) == nullptr,
    "string size overflow rejected");
  bool rejected = false;
  try {
    ChainedPoolAlloc<Aligned16> huge(std::numeric_limits<size_t>::max());
  } catch (const std::bad_array_new_length&) {
    rejected = true;
  }
  testTrue(g, rejected, "pool size overflow rejected before allocation");
}

static int
runAllocatorCase(void (*testFunction)())
{
  g.failures = 0;
  Tracked::liveCount = 0;
  testFunction();
  return g.failures;
}

void
registerAllocatorTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Alloc.OverAlignment",
               []() { return runAllocatorCase(testAllocatorOverAlignment); });
  registry.add("Illumo.Alloc.ArenaBasicAndReuse",
               []() { return runAllocatorCase(testArenaBasicAndReuse); });
  registry.add("Illumo.Alloc.ArenaChunkGrowthAndCap",
               []() { return runAllocatorCase(testArenaChunkGrowthAndCap); });
  registry.add("Illumo.Alloc.ArenaAlignment",
               []() { return runAllocatorCase(testArenaAlignment); });
  registry.add("Illumo.Alloc.StackLifoAndDestructors",
               []() { return runAllocatorCase(testStackLifoAndDestructors); });
  registry.add("Illumo.Alloc.StackChunkCrossing",
               []() { return runAllocatorCase(testStackChunkCrossing); });
  registry.add("Illumo.Alloc.PoolAllocateRecycleAndCap", []() {
    return runAllocatorCase(testPoolAllocateRecycleAndCap);
  });
  registry.add("Illumo.Alloc.MallocViaInterface",
               []() { return runAllocatorCase(testMallocAllocViaInterface); });
  registry.add("Illumo.Alloc.ArenaBytesAndCString",
               []() { return runAllocatorCase(testArenaBytesAndCString); });
  registry.add("Illumo.Alloc.StackNestedCStringFrames",
               []() { return runAllocatorCase(testStackNestedCStringFrames); });
}
