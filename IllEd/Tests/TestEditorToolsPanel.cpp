#include "EditorToolsPanel.h"
#include "PanelTestHelpers.h"
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>

static TestCounters g;

static void
testToolHits()
{
  testSection("EditorToolsPanel: every control issues its command");
  HeadlessRenderFixture fixture(1280, 720);
  EditorToolsPanel tools(&fixture.window, &fixture.renderer);
  tools.setPlacement(panelArea(1030.0f, 46.0f, 250.0f, 400.0f));
  tools.update(nullptr, 0.016f);
  const EditorCommand commands[] = {
    EditorCommand::SetMode2D,     EditorCommand::SetMode3D,
    EditorCommand::TranslateMode, EditorCommand::RotateMode,
    EditorCommand::ScaleMode,     EditorCommand::ToggleGizmoSpace,
    EditorCommand::ToggleSnap,    EditorCommand::SelectTool,
    EditorCommand::CreateEllipse, EditorCommand::CreatePyramid
  };
  for (EditorCommand command : commands) {
    float x = 0.0f;
    float y = 0.0f;
    testTrue(g,
             tools.controlCenterForTesting(command, &x, &y) &&
               tools.containsScreenPoint(x, y) &&
               tools.clickAtForTesting(x, y) == command,
             "a control hits its command");
  }
  testTrue(g,
           !tools.containsScreenPoint(20.0f, 80.0f),
           "the viewport is outside the panel");
  testTrue(g,
           tools.clickAtForTesting(1040.0f, 440.0f) == EditorCommand::None,
           "empty panel space issues nothing");
}

static void
testPlacementAndState()
{
  testSection("EditorToolsPanel: follows its placement and shows state");
  HeadlessRenderFixture fixture(1280, 720);
  EditorToolsPanel tools(&fixture.window, &fixture.renderer);
  tools.setPlacement(panelArea(0.0f, 22.0f, 280.0f, 380.0f));
  tools.update(nullptr, 0.016f);
  float x = 0.0f;
  float y = 0.0f;
  testTrue(g,
           tools.controlCenterForTesting(EditorCommand::SetMode3D, &x, &y) &&
             x < 280.0f && y > 22.0f,
           "controls lay out inside a detached window's content");
  EditorToolsState state;
  state.is3D = true;
  state.gizmoMode = GizmoMode::Rotate;
  state.snap = true;
  tools.setState(state);
  tools.update(nullptr, 0.016f);
  testTrue(g, tools.state().is3D && tools.state().snap, "state is kept");
  testTrue(g,
           tools.getVisual().shapeCount() > 0u &&
             tools.getVisual().textCount() > 0u,
           "the panel draws its controls");
  tools.setFontSize(26.0f);
  tools.update(nullptr, 0.016f);
  testTrue(g,
           tools.contentHeight() > 400.0f,
           "a larger font grows the rows (the panel scrolls)");
  tools.setPlacement(panelArea(0.0f, 22.0f, 280.0f, 380.0f, false));
  tools.update(nullptr, 0.016f);
  testTrue(g,
           !tools.containsScreenPoint(x, y) &&
             tools.getVisual().shapeCount() == 0u,
           "a hidden panel draws nothing and takes no input");
}

void
registerEditorToolsPanelTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Tools.ControlHits", []() {
    g = {};
    testToolHits();
    return g.failures;
  });
  registry.add("IllEd.Tools.PlacementAndState", []() {
    g = {};
    testPlacementAndState();
    return g.failures;
  });
}
