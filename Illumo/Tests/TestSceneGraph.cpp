#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <Illumo/Scene/SceneGraph.h>
#include <Illumo/Scene/Transform3D.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

static TestCounters g;

static bool
translationEquals(const Matrix4& transform, float x, float y, float z)
{
  const float epsilon = 0.0001f;
  return std::abs(transform[3][0] - x) <= epsilon &&
         std::abs(transform[3][1] - y) <= epsilon &&
         std::abs(transform[3][2] - z) <= epsilon;
}

class RecordingSceneAttachment : public ISceneRenderAttachment
{
public:
  RecordingSceneAttachment(int attachmentId,
                           std::vector<int>* appendOrder,
                           std::vector<Matrix4>* appendedTransforms)
    : id(attachmentId)
    , order(appendOrder)
    , transforms(appendedTransforms)
  {
  }

  void appendSceneCommands(Renderer* renderer,
                           const Matrix4& worldTransform) override
  {
    (void)renderer;
    order->push_back(id);
    transforms->push_back(worldTransform);
  }

private:
  int id;
  std::vector<int>* order;
  std::vector<Matrix4>* transforms;
};

class MutatingSceneAttachment : public ISceneRenderAttachment
{
public:
  MutatingSceneAttachment(SceneGraph* owningGraph, SceneNodeHandle attachedNode)
    : graph(owningGraph)
    , node(attachedNode)
  {
  }

  void appendSceneCommands(Renderer* renderer,
                           const Matrix4& worldTransform) override
  {
    (void)renderer;
    (void)worldTransform;
    mutationAccepted = graph->destroyNode(node);
  }

  bool mutationAccepted = true;

private:
  SceneGraph* graph;
  SceneNodeHandle node;
};

static void
testSceneGraphHandlesAndLifetime()
{
  testSection("SceneGraph: handles, ownership, and subtree destruction");

  SceneGraph graph;
  const SceneNodeHandle root = graph.createNode();
  const SceneNodeHandle child = graph.createNode(root);
  const SceneNodeHandle otherRoot = graph.createNode();

  testTrue(g,
           root.isValid() && child.isValid() && otherRoot.isValid(),
           "created handles are structurally valid");
  testTrue(g,
           graph.isNodeValid(root) && graph.isNodeValid(child),
           "created handles resolve in their graph");
  testEqSize(g, graph.getNodeCount(), 3u, "graph owns three live nodes");
  testEqSize(g, graph.getRootCount(), 2u, "two roots retained");
  testTrue(g,
           graph.getRoot(0) == root && graph.getRoot(1) == otherRoot,
           "root insertion order is deterministic");
  testTrue(g, graph.getParent(child) == root, "child parent resolves");
  testEqSize(g, graph.getChildCount(root), 1u, "root owns one child");
  testTrue(g, graph.getChild(root, 0) == child, "child order resolves");

  SceneGraph foreignGraph;
  const SceneNodeHandle foreign = foreignGraph.createNode();
  testTrue(g, !graph.isNodeValid(foreign), "foreign handle is rejected");
  testTrue(g,
           graph.createNode(foreign).isNull(),
           "foreign parent cannot create a node");
  testTrue(g,
           !graph.setParent(otherRoot, foreign),
           "foreign parent cannot reparent a node");

  testTrue(g, graph.destroyNode(root), "destroying a live subtree succeeds");
  testTrue(g,
           !graph.isNodeValid(root) && !graph.isNodeValid(child),
           "subtree destruction invalidates parent and child handles");
  testEqSize(g, graph.getNodeCount(), 1u, "unrelated root survives");
  testEqSize(g, graph.getRootCount(), 1u, "destroyed root is removed");

  const SceneNodeHandle reused = graph.createNode();
  testTrue(g, reused.slot == root.slot, "released parent slot is reused first");
  testTrue(
    g, reused.generation != root.generation, "slot reuse advances generation");
  testTrue(g,
           !graph.destroyNode(root),
           "stale handle cannot destroy the replacement node");
  testTrue(g, graph.isNodeValid(reused), "replacement remains live");

  graph.clear();
  testEqSize(g, graph.getNodeCount(), 0u, "clear releases all nodes");
  testTrue(g,
           !graph.isNodeValid(otherRoot) && !graph.isNodeValid(reused),
           "clear invalidates every prior handle");
}

