#include "EditorSidebar.h"
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

void
registerEditorSidebarTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Sidebar.ToolHits", []() {
    g = {};
    testSidebarHits();
    return g.failures;
  });
}
