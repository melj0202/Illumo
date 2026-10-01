#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Gui/GuiPanelDock.h>
#include <Illumo/Gui/GuiToolStyle.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/FakePanelSurfaces.h>
#include <Illumo/Testing/TestAccess.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>

static GuiPanelDock
threePanels()
{
  GuiPanelDock dock;
  dock.addPanel({ "hierarchy", "Hierarchy", GuiDockSide::Left, 11, 1.0f });
  dock.addPanel({ "assets", "Assets", GuiDockSide::Left, 12, 1.0f });
  dock.addPanel({ "inspector", "Inspector", GuiDockSide::Right, 13, 1.0f });
  return dock;
}

static bool
approx(float a, float b)
{
  return std::abs(a - b) < 0.51f;
}

static GuiDockPointer
at(float x, float y, bool down)
{
  return { x, y, down };
}

static int
testPanelDockLayout()
{
  TestCounters counters;
  GuiPanelDock dock = threePanels();
  dock.layout(1280, 720, 24, 22, 1.0f);
  const GuiDockView& hierarchy = dock.view("hierarchy");
  const GuiDockView& assets = dock.view("assets");
  const GuiDockView& inspector = dock.view("inspector");
  testTrue(counters,
           hierarchy.visible && assets.visible && inspector.visible &&
             hierarchy.surface == 0,
           "every panel starts docked in the main window");
  testTrue(
    counters,
    approx(hierarchy.frame.x, 0) && approx(hierarchy.frame.y, 24) &&
      approx(hierarchy.frame.w, GuiPanelDock::kDefaultColumn) &&
      approx(assets.frame.y,
             hierarchy.frame.y + hierarchy.frame.h + GuiToolStyle::kSplitter) &&
      approx(assets.frame.y + assets.frame.h, 720 - 22),
    "the left column stacks its panels between the bars");
  testTrue(counters,
           approx(inspector.frame.x + inspector.frame.w, 1280) &&
             approx(inspector.frame.h, 720 - 24 - 22),
           "the right column fills its side");
  testTrue(counters,
           approx(hierarchy.content.y,
                  hierarchy.frame.y + GuiToolStyle::kTitleHeight) &&
             approx(hierarchy.content.h,
                    hierarchy.frame.h - GuiToolStyle::kTitleHeight),
           "content sits below the title bar");
  const GuiToolRect centre = dock.center();
  testTrue(
    counters,
    approx(centre.x, GuiPanelDock::kDefaultColumn + GuiToolStyle::kSplitter) &&
      approx(centre.x + centre.w,
             1280 - GuiPanelDock::kDefaultColumn - GuiToolStyle::kSplitter) &&
      approx(centre.y, 24),
    "the centre is what the columns leave");
  testTrue(counters,
           dock.overPanels(10, 100) && !dock.overPanels(640, 360) &&
             dock.overPanels(GuiPanelDock::kDefaultColumn + 1, 300),
           "panels and splitters cover the columns only");
  dock.setHidden("inspector", true);
  dock.layout(1280, 720, 24, 22, 1.0f);
  testTrue(counters,
           !dock.view("inspector").visible &&
             approx(dock.center().x + dock.center().w, 1280) &&
             dock.mode("inspector") == GuiDockMode::Hidden,
           "hiding the only right panel gives its column to the centre");
  dock.setHidden("hierarchy", true);
  dock.layout(1280, 720, 24, 22, 1.0f);
  testTrue(counters,
           approx(dock.view("assets").frame.y, 24) &&
             approx(dock.view("assets").frame.h, 720 - 24 - 22),
           "the remaining panel fills its column");
  dock.reset();
  dock.layout(400, 300, 24, 22, 1.0f);
  testTrue(counters,
           dock.view("hierarchy").frame.w + dock.view("inspector").frame.w <=
               400.0f - 120.0f + 0.5f &&
             dock.center().w > 100.0f,
           "a small window shrinks the columns to keep a centre");
  testTrue(counters,
           !dock.view("missing").visible &&
             dock.mode("missing") == GuiDockMode::Hidden,
           "unknown panels are hidden");
  return counters.failures;
}