static void
testSceneGraphHierarchyAndTransforms()
{
  testSection("SceneGraph: hierarchy, transforms, and cycle rejection");

  SceneGraph graph;
  const SceneNodeHandle root = graph.createNode();
  const SceneNodeHandle child = graph.createNode(root);
  const SceneNodeHandle grandchild = graph.createNode(child);
  const SceneNodeHandle otherRoot = graph.createNode();

  graph.setLocalTransform(
    root, glm::translate(Matrix4(1.0f), Vector3(2.0f, 0.0f, 0.0f)));
  graph.setLocalTransform(
    child, glm::translate(Matrix4(1.0f), Vector3(0.0f, 3.0f, 0.0f)));
  graph.setLocalTransform(
    grandchild, glm::translate(Matrix4(1.0f), Vector3(0.0f, 0.0f, 4.0f)));
  graph.setLocalTransform(
    otherRoot, glm::translate(Matrix4(1.0f), Vector3(10.0f, 0.0f, 0.0f)));

  Matrix4 world(1.0f);
  testTrue(g,
           graph.getWorldTransform(grandchild, &world),
           "grandchild world transform resolves");
  testTrue(g,
           translationEquals(world, 2.0f, 3.0f, 4.0f),
           "world transform composes every ancestor");

  graph.setLocalTransform(
    root, glm::translate(Matrix4(1.0f), Vector3(5.0f, 0.0f, 0.0f)));
  graph.getWorldTransform(grandchild, &world);
  testTrue(g,
           translationEquals(world, 5.0f, 3.0f, 4.0f),
           "dirty ancestor updates descendants");

  testTrue(g, graph.setParent(child, otherRoot), "valid reparent succeeds");
  graph.getWorldTransform(grandchild, &world);
  testTrue(g,
           translationEquals(world, 10.0f, 3.0f, 4.0f),
           "reparent recomputes moved subtree transforms");
  testEqSize(g, graph.getChildCount(root), 0u, "old parent detaches child");
  testTrue(g,
           graph.getChild(otherRoot, 0) == child,
           "new parent appends child deterministically");

  testTrue(g,
           !graph.setParent(otherRoot, grandchild),
           "ancestor-to-descendant reparent rejects cycle");
  testTrue(g, !graph.setParent(child, child), "self-parenting is rejected");
  testTrue(g,
           graph.getParent(child) == otherRoot,
           "failed cycle operations leave hierarchy unchanged");

  testTrue(g,
           graph.setParent(child, SceneNodeHandle{}),
           "invalid null handle detaches node to root");
  testTrue(g, graph.getParent(child).isNull(), "detached node has no parent");
  testTrue(g,
           graph.getRoot(graph.getRootCount() - 1) == child,
           "detached node appends after existing roots");
  graph.getWorldTransform(grandchild, &world);
  testTrue(g,
           translationEquals(world, 0.0f, 3.0f, 4.0f),
           "detached subtree resolves from its local root transform");

  Matrix4 unchanged(1.0f);
  testTrue(g,
           !graph.getWorldTransform(SceneNodeHandle{}, &unchanged),
           "null world-transform query fails safely");
  testTrue(g,
           !graph.getLocalTransform(root, nullptr),
           "null local-transform output fails safely");

  const Transform3D trs = Transform3D::fromPosition(Vector3(7.0f, 8.0f, 9.0f));
  testTrue(g,
           graph.setLocalTransform(root, trs),
           "Transform3D overload sets local transform successfully");
  graph.getWorldTransform(root, &world);
  testTrue(g,
           translationEquals(world, 7.0f, 8.0f, 9.0f),
           "Transform3D local transform propagates to world transform");
}

