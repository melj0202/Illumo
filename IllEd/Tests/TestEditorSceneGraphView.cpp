#include "EditorSceneGraphView.h"
#include "EditorToolbar.h"
#include "PanelTestHelpers.h"
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>

static TestCounters g;

static std::string
make(EditorDocument& document, const char* kind, const std::string& parent = {})
{
  ScenePrimitiveShape shape = ScenePrimitiveShape::Cube;
  const bool isEmpty = std::string(kind) == "empty";
  if (!isEmpty) {
    parseScenePrimitiveShape(kind, shape);
  }
  return document.createPrimitive(isEmpty, shape, parent, Transform3D{});
}

static std::string
parentOf(const EditorDocument& document, const std::string& id)
{
  return document.scene().idOf(
    document.graph().getParent(document.nodeHandle(id)));
}
static void
testHierarchyOrderingAndIndentation()
{
  testSection("EditorSceneGraphView: hierarchy tree ordering and indentation");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);
  view.setPlacement(panelArea(0.0f, 46.0f, 250.0f, 652.0f));

  EditorDocument doc;
  const std::string rootA = make(doc, "empty");
  const std::string childA1 = make(doc, "cube", rootA);
  make(doc, "ellipse", rootA);
  make(doc, "wire_sphere", childA1);
  make(doc, "rect");

  EditorSelection selected;
  view.update(nullptr, &doc, &selected, 0.016f);

  testEqSize(g, view.visibleRowCountForTesting(), 5u, "5 visible rows in tree");
  testTrue(g,
           view.containsScreenPoint(10.0f, 60.0f),
           "contains screen point inside panel");
  testTrue(g,
           !view.containsScreenPoint(300.0f, 60.0f),
           "does not contain outside point");
  testTrue(g,
           std::fabs(view.rowScreenY(0) - 50.0f) < 0.5f,
           "rows start at the top of the content rectangle");
}

static void
testSelectionViaTreeClick()
{
  testSection("EditorSceneGraphView: select node by clicking tree row");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);
  view.setPlacement(panelArea(0.0f, 46.0f, 250.0f, 652.0f));

  EditorDocument doc;
  const std::string root = make(doc, "empty");
  const std::string child = make(doc, "cube", root);

  EditorSelection selected;
  view.update(nullptr, &doc, &selected, 0.016f);
  const float row0 = view.rowScreenY(0) + view.rowHeight() * 0.5f;
  const float row1 = view.rowScreenY(1) + view.rowHeight() * 0.5f;
  view.clickAtForTesting(40.0f, row0, &doc, &selected);
  testEqStr(g, selected.primary(), root, "clicked row 0 selects root");

  view.clickAtForTesting(40.0f, row1, &doc, &selected);
  testEqStr(g, selected.primary(), child, "clicked row 1 selects child");
  view.clickAtForTesting(40.0f, row0, &doc, &selected, true);
  testTrue(g,
           selected.size() == 2 && selected.contains(root) &&
             selected.primary() == root,
           "a toggle click adds to the selection");
  view.clickAtForTesting(40.0f, row0, &doc, &selected, true);
  testTrue(g,
           selected.size() == 1 && selected.primary() == child,
           "a second toggle click removes it");
}