static int
testPanelDockSplitters()
{
  TestCounters counters;
  GuiPanelDock dock = threePanels();
  dock.layout(1280, 720, 24, 22, 1.0f);
  const float splitterX = GuiPanelDock::kDefaultColumn + 1.0f;
  dock.update(at(splitterX, 300, true));
  testTrue(counters,
           dock.consumedPress(0) && dock.dragging(),
           "a press on the column splitter starts a drag");
  dock.update(at(splitterX + 60, 300, true));
  dock.update(at(splitterX + 60, 300, false));
  dock.layout(1280, 720, 24, 22, 1.0f);
  testTrue(
    counters,
    approx(dock.view("hierarchy").frame.w, GuiPanelDock::kDefaultColumn + 60) &&
      !dock.dragging(),
    "dragging it widens the column");
  dock.update(at(splitterX + 60, 300, true));
  dock.update(at(-500, 300, true));
  dock.update(at(-500, 300, false));
  dock.layout(1280, 720, 24, 22, 1.0f);
  testTrue(counters,
           approx(dock.view("hierarchy").frame.w, GuiPanelDock::kMinimumColumn),
           "a column never shrinks below its minimum");
  const GuiDockView& upper = dock.view("hierarchy");
  const float rowY = upper.frame.y + upper.frame.h + 1.0f;
  const float upperHeight = upper.frame.h;
  dock.update(at(40, rowY, true));
  dock.update(at(40, rowY + 50, true));
  dock.update(at(40, rowY + 50, false));
  dock.layout(1280, 720, 24, 22, 1.0f);
  testTrue(counters,
           approx(dock.view("hierarchy").frame.h, upperHeight + 50),
           "dragging a row splitter moves the boundary");
  dock.update(at(40, dock.view("assets").frame.y - 1.0f, true));
  dock.update(at(40, 2000, true));
  dock.update(at(40, 2000, false));
  dock.layout(1280, 720, 24, 22, 1.0f);
  testTrue(counters,
           dock.view("assets").frame.h >= 80.0f - 0.5f,
           "the panel below keeps its minimum height");
  dock.update(at(640, 360, true));
  testTrue(counters,
           !dock.consumedPress(0),
           "presses in the centre stay with the product");
  dock.update(at(640, 360, false));
  return counters.failures;
}

struct SurfaceFixture
{
  NullRenderWindow window{ 1280, 720 };
  EnvVars env;
  Camera camera{ glm::vec2(0, 0), 1, &env };
  FakePanelSurfaces surfaces{ &window, &camera };
};

static int
testPanelDockTearOff()
{
  TestCounters counters;
  GuiPanelDock dock = threePanels();
  dock.layout(1280, 720, 24, 22, 1.0f);
  const GuiToolRect title = dock.view("inspector").title;
  const GuiToolRect hide = GuiToolStyle::titleButtonRect(title, 0);
  const GuiToolRect popOut = GuiToolStyle::titleButtonRect(title, 1);
  dock.update(at(popOut.x + 2, popOut.y + 2, true));
  dock.update(at(popOut.x + 2, popOut.y + 2, false));
  testTrue(counters,
           !dock.canDetach() && dock.mode("inspector") == GuiDockMode::Docked,
           "without surfaces there is no pop-out button");

  SurfaceFixture fixture;
  fixture.surfaces.mainOrigin = { 100, 50 };
  dock.setSurfaces(&fixture.surfaces);
  dock.layout(1280, 720, 24, 22, 2.0f);
  dock.update(at(popOut.x + 2, popOut.y + 2, true));
  testTrue(
    counters,
    dock.consumedPress(0) && dock.mode("inspector") == GuiDockMode::Opening &&
      fixture.surfaces.window(13) != nullptr &&
      fixture.surfaces.window(13)->size == std::array<int, 2>{ 600, 840 },
    "the pop-out button asks for a window at the UI scale");
  dock.update(at(popOut.x + 2, popOut.y + 2, false));
  fixture.surfaces.step();
  dock.update(at(0, 0, false));
  dock.layout(1280, 720, 24, 22, 2.0f);
  const GuiDockView& detached = dock.view("inspector");
  testTrue(counters,
           dock.mode("inspector") == GuiDockMode::Detached &&
             detached.visible && detached.surface == 13 &&
             approx(detached.frame.w, 300) && approx(detached.frame.h, 420) &&
             approx(dock.center().x + dock.center().w, 1280),
           "once open the panel fills its window and leaves the column");
  FakePanelSurfaces::Window* os = fixture.surfaces.window(13);
  const GuiToolRect dockButton =
    GuiToolStyle::titleButtonRect(detached.title, 0);
  os->pointer.x = (dockButton.x + 2) * 2.0;
  os->pointer.y = (dockButton.y + 2) * 2.0;
  os->pointer.left = true;
  dock.update(at(0, 0, false));
  testTrue(counters,
           dock.consumedPress(13) &&
             dock.mode("inspector") == GuiDockMode::Docked &&
             fixture.surfaces.window(13) == nullptr,
           "the dock button closes the window and docks the panel");

  dock.layout(1280, 720, 24, 22, 2.0f);
  const GuiToolRect hierarchyTitle = dock.view("hierarchy").title;
  dock.update(at(hierarchyTitle.x + 30, hierarchyTitle.y + 5, true));
  testTrue(counters,
           dock.dragging() && dock.mode("hierarchy") == GuiDockMode::Docked,
           "a title drag starts inside the window");
  dock.update(at(-30, 200, true));
  FakePanelSurfaces::Window* torn = fixture.surfaces.window(11);
  testTrue(counters,
           dock.mode("hierarchy") == GuiDockMode::Opening && torn != nullptr &&
             torn->requestedX == -120 && torn->requestedY == 390,
           "dragging past the edge tears the panel off under the cursor");
  dock.update(at(-30, 200, false));
  fixture.surfaces.step();
  dock.update(at(0, 0, false));
  fixture.surfaces.move(11, 20, 300);
  fixture.surfaces.userClose(11);
  dock.update(at(0, 0, false));
  testTrue(counters,
           dock.mode("hierarchy") == GuiDockMode::Docked &&
             fixture.surfaces.window(11) == nullptr,
           "closing the window docks the panel");

  fixture.surfaces.refuseNextOpen = true;
  testTrue(counters, dock.detach("assets"), "a detach request goes out");
  fixture.surfaces.step();
  dock.update(at(0, 0, false));
  testTrue(counters,
           dock.mode("assets") == GuiDockMode::Docked,
           "a refused window leaves the panel docked");
  dock.detach("assets");
  fixture.surfaces.step();
  dock.update(at(0, 0, false));
  dock.setHidden("assets", true);
  testTrue(counters,
           dock.mode("assets") == GuiDockMode::Hidden &&
             fixture.surfaces.window(12) == nullptr,
           "hiding a detached panel closes its window");
  dock.setHidden("assets", false);
  dock.layout(1280, 720, 24, 22, 2.0f);
  dock.update(at(hide.x + 2, hide.y + 2, true));
  dock.update(at(hide.x + 2, hide.y + 2, false));
  testTrue(counters,
           dock.mode("inspector") == GuiDockMode::Hidden,
           "the hide button hides a docked panel");
  dock.setSurfaces(nullptr);
  dock.detach("assets");
  dock.update(at(0, 0, false));
  testTrue(counters,
           dock.mode("assets") != GuiDockMode::Detached,
           "without surfaces nothing detaches");
  return counters.failures;
}

