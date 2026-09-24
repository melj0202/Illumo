#include "EditorDocument.h"
#include <Illumo/Content/IlscCodec.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <string>

static TestCounters g;

static std::string
shape(EditorDocument& document,
      ScenePrimitiveShape kind,
      const std::string& parent = {},
      const Vector3& position = Vector3(0.0f))
{
  return document.createPrimitive(
    false, kind, parent, Transform3D::fromPosition(position));
}

static std::string
empty(EditorDocument& document, const std::string& parent = {})
{
  return document.createPrimitive(
    true, ScenePrimitiveShape::Cube, parent, Transform3D{});
}

static std::string
parentOf(const EditorDocument& document, const std::string& id)
{
  return document.scene().idOf(
    document.graph().getParent(document.nodeHandle(id)));
}

static void
testCreateParentDelete()
{
  testSection("EditorDocument: create, parent, delete");
  EditorDocument document;
  const std::string root = empty(document);
  const std::string child = shape(document, ScenePrimitiveShape::Cube, root);
  testTrue(g, !root.empty() && !child.empty(), "creates two nodes");
  testEqSize(g, document.nodeCount(), 2u, "two live nodes");
  testEqStr(g, parentOf(document, child), root, "child parented to root");
  testTrue(g, document.setParent(child, {}), "unparent succeeds");
  testEqStr(g, parentOf(document, child), "", "child is a root");
  testTrue(g, document.setParent(child, root), "reparent succeeds");
  testTrue(g, !document.setParent(root, child), "cycle is rejected");
  testTrue(g, document.destroySubtree(root), "destroy subtree");
  testEqSize(g, document.nodeCount(), 0u, "subtree removed");
}

static void
testPickAndTranslate()
{
  testSection("EditorDocument: pick and translate");
  EditorDocument document;
  const std::string id =
    shape(document, ScenePrimitiveShape::Cube, {}, Vector3(3.0f, 4.0f, 0.0f));
  std::string hit;
  testTrue(g,
           document.pickRay(
             Vector3(3.1f, 4.1f, 100.0f), Vector3(0.0f, 0.0f, -1.0f), &hit),
           "picks the cube from above in 2D");
  testEqStr(g, hit, id, "picked id matches");
  testTrue(g, document.translate(id, Vector3(1.0f, -2.0f, 0.0f)), "translates");
  const SceneNode* node = document.findNode(id);
  testTrue(g,
           node != nullptr && node->transform.position.x == 4.0f &&
             node->transform.position.y == 2.0f,
           "translation applied");
}

static void
testPropertiesPickAndSceneDetail()
{
  testSection("EditorDocument: properties, pick, scene detail");
  EditorDocument document;
  document.setWorldMode(SceneWorldMode::World3D);
  testTrue(
    g, document.worldMode() == SceneWorldMode::World3D, "world mode switches");
  const std::string ellipse = shape(document, ScenePrimitiveShape::Ellipse);
  const std::string pyramid =
    shape(document, ScenePrimitiveShape::Pyramid, ellipse);
  testTrue(g,
           document.setExtent(ellipse, Vector3(1.5f, 0.5f, 0.5f)),
           "sets ellipse size");
  testTrue(g,
           document.setColor(ellipse, ColorRgba{ 11, 22, 33, 255 }),
           "sets ellipse color");
  testTrue(g, document.setName(pyramid, "Peak"), "sets pyramid name");
  document.setTransform(ellipse,
                        Transform3D::fromPosition(Vector3(4.0f, 0.0f, 5.0f)));
  EditorSelection selection;
  selection.set(ellipse);
  const EditorSceneDetail detail = document.sceneDetail(selection);
  testEqSize(g, detail.nodeCount, 2u, "scene has two nodes");
  testTrue(g, detail.hasSelection, "detail has selection");
  testEqStr(g, detail.selectedId, ellipse, "detail id");
  testEqStr(g, detail.kindLabel, "Ellipse", "detail kind");
  testTrue(g, detail.hasPrimitive && detail.extent.x == 1.5f, "detail extent");
  testTrue(g, detail.color.g == 22, "detail color");
  testTrue(g, detail.transform.position.z == 5.0f, "detail transform");
  testTrue(g, detail.worldMode == SceneWorldMode::World3D, "detail world mode");
  testEqStr(g, document.findNode(pyramid)->name, "Peak", "rename applied");
  testTrue(g,
           !document.setName(pyramid, "two\nlines") &&
             document.findNode(pyramid)->name == "Peak",
           "invalid names are rejected");
}