static void
testSceneGraphRenderExtraction()
{
  testSection("SceneGraph: render order and subtree state");

  SceneGraph graph;
  const SceneNodeHandle rootA = graph.createNode();
  const SceneNodeHandle childA = graph.createNode(rootA);
  const SceneNodeHandle grandchildA = graph.createNode(childA);
  const SceneNodeHandle childB = graph.createNode(rootA);
  const SceneNodeHandle rootB = graph.createNode();

  graph.setLocalTransform(
    rootA, glm::translate(Matrix4(1.0f), Vector3(1.0f, 0.0f, 0.0f)));
  graph.setLocalTransform(
    childA, glm::translate(Matrix4(1.0f), Vector3(0.0f, 2.0f, 0.0f)));
  graph.setLocalTransform(
    grandchildA, glm::translate(Matrix4(1.0f), Vector3(0.0f, 0.0f, 3.0f)));

  std::vector<int> order;
  std::vector<Matrix4> transforms;
  RecordingSceneAttachment attachmentRootA(1, &order, &transforms);
  RecordingSceneAttachment attachmentChildA(2, &order, &transforms);
  RecordingSceneAttachment attachmentGrandchildA(3, &order, &transforms);
  RecordingSceneAttachment attachmentChildB(4, &order, &transforms);
  RecordingSceneAttachment attachmentRootB(5, &order, &transforms);

  graph.setRenderAttachment(rootA, &attachmentRootA);
  graph.setRenderAttachment(childA, &attachmentChildA);
  graph.setRenderAttachment(grandchildA, &attachmentGrandchildA);
  graph.setRenderAttachment(childB, &attachmentChildB);
  graph.setRenderAttachment(rootB, &attachmentRootB);

  testTrue(g, graph.AppendCommands(nullptr), "graph owns the token path");
  const std::vector<int> expectedOrder{ 1, 2, 3, 4, 5 };
  testTrue(
    g, order == expectedOrder, "attachments emit in hierarchy pre-order");
  testEqSize(g, transforms.size(), 5u, "every attachment receives a transform");
  testTrue(g,
           translationEquals(transforms[2], 1.0f, 2.0f, 3.0f),
           "attachment receives resolved world transform");

  order.clear();
  transforms.clear();
  graph.setVisible(childA, false);
  graph.AppendCommands(nullptr);
  const std::vector<int> hiddenOrder{ 1, 4, 5 };
  testTrue(
    g, order == hiddenOrder, "invisible node suppresses its complete subtree");

  order.clear();
  transforms.clear();
  graph.setVisible(childA, true);
  graph.setEnabled(rootA, false);
  graph.AppendCommands(nullptr);
  const std::vector<int> disabledOrder{ 5 };
  testTrue(
    g, order == disabledOrder, "disabled node suppresses its complete subtree");

  order.clear();
  transforms.clear();
  graph.setEnabled(rootA, true);
  graph.setVisible(false);
  graph.AppendCommands(nullptr);
  testEqSize(g, order.size(), 0u, "hidden graph emits no attachments");

  graph.setVisible(true);
  graph.setRenderAttachment(childB, nullptr);
  testTrue(g,
           graph.getRenderAttachment(childB) == nullptr,
           "render attachment detaches without changing the node");

  MutatingSceneAttachment mutatingAttachment(&graph, childB);
  graph.setRenderAttachment(childB, &mutatingAttachment);
  graph.AppendCommands(nullptr);
  testTrue(g,
           !mutatingAttachment.mutationAccepted && graph.isNodeValid(childB),
           "render callbacks cannot mutate graph storage during traversal");
}