static int
testPanelDockPersistence()
{
  TestCounters counters;
  SurfaceFixture fixture;
  GuiPanelDock dock = threePanels();
  dock.setSurfaces(&fixture.surfaces);
  dock.setColumnWidth(GuiDockSide::Left, 300);
  dock.setHidden("assets", true);
  dock.layout(1280, 720, 24, 22, 1.0f);
  dock.detach("inspector");
  fixture.surfaces.step();
  dock.update(at(0, 0, false));
  fixture.surfaces.move(13, 500, 90);
  fixture.surfaces.resize(13, 320, 500);
  dock.update(at(0, 0, false));
  const std::string saved = dock.serialize();
  testTrue(counters,
           saved.find("column left 300") != std::string::npos &&
             saved.find("panel assets hidden") != std::string::npos &&
             saved.find("panel inspector detached") != std::string::npos,
           "the layout serializes columns, modes and windows");

  SurfaceFixture later;
  GuiPanelDock restored = threePanels();
  restored.setSurfaces(&later.surfaces);
  testTrue(counters, restored.restore(saved), "the saved layout restores");
  restored.layout(1280, 720, 24, 22, 1.0f);
  restored.update(at(0, 0, false));
  FakePanelSurfaces::Window* reopened = later.surfaces.window(13);
  testTrue(counters,
           restored.mode("assets") == GuiDockMode::Hidden &&
             approx(restored.columnWidth(GuiDockSide::Left), 300) &&
             reopened != nullptr && reopened->requestedX == 400 &&
             reopened->requestedY == 40 &&
             reopened->size == std::array<int, 2>{ 320, 500 },
           "a detached panel reopens where its window was");

  GuiPanelDock headless = threePanels();
  testTrue(counters,
           headless.restore(saved) &&
             headless.mode("inspector") == GuiDockMode::Docked,
           "without surfaces a detached panel restores docked");
  headless.update(at(0, 0, false));
  testTrue(counters,
           headless.mode("inspector") == GuiDockMode::Docked,
           "and stays docked");

  GuiPanelDock untouched = threePanels();
  untouched.setColumnWidth(GuiDockSide::Right, 280);
  testTrue(
    counters,
    !untouched.restore("wrong header\n") &&
      !untouched.restore("illumo-dock 1\ncolumn left nope\n") &&
      !untouched.restore("illumo-dock 1\npanel assets floating 1 0 0 0 0\n") &&
      !untouched.restore("illumo-dock 1\npanel assets docked nan 0 0 0 0\n") &&
      !untouched.restore("illumo-dock 1\nsomething else\n") &&
      approx(untouched.columnWidth(GuiDockSide::Right), 280),
    "invalid layouts are refused and change nothing");
  testTrue(
    counters,
    untouched.restore(
      "illumo-dock 1\npanel removed docked 1 0 0 0 0\ncolumn right 200\n") &&
      approx(untouched.columnWidth(GuiDockSide::Right), 200),
    "panels a build no longer has are ignored");
  return counters.failures;
}