static void
testTransactionalLoad()
{
  testSection("EditorDocument: load failure leaves prior state");
  EditorDocument document;
  empty(document);
  const size_t before = document.nodeCount();
  std::string error;
  testTrue(g,
           !document.loadFromText("{\"format\":\"nope\"}", &error),
           "invalid text fails");
  testEqSize(g, document.nodeCount(), before, "prior nodes remain");
  testTrue(g,
           !document.loadFromText(R"({"format":"ilsc","version":1,"nodes":[]})",
                                  &error) &&
             error.find("format 1") != std::string::npos,
           "format 1 is refused with an explicit message");
  const char* valid = R"({
    "format": "ilsc",
    "format_version": [2, 0],
    "nodes": [{"id": "keep", "name": "Keep"}]
  })";
  testTrue(g, document.loadFromText(valid, &error), "valid replacement");
  testEqSize(g, document.nodeCount(), 1u, "replaced with one node");
  testTrue(g, document.findNode("keep") != nullptr, "loaded id");
  testTrue(g, !document.isDirty(), "successful load is clean");
  testTrue(g, !document.history().canUndo(), "load clears history");
}

static void
test3DTranslateWithParent()
{
  testSection("EditorDocument: 3D translate with parent transform");
  EditorDocument document;
  document.setWorldMode(SceneWorldMode::World3D);
  const std::string parentId = empty(document);
  Transform3D parentTransform;
  parentTransform.position = Vector3(10.0f, 5.0f, -2.0f);
  parentTransform.scale = Vector3(2.0f, 2.0f, 2.0f);
  document.setTransform(parentId, parentTransform);
  const std::string childId =
    shape(document, ScenePrimitiveShape::Cube, parentId);

  // A world move of (4, 2, 6) is (2, 1, 3) locally under a parent scale of 2.
  testTrue(g,
           document.translate(childId, Vector3(4.0f, 2.0f, 6.0f)),
           "translate child");
  const SceneNode* childNode = document.findNode(childId);
  testTrue(g,
           childNode != nullptr &&
             std::abs(childNode->transform.position.x - 2.0f) < 0.001f &&
             std::abs(childNode->transform.position.y - 1.0f) < 0.001f &&
             std::abs(childNode->transform.position.z - 3.0f) < 0.001f,
           "local delta divides by the parent scale");
  const Matrix4 worldMat = document.worldMatrix(childId);
  testTrue(g,
           std::abs(worldMat[3][0] - 14.0f) < 0.001f &&
             std::abs(worldMat[3][1] - 7.0f) < 0.001f &&
             std::abs(worldMat[3][2] - 4.0f) < 0.001f,
           "world position moved by the world delta");
}

