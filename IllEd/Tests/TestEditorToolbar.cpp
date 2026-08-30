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
testFontSizeScaling()
{
  testSection("EditorToolbar: font size scaling");
  HeadlessRenderFixture fixture(1280, 720);
  EditorToolbar toolbar(&fixture.window, &fixture.renderer);

  testTrue(
    g, std::abs(toolbar.fontSize() - 13.0f) < 0.001f, "default fontSize 13");
  testTrue(
    g, std::abs(toolbar.barHeight() - 28.0f) < 0.001f, "default barHeight 28");
  testTrue(g,
           std::abs(toolbar.statusHeight() - 22.0f) < 0.001f,
           "default statusHeight 22");

  toolbar.setFontSize(26.0f);
  testTrue(
    g, std::abs(toolbar.fontSize() - 26.0f) < 0.001f, "fontSize updated to 26");
  testTrue(g, toolbar.barHeight() >= 56.0f, "barHeight scaled to >= 56");
  testTrue(g, toolbar.statusHeight() >= 44.0f, "statusHeight scaled to >= 44");
  testTrue(
    g, toolbar.containsScreenPoint(20.0f, 40.0f), "bar contains scaled point");
}

void
registerEditorToolbarTests(IllumoTestRegistry& registry)
{
  registry.add("IllEd.Toolbar.MenuHits", []() {
    g = {};
    testMenuHits();
    return g.failures;
  });
  registry.add("IllEd.Toolbar.FontSizeScaling", []() {
    g = {};
    testFontSizeScaling();
    return g.failures;
  });
}
