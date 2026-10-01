#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Scene/SceneGraphDrawable.h>
#include <Illumo/Services/WorkerPool.h>
#include <Illumo/Testing/TestRegistry.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

// Deliberately independent model: integer identities, parent links, insertion
// serials, and full recomposition after every edit. Never read graph internals.
struct OracleNode : ISceneRenderAttachment
{
  int id = 0;
  int parent = -1;
  bool alive = false;
  bool enabled = true;
  bool visible = true;
  uint64_t order = 0;
  Transform3D local;
  SceneNodeHandle handle;
  std::vector<int>* emitted = nullptr;

  bool getSceneLocalBounds(AxisAlignedBounds3* bounds) const override
  {
    *bounds = { Vector3(-0.5f), Vector3(0.5f) };
    return true;
  }
  void appendSceneCommands(Renderer*, const Matrix4&) override
  {
    emitted->push_back(id);
  }
};

static uint32_t
nextRandom(uint32_t& state)
{
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

static bool
sameMatrixBits(const Matrix4& a, const Matrix4& b)
{
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      if (std::bit_cast<uint32_t>(a[column][row]) !=
          std::bit_cast<uint32_t>(b[column][row])) {
        return false;
      }
    }
  }
  return true;
}

static int
runSceneOracle()
{
  SceneGraph graph;
  SceneGraphDrawable graphDrawable(graph);
  std::array<OracleNode, 32> nodes;
  std::array<Matrix4, 32> oracleWorld;
  std::array<AxisAlignedBounds3, 32> oracleBounds;
  std::vector<SceneNodeHandle> indexedOverlap, linearOverlap;
  std::vector<int> actual;
  std::vector<int> expected;
  std::vector<int> pending;
  std::vector<int> siblings;
  std::vector<int> chain;
  uint32_t random = 0x154917u;
  uint64_t order = 0;
  for (size_t i = 0; i < nodes.size(); ++i) {
    nodes[i].id = static_cast<int>(i);
    nodes[i].emitted = &actual;
  }
  const size_t operations = 1000000;
  for (size_t step = 0; step < operations; ++step) {
    const int index = static_cast<int>(nextRandom(random) % nodes.size());
    OracleNode& node = nodes[index];
    const unsigned operation = nextRandom(random) % 9u;
    if (!node.alive) {
      int parent = static_cast<int>(nextRandom(random) % nodes.size());
      if (!nodes[parent].alive || parent == index) {
        parent = -1;
      }
      node.handle =
        graph.createNode(parent < 0 ? SceneNodeHandle{} : nodes[parent].handle);
      if (!node.handle.isValid()) {
        return 1;
      }
      node.parent = parent;
      node.alive = true;
      node.enabled = true;
      node.visible = true;
      node.local = Transform3D{};
      node.order = ++order;
      graph.setRenderAttachment(node.handle, &node);
    } else if (operation < 3) {
      if (operation == 1) {
        node.local.rotation =
          Transform3D::fromEuler(
            static_cast<float>(nextRandom(random) % 20u) * 0.1f,
            static_cast<float>(nextRandom(random) % 20u) * 0.1f,
            static_cast<float>(nextRandom(random) % 20u) * 0.1f)
            .rotation;
      } else if (operation == 2) {
        node.local.scale =
          Vector3(0.25f + static_cast<float>(nextRandom(random) % 8u) * 0.25f,
                  0.25f + static_cast<float>(nextRandom(random) % 8u) * 0.25f,
                  0.25f + static_cast<float>(nextRandom(random) % 8u) * 0.25f);
      }
      node.local.position =
        Vector3(static_cast<float>(nextRandom(random) % 21u) - 10.0f,
                static_cast<float>(nextRandom(random) % 21u) - 10.0f,
                static_cast<float>(nextRandom(random) % 21u) - 10.0f);
      graph.setLocalTransform(node.handle, node.local);
    } else if (operation == 3 || operation == 4) {
      int parent =
        static_cast<int>(nextRandom(random) % (nodes.size() + 1)) - 1;
      if (parent >= 0 && !nodes[parent].alive) {
        parent = -1;
      }
      int ancestor = parent;
      bool cycle = false;
      while (ancestor >= 0) {
        if (ancestor == index) {
          cycle = true;
          break;
        }
        ancestor = nodes[ancestor].parent;
      }
      const bool accepted = graph.setParent(
        node.handle, parent < 0 ? SceneNodeHandle{} : nodes[parent].handle);
      if (accepted == cycle) {
        return 2;
      }
      if (accepted && node.parent != parent) {
        node.parent = parent;
        node.order = ++order;
      }
    } else if (operation == 5) {
      node.enabled = !node.enabled;
      graph.setEnabled(node.handle, node.enabled);
    } else if (operation == 6) {
      node.visible = !node.visible;
      graph.setVisible(node.handle, node.visible);
    } else if (operation == 7) {
      graph.destroyNode(node.handle);
      for (OracleNode& candidate : nodes) {
        if (!candidate.alive) {
          continue;
        }
        int ancestor = candidate.id;
        while (ancestor >= 0 && ancestor != index) {
          ancestor = nodes[ancestor].parent;
        }
        if (ancestor == index) {
          candidate.alive = false;
        }
      }
    } else {
      graph.updateWorldTransforms();
    }

    for (const OracleNode& candidate : nodes) {
      if (graph.isNodeValid(candidate.handle) != candidate.alive) {
        return 3;
      }
      if (!candidate.alive) {
        continue;
      }
      chain.clear();
      for (int ancestor = candidate.id; ancestor >= 0;
           ancestor = nodes[ancestor].parent) {
        chain.push_back(ancestor);
      }
      Matrix4 expectedWorld(1.0f);
      for (size_t i = chain.size(); i-- > 0;) {
        const Matrix4 local = nodes[chain[i]].local.toMatrix();
        expectedWorld = i == chain.size() - 1 ? local : expectedWorld * local;
      }
      oracleWorld[candidate.id] = expectedWorld;
      Matrix4 world(1.0f);
      if (!graph.getWorldTransform(candidate.handle, &world) ||
          !sameMatrixBits(world, expectedWorld)) {
        std::printf(
          "oracle world mismatch at step %zu node %d\n", step, candidate.id);
        return 4;
      }
      AxisAlignedBounds3 expectedBounds;
      AxisAlignedBounds3 bounds;
      AxisAlignedBounds3{ Vector3(-0.5f), Vector3(0.5f) }.transformed(
        expectedWorld, &expectedBounds);
      if (!graph.getWorldBounds(candidate.handle, &bounds) ||
          bounds.minimum != expectedBounds.minimum ||
          bounds.maximum != expectedBounds.maximum) {
        return 5;
      }
      oracleBounds[candidate.id] = expectedBounds;
    }

    actual.clear();
    expected.clear();
    pending.clear();
    siblings.clear();
    for (const OracleNode& candidate : nodes) {
      if (candidate.alive && candidate.parent < 0) {
        siblings.push_back(candidate.id);
      }
    }
    const std::function<bool(int, int)> reverseOrder = [&nodes](int a, int b) {
      return nodes[a].order > nodes[b].order;
    };
    std::sort(siblings.begin(), siblings.end(), reverseOrder);
    pending.insert(pending.end(), siblings.begin(), siblings.end());
    while (!pending.empty()) {
      const int current = pending.back();
      pending.pop_back();
      if (!nodes[current].enabled || !nodes[current].visible) {
        continue;
      }
      expected.push_back(current);
      siblings.clear();
      for (const OracleNode& candidate : nodes) {
        if (candidate.alive && candidate.parent == current) {
          siblings.push_back(candidate.id);
        }
      }
      std::sort(siblings.begin(), siblings.end(), reverseOrder);
      pending.insert(pending.end(), siblings.begin(), siblings.end());
    }
    graphDrawable.AppendCommands(nullptr);
    if (actual != expected) {
      std::printf("oracle order mismatch at step %zu\n", step);
      return 6;
    }
    const SceneSnapshotView view = graph.extract(nullptr);
    if (!view.get() || view.get()->items.size() != expected.size()) {
      return 7;
    }
    for (size_t i = 0; i < expected.size(); ++i) {
      const SceneRenderItem& item = view.get()->items[i];
      const int id = expected[i];
      if (item.node != nodes[id].handle ||
          !sameMatrixBits(item.worldTransform, oracleWorld[id]) ||
          !item.boundsValid ||
          item.worldBounds.minimum != oracleBounds[id].minimum ||
          item.worldBounds.maximum != oracleBounds[id].maximum) {
        std::printf(
          "oracle compiled snapshot mismatch at step %zu node %d\n", step, id);
        return 8;
      }
    }
    const Vector3 origin(static_cast<float>(step % 31) - 15.0f, 0, -30);
    SceneRayHit indexedHit, linearHit;
    const bool indexedFound =
      graph.raycast(origin, Vector3(0, 0, 1), &indexedHit);
    const bool linearFound =
      graph.raycast(origin, Vector3(0, 0, 1), &linearHit, false);
    if (indexedFound != linearFound ||
        (indexedFound && (indexedHit.node != linearHit.node ||
                          indexedHit.distance != linearHit.distance))) {
      return 9;
    }
    const AxisAlignedBounds3 query{ origin - Vector3(8),
                                    origin + Vector3(8, 8, 60) };
    graph.queryBounds(query, &indexedOverlap);
    graph.queryBounds(query, &linearOverlap, false);
    if (indexedOverlap != linearOverlap) {
      return 10;
    }
  }
  std::printf("Oracle: %zu operations, seed 0x154917, exact authoritative and "
              "snapshot matrices/bounds/order plus indexed query parity\n",
              operations);
  return 0;
}