static void
testReparentKeepsWorldPose()
{
  testSection("EditorDocument: reparent keeps the world pose");
  EditorDocument document;
  const std::string parent =
    shape(document, ScenePrimitiveShape::Cube, {}, Vector3(5.0f, 0.0f, 0.0f));
  Transform3D scaled = document.findNode(parent)->transform;
  scaled.scale = Vector3(2.0f);
  document.setTransform(parent, scaled);
  const std::string child =
    shape(document, ScenePrimitiveShape::Cube, {}, Vector3(7.0f, 2.0f, 0.0f));
  testTrue(g, document.setParent(child, parent), "reparent");
  const Matrix4 world = document.worldMatrix(child);
  testTrue(g,
           std::abs(world[3][0] - 7.0f) < 1e-4f &&
             std::abs(world[3][1] - 2.0f) < 1e-4f,
           "world position is unchanged");
  testTrue(
    g,
    std::abs(document.findNode(child)->transform.position.x - 1.0f) < 1e-4f &&
      std::abs(document.findNode(child)->transform.scale.x - 0.5f) < 1e-4f,
    "local transform compensates the parent");
  testTrue(g, document.undo(), "undo reparent");
  testTrue(g,
           parentOf(document, child).empty() &&
             document.findNode(child)->transform.position.x == 7.0f,
           "undo restores parent and local transform");
}

