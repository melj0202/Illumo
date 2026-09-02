#include "EditorSidebar.h"
#include "EditorToolbar.h"
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>

static TestCounters g;

static void
testSidebarHits()
{
  testSection("EditorSidebar: tool and mode hits");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSidebar sidebar(&fixture.window, &fixture.renderer);
  const float x = sidebar.sidebarX() + 20.0f;
  testTrue(g, sidebar.containsScreenPoint(x, 80.0f), "sidebar contains tools");
  testTrue(g,
           !sidebar.containsScreenPoint(20.0f, 80.0f),
           "left canvas is outside sidebar");

  const EditorCommand mode2d =
    sidebar.clickAtForTesting(sidebar.sidebarX() + 20.0f, 28.0f + 24.0f + 8.0f);
  testTrue(g, mode2d == EditorCommand::SetMode2D, "2D mode button");
  const EditorCommand mode3d = sidebar.clickAtForTesting(
    sidebar.sidebarX() + 120.0f, 28.0f + 24.0f + 8.0f);
  testTrue(g, mode3d == EditorCommand::SetMode3D, "3D mode button");

  const EditorCommand select =
    sidebar.clickAtForTesting(x, 28.0f + 24.0f + 30.0f + 8.0f);
  testTrue(g, select == EditorCommand::SelectTool, "Select tool row");
  const EditorCommand ellipse =
    sidebar.clickAtForTesting(x, 28.0f + 24.0f + 30.0f + 24.0f * 3.0f + 8.0f);
  testTrue(g, ellipse == EditorCommand::CreateEllipse, "Ellipse tool row");
  const EditorCommand pyramid =
    sidebar.clickAtForTesting(x, 28.0f + 24.0f + 30.0f + 24.0f * 6.0f + 8.0f);
  testTrue(g, pyramid == EditorCommand::CreatePyramid, "Pyramid tool row");
}

static void
testFontSizeScaling()
{
  testSection("EditorSidebar: font size scaling");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSidebar sidebar(&fixture.window, &fixture.renderer);

  testTrue(
    g, std::abs(sidebar.fontSize() - 13.0f) < 0.001f, "default fontSize 13");
  testTrue(
    g, std::abs(sidebar.panelWidth() - 200.0f) < 0.001f, "default width 200");

  sidebar.setFontSize(26.0f);
  sidebar.setToolbarDimensions(56.0f, 44.0f);
  testTrue(
    g, std::abs(sidebar.fontSize() - 26.0f) < 0.001f, "fontSize updated to 26");
  testTrue(g, sidebar.panelWidth() >= 400.0f, "width scaled to >= 400");
  testTrue(g,
           sidebar.containsScreenPoint(sidebar.sidebarX() + 20.0f, 70.0f),
           "contains scaled point");
}

static void
testCollapseExpand()
{
  testSection("EditorSidebar: collapse and expand behavior");
  HeadlessRenderFixture fixture(1280, 720);
  EditorSidebar sidebar(&fixture.window, &fixture.renderer);

  testTrue(g, !sidebar.isCollapsed(), "starts expanded by default");
  testTrue(g, sidebar.panelWidth() >= 200.0f, "expanded width is normal");
  const float origX = sidebar.sidebarX();
  testTrue(g,
           sidebar.containsScreenPoint(origX + 10.0f, 100.0f),
           "contains screen point when expanded");

  // Collapse via setter
  sidebar.setCollapsed(true);
  testTrue(g, sidebar.isCollapsed(), "isCollapsed is true");

  // Advance animation to completion
  for (int f = 0; f < 30; ++f) {
    sidebar.update(nullptr, 0.033f);
  }
  testTrue(g, sidebar.panelWidth() <= 32.0f, "collapsed width is narrow tab");
  testTrue(g, sidebar.sidebarX() > origX, "docked against right edge");
  testTrue(g,
           sidebar.containsScreenPoint(1280.0f - 10.0f, 100.0f),
           "contains right strip point");
  testTrue(g,
           !sidebar.containsScreenPoint(origX + 10.0f, 100.0f),
           "does not contain old expanded interior");

  // Clicking collapsed tab expands it
  sidebar.clickAtForTesting(1280.0f - 10.0f, 100.0f);
  testTrue(g, !sidebar.isCollapsed(), "clicking collapsed tab expands sidebar");
  for (int f = 0; f < 30; ++f) {
    sidebar.update(nullptr, 0.033f);
  }
  testTrue(
    g, sidebar.panelWidth() >= 200.0f, "width restored to expanded size");

  // Clicking collapse button [>] in header collapses it
  const float fontScale = sidebar.fontSize() / EditorToolbar::kDefaultFontSize;
  const float collapseBtnX = sidebar.sidebarX() + 6.0f * fontScale;
  sidebar.clickAtForTesting(collapseBtnX + 2.0f, 28.0f + 6.0f);
  testTrue(g,
           sidebar.isCollapsed(),
           "clicking header collapse button collapses sidebar");
}

void
registerEditorSidebarTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Sidebar.ToolHits", []() {
    g = {};
    testSidebarHits();
    return g.failures;
  });
  registry.add("IllEd.Sidebar.FontSizeScaling", []() {
    g = {};
    testFontSizeScaling();
    return g.failures;
  });
  registry.add("IllEd.Sidebar.CollapseExpand", []() {
    g = {};
    testCollapseExpand();
    return g.failures;
  });
}