static void
testTransformConversions()
{
  testSection(
    "SceneGraph: 2D and 3D transform decomposition and reconstruction");

  // 1. Transform2D translation
  {
    Transform2D t;
    t.x = 12.5f;
    t.y = -34.2f;
    const Matrix4 mat = t.toMatrix();
    const Transform2D parsed = Transform2D::fromMatrix(mat);
    testTrue(
      g, std::abs(parsed.x - 12.5f) < 0.0001f, "2D translation X round trips");
    testTrue(
      g, std::abs(parsed.y - -34.2f) < 0.0001f, "2D translation Y round trips");
    testTrue(
      g, std::abs(parsed.scaleX - 1.0f) < 0.0001f, "2D unit scale X preserved");
    testTrue(
      g, std::abs(parsed.scaleY - 1.0f) < 0.0001f, "2D unit scale Y preserved");
    testTrue(g,
             std::abs(parsed.rotationRadians) < 0.0001f,
             "2D zero rotation preserved");
  }

  // 2. Transform2D scale, rotation, and translation combined
  {
    Transform2D t;
    t.x = -5.0f;
    t.y = 8.0f;
    t.scaleX = 2.5f;
    t.scaleY = 0.5f;
    t.rotationRadians = 0.785398f; // ~45 deg
    const Matrix4 mat = t.toMatrix();
    const Transform2D parsed = Transform2D::fromMatrix(mat);
    testTrue(g, std::abs(parsed.x - t.x) < 0.001f, "2D combined translation X");
    testTrue(g, std::abs(parsed.y - t.y) < 0.001f, "2D combined translation Y");
    testTrue(
      g, std::abs(parsed.scaleX - t.scaleX) < 0.001f, "2D combined scale X");
    testTrue(
      g, std::abs(parsed.scaleY - t.scaleY) < 0.001f, "2D combined scale Y");
    testTrue(g,
             std::abs(parsed.rotationRadians - t.rotationRadians) < 0.001f,
             "2D combined rotation");
  }

  // 3. Transform3D translation and scale
  {
    Transform3D t;
    t.position = Vector3(10.0f, -20.0f, 30.0f);
    t.scale = Vector3(2.0f, 0.5f, 4.0f);
    const Matrix4 mat = t.toMatrix();
    const Transform3D parsed = Transform3D::fromMatrix(mat);
    testTrue(g, std::abs(parsed.position.x - 10.0f) < 0.0001f, "3D position X");
    testTrue(
      g, std::abs(parsed.position.y - -20.0f) < 0.0001f, "3D position Y");
    testTrue(g, std::abs(parsed.position.z - 30.0f) < 0.0001f, "3D position Z");
    testTrue(g, std::abs(parsed.scale.x - 2.0f) < 0.0001f, "3D scale X");
    testTrue(g, std::abs(parsed.scale.y - 0.5f) < 0.0001f, "3D scale Y");
    testTrue(g, std::abs(parsed.scale.z - 4.0f) < 0.0001f, "3D scale Z");
  }

  // 4. Transform3D Euler rotation round-trip
  {
    const Transform3D t = Transform3D::fromEuler(0.2f, 0.4f, 0.6f);
    const Matrix4 mat = t.toMatrix();
    const Transform3D parsed = Transform3D::fromMatrix(mat);
    const Matrix4 reconstructedMat = parsed.toMatrix();
    for (int col = 0; col < 4; ++col) {
      for (int row = 0; row < 4; ++row) {
        testTrue(g,
                 std::abs(mat[col][row] - reconstructedMat[col][row]) < 0.001f,
                 "3D rotation matrix round-trip invariant");
      }
    }
  }
}

static int
runSceneGraphCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

void
registerSceneGraphTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.SceneGraph.HandlesAndLifetime", []() {
    return runSceneGraphCase(testSceneGraphHandlesAndLifetime);
  });
  registry.add("Illumo.SceneGraph.HierarchyAndTransforms", []() {
    return runSceneGraphCase(testSceneGraphHierarchyAndTransforms);
  });
  registry.add("Illumo.SceneGraph.RenderExtraction", []() {
    return runSceneGraphCase(testSceneGraphRenderExtraction);
  });
  registry.add("Illumo.SceneGraph.TransformConversions",
               []() { return runSceneGraphCase(testTransformConversions); });
}
