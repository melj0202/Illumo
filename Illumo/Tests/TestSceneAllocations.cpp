#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cstdio>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif

// Test-runner allocation instrumentation; inactive outside the bounded check.
static thread_local bool g_countSceneAllocations = false;
static thread_local size_t g_sceneAllocations = 0;
static thread_local int g_failSceneAllocationAfter = -1;
static void
checkSceneAllocationFailure()
{
  if (g_failSceneAllocationAfter < 0) {
    return;
  }
  const bool fail = g_failSceneAllocationAfter == 0;
  --g_failSceneAllocationAfter;
  if (fail) {
    throw std::bad_alloc();
  }
}

void*
operator new(size_t size)
{
  checkSceneAllocationFailure();
  if (g_countSceneAllocations) {
    ++g_sceneAllocations;
  }
  void* pointer = std::malloc(size == 0 ? 1 : size);
  if (pointer == nullptr) {
    throw std::bad_alloc();
  }
  return pointer;
}
void*
operator new[](size_t size)
{
  return ::operator new(size);
}
void
operator delete(void* pointer) noexcept
{
  std::free(pointer);
}
void
operator delete[](void* pointer) noexcept
{
  std::free(pointer);
}
void
operator delete(void* pointer, size_t) noexcept
{
  std::free(pointer);
}
void
operator delete[](void* pointer, size_t) noexcept
{
  std::free(pointer);
}
void*
operator new(size_t size, const std::nothrow_t&) noexcept
{
  try {
    return ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}
void*
operator new[](size_t size, const std::nothrow_t&) noexcept
{
  return ::operator new(size, std::nothrow);
}
void
operator delete(void* pointer, const std::nothrow_t&) noexcept
{
  ::operator delete(pointer);
}
void
operator delete[](void* pointer, const std::nothrow_t&) noexcept
{
  ::operator delete[](pointer);
}
void*
operator new(size_t size, std::align_val_t alignment)
{
  checkSceneAllocationFailure();
  if (g_countSceneAllocations) {
    ++g_sceneAllocations;
  }
  const size_t align = static_cast<size_t>(alignment);
#ifdef _WIN32
  void* pointer = _aligned_malloc(size == 0 ? 1 : size, align);
#else
  const size_t rounded = ((size == 0 ? 1 : size) + align - 1) / align * align;
  void* pointer = std::aligned_alloc(align, rounded);
#endif
  if (pointer == nullptr) {
    throw std::bad_alloc();
  }
  return pointer;
}
void*
operator new[](size_t size, std::align_val_t alignment)
{
  return ::operator new(size, alignment);
}
void
operator delete(void* pointer, std::align_val_t) noexcept
{
#ifdef _WIN32
  _aligned_free(pointer);
#else
  std::free(pointer);
#endif
}
void
operator delete[](void* pointer, std::align_val_t alignment) noexcept
{
  ::operator delete(pointer, alignment);
}
void
operator delete(void* pointer, size_t, std::align_val_t alignment) noexcept
{
  ::operator delete(pointer, alignment);
}
void
operator delete[](void* pointer, size_t, std::align_val_t alignment) noexcept
{
  ::operator delete(pointer, alignment);
}

class AllocationProbeAttachment : public ISceneRenderAttachment
{
public:
  uint64_t getSceneBoundsRevision() const override { return 1; }
  bool getSceneLocalBounds(AxisAlignedBounds3* bounds) const override
  {
    *bounds = { Vector3(-1), Vector3(1) };
    return true;
  }
  void appendSceneCommands(Renderer*, const Matrix4&) override {}
};

static int
testSteadySceneAllocations()
{
  SceneGraph graph;
  AllocationProbeAttachment attachment;
  const SceneNodeHandle root = graph.createNode();
  for (size_t i = 0; i < 2048; ++i) {
    graph.addAttachment(graph.createNode(root), &attachment);
  }
  graph.setName(
    root, "long imported stable scene identifier beyond small string storage");
  graph.extract(nullptr);
  graph.extract(nullptr);
  g_sceneAllocations = 0;
  g_countSceneAllocations = true;
  try {
    for (size_t i = 0; i < 100; ++i) {
      graph.setLocalTransform(
        root, Transform3D::fromPosition(Vector3(static_cast<float>(i), 0, 0)));
      graph.extract(nullptr);
      if (graph.findByName("long imported stable scene identifier beyond small "
                           "string storage") != root) {
        g_countSceneAllocations = false;
        return 2;
      }
      Matrix4 world(1.0f);
      graph.getWorldTransform(graph.getChild(root, 10), &world);
    }
  } catch (...) {
    g_countSceneAllocations = false;
    throw;
  }
  g_countSceneAllocations = false;
  std::printf(
    "Scene steady-state heap allocations over 100 animated frames: %zu\n",
    g_sceneAllocations);
  return g_sceneAllocations == 0 ? 0 : 1;
}

static int
testSceneAllocationFallbacks()
{
  // Each derived-array allocation can fail independently. The one-shot injector
  // then permits the authoritative fallback and snapshot publication to finish.
  for (int allocation = 0; allocation < 13; ++allocation) {
    SceneGraph graph;
    AllocationProbeAttachment attachment;
    const SceneNodeHandle root = graph.createNode();
    const SceneNodeHandle child = graph.createNode(root);
    graph.addAttachment(child, &attachment);
    graph.setLocalTransform(root, Transform3D::fromPosition(Vector3(3, 4, 5)));
    g_failSceneAllocationAfter = allocation;
    const SceneSnapshotView fallback = graph.extract(nullptr);
    const bool failed = g_failSceneAllocationAfter < 0;
    g_failSceneAllocationAfter = -1;
    if (!failed || !fallback.get() || fallback.get()->items.size() != 1 ||
        fallback.get()->items[0].worldTransform[3] != Vector4(3, 4, 5, 1)) {
      return 1;
    }
    const SceneSnapshotView recovered = graph.extract(nullptr);
    if (!recovered.get() || graph.getStatistics().compilations != 1 ||
        recovered.get()->items[0].worldBounds.minimum != Vector3(2, 3, 4)) {
      return 2;
    }
  }
  // The index has three retained allocation sites; failure falls back to the
  // same compiled boxes and the next query retries construction.
  for (int allocation = 0; allocation < 3; ++allocation) {
    SceneGraph graph;
    AllocationProbeAttachment attachment;
    const SceneNodeHandle node = graph.createNode();
    graph.addAttachment(node, &attachment);
    graph.extract(nullptr);
    g_failSceneAllocationAfter = allocation;
    SceneRayHit hit;
    const bool found = graph.raycast(Vector3(0, 0, -5), Vector3(0, 0, 1), &hit);
    const bool failed = g_failSceneAllocationAfter < 0;
    g_failSceneAllocationAfter = -1;
    if (!failed || !found || hit.node != node || hit.distance != 4) {
      return 3;
    }
    if (!graph.raycast(Vector3(0, 0, -5), Vector3(0, 0, 1), &hit) ||
        hit.node != node) {
      return 4;
    }
  }
  return 0;
}

void
registerSceneAllocationTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.SceneGraph.SteadyStateAllocations",
               testSteadySceneAllocations);
  registry.add("Illumo.SceneGraph.AllocationFallbacks",
               testSceneAllocationFallbacks);
}