static int
runSceneGolden()
{
  SceneGraph graph;
  SceneGraphDrawable graphDrawable(graph);
  std::array<OracleNode, 6> nodes;
  std::vector<int> emitted;
  for (int i = 0; i < 6; ++i) {
    nodes[i].id = i + 1;
    nodes[i].emitted = &emitted;
    nodes[i].handle = graph.createNode(i == 1 || i == 3 ? nodes[0].handle
                                       : i == 2         ? nodes[1].handle
                                                        : SceneNodeHandle{});
    graph.setRenderAttachment(nodes[i].handle, &nodes[i]);
  }
  graphDrawable.AppendCommands(nullptr);
  graph.setParent(nodes[1].handle, nodes[4].handle);
  graphDrawable.AppendCommands(nullptr);
  graph.setVisible(nodes[4].handle, false);
  graphDrawable.AppendCommands(nullptr);
  graph.destroyNode(nodes[0].handle);
  graph.setVisible(nodes[4].handle, true);
  graphDrawable.AppendCommands(nullptr);
  const std::vector<int> golden{ 1, 2, 3, 4, 5, 6, 1, 4, 5, 2,
                                 3, 6, 1, 4, 6, 5, 2, 3, 6 };
  return emitted.size() == golden.size() &&
             std::memcmp(
               emitted.data(), golden.data(), golden.size() * sizeof(int)) == 0
           ? 0
           : 1;
}