void
registerEditorDocumentTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Document.GraphOrderRoundTrip", []() {
    g = {};
    EditorDocument document;
    const std::string a = empty(document);
    const std::string b = empty(document);
    const std::string c = empty(document);
    const SceneNodeHandle retained = document.nodeHandle(b);
    document.setParent(a, b);
    document.setParent(a, {});
    testTrue(g,
             document.graph().getName(document.graph().getRoot(0)) == b &&
               document.graph().getName(document.graph().getRoot(2)) == a,
             "reparent appends to destination order");
    document.setTransform(b, Transform3D::fromPosition(Vector3(2, 3, 4)));
    testTrue(g,
             document.nodeHandle(b) == retained,
             "transform edit retains graph identity");
    const std::string encoded = document.encode();
    testTrue(
      g, document.loadFromText(encoded, nullptr), "ordered document reloads");
    testTrue(g,
             document.graph().getName(document.graph().getRoot(0)) == b &&
               document.graph().getName(document.graph().getRoot(1)) == c &&
               document.graph().getName(document.graph().getRoot(2)) == a,
             "root order survives serialization");
    testTrue(g, document.setParent(a, {}, b), "insert before a sibling");
    testTrue(g,
             document.graph().getName(document.graph().getRoot(0)) == a,
             "insert position respected");
    document.destroySubtree(c);
    testTrue(g,
             document.findNode(a) != nullptr &&
               document.findNode(b) != nullptr &&
               document.findNode(c) == nullptr,
             "destroy removes only its subtree");
    return g.failures;
  });
  registry.add("IllEd.Document.ImportedIdAllocation", []() {
    g = {};
    SceneDocument imported;
    for (const char* id : { "n1",
                            "n4294967295",
                            "n18446744073709551615",
                            "n2",
                            "n0002",
                            "arbitrary-id" }) {
      SceneNode node;
      node.id = id;
      imported.nodes.push_back(node);
    }
    EditorDocument document;
    std::string error;
    testTrue(g,
             document.loadFromText(IlscCodec::encode(imported), &error),
             "loads long and opaque ids");
    for (int count = 0; count < 4; ++count) {
      const std::string created =
        shape(document, ScenePrimitiveShape::Cube, "n1");
      testTrue(g, !created.empty(), "creates a unique node");
      SceneDocument parsed;
      testTrue(g,
               IlscCodec::parse(document.encode(), parsed, error),
               "created scene reloads with unique ids");
      testEqSize(g,
                 parsed.nodes.size(),
                 imported.nodes.size() + static_cast<size_t>(count) + 1,
                 "new node is retained");
    }
    const std::string saved = document.encode();
    testTrue(g, document.loadFromText(saved, &error), "reloads extended scene");
    testTrue(g, !empty(document).empty(), "allocation skips occupied ids");
    return g.failures;
  });
  registry.add("IllEd.Document.RayPicking", []() {
    g = {};
    EditorDocument document;
    document.setWorldMode(SceneWorldMode::World3D);
    const std::string nearId =
      shape(document, ScenePrimitiveShape::Cube, {}, Vector3(0, 5, 4));
    const std::string farId =
      shape(document, ScenePrimitiveShape::Cube, {}, Vector3(0, 5, 0));
    std::string hit;
    testTrue(g,
             document.pickRay(Vector3(0, 5, 10), Vector3(0, 0, -1), &hit),
             "elevated ray hits");
    testEqStr(g, hit, nearId, "nearest beats later document entry");
    Transform3D farTransform = document.findNode(farId)->transform;
    farTransform.scale = Vector3(20, 3, 4);
    document.setTransform(farId, farTransform);
    testTrue(g,
             document.pickRay(Vector3(0, 5, 10), Vector3(0, 0, -1), &hit),
             "scaled overlap hits");
    testEqStr(
      g, hit, nearId, "world distance wins over shorter local ray distance");
    farTransform.scale = Vector3(1);
    document.setTransform(farId, farTransform);
    Transform3D rotated = Transform3D::fromEuler(0, 0, glm::radians(90.0f));
    rotated.position = Vector3(0, 5, 4);
    document.setTransform(nearId, rotated);
    document.setExtent(nearId, Vector3(2, 0.1f, 0.2f));
    testTrue(g,
             document.pickRay(Vector3(0, 6.5f, 10), Vector3(0, 0, -1), &hit),
             "rotated long bound hits");
    testEqStr(g, hit, nearId, "rotation follows object local axes");
    testTrue(g,
             !document.pickRay(Vector3(1.5f, 5, 10), Vector3(0, 0, -1), &hit),
             "rotated empty region misses");
    const std::string parent = empty(document);
    Transform3D parentTransform = Transform3D::fromPosition(Vector3(0, 5, 0));
    parentTransform.scale = Vector3(2, 3, -2);
    document.setTransform(parent, parentTransform);
    const std::string child =
      shape(document, ScenePrimitiveShape::Cube, parent);
    document.setTransform(child, Transform3D::fromPosition(Vector3(2, 0, -2)));
    testTrue(g,
             document.pickRay(Vector3(4, 5, 10), Vector3(0, 0, -2), &hit),
             "parent scaled reflected bound hits");
    testEqStr(g, hit, child, "child selected through parent transform");
    document.setVisible(parent, false);
    testTrue(g,
             !document.pickRay(Vector3(4, 5, 10), Vector3(0, 0, -1), &hit),
             "hidden ancestor excludes child");
    document.setVisible(parent, true);
    document.setEnabled(parent, false);
    testTrue(g,
             !document.pickRay(Vector3(4, 5, 10), Vector3(0, 0, -1), &hit),
             "disabled ancestor excludes child");
    document.setEnabled(parent, true);
    Transform3D singular = document.findNode(child)->transform;
    singular.scale = Vector3(0);
    testTrue(g,
             !document.setTransform(child, singular),
             "singular scale is rejected before it reaches the graph");
    testTrue(g,
             !document.pickRay(Vector3(0, 5, 10), Vector3(0), &hit),
             "zero ray rejected");
    testTrue(g,
             !document.pickRay(Vector3(0, 5, 10), Vector3(0, 0, 1), &hit),
             "objects behind ray rejected");
    return g.failures;
  });
  registry.add("IllEd.Document.Hierarchy", []() {
    g = {};
    testCreateParentDelete();
    return g.failures;
  });
  registry.add("IllEd.Document.PickTranslate", []() {
    g = {};
    testPickAndTranslate();
    return g.failures;
  });
  registry.add("IllEd.Document.Translate3DWithParent", []() {
    g = {};
    test3DTranslateWithParent();
    return g.failures;
  });
  registry.add("IllEd.Document.ReparentKeepsWorldPose", []() {
    g = {};
    testReparentKeepsWorldPose();
    return g.failures;
  });
  registry.add("IllEd.Document.TransactionalLoad", []() {
    g = {};
    testTransactionalLoad();
    return g.failures;
  });
  registry.add("IllEd.Document.PropertiesAndDetail", []() {
    g = {};
    testPropertiesPickAndSceneDetail();
    return g.failures;
  });
}
