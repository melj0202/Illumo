#include "EditorToolbar.h"
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>

static TestCounters g;

static void
testMenuHits()
{
  testSection("EditorToolbar: menu bar hits");
  HeadlessRenderFixture fixture(1280, 720);
  EditorToolbar toolbar(&fixture.window, &fixture.renderer);
  testTrue(g, toolbar.containsScreenPoint(20.0f, 8.0f), "bar contains click");
  testTrue(g,
           !toolbar.containsScreenPoint(20.0f, 80.0f),
           "world click is outside closed menus");
  const EditorCommand fileClick = toolbar.clickAtForTesting(20.0f, 8.0f);
  testTrue(
    g, fileClick == EditorCommand::None, "opening File is not a command");
  testEqInt(g, toolbar.openMenuForTesting(), 0, "File menu opens");
  testTrue(g,
           toolbar.containsScreenPoint(20.0f, 40.0f),
           "open dropdown captures clicks");
  const EditorCommand newClick = toolbar.clickAtForTesting(30.0f, 36.0f);
  testTrue(g, newClick == EditorCommand::NewDocument, "New item fires");
  testEqInt(g, toolbar.openMenuForTesting(), -1, "menu closes after item");
}

static void
testChromeMetrics()
{
  testSection("EditorToolbar: plain chrome keeps the tool metrics");
  HeadlessRenderFixture fixture(1280, 720);
  EditorToolbar toolbar(&fixture.window, &fixture.renderer);

  testTrue(
    g, std::abs(toolbar.fontSize() - 13.0f) < 0.001f, "default fontSize 13");
  testTrue(g,
           std::abs(toolbar.barHeight() - GuiToolStyle::kMenuHeight) < 0.001f,
           "the menu bar has the tool height");
  testTrue(g,
           std::abs(toolbar.statusHeight() - GuiToolStyle::kStatusHeight) <
             0.001f,
           "the status bar has the tool height");

  toolbar.setFontSize(26.0f);
  testTrue(
    g, std::abs(toolbar.fontSize() - 26.0f) < 0.001f, "fontSize updated to 26");
  testTrue(g,
           std::abs(toolbar.barHeight() - GuiToolStyle::kMenuHeight) < 0.001f,
           "panel font size leaves the chrome alone");
  toolbar.update(nullptr, 0.016f);
  testTrue(g,
           toolbar.getVisual().shapeCount() > 0u &&
             toolbar.getVisual().spriteCount() == 0u,
           "the plain bars draw flat shapes, no sprites");
}

static void
testViewMenuPanels()
{
  testSection("EditorToolbar: View menu lists the dock panels");
  HeadlessRenderFixture fixture(1280, 720);
  EditorToolbar toolbar(&fixture.window, &fixture.renderer);
  std::vector<EditorPanelMenuEntry> panels;
  EditorPanelMenuEntry hierarchy;
  hierarchy.title = "Hierarchy";
  hierarchy.toggle = EditorCommand::ToggleHierarchyPanel;
  hierarchy.popOut = EditorCommand::PopOutHierarchyPanel;
  panels.push_back(hierarchy);
  toolbar.setPanels(panels, false);

  float x = 0.0f;
  float y = 0.0f;
  testTrue(g,
           toolbar.menuItemCenterForTesting(
             EditorCommand::ToggleHierarchyPanel, &x, &y),
           "the panel has a show/hide item");
  testTrue(g,
           toolbar.clickAtForTesting(x, y) ==
             EditorCommand::ToggleHierarchyPanel,
           "the show/hide item fires");
  testTrue(g,
           toolbar.menuItemCenterForTesting(
             EditorCommand::PopOutHierarchyPanel, &x, &y),
           "the panel has a pop-out item");
  testTrue(g,
           toolbar.clickAtForTesting(x, y) == EditorCommand::None &&
             toolbar.isMenuOpen(),
           "pop-out is disabled where windows are unavailable");
  toolbar.closeMenus();
  toolbar.setPanels(panels, true);
  toolbar.menuItemCenterForTesting(EditorCommand::PopOutHierarchyPanel, &x, &y);
  testTrue(g,
           toolbar.clickAtForTesting(x, y) ==
             EditorCommand::PopOutHierarchyPanel,
           "pop-out fires where windows are available");
  testTrue(g,
           toolbar.menuItemCenterForTesting(EditorCommand::ResetLayout, &x, &y),
           "the View menu resets the layout");
}
void
registerEditorToolbarTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Toolbar.MenuHits", []() {
    g = {};
    testMenuHits();
    return g.failures;
  });
  registry.add("IllEd.Toolbar.ChromeMetrics", []() {
    g = {};
    testChromeMetrics();
    return g.failures;
  });
  registry.add("IllEd.Toolbar.ViewMenuPanels", []() {
    g = {};
    testViewMenuPanels();
    return g.failures;
  });
}