static int
testToolStyleTokens()
{
  TestCounters counters;
  GameVisual visual;
  GuiToolStyle::panel(visual, { 0, 0, 200, 100 });
  GuiToolStyle::titleBar(
    visual,
    { 0, 0, 200, GuiToolStyle::kTitleHeight },
    "Inspector",
    { GuiToolTitleButton::Hide, GuiToolTitleButton::PopOut },
    1);
  GuiToolStyle::toggle(visual, { 0, 30, 200, 20 }, "Grid", true, false);
  GuiToolStyle::slider(
    visual, { 0, 50, 200, 20 }, "Bias", 0.25f, "0.25", true, false);
  GuiToolStyle::button(visual, { 0, 70, 80, 20 }, "Open", false, true);
  GuiToolStyle::scrollbar(visual, { 194, 22, 6, 78 }, 10, 5, 40);
  testTrue(counters,
           visual.shapeCount() > 10 && visual.textCount() >= 4,
           "the tool style draws flat shapes and text");
  const GuiToolRect track = GuiToolStyle::sliderTrack({ 0, 50, 200, 20 });
  testTrue(counters,
           track.x > 0 && track.x + track.w < 200 && track.w > 8,
           "the slider track sits between its label and value");
  const std::vector<std::string> menus{ "File", "Edit", "View" };
  const GuiToolRect bar{ 0, 0, 800, GuiToolStyle::kMenuHeight };
  const GuiToolRect edit = GuiToolStyle::menuRect(bar, menus, 1);
  testTrue(counters,
           edit.x > GuiToolStyle::menuRect(bar, menus, 0).x && edit.w > 0 &&
             GuiToolStyle::menuRect(bar, menus, 5).w == 0,
           "menu titles lay out left to right");
  std::vector<GuiToolStyle::MenuItem> items(3);
  items[0].label = "New";
  items[0].hint = "Ctrl+N";
  items[1].separator = true;
  items[2].label = "Quit";
  testTrue(
    counters,
    GuiToolStyle::dropdownItemAt(10, 30, items, 20, 30 + 3 + 2) == 0 &&
      GuiToolStyle::dropdownItemAt(10, 30, items, 20, 30 + 3 + 21) == -1 &&
      GuiToolStyle::dropdownItemAt(10, 30, items, 20, 30 + 3 + 29) == 2 &&
      GuiToolStyle::dropdownItemAt(10, 30, items, 5, 35) == -1,
    "dropdown hits skip separators and the outside");
  return counters.failures;
}

static int
testSurfacePointerTracker()
{
  TestCounters counters;
  GuiPointerTracker tracker;
  PanelSurfacePointer pointer;
  pointer.x = 40.0;
  pointer.y = 20.0;
  tracker.sample(pointer, 2.0f);
  testTrue(counters,
           tracker.x() == 20.0f && tracker.y() == 10.0f && !tracker.clicked(),
           "a surface pointer maps into layout units");
  pointer.left = true;
  tracker.sample(pointer, 2.0f);
  testTrue(counters,
           tracker.clicked() && tracker.pressed(),
           "the press edge fires once");
  tracker.sample(pointer, 2.0f);
  pointer.left = false;
  pointer.x = -30.0;
  tracker.sample(pointer, 2.0f);
  testTrue(counters,
           !tracker.clicked() && tracker.released() && tracker.x() == -15.0f,
           "the release edge fires, even outside the window");
  return counters.failures;
}

void
registerGuiPanelDockTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.Gui.SurfacePointerTracker",
               []() { return testSurfacePointerTracker(); });
  registry.add("Illumo.Gui.PanelDockLayout",
               []() { return testPanelDockLayout(); });
  registry.add("Illumo.Gui.PanelDockSplitters",
               []() { return testPanelDockSplitters(); });
  registry.add("Illumo.Gui.PanelDockTearOff",
               []() { return testPanelDockTearOff(); });
  registry.add("Illumo.Gui.PanelDockPersistence",
               []() { return testPanelDockPersistence(); });
  registry.add("Illumo.Gui.ToolStyleTokens",
               []() { return testToolStyleTokens(); });
}
