#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
#include <string>
#ifdef _WIN32
#include <malloc.h>
#endif

// Test-runner allocation instrumentation; inactive outside the bounded check.
static thread_local bool g_countSceneAllocations = false;
static thread_local size_t g_sceneAllocations = 0;

void*
operator new(size_t size)
{
  if (g_countSceneAllocations) {
    ++g_sceneAllocations;
  }
  void* pointer = std::malloc(size == 0 ? 1 : size);
  if (pointer == nullptr) {
    // Allocation failure is fatal in this build (no exceptions).
    std::abort();
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
  if (g_countSceneAllocations) {
    ++g_sceneAllocations;
  }
  return std::malloc(size == 0 ? 1 : size);
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
    std::abort();
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
  g_countSceneAllocations = false;
  std::printf(
    "Scene steady-state heap allocations over 100 animated frames: %zu\n",
    g_sceneAllocations);
  return g_sceneAllocations == 0 ? 0 : 1;
}

// UI rebuilt every frame (clear, then add the same labels) reuses the text
// strings it cleared instead of allocating them again.
static int
testSteadyGameVisualText()
{
  GameVisual visual(256u);
  const std::string label =
    "a label long enough to need heap storage in any std::string";
  const std::function<void()> rebuild = [&visual, &label]() {
    visual.clearPrimitives();
    for (int index = 0; index < 32; ++index) {
      visual.addText(label,
                     4.0f,
                     4.0f + static_cast<float>(index) * 12.0f,
                     12.0f,
                     ColorRgba{});
      visual.addFilledRect(0.0f, 0.0f, 10.0f, 10.0f, ColorRgba{});
    }
  };
  rebuild();
  rebuild();
  g_sceneAllocations = 0;
  g_countSceneAllocations = true;
  for (int frame = 0; frame < 100; ++frame) {
    rebuild();
  }
  g_countSceneAllocations = false;
  std::printf("GameVisual steady-state heap allocations over 100 rebuilds of "
              "32 labels: %zu\n",
              g_sceneAllocations);
  return g_sceneAllocations == 0 ? 0 : 1;
}

void
registerSceneAllocationTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.GameVisual.SteadyTextAllocations",
               testSteadyGameVisualText);
  registry.add("Illumo.SceneGraph.SteadyStateAllocations",
               testSteadySceneAllocations);
}
