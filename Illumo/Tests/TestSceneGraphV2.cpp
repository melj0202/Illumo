#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Scene/SceneGraphDrawable.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <chrono>
#include <cstdio>
#include <limits>

static TestCounters g;

class RevisionAttachment : public ISceneRenderAttachment
{
public:
  AxisAlignedBounds3 bounds{ Vector3(-0.5f), Vector3(0.5f) };
  uint64_t revision = 1;
  mutable size_t queries = 0;
  size_t calls = 0;
  bool valid = true;
  SceneGraph* graph = nullptr;
  SceneNodeHandle mutationTarget;
  bool mutationAccepted = false;
  bool mutateFromBounds = false;
  bool getSceneLocalBounds(AxisAlignedBounds3* output) const override
  {
    ++queries;
    if (mutateFromBounds) {
      graph->destroyNode(mutationTarget);
    }
    *output = bounds;
    return valid;
  }
  uint64_t getSceneBoundsRevision() const override { return revision; }
  void appendSceneCommands(Renderer*, const Matrix4&) override
  {
    ++calls;
    if (graph != nullptr && !mutateFromBounds) {
      mutationAccepted = graph->destroyNode(mutationTarget);
    }
  }
};

static void
testIdentityAndJournal()
{
  SceneGraph graph;
  SceneNodeDesc description;
  description.name = "shared";
  description.userData = 42;
  description.transform = Transform3D::fromPosition(Vector3(1, 2, 3));
  const SceneNodeHandle first = graph.createNode(description);
  const SceneNodeHandle second = graph.createNode(description);
  testTrue(g,
           graph.findByName("shared") == first,
           "duplicate name resolves first preorder node");
  graph.setParent(first, second);
  testTrue(g,
           graph.findByName("shared") == second,
           "name lookup follows reparent order");
  graph.setName(second, "other");
  testTrue(g,
           graph.findByName("shared") == first &&
             graph.getName(second) == "other",
           "interned names rename independently");
  uint64_t data = 0;
  testTrue(g,
           graph.getUserData(first, &data) && data == 42,
           "opaque payload survives hierarchy edits");
  graph.setUserData(first, 77);
  Transform3D local;
  graph.getLocalTransform(first, &local);
  testTrue(g,
           local.position == description.transform.position,
           "TRS query is lossless");
  SceneGraph foreign;
  const SceneNodeHandle handles[]{ first, foreign.createNode() };
  const Transform3D transforms[]{ Transform3D{}, Transform3D{} };
  testTrue(g,
           !graph.setLocalTransforms(handles, transforms, 2),
           "batch rejects foreign handle transactionally");
  graph.getLocalTransform(first, &local);
  testTrue(g,
           local.position == description.transform.position,
           "failed batch changes no prior element");
  const SceneNodeHandle accepted[]{ first, second };
  testTrue(g,
           graph.setLocalTransforms(accepted, transforms, 2),
           "valid batch commits");
  const uint64_t cursor = graph.getChangeSequence();
  graph.destroyNode(second);
  std::vector<SceneChange> changes;
  testTrue(g,
           graph.readChanges(cursor, &changes) && changes.size() == 2 &&
             changes[0].node == first && changes[1].node == second,
           "journal retains stale destroyed identities");
  const SceneNodeHandle replacement = graph.createNode();
  for (size_t i = 0; i < 4100; ++i) {
    graph.setUserData(replacement, i);
  }
  testTrue(g,
           !graph.readChanges(cursor, &changes) && changes.empty(),
           "overflow explicitly requires resync");
  testTrue(g,
           !graph.readChanges(graph.getChangeSequence() + 1, &changes),
           "future cursor rejected");
  testTrue(g,
           graph.firstNode() == replacement &&
             graph.nextNode(replacement).isNull(),
           "ordered iteration yields handles");
}