static void
testDragAndDropReparenting()
{
  testSection(
    "EditorSceneGraphView: drag and drop reparenting and cycle rejection");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);
  view.setPlacement(panelArea(0.0f, 46.0f, 250.0f, 652.0f));

  EditorDocument doc;
  const std::string parentA = make(doc, "empty");
  const std::string parentB = make(doc, "empty");
  const std::string item = make(doc, "cube", parentA);

  testEqStr(g, parentOf(doc, item), parentA, "item starts under parentA");

  // Valid reparent: item -> parentB
  view.dragAndDropForTesting(item, parentB, &doc);
  testEqStr(g, parentOf(doc, item), parentB, "item reparented to parentB");

  // Valid unparent to root
  view.dragAndDropForTesting(item, "", &doc);
  testTrue(g, parentOf(doc, item).empty(), "item reparented to root");

  // Reparent back under parentA
  view.dragAndDropForTesting(item, parentA, &doc);
  testEqStr(g, parentOf(doc, item), parentA, "item back under parentA");

  // Cycle rejection: parentA cannot be parented to item (its child)
  view.dragAndDropForTesting(parentA, item, &doc);
  testTrue(
    g, parentOf(doc, parentA).empty(), "cycle rejected: parentA remains root");

  // Self parent rejection
  view.dragAndDropForTesting(parentA, parentA, &doc);
  testTrue(g, parentOf(doc, parentA).empty(), "self-parent rejected");
}

static void
testVisualTokenEmission()
{
  testSection("EditorSceneGraphView: visual primitives and token emission");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);
  view.setPlacement(panelArea(0.0f, 46.0f, 250.0f, 652.0f));

  EditorDocument doc;
  const std::string first = make(doc, "cube");
  make(doc, "triangle");

  // The dock draws the panel body; the content draws rows and marks.
  EditorSelection selected;
  selected.set(first);
  view.update(nullptr, &doc, &selected, 0.016f);

  testTrue(g,
           view.getVisual().shapeCount() > 0u,
           "visual has shapes (the selected row)");
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
  testSection("EditorSceneGraphView: font size scales the rows");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);
  view.setPlacement(panelArea(0.0f, 46.0f, 250.0f, 652.0f));

  testTrue(
    g, std::abs(view.fontSize() - 13.0f) < 0.001f, "default fontSize 13");
  testTrue(
    g, std::abs(view.rowHeight() - 20.0f) < 0.001f, "default rowHeight 20");
  view.setFontSize(26.0f);
  testTrue(
    g, std::abs(view.fontSize() - 26.0f) < 0.001f, "fontSize updated to 26");
  testTrue(g, view.rowHeight() >= 40.0f, "rowHeight scaled to >= 40");
  testTrue(g,
           std::abs(view.panelWidth() - 250.0f) < 0.001f,
           "the dock, not the font, sets the width");
}

