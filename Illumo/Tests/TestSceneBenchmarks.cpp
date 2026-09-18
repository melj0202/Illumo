#include <Illumo/Rendering/Scene.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestRegistry.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>

class BenchSceneAttachment : public ISceneRenderAttachment
{
public:
  uint64_t getSceneBoundsRevision() const override { return 1; }
  bool getSceneLocalBounds(AxisAlignedBounds3* bounds) const override
  {
    *bounds = { Vector3(-0.4f), Vector3(0.4f) };
    return true;
  }
  void appendSceneCommands(Renderer*, const Matrix4&) override {}
};

static double
elapsedMicros(std::chrono::steady_clock::time_point start)
{
  return std::chrono::duration<double, std::micro>(
           std::chrono::steady_clock::now() - start)
    .count();
}

static int
visibilityBench()
{
  constexpr size_t kCount = 100000;
  for (unsigned visibility : { 10u, 50u, 100u }) {
    HeadlessRenderFixture fixture(640, 480);
    fixture.renderer.getFrameContext();
    SceneGraph graph;
    BenchSceneAttachment attachment;
    const SceneNodeHandle root = graph.createNode();
    for (size_t i = 0; i < kCount; ++i) {
      SceneNodeDesc desc;
      desc.parent = root;
      desc.transform.position =
        Vector3(i < kCount * visibility / 100 ? 0.0f : 10000.0f, 0, 0);
      graph.addAttachment(graph.createNode(desc), &attachment);
    }
    // Capture a valid camera context through a tiny frame, then benchmark
    // extraction inside a drawable so frustum predicates are actually active.
    class ExtractBench : public DrawableBase
    {
    public:
      explicit ExtractBench(SceneGraph& value)
        : graph(value)
      {
      }
      SceneGraph& graph;
      SceneNodeHandle root;
      double stable = 0, leaf = 0, dynamic = 0, transforms = 0,
             boundsAndExtract = 0;
      size_t visible = 0;
      void Draw() override {}
      bool AppendCommands(Renderer* renderer) override
      {
        graph.extract(renderer);
        graph.extract(renderer);
        const size_t repeats = 12;
        std::chrono::steady_clock::time_point start =
          std::chrono::steady_clock::now();
        for (size_t i = 0; i < repeats; ++i) {
          graph.extract(renderer);
        }
        stable = elapsedMicros(start) / repeats;
        const SceneNodeHandle child = graph.getChild(root, 0);
        start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < repeats; ++i) {
          graph.setLocalTransform(child,
                                  Transform3D::fromPosition(Vector3(
                                    static_cast<float>(i) * 0.001f, 0, 0)));
          graph.extract(renderer);
        }
        leaf = elapsedMicros(start) / repeats;
        start = std::chrono::steady_clock::now();
        for (size_t i = 0; i < repeats; ++i) {
          graph.setLocalTransform(root,
                                  Transform3D::fromPosition(Vector3(
                                    static_cast<float>(i) * 0.001f, 0, 0)));
          graph.extract(renderer);
        }
        dynamic = elapsedMicros(start) / repeats;
        for (size_t i = 0; i < repeats; ++i) {
          graph.setLocalTransform(root,
                                  Transform3D::fromPosition(Vector3(
                                    static_cast<float>(i) * 0.001f, 0, 0)));
          start = std::chrono::steady_clock::now();
          graph.updateWorldTransforms();
          transforms += elapsedMicros(start) / repeats;
          start = std::chrono::steady_clock::now();
          graph.extract(renderer);
          boundsAndExtract += elapsedMicros(start) / repeats;
        }
        const SceneSnapshotView snapshot = graph.extract(renderer);
        for (const SceneRenderItem& item : snapshot.get()->items) {
          visible += item.cameraVisible ? 1u : 0u;
        }
        return true;
      }
    } bench(graph);
    bench.root = root;
    Scene scene(&fixture.window, &fixture.camera);
    scene.AddDrawable(&bench);
    fixture.renderer.BeginFrame();
    fixture.renderer.RenderScene(&scene, &fixture.camera);
    fixture.renderer.EndFrame();
    std::printf("SceneVisibility nodes=%zu visible=%u%% actual=%zu "
                "stable_us=%.3f leaf_edit_us=%.3f all_dynamic_us=%.3f "
                "transforms_us=%.3f bounds_plus_extract_us=%.3f "
                "snapshot_item_bytes=%zu static_fraction_leaf=%.6f\n",
                kCount,
                visibility,
                bench.visible,
                bench.stable,
                bench.leaf,
                bench.dynamic,
                bench.transforms,
                bench.boundsAndExtract,
                sizeof(SceneRenderItem),
                1.0 - 1.0 / kCount);
  }
  return 0;
}