static void
testBoundsAndSnapshotLifetime()
{
  SceneGraph graph;
  RevisionAttachment a, b;
  const SceneNodeHandle node = graph.createNode();
  graph.addAttachment(node, &a);
  graph.addAttachment(node, &b);
  testTrue(
    g,
    graph.getAttachmentCount(node) == 2 && graph.getAttachment(node, 0) == &a &&
      graph.getAttachment(node, 1) == &b && !graph.addAttachment(node, &a),
    "attachment insertion order and duplicate rejection");
  SceneSnapshotView first = graph.extract(nullptr);
  const SceneSnapshot* captured = first.get();
  testTrue(g,
           captured && captured->items.size() == 2 && a.queries == 1 &&
             b.queries == 1,
           "one bounds query per attachment revision");
  graph.setLocalTransform(node, Transform3D::fromPosition(Vector3(4, 0, 0)));
  testTrue(g,
           first.get() && first.get()->items[0].worldTransform[3][0] == 0,
           "transform edits preserve captured values");
  SceneSnapshotView second = graph.extract(nullptr);
  testTrue(g,
           first.get() && second.get() && a.queries == 1 &&
             second.get()->items[0].worldTransform[3][0] == 4,
           "two buffers reuse local bounds cache");
  SceneSnapshotView third = graph.extract(nullptr);
  testTrue(g,
           !first.get() && second.get() && third.get(),
           "ring reuse retires only reused publication");
  graph.removeAttachment(node, &a);
  testTrue(g,
           !second.get() && !third.get(),
           "detaching retires all outstanding snapshots");
  b.bounds.maximum = Vector3(2);
  ++b.revision;
  SceneSnapshotView changed = graph.extract(nullptr);
  testTrue(g,
           b.queries == 2 && changed.get()->items[0].worldBounds.maximum.x == 6,
           "bounds revision refreshes world cache");
  b.revision = 0;
  graph.extract(nullptr);
  graph.extract(nullptr);
  testTrue(
    g, b.queries == 4, "zero revision remains conservative and uncacheable");
  b.graph = &graph;
  b.mutationTarget = node;
  b.mutateFromBounds = true;
  ++b.revision;
  graph.extract(nullptr);
  testTrue(g,
           graph.isNodeValid(node),
           "bounds callback cannot mutate extraction storage");
  b.graph = nullptr;
  b.mutateFromBounds = false;
  SceneSnapshotView destroyed;
  {
    SceneGraph temporary;
    const SceneNodeHandle n = temporary.createNode();
    temporary.addAttachment(n, &a);
    destroyed = temporary.extract(nullptr);
  }
  testTrue(
    g, !destroyed.get(), "graph destruction safely retires external views");

  SceneGraph appendGraph;
  const SceneNodeHandle appendNode = appendGraph.createNode();
  appendGraph.addAttachment(appendNode, &a);
  const SceneSnapshotView beforeAppend = appendGraph.extract(nullptr);
  appendGraph.addAttachment(appendNode, &b);
  testTrue(g,
           beforeAppend.get() && beforeAppend.get()->items.size() == 1,
           "attachment addition preserves previously captured items");
  const SceneSnapshotView afterAppend = appendGraph.extract(nullptr);
  testTrue(g,
           afterAppend.get() && afterAppend.get()->items.size() == 2,
           "next extraction includes appended attachment");

  HeadlessRenderFixture fixture(640, 480);
  SceneGraph callbackGraph;
  SceneGraphDrawable drawable(callbackGraph);
  RevisionAttachment killer, victim;
  const SceneNodeHandle k = callbackGraph.createNode(),
                        v = callbackGraph.createNode();
  killer.graph = &callbackGraph;
  killer.mutationTarget = v;
  callbackGraph.addAttachment(k, &killer);
  callbackGraph.addAttachment(v, &victim);
  drawable.AppendCommands(&fixture.renderer);
  testTrue(g,
           killer.mutationAccepted && victim.calls == 0,
           "callback detach cannot invoke stale next attachment");
  testTrue(g,
           !fixture.renderer.frameError().empty(),
           "invalidated emission is observable");
}

static void
testSnapshotPassParity()
{
  HeadlessRenderFixture fixture(640, 480);
  // EnvVars persists inside the isolated case directory between test runs.
  fixture.env.setVar("sceneSnapshotExtraction", 1);
  SceneGraph graph;
  SceneGraphDrawable drawable(graph);
  RevisionAttachment attachment;
  graph.addAttachment(graph.createNode(), &attachment);
  Scene scene(&fixture.window, &fixture.camera);
  scene.AddDrawable(&drawable, RenderLayerId::World);
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();
  testTrue(g,
           graph.getStatistics().extractions == 1 && attachment.calls == 1,
           "one extraction spans collection and color");
  fixture.env.setVar("sceneSnapshotExtraction", 0);
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();
  testTrue(g,
           graph.getStatistics().extractions == 1 && attachment.calls == 2,
           "containment path emits without snapshots");
  fixture.env.setVar("sceneSnapshotExtraction", 1);
}

