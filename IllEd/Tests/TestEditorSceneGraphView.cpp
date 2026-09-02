#include "EditorSceneGraphView.h"
#include "EditorToolbar.h"
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
  doc.createNode(SceneNodeKind::FilledEllipse, rootA);
  doc.createNode(SceneNodeKind::WireSphere, childA1);
  doc.createNode(SceneNodeKind::FilledRect, {});

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

static void
testFontSizeScaling()
{
  testSection("EditorSceneGraphView: font size scaling");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);

  testTrue(
    g, std::abs(view.fontSize() - 13.0f) < 0.001f, "default fontSize 13");
  testTrue(
    g, std::abs(view.panelWidth() - 220.0f) < 0.001f, "default width 220");
  testTrue(
    g, std::abs(view.rowHeight() - 22.0f) < 0.001f, "default rowHeight 22");

  view.setFontSize(26.0f);
  view.setToolbarDimensions(56.0f, 44.0f);
  testTrue(
    g, std::abs(view.fontSize() - 26.0f) < 0.001f, "fontSize updated to 26");
  testTrue(g, view.panelWidth() >= 440.0f, "width scaled to >= 440");
  testTrue(g, view.rowHeight() >= 44.0f, "rowHeight scaled to >= 44");
  testTrue(g, view.containsScreenPoint(300.0f, 70.0f), "contains scaled point");
}

static void
testCollapseExpand()
{
  testSection("EditorSceneGraphView: collapse and expand behavior");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);

  EditorDocument doc;
  const std::string root = doc.createNode(SceneNodeKind::Empty, {});
  doc.createNode(SceneNodeKind::SolidCube, root);

  testTrue(g, !view.isCollapsed(), "starts expanded by default");
  testTrue(g, view.panelWidth() >= 220.0f, "expanded width is normal");
  testTrue(g,
           view.containsScreenPoint(100.0f, 100.0f),
           "contains 100,100 when expanded");

  std::string selected;

  // Collapse via setter
  view.setCollapsed(true);
  testTrue(g, view.isCollapsed(), "isCollapsed is true");

  // Advance animation to completion
  for (int f = 0; f < 30; ++f) {
    view.update(nullptr, &doc, &selected, 0.033f);
  }
  testTrue(g, view.panelWidth() <= 32.0f, "collapsed width is narrow tab");
  testTrue(g,
           view.containsScreenPoint(10.0f, 100.0f),
           "contains 10,100 when collapsed");
  testTrue(g,
           !view.containsScreenPoint(100.0f, 100.0f),
           "does not contain 100,100 when collapsed");

  // Clicking collapsed tab expands it
  view.clickAtForTesting(10.0f, 100.0f, &doc, &selected);
  testTrue(g, !view.isCollapsed(), "clicking collapsed tab expands view");
  for (int f = 0; f < 30; ++f) {
    view.update(nullptr, &doc, &selected, 0.033f);
  }
  testTrue(g, view.panelWidth() >= 220.0f, "width restored to expanded size");

  // Clicking collapse button [<] in header collapses it
  const float fontScale = view.fontSize() / EditorToolbar::kDefaultFontSize;
  const float btnSize = std::max(18.0f, std::round(18.0f * fontScale));
  const float collapseBtnX =
    view.panelX() + view.panelWidth() - btnSize - 6.0f * fontScale;
  view.clickAtForTesting(collapseBtnX + 2.0f, 28.0f + 6.0f, &doc, &selected);
  testTrue(
    g, view.isCollapsed(), "clicking header collapse button collapses view");
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
  registry.add("IllEd.SceneGraphView.FontSizeScaling", []() {
    g = {};
    testFontSizeScaling();
    return g.failures;
  });
  registry.add("IllEd.SceneGraphView.CollapseExpand", []() {
    g = {};
    testCollapseExpand();
    return g.failures;
  });
}