static void
testPlacement()
{
  testSection("EditorSceneGraphView: draws where it is placed");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);
  EditorDocument doc;
  const std::string root = make(doc, "empty");
  make(doc, "cube", root);
  EditorSelection selected;

  // A detached window: the content starts below its own title bar.
  view.setPlacement(panelArea(0.0f, 22.0f, 300.0f, 458.0f));
  view.update(nullptr, &doc, &selected, 0.016f);
  testTrue(g,
           std::fabs(view.rowScreenY(0) - 26.0f) < 0.5f &&
             std::fabs(view.panelWidth() - 300.0f) < 0.001f,
           "rows follow the placement");
  view.clickAtForTesting(
    40.0f, view.rowScreenY(0) + view.rowHeight() * 0.5f, &doc, &selected);
  testEqStr(g, selected.primary(), root, "clicks hit the placed rows");

  view.openContextMenuForTesting(
    280.0f, view.rowScreenY(0) + 4.0f, &doc, &selected);
  float x = 0.0f;
  float y = 0.0f;
  testTrue(g,
           view.menuItemCenterForTesting(EditorCommand::DeleteNode, &x, &y) &&
             x < 300.0f,
           "the context menu stays inside the panel's window");

  view.setPlacement(panelArea(0.0f, 22.0f, 300.0f, 458.0f, false));
  testTrue(g, !view.menuOpen(), "hiding the panel closes its menu");
  view.update(nullptr, &doc, &selected, 0.016f);
  testTrue(g,
           !view.containsScreenPoint(40.0f, 40.0f) &&
             view.visibleRowCountForTesting() == 0u &&
             view.getVisual().shapeCount() == 0u,
           "a hidden panel draws nothing and takes no input");
}
static void
testRowWindowScroll()
{
  testSection("EditorSceneGraphView: rows scroll inside a clipped window");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);
  view.setPlacement(panelArea(0.0f, 46.0f, 250.0f, 652.0f));
  EditorDocument doc;
  std::vector<std::string> ids;
  for (int index = 0; index < 80; ++index) {
    ids.push_back(make(doc, "cube"));
  }
  EditorSelection selected;
  view.update(nullptr, &doc, &selected, 0.016f);
  const float rowHeight = view.rowHeight();
  const float firstY = view.rowScreenY(0);
  testTrue(g,
           view.getVisual().textCount() < 60u,
           "only rows inside the window are drawn");

  view.scrollForTesting(rowHeight * 10.0f);
  view.update(nullptr, &doc, &selected, 0.016f);
  testTrue(g,
           std::fabs(view.rowScreenY(10) - firstY) < 0.5f,
           "scrolling ten rows brings row 10 to the top");
  view.clickAtForTesting(40.0f, firstY + rowHeight * 0.5f, &doc, &selected);
  testEqStr(g, selected.primary(), ids[10], "a click hits the scrolled row");
  view.clickAtForTesting(40.0f, firstY - rowHeight * 2.0f, &doc, &selected);
  testEqStr(g,
            selected.primary(),
            ids[10],
            "rows scrolled above the window are not hit");

  view.scrollForTesting(1.0e6f);
  view.update(nullptr, &doc, &selected, 0.016f);
  const float lastBottom = view.rowScreenY(ids.size() - 1) + rowHeight;
  testTrue(g,
           lastBottom <= view.panelBottom() && lastBottom > firstY,
           "scrolling stops at the last row");
  view.scrollForTesting(-1.0e6f);
  view.update(nullptr, &doc, &selected, 0.016f);
  testTrue(g, view.scrollOffset() == 0.0f, "scrolling stops at the first row");

  // Selecting a far node (as the viewport would) scrolls it into view.
  selected.set(ids[70]);
  view.update(nullptr, &doc, &selected, 0.016f);
  testTrue(g,
           view.rowScreenY(70) >= firstY &&
             view.rowScreenY(70) < view.panelBottom(),
           "a new primary selection is scrolled into view");
}

static std::vector<std::string>
rootOrder(const EditorDocument& doc)
{
  return doc.scene().childIds("");
}

static void
testReorderUndo()
{
  testSection("EditorSceneGraphView: drops reorder siblings with undo");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);
  view.setPlacement(panelArea(0.0f, 46.0f, 250.0f, 652.0f));
  EditorDocument doc;
  const std::string a = make(doc, "cube");
  const std::string b = make(doc, "rect");
  const std::string c = make(doc, "sphere");

  testTrue(g, view.dropForTesting(c, a, -1, &doc), "drop c before a");
  testTrue(g,
           rootOrder(doc) == std::vector<std::string>{ c, a, b },
           "c moves to the front");
  testTrue(g, doc.undo(), "the reorder undoes");
  testTrue(g,
           rootOrder(doc) == std::vector<std::string>{ a, b, c },
           "undo restores the order");
  testTrue(g, view.dropForTesting(a, c, 1, &doc), "drop a after c");
  testTrue(g,
           rootOrder(doc) == std::vector<std::string>{ b, c, a },
           "a moves to the end");
  testTrue(g, view.dropForTesting(a, b, 0, &doc), "drop a into b");
  testEqStr(g, parentOf(doc, a), b, "a becomes b's child");
  testTrue(g,
           !view.dropForTesting(b, a, -1, &doc),
           "a node cannot be dropped beside its own descendant");
  testTrue(g, view.dropForTesting(c, a, 1, &doc), "drop c after a (a child)");
  testEqStr(g, parentOf(doc, c), b, "c joins a's parent");
  testTrue(g,
           doc.scene().childIds(b) == std::vector<std::string>{ a, c },
           "c lands right after a");
  testTrue(g,
           view.dropForTesting(a, c, -1, &doc) &&
             doc.scene().childIds(b) == std::vector<std::string>{ a, c },
           "dropping into the current position changes nothing");
}