static int
runSceneBench(size_t count)
{
  SceneGraph graph;
  SceneGraphDrawable graphDrawable(graph);
  std::vector<SceneNodeHandle> nodes;
  nodes.reserve(count);
  const std::chrono::steady_clock::time_point start =
    std::chrono::steady_clock::now();
  for (size_t i = 0; i < count; ++i) {
    nodes.push_back(
      graph.createNode(i == 0 ? SceneNodeHandle{} : nodes[(i - 1) / 8]));
  }
  graph.updateWorldTransforms();
  const std::chrono::steady_clock::time_point created =
    std::chrono::steady_clock::now();
  const size_t repeats = 100;
  for (size_t i = 0; i < repeats; ++i) {
    graph.setLocalTransform(
      nodes[0],
      Transform3D::fromPosition(Vector3(static_cast<float>(i), 0, 0)));
  }
  const std::chrono::steady_clock::time_point set =
    std::chrono::steady_clock::now();
  for (size_t i = 0; i < repeats; ++i) {
    graph.setLocalTransform(
      nodes[0],
      Transform3D::fromPosition(Vector3(static_cast<float>(i), 0, 0)));
    graph.updateWorldTransforms();
  }
  const std::chrono::steady_clock::time_point updated =
    std::chrono::steady_clock::now();
  std::printf(
    "SceneBench nodes=%zu branching=8 create_ms=%.3f root_set_us=%.3f "
    "set_update_us=%.3f\n",
    count,
    std::chrono::duration<double, std::milli>(created - start).count(),
    std::chrono::duration<double, std::micro>(set - created).count() / repeats,
    std::chrono::duration<double, std::micro>(updated - set).count() / repeats);
  return 0;
}

static void
fillWorkerRange(void* context, size_t begin, size_t end) noexcept
{
  std::vector<unsigned>& values = *static_cast<std::vector<unsigned>*>(context);
  for (size_t i = begin; i < end; ++i) {
    ++values[i];
  }
}

static int
runWorkerPool()
{
  WorkerPool pool;
  std::vector<unsigned> values(12347, 0);
  for (size_t workers : { 0u, 1u, 4u }) {
    if (!pool.start(workers)) {
      return 1;
    }
    for (size_t iteration = 0; iteration < 100; ++iteration) {
      if (!pool.submitRange(values.size(), 127, fillWorkerRange, &values) ||
          pool.submitRange(1, 1, fillWorkerRange, &values)) {
        return 2;
      }
      pool.join();
    }
    if (!pool.submitRange(0, 1, fillWorkerRange, &values)) {
      return 3;
    }
    pool.stop();
  }
  for (unsigned value : values) {
    if (value != 300) {
      return 4;
    }
  }
  return 0;
}

void
registerSceneGraphOracleTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.SceneGraph.Oracle", runSceneOracle, 1800);
  registry.add("Illumo.SceneGraph.EmissionGolden", runSceneGolden);
  registry.add("Illumo.Services.WorkerPool", runWorkerPool);
  registry.add("Illumo.SceneGraph.Bench.1000",
               []() { return runSceneBench(1000); });
  registry.add("Illumo.SceneGraph.Bench.10000",
               []() { return runSceneBench(10000); });
  registry.add(
    "Illumo.SceneGraph.Bench.100000",
    []() { return runSceneBench(100000); },
    120);
}