static void
testIndexedQueryParity()
{
  SceneGraph graph;
  std::vector<RevisionAttachment> attachments(257);
  std::vector<SceneNodeHandle> nodes;
  for (size_t i = 0; i < attachments.size(); ++i) {
    const size_t row = i / 17;
    SceneNodeDesc desc;
    desc.transform =
      Transform3D::fromEuler(0.1f, static_cast<float>(i) * 0.1f, 0.25f);
    desc.transform.position = Vector3(static_cast<float>(i % 17) * 3,
                                      static_cast<float>(row) * 3,
                                      static_cast<float>(i % 5));
    nodes.push_back(graph.createNode(desc));
    graph.addAttachment(nodes.back(), &attachments[i]);
  }
  for (size_t iteration = 0; iteration < 1000; ++iteration) {
    const size_t index = iteration % nodes.size();
    if (iteration % 3 == 0) {
      graph.setVisible(nodes[index], iteration % 2 == 0);
    }
    if (iteration % 7 == 0) {
      attachments[index].bounds.maximum.x =
        static_cast<float>(iteration % 5 + 1);
      ++attachments[index].revision;
    }
    const Vector3 origin(static_cast<float>((iteration * 7) % 51),
                         static_cast<float>((iteration * 13) % 47),
                         -10);
    SceneRayHit indexed, linear;
    const bool a = graph.raycast(origin, Vector3(0, 0, 2), &indexed);
    const bool b = graph.raycast(origin, Vector3(0, 0, 2), &linear, false);
    if (a != b || (a && (indexed.node != linear.node ||
                         indexed.distance != linear.distance))) {
      testTrue(g, false, "indexed ray exactly matches linear path");
      return;
    }
    std::vector<SceneNodeHandle> indexedBounds, linearBounds;
    const AxisAlignedBounds3 query{ origin - Vector3(4, 4, 0),
                                    origin + Vector3(4, 4, 20) };
    graph.queryBounds(query, &indexedBounds);
    graph.queryBounds(query, &linearBounds, false);
    if (indexedBounds != linearBounds) {
      testTrue(g, false, "indexed overlap matches linear order");
      return;
    }
  }
  testTrue(
    g,
    true,
    "1000 mutating indexed ray and overlap queries exactly match linear path");
  SceneRayHit hit;
  testTrue(
    g, !graph.raycast(Vector3(0), Vector3(0), &hit), "zero ray rejected");
  testTrue(g,
           !graph.raycast(Vector3(std::numeric_limits<float>::quiet_NaN()),
                          Vector3(0, 0, 1),
                          &hit),
           "nonfinite ray rejected");
  SceneGraph multiple;
  RevisionAttachment a, b;
  a.bounds = { Vector3(-2.1f, -2.1f, -0.1f), Vector3(-1.9f, -1.9f, 0.1f) };
  b.bounds = { Vector3(1.9f, 1.9f, -0.1f), Vector3(2.1f, 2.1f, 0.1f) };
  const SceneNodeHandle node = multiple.createNode();
  multiple.setLocalTransform(node,
                             Transform3D::fromEuler(0, 0, glm::radians(45.0f)));
  multiple.addAttachment(node, &a);
  multiple.addAttachment(node, &b);
  AxisAlignedBounds3 world;
  multiple.getWorldBounds(node, &world);
  const SceneSnapshotView view = multiple.extract(nullptr);
  AxisAlignedBounds3 expected = view.get()->items[0].worldBounds;
  expected.include(view.get()->items[1].worldBounds);
  testTrue(g,
           world.minimum == expected.minimum &&
             world.maximum == expected.maximum,
           "multi-attachment authoritative and compiled world bounds agree");
  testTrue(g,
           !multiple.raycast(Vector3(2, 0, -1), Vector3(0, 0, 1), &hit),
           "rotation does not invent a broad unioned-local-box hit");
}

class BoundsCountingMesh : public MeshVisual
{
public:
  mutable size_t localQueries = 0;
  bool getSceneLocalBounds(AxisAlignedBounds3* bounds) const override
  {
    ++localQueries;
    return MeshVisual::getSceneLocalBounds(bounds);
  }
};
static int
testMeshSnapshotBoundsCache()
{
  HeadlessRenderFixture fixture(640, 480);
  BoundsCountingMesh mesh;
  mesh.addLine(
    Vector3(-1, 0, 0), Vector3(1, 0, 0), ColorRgba{ 255, 255, 255, 255 });
  SceneGraph graph;
  graph.addAttachment(graph.createNode(), &mesh);
  SceneGraphDrawable drawable(graph);
  Scene scene(&fixture.window, &fixture.camera);
  scene.AddDrawable(&drawable);
  for (size_t i = 0; i < 3; ++i) {
    fixture.renderer.BeginFrame();
    fixture.renderer.RenderScene(&scene, &fixture.camera);
    fixture.renderer.EndFrame();
  }
  if (mesh.localQueries != 1) {
    return 1;
  }
  mesh.setModelMatrix(glm::translate(Matrix4(1.0f), Vector3(2, 0, 0)));
  fixture.renderer.BeginFrame();
  fixture.renderer.RenderScene(&scene, &fixture.camera);
  fixture.renderer.EndFrame();
  return mesh.localQueries == 2 ? 0 : 2;
}

void
registerSceneGraphV2Tests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.SceneGraph.MeshBoundsRevision",
               testMeshSnapshotBoundsCache);
  registry.add("Illumo.SceneGraph.IdentityAndJournal", []() {
    g = {};
    testIdentityAndJournal();
    return g.failures;
  });
  registry.add("Illumo.SceneGraph.SnapshotLifetime", []() {
    g = {};
    testBoundsAndSnapshotLifetime();
    return g.failures;
  });
  registry.add("Illumo.SceneGraph.SnapshotPassParity", []() {
    g = {};
    testSnapshotPassParity();
    return g.failures;
  });
  registry.add("Illumo.SceneGraph.IndexedQueryParity", []() {
    g = {};
    testIndexedQueryParity();
    return g.failures;
  });
}