static void
testFoldAndVisibility()
{
  testSection("EditorSceneGraphView: fold arrows, eye toggles, context menu");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSceneGraphView view(&fixture.window, &fixture.renderer);
  view.setPlacement(panelArea(0.0f, 46.0f, 250.0f, 652.0f));
  EditorDocument doc;
  const std::string root = make(doc, "empty");
  const std::string c1 = make(doc, "cube", root);
  make(doc, "rect", root);
  const std::string solo = make(doc, "sphere");
  EditorSelection selected;
  view.update(nullptr, &doc, &selected, 0.016f);
  testEqSize(g, view.visibleRowCountForTesting(), 4u, "four rows unfolded");

  float x = 0.0f;
  float y = 0.0f;
  testTrue(
    g, view.foldCenterForTesting(0, &x, &y), "a parent has a fold arrow");
  testTrue(g, !view.foldCenterForTesting(3, &x, &y), "a leaf has none");
  view.foldCenterForTesting(0, &x, &y);
  view.clickAtForTesting(x, y, &doc, &selected);
  testTrue(g, view.isFolded(root), "clicking the arrow folds the row");
  testTrue(g, selected.empty(), "folding does not select");
  view.update(nullptr, &doc, &selected, 0.016f);
  testEqSize(g, view.visibleRowCountForTesting(), 2u, "folded children hide");

  selected.set(c1);
  view.update(nullptr, &doc, &selected, 0.016f);
  testTrue(g,
           !view.isFolded(root) && view.visibleRowCountForTesting() == 4u,
           "selecting a hidden child unfolds its ancestors");

  testTrue(g, view.eyeCenterForTesting(1, &x, &y), "rows have an eye");
  view.clickAtForTesting(x, y, &doc, &selected);
  testTrue(g, !doc.findNode(c1)->visible, "the eye hides the node");
  testTrue(g,
           selected.primary() == c1 && selected.size() == 1,
           "the eye does not change the selection");
  doc.undo();
  testTrue(g, doc.findNode(c1)->visible, "hiding undoes");

  view.update(nullptr, &doc, &selected, 0.016f);
  view.openContextMenuForTesting(
    200.0f, view.rowScreenY(3) + view.rowHeight() * 0.5f, &doc, &selected);
  testTrue(g, view.menuOpen(), "a right-click opens the context menu");
  testEqStr(g, selected.primary(), solo, "the menu selects its row");
  testTrue(g,
           view.menuItemCenterForTesting(EditorCommand::DeleteNode, &x, &y),
           "the menu lists Delete");
  view.clickAtForTesting(x, y, &doc, &selected);
  std::string target;
  testTrue(g,
           view.takeCommand(&target) == EditorCommand::DeleteNode &&
             target == solo,
           "choosing an item hands its command to the module");
  testTrue(g, !view.menuOpen(), "choosing an item closes the menu");
  testTrue(g,
           view.takeCommand(nullptr) == EditorCommand::None,
           "a command is taken once");
}

void
registerEditorSceneGraphViewTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Hierarchy.RowWindowScroll", []() {
    g = {};
    testRowWindowScroll();
    return g.failures;
  });
  registry.add("IllEd.Hierarchy.ReorderUndo", []() {
    g = {};
    testReorderUndo();
    return g.failures;
  });
  registry.add("IllEd.Hierarchy.FoldAndVisibility", []() {
    g = {};
    testFoldAndVisibility();
    return g.failures;
  });
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
  registry.add("IllEd.SceneGraphView.Placement", []() {
    g = {};
    testPlacement();
    return g.failures;
  });
}
