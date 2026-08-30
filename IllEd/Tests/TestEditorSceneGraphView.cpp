#include "EditorSceneGraphView.h"
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>

static TestCounters g;

static void
testHierarchyOrderingAndIndentation()
{
  testSection("EditorSceneGraphView: hierarchy tree ordering and indentation");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);

  EditorDocument doc;
  const std::string rootA = doc.createNode(SceneNodeKind::Empty, {});
  const std::string childA1 = doc.createNode(SceneNodeKind::SolidCube, rootA);
  const std::string childA2 =
    doc.createNode(SceneNodeKind::FilledEllipse, rootA);
  const std::string grandChildA1 =
    doc.createNode(SceneNodeKind::WireSphere, childA1);
  const std::string rootB = doc.createNode(SceneNodeKind::FilledRect, {});

  std::string selected;
  view.update(nullptr, &doc, &selected, 0.016f);

  testEqSize(g, view.visibleRowCountForTesting(), 5u, "5 visible rows in tree");
  testTrue(g,
           view.containsScreenPoint(10.0f, 60.0f),
           "contains screen point inside panel");
  testTrue(g,
           !view.containsScreenPoint(300.0f, 60.0f),
           "does not contain outside point");
}

static void
testSelectionViaTreeClick()
{
  testSection("EditorSceneGraphView: select node by clicking tree row");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);

  EditorDocument doc;
  const std::string root = doc.createNode(SceneNodeKind::Empty, {});
  const std::string child = doc.createNode(SceneNodeKind::SolidCube, root);

  std::string selected;
  // Row 0 is at y = 28 + 30 = 58. Row 1 is at y = 58 + 22 = 80
  view.clickAtForTesting(30.0f, 58.0f + 10.0f, &doc, &selected);
  testEqStr(g, selected, root, "clicked row 0 selects root");

  view.clickAtForTesting(30.0f, 80.0f + 10.0f, &doc, &selected);
  testEqStr(g, selected, child, "clicked row 1 selects child");
}

static void
testDragAndDropReparenting()
{
  testSection(
    "EditorSceneGraphView: drag and drop reparenting and cycle rejection");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);

  EditorDocument doc;
  const std::string parentA = doc.createNode(SceneNodeKind::Empty, {});
  const std::string parentB = doc.createNode(SceneNodeKind::Empty, {});
  const std::string item = doc.createNode(SceneNodeKind::SolidCube, parentA);

  testEqStr(
    g, doc.findNode(item)->parentId, parentA, "item starts under parentA");

  // Valid reparent: item -> parentB
  view.dragAndDropForTesting(item, parentB, &doc);
  testEqStr(
    g, doc.findNode(item)->parentId, parentB, "item reparented to parentB");

  // Valid unparent to root
  view.dragAndDropForTesting(item, "", &doc);
  testTrue(g, doc.findNode(item)->parentId.empty(), "item reparented to root");

  // Reparent back under parentA
  view.dragAndDropForTesting(item, parentA, &doc);
  testEqStr(
    g, doc.findNode(item)->parentId, parentA, "item back under parentA");

  // Cycle rejection: parentA cannot be parented to item (its child)
  view.dragAndDropForTesting(parentA, item, &doc);
  testTrue(g,
           doc.findNode(parentA)->parentId.empty(),
           "cycle rejected: parentA remains root");

  // Self parent rejection
  view.dragAndDropForTesting(parentA, parentA, &doc);
  testTrue(g, doc.findNode(parentA)->parentId.empty(), "self-parent rejected");
}

static void
testVisualTokenEmission()
{
  testSection("EditorSceneGraphView: visual primitives and token emission");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);

  EditorDocument doc;
  doc.createNode(SceneNodeKind::SolidCube, {});
  doc.createNode(SceneNodeKind::FilledTriangle, {});

  std::string selected;
  view.update(nullptr, &doc, &selected, 0.016f);

  testTrue(g,
           view.getVisual().shapeCount() > 0u,
           "visual has shapes (background, borders)");
  testTrue(g,
           view.getVisual().textCount() > 0u,
           "visual has text primitives (title, labels)");

  fixture.renderer.BeginFrame();
  testTrue(
    g, view.AppendCommands(&fixture.renderer), "appends render commands");
  fixture.renderer.EndFrame();

  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "emits DrawIndexed commands");
}

void
registerEditorSceneGraphViewTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.SceneGraphView.Hierarchy", []() {
    g = {};
    testHierarchyOrderingAndIndentation();
    return g.failures;
  });
  registry.add("IllEd.SceneGraphView.Selection", []() {
    g = {};
    testSelectionViaTreeClick();
    return g.failures;
  });
  registry.add("IllEd.SceneGraphView.DragReparent", []() {
    g = {};
    testDragAndDropReparenting();
    return g.failures;
  });
  registry.add("IllEd.SceneGraphView.VisualEmission", []() {
    g = {};
    testVisualTokenEmission();
    return g.failures;
  });
}