static int
queryBench(size_t count)
{
  SceneGraph graph;
  BenchSceneAttachment attachment;
  std::vector<AxisAlignedBounds3> boxes;
  boxes.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const size_t row = i / 317;
    const Vector3 center(
      static_cast<float>(i % 317) * 2, static_cast<float>(row) * 2, 0);
    SceneNodeDesc desc;
    desc.transform.position = center;
    graph.addAttachment(graph.createNode(desc), &attachment);
    boxes.push_back(
      AxisAlignedBounds3{ center - Vector3(0.4f), center + Vector3(0.4f) });
  }
  graph.extract(nullptr);
  SceneRayHit hit;
  std::chrono::steady_clock::time_point start =
    std::chrono::steady_clock::now();
  graph.raycast(Vector3(0, 0, -10), Vector3(0, 0, 1), &hit);
  const double build = elapsedMicros(start);
  const size_t repeats = 100;
  start = std::chrono::steady_clock::now();
  for (size_t i = 0; i < repeats; ++i) {
    graph.raycast(
      Vector3(static_cast<float>(i) * 2, 0, -10), Vector3(0, 0, 1), &hit);
  }
  const double indexed = elapsedMicros(start) / repeats;
  start = std::chrono::steady_clock::now();
  for (size_t i = 0; i < repeats; ++i) {
    graph.raycast(Vector3(static_cast<float>(i) * 2, 0, -10),
                  Vector3(0, 0, 1),
                  &hit,
                  false);
  }
  const double linear = elapsedMicros(start) / repeats;
  // Favorable planar-grid prototype: insert every overlapped XY cell and do
  // exact slab acceptance along each benchmark's +Z ray. This measures real
  // hits, not a hash lookup, but intentionally does not claim general ray DDA.
  std::unordered_map<uint64_t, std::vector<size_t>> grid;
  const std::function<uint64_t(int, int)> key = [](int x, int y) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
           static_cast<uint32_t>(y);
  };
  start = std::chrono::steady_clock::now();
  for (size_t i = 0; i < count; ++i) {
    for (int x = static_cast<int>(std::floor(boxes[i].minimum.x / 4));
         x <= static_cast<int>(std::floor(boxes[i].maximum.x / 4));
         ++x) {
      for (int y = static_cast<int>(std::floor(boxes[i].minimum.y / 4));
           y <= static_cast<int>(std::floor(boxes[i].maximum.y / 4));
           ++y) {
        grid[key(x, y)].push_back(i);
      }
    }
  }
  const double gridBuild = elapsedMicros(start);
  size_t candidates = 0, gridHits = 0;
  start = std::chrono::steady_clock::now();
  for (size_t i = 0; i < repeats; ++i) {
    const float x = static_cast<float>(i) * 2;
    const std::unordered_map<uint64_t, std::vector<size_t>>::const_iterator
      found = grid.find(key(static_cast<int>(std::floor(x / 4)), 0));
    float nearest = std::numeric_limits<float>::infinity();
    if (found != grid.end()) {
      for (size_t candidate : found->second) {
        ++candidates;
        const AxisAlignedBounds3& box = boxes[candidate];
        if (x >= box.minimum.x && x <= box.maximum.x && 0 >= box.minimum.y &&
            0 <= box.maximum.y && box.maximum.z >= -10) {
          nearest = std::min(nearest, std::max(0.0f, box.minimum.z + 10));
        }
      }
    }
    gridHits += std::isfinite(nearest) ? 1u : 0u;
  }
  const double gridQuery = elapsedMicros(start) / repeats;
  std::printf(
    "SceneQuery nodes=%zu bvh_first_us=%.3f indexed_us=%.3f linear_us=%.3f "
    "grid_build_us=%.3f grid_vertical_ray_us=%.3f grid_candidates=%zu "
    "hits=%zu\n",
    count,
    build,
    indexed,
    linear,
    gridBuild,
    gridQuery,
    candidates,
    gridHits);
  if (gridHits != repeats) {
    return 1;
  }
  return 0;
}

static int
structuralDepthBench()
{
  for (size_t count : { 1000u, 10000u, 100000u }) {
    for (size_t depth : { 1u, 8u, 64u }) {
      SceneGraph graph;
      std::vector<SceneNodeHandle> nodes;
      nodes.reserve(count);
      for (size_t i = 0; i < count; ++i) {
        nodes.push_back(
          graph.createNode(i % depth == 0 ? SceneNodeHandle{} : nodes.back()));
      }
      graph.updateWorldTransforms();
      std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();
      for (size_t i = 0; i < 100; ++i) {
        graph.setLocalTransform(
          nodes[0],
          Transform3D::fromPosition(Vector3(static_cast<float>(i), 0, 0)));
      }
      const double setter = elapsedMicros(start) / 100;
      start = std::chrono::steady_clock::now();
      for (size_t i = 0; i < 10; ++i) {
        for (size_t root = 0; root < count; root += depth) {
          graph.setLocalTransform(
            nodes[root],
            Transform3D::fromPosition(Vector3(static_cast<float>(i), 0, 0)));
        }
        graph.updateWorldTransforms();
      }
      const double update = elapsedMicros(start) / 10;
      start = std::chrono::steady_clock::now();
      for (size_t i = 0; i < 1000; ++i) {
        const SceneNodeHandle node = nodes[count - 1 - (i % (count - 1))];
        graph.setParent(node, i % 2 == 0 ? nodes[0] : SceneNodeHandle{});
      }
      const double edits = elapsedMicros(start);
      start = std::chrono::steady_clock::now();
      graph.updateWorldTransforms();
      const double compile = elapsedMicros(start);
      std::printf(
        "SceneDepth nodes=%zu max_chain=%zu setter_us=%.3f "
        "all_roots_update_us=%.3f reparent1000_us=%.3f recompile_us=%.3f\n",
        count,
        depth,
        setter,
        update,
        edits,
        compile);
    }
  }
  return 0;
}

void
registerSceneBenchmarks(IllumoTestRegistry& registry)
{
  registry.add(
    "Illumo.SceneGraph.Bench.StructuralDepth", structuralDepthBench, 120);
  registry.add("Illumo.SceneGraph.Bench.Visibility", visibilityBench, 120);
  registry.add("Illumo.SceneGraph.Bench.Query1000",
               []() { return queryBench(1000); });
  registry.add("Illumo.SceneGraph.Bench.Query10000",
               []() { return queryBench(10000); });
  registry.add(
    "Illumo.SceneGraph.Bench.Query100000",
    []() { return queryBench(100000); },
    120);
}
