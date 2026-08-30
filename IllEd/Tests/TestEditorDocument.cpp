#include "EditorDocument.h"
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <string>

static TestCounters g;

static void
testCreateParentDelete()
{
  testSection("EditorDocument: create, parent, delete");
  EditorDocument document;
  const std::string root = document.createNode(SceneNodeKind::Empty, {});
  const std::string child = document.createNode(SceneNodeKind::SolidCube, root);
  testTrue(g, !root.empty() && !child.empty(), "creates two nodes");
  testEqSize(g, document.nodeCount(), 2u, "two live nodes");
  const IlscNode* childNode = document.findNode(child);
  testTrue(g,
           childNode != nullptr && childNode->parentId == root,
           "child parented to root");
  testTrue(g, document.setParent(child, {}), "unparent succeeds");
  testTrue(g,
           document.findNode(child)->parentId.empty(),
           "child is a root after unparent");
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
  const std::string id = document.createNode(SceneNodeKind::SolidCube, {});
  Transform3D transform = Transform3D::fromPosition(Vector3(3.0f, 4.0f, 0.0f));
  document.setTransform(id, transform);
  std::string hit;
  testTrue(g, document.pick(3.1f, 4.1f, &hit), "picks cube");
  testEqStr(g, hit, id, "picked id matches");
  testTrue(g, document.translate(id, 1.0f, -2.0f), "translates");
  const IlscNode* node = document.findNode(id);
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
  document.setWorldMode(IlscWorldMode::World3D);
  testTrue(
    g, document.worldMode() == IlscWorldMode::World3D, "world mode switches");
  const std::string ellipse =
    document.createNode(SceneNodeKind::FilledEllipse, {});
  const std::string pyramid =
    document.createNode(SceneNodeKind::SolidPyramid, ellipse);
  testTrue(g,
           document.setExtent(ellipse, Vector3(1.5f, 0.5f, 0.5f)),
           "sets ellipse size");
  testTrue(g,
           document.setColor(ellipse, ColorRgba{ 11, 22, 33, 255 }),
           "sets ellipse color");
  testTrue(g, document.setName(pyramid, "Peak"), "sets pyramid name");
  document.setTransform(ellipse,
                        Transform3D::fromPosition(Vector3(4.0f, 0.0f, 5.0f)));
  document.setTransform(pyramid,
                        Transform3D::fromPosition(Vector3(4.0f, 0.0f, 0.0f)));
  std::string hit;
  testTrue(g, document.pick(4.2f, 5.1f, &hit), "picks ellipse on XZ ground");
  testEqStr(g, hit, ellipse, "pick id is ellipse");
  testTrue(g, !document.pick(4.2f, 0.1f, &hit), "3D pick ignores XY height");
  const EditorSceneDetail detail = document.sceneDetail(ellipse);
  testEqSize(g, detail.nodeCount, 2u, "scene has two nodes");
  testTrue(g, detail.hasSelection, "detail has selection");
  testEqStr(g, detail.selectedId, ellipse, "detail id");
  testTrue(
    g, detail.selectedKind == SceneNodeKind::FilledEllipse, "detail kind");
  testTrue(g, detail.extent.x == 1.5f, "detail extent");
  testTrue(g, detail.color.g == 22, "detail color");
  testTrue(g, detail.transform.position.z == 5.0f, "detail transform");
  testTrue(g, detail.worldMode == IlscWorldMode::World3D, "detail world mode");
}

static void
testTransactionalLoad()
{
  testSection("EditorDocument: load failure leaves prior state");
  EditorDocument document;
  document.createNode(SceneNodeKind::Empty, {});
  const size_t before = document.nodeCount();
  std::string error;
  testTrue(g,
           !document.loadFromText("{\"format\":\"nope\"}", &error),
           "invalid text fails");
  testEqSize(g, document.nodeCount(), before, "prior nodes remain");
  const char* valid = R"({
    "format": "ilsc",
    "version": 1,
    "nodes": [{"id": "keep", "kind": "empty", "name": "Keep"}]
  })";
  testTrue(g, document.loadFromText(valid, &error), "valid replacement");
  testEqSize(g, document.nodeCount(), 1u, "replaced with one node");
  testEqStr(g, document.nodeAt(0)->id, "keep", "loaded id");
  testTrue(g, !document.isDirty(), "successful load is clean");
}

void
registerEditorDocumentTests(IllumoTestRegistry& registry)
{
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
