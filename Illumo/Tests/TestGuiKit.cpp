#include <Illumo/Gui/GridAtlas.h>
#include <Illumo/Gui/GuiDialog.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Services/InputManager.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <cmath>
#include <limits>

static TestCounters g;

static void
testTextMetricsAndLayout()
{
  testSection("GuiKit: text metrics and layout helpers");
  const float width = GuiKit::estimateTextWidth("Test", 10.0f);
  testTrue(g, std::abs(width - 24.0f) < 0.01f, "estimateTextWidth calculation");

  const float lineHeight = GuiKit::defaultLineHeight(10.0f);
  testTrue(
    g, std::abs(lineHeight - 13.5f) < 0.01f, "defaultLineHeight calculation");

  GameVisual visual(128u);
  GuiKit::drawTextCentered(
    visual, "Centered", 100.0f, 50.0f, 12.0f, UiTheme::textPrimary());
  testEqSize(g, visual.textCount(), 1u, "drawTextCentered added one text run");

  GuiKit::drawTextAligned(visual,
                          "Left",
                          10.0f,
                          20.0f,
                          100.0f,
                          12.0f,
                          UiTheme::textPrimary(),
                          GuiAlignment::Left);
  GuiKit::drawTextAligned(visual,
                          "Center",
                          10.0f,
                          40.0f,
                          100.0f,
                          12.0f,
                          UiTheme::textPrimary(),
                          GuiAlignment::Center);
  GuiKit::drawTextAligned(visual,
                          "Right",
                          10.0f,
                          60.0f,
                          100.0f,
                          12.0f,
                          UiTheme::textPrimary(),
                          GuiAlignment::Right);
  testEqSize(g, visual.textCount(), 4u, "drawTextAligned text runs count");

  GuiKit::drawLabelValue(visual, "Key", "Value", 0.0f, 80.0f, 200.0f, 12.0f);
  testEqSize(
    g, visual.textCount(), 6u, "drawLabelValue added label and value text");
}

static void
testGuiPrimitivesAndControls()
{
  testSection("GuiKit: primitives and controls");
  GameVisual visual(256u);

  // Backdrop and Shadow
  GuiKit::drawBackdrop(visual, 800.0f, 600.0f, 180);
  testTrue(g, visual.shapeCount() >= 1u, "drawBackdrop added rect");

  GuiKit::drawShadow(visual, 50.0f, 50.0f, 200.0f, 150.0f, 4.0f);
  testTrue(g, visual.shapeCount() >= 2u, "drawShadow added shadow");

  // Panel & Card
  GuiPanelChrome chrome;
  chrome.background = UiTheme::panelSurface();
  chrome.border = UiTheme::panelBorder();
  chrome.drawShadow = true;
  chrome.drawAccent = true;
  chrome.accentWidth = 4.0f;
  GuiKit::drawPanel(visual, 10.0f, 10.0f, 300.0f, 200.0f, chrome);

  GuiKit::drawCard(visual, 20.0f, 20.0f, 150.0f, 80.0f);

  // Header and Divider
  GuiKit::drawHeaderBar(visual, 0.0f, 0.0f, 300.0f, 30.0f, "Title", 13.0f);
  GuiKit::drawDivider(visual, 0.0f, 35.0f, 300.0f, false);
  GuiKit::drawDivider(visual, 150.0f, 0.0f, 200.0f, true);

  // Buttons in different states
  GuiKit::drawButton(visual,
                     10.0f,
                     100.0f,
                     80.0f,
                     28.0f,
                     "Normal",
                     12.0f,
                     GuiButtonState::Normal);
  GuiKit::drawButton(visual,
                     100.0f,
                     100.0f,
                     80.0f,
                     28.0f,
                     "Hover",
                     12.0f,
                     GuiButtonState::Hover);
  GuiKit::drawButton(visual,
                     190.0f,
                     100.0f,
                     80.0f,
                     28.0f,
                     "Pressed",
                     12.0f,
                     GuiButtonState::Pressed);
  GuiKit::drawButton(visual,
                     280.0f,
                     100.0f,
                     80.0f,
                     28.0f,
                     "Disabled",
                     12.0f,
                     GuiButtonState::Disabled);

  // Form Controls
  GuiKit::drawPropertyRow(visual,
                          10.0f,
                          140.0f,
                          280.0f,
                          24.0f,
                          "RuleSet",
                          "Game of Life",
                          12.0f,
                          true,
                          false);
  GuiKit::drawToggle(visual, 10.0f, 170.0f, 100.0f, 24.0f, true, 12.0f, false);
  GuiKit::drawSlider(
    visual, 10.0f, 200.0f, 200.0f, 24.0f, 0.5f, "50%", 12.0f, false);
  GuiKit::drawNumericStepper(
    visual, 10.0f, 230.0f, 150.0f, 24.0f, "12 TPS", 12.0f, false);

  // Menus and Toasts
  GuiKit::drawMenuBar(visual, 0.0f, 0.0f, 800.0f, 28.0f);
  GuiKit::drawMenuItem(
    visual, 10.0f, 28.0f, 120.0f, 22.0f, "Open", "Ctrl+O", 12.0f, true, false);
  GuiKit::drawStatusBar(visual, 0.0f, 580.0f, 800.0f, 20.0f, "Ready", 12.0f);
  GuiKit::drawToast(visual, 200.0f, 20.0f, 400.0f, 32.0f, "Saved!", 13.0f);

  // Tree row
  GuiKit::drawTreeRow(
    visual, 10.0f, 260.0f, 200.0f, 22.0f, 2, "Child Node", true, false);

  // Hit test helper
  testTrue(g,
           GuiKit::isPointInRect(50.0f, 50.0f, 0.0f, 0.0f, 100.0f, 100.0f),
           "isPointInRect inside test");
  testTrue(g,
           !GuiKit::isPointInRect(150.0f, 50.0f, 0.0f, 0.0f, 100.0f, 100.0f),
           "isPointInRect outside test");
}

static void
testGridAtlasCoordinates()
{
  testSection("GridAtlas: UV coordinate calculation");
  GridAtlas atlas(4, 4);
  testEqSize(g, atlas.columns(), 4u, "columns count");
  testEqSize(g, atlas.rows(), 4u, "rows count");

  // Cell (0, 0)
  TextureRegion r00 = atlas.regionForCell(0, 0);
  testTrue(g, std::abs(r00.u0 - 0.0f) < 0.001f, "u0 cell 0,0");
  testTrue(g, std::abs(r00.u1 - 0.25f) < 0.001f, "u1 cell 0,0");
  testTrue(g, std::abs(r00.v0 - 0.0f) < 0.001f, "v0 cell 0,0");
  testTrue(g, std::abs(r00.v1 - 0.25f) < 0.001f, "v1 cell 0,0");

  // Cell (2, 3)
  TextureRegion r23 = atlas.regionForCell(2, 3);
  testTrue(g, std::abs(r23.u0 - 0.50f) < 0.001f, "u0 cell 2,3");
  testTrue(g, std::abs(r23.u1 - 0.75f) < 0.001f, "u1 cell 2,3");
  testTrue(g, std::abs(r23.v0 - 0.75f) < 0.001f, "v0 cell 2,3");
  testTrue(g, std::abs(r23.v1 - 1.00f) < 0.001f, "v1 cell 2,3");

  // Flipped V
  TextureRegion rFlipped = atlas.regionForCell(0, 0, true);
  testTrue(g, std::abs(rFlipped.v0 - 0.25f) < 0.001f, "v0 flip");
  testTrue(g, std::abs(rFlipped.v1 - 0.0f) < 0.001f, "v1 flip");
}

static void
testGuiDialogModalFlow()
{
  testSection("GuiDialog: modal flow, shortcuts, and animation");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("uiScale", 1);
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);

  GuiDialog dialog(&window, &renderer);
  dialog.setTitle("Confirm Action");
  dialog.setMessage("Do you wish to proceed?");

  GuiButtonDef cancelBtn;
  cancelBtn.label = "Cancel";
  cancelBtn.actionId = 1;
  cancelBtn.isCancel = true;
  cancelBtn.shortcutKey = KeyCode::Escape;

  GuiButtonDef confirmBtn;
  confirmBtn.label = "Confirm";
  confirmBtn.actionId = 2;
  confirmBtn.isDefault = true;
  confirmBtn.shortcutKey = KeyCode::Enter;

  dialog.addButton(cancelBtn);
  dialog.addButton(confirmBtn);

  testTrue(g, !dialog.isOpen(), "Dialog should initially be closed");
  testEqSize(g, dialog.buttonCount(), 2u, "Button count");

  dialog.open();
  testTrue(g, dialog.isOpen(), "Dialog should be open after open()");
  testTrue(g,
           std::abs(dialog.animationProgress() - 0.0f) < 0.01f,
           "Initial animation progress");

  // Tick animation
  dialog.tick(0.30f);
  testTrue(g,
           std::abs(dialog.animationProgress() - 1.0f) < 0.01f,
           "Completed animation progress");

  // Selection navigation
  dialog.selectButton(0);
  testEqInt(g, dialog.selectedButton(), 0, "Selected button 0");
  dialog.selectButton(1);
  testEqInt(g, dialog.selectedButton(), 1, "Selected button 1");

  // Keyboard Enter triggers active selected button
  InputManager input(nullptr);
  input.getKeyQueue().push(
    InputManager::KeyPressEvent{ KeyCode::Enter, InputAction::Press, 0 });
  const int action = dialog.update(&input, 0.016f);
  testEqInt(g, action, 2, "Enter key should activate confirm button");

  // Click hit testing
  dialog.updateLayout();
  testEqInt(g,
            dialog.clickAt(574.0f, 408.0f),
            1,
            "clicking the rendered left button activates Cancel");
  testEqInt(g,
            dialog.clickAt(706.0f, 408.0f),
            2,
            "clicking the rendered right button activates Confirm");
  testEqInt(g,
            dialog.clickAt(100.0f, 100.0f),
            0,
            "clicking outside the panel leaves it open");

  env.setVar("uiScale", 0.5);
  dialog.rebuildVisual();
  const ShapePrimitive* backdrop = dialog.getVisual().getShape(0);
  testTrue(g,
           backdrop != nullptr && backdrop->rect.w == 2560.0f &&
             backdrop->rect.h == 1440.0f,
           "fractional UI scale keeps the default backdrop full-screen");
  testEqInt(g,
            dialog.clickAt(1214.0f, 768.0f),
            1,
            "fractional UI scale preserves default action geometry");

  dialog.close();
  testTrue(g, !dialog.isOpen(), "Dialog closed");
}

static void
testGuiMenuShellClocksAndLayout()
{
  testSection("GuiMenuShell: shared clocks, panel fit, and pointer edges");

  testTrue(g,
           GuiEasing::outCubic(0.0f) == 0.0f &&
             GuiEasing::outCubic(1.0f) == 1.0f,
           "outCubic pins both ends exactly");
  testTrue(g,
           GuiEasing::outCubic(0.5f) > 0.5f,
           "outCubic decelerates toward the target");
  testTrue(g,
           GuiEasing::outCubic(-1.0f) == 0.0f &&
             GuiEasing::outCubic(2.0f) == 1.0f,
           "outCubic clamps out-of-range progress");
  testTrue(g,
           GuiEasing::approach(0.0f, 1.0f, 18.0f, 0.0f) == 0.0f,
           "approach ignores a zero delta");
  const float approached = GuiEasing::approach(0.0f, 1.0f, 18.0f, 0.016f);
  testTrue(g,
           approached > 0.0f && approached < 1.0f,
           "approach moves partway toward the target");

  testTrue(g,
           GuiEasing::approachLinear(0.0f, 1.0f, 18.0f, 0.0f, 0.001f) == 0.0f,
           "approachLinear ignores a zero delta");
  const float slid = GuiEasing::approachLinear(0.0f, 1.0f, 18.0f, 0.016f, 0.0f);
  testTrue(g, slid > 0.0f && slid < 1.0f, "approachLinear slides partway");
  testTrue(g,
           GuiEasing::approachLinear(0.0f, 1.0f, 18.0f, 1.0f, 0.0f) == 1.0f,
           "approachLinear clamps its blend at one full step");
  testTrue(g,
           GuiEasing::approachLinear(0.9999f, 1.0f, 18.0f, 0.016f, 0.001f) ==
             1.0f,
           "approachLinear snaps once inside the epsilon");
  testTrue(g,
           GuiEasing::approachLinear(0.9999f, 1.0f, 18.0f, 0.016f, 0.0f) !=
             1.0f,
           "a zero epsilon never snaps");

  GuiMenuAnimator animator;
  animator.restart();
  testTrue(g, animator.openProgress() == 0.0f, "restart rewinds the reveal");
  animator.tick(std::numeric_limits<float>::quiet_NaN());
  animator.tick(-1.0f);
  testTrue(g,
           animator.openProgress() == 0.0f,
           "invalid frame deltas preserve animation state");
  animator.tick(0.12f);
  testTrue(g,
           animator.openProgress() > 0.0f && animator.openProgress() < 1.0f,
           "the reveal advances incrementally");
  testTrue(g,
           animator.rowReveal(4, 0) < animator.rowReveal(0, 0),
           "later rows enter behind earlier ones");
  testTrue(g,
           animator.rowReveal(4, 4) == animator.rowReveal(0, 0),
           "the first visible row carries no stagger");
  animator.tick(5.0f);
  testTrue(g, animator.openProgress() == 1.0f, "the reveal clamps at one");
  testTrue(g,
           animator.panelReveal() == 1.0f && animator.panelOffsetY() == 0.0f,
           "a settled panel sits at its resting position");

  animator.beginSelectionTravel(0.0f);
  testTrue(g,
           animator.selectionPosition(1.0f) == 0.0f,
           "selection travel starts at the row it leaves");
  animator.tick(0.07f);
  const float midway = animator.selectionPosition(1.0f);
  testTrue(
    g, midway > 0.0f && midway < 1.0f, "the highlight glides between rows");
  animator.settleSelection();
  testTrue(g,
           animator.selectionPosition(1.0f) == 1.0f,
           "settling snaps the highlight to its row");

  animator.triggerValuePulse();
  testTrue(g, animator.valuePulse() == 1.0f, "a value pulse starts at full");
  animator.tick(1.0f);
  testTrue(g, animator.valuePulse() == 0.0f, "the value pulse decays to zero");

  animator.resetCaret();
  testTrue(
    g, animator.caretVisible(), "the caret shows right after a keystroke");
  animator.tick(0.8f);
  testTrue(
    g, !animator.caretVisible(), "the caret blinks off later in the period");

  GuiMenuAnimator still;
  still.setReducedMotion(true);
  still.restart();
  still.beginSelectionTravel(0.0f);
  still.triggerValuePulse();
  still.tick(0.01f);
  testTrue(g,
           still.openProgress() == 1.0f && still.panelOffsetY() == 0.0f &&
             still.selectionPosition(3.0f) == 3.0f &&
             still.valuePulse() == 0.0f && still.rowReveal(9, 0) == 1.0f &&
             still.ambientPhase() == 0.0f && still.caretVisible(),
           "reduced motion settles every clock immediately");

  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("uiScale", 1);
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  GameVisual visual(128u);

  const GuiPanelFit fit = GuiPanelLayout::fit(&window, &renderer, &visual);
  testTrue(g,
           fit.layoutScale == 1.0f && fit.virtualWidth == 1280.0f &&
             fit.virtualHeight == 720.0f,
           "a roomy window lays out at its own pixel size");
  const GuiPanelFit missing = GuiPanelLayout::fit(nullptr, nullptr, nullptr);
  testTrue(g,
           missing.virtualWidth == 1280.0f && missing.virtualHeight == 720.0f,
           "a missing window falls back to the default design size");

  NullRenderWindow small(320, 240);
  const GuiPanelFit shrunk = GuiPanelLayout::fit(&small, &renderer, &visual);
  testTrue(g,
           shrunk.layoutScale == 0.5f && shrunk.virtualWidth == 640.0f &&
             shrunk.virtualHeight == 480.0f,
           "a window below the design size scales down uniformly");

  const GuiPanelFit docked = GuiPanelLayout::viewport(&small, &renderer);
  testTrue(g,
           docked.layoutScale == 1.0f && docked.virtualWidth == 320.0f &&
             docked.virtualHeight == 240.0f,
           "viewport anchors to the real window with no design-size floor");
  const GuiPanelFit dockedMissing = GuiPanelLayout::viewport(nullptr, nullptr);
  testTrue(g,
           dockedMissing.layoutScale == 1.0f &&
             dockedMissing.virtualWidth == 1280.0f &&
             dockedMissing.virtualHeight == 720.0f,
           "viewport falls back to the default window size");

  testEqInt(g,
            GuiPanelLayout::visibleRowCount(340.0f, 18),
            10,
            "the body area reports how many rows fit");
  testEqInt(g,
            GuiPanelLayout::visibleRowCount(0.0f, 18),
            1,
            "a collapsed body still shows one row");
  testEqInt(g,
            GuiPanelLayout::clampFirstVisibleRow(40, 18, 10),
            8,
            "the scroll offset stops at the last full page");
  testEqInt(g,
            GuiPanelLayout::clampFirstVisibleRow(3, 4, 10),
            0,
            "a list shorter than its window never scrolls");
  testEqInt(g,
            GuiPanelLayout::scrollToRow(5, 2, 4),
            2,
            "scrolling up reveals the selected row");
  testEqInt(g,
            GuiPanelLayout::scrollToRow(0, 6, 4),
            3,
            "scrolling down keeps the selected row last");
  testEqInt(g,
            GuiPanelLayout::scrollToRow(2, 3, 4),
            2,
            "an already visible row does not scroll");

  InputManager input(nullptr);
  bool scrolled = true;
  testEqInt(g,
            GuiPanelLayout::applyWheelScroll(&input, 4, 18, 10, &scrolled),
            4,
            "an idle wheel leaves the scroll offset alone");
  testTrue(g, !scrolled, "an idle wheel reports no scroll");
  double* wheel = input.getMouseScrollOffset();
  testTrue(g, wheel != nullptr, "the input manager exposes a wheel offset");
  if (wheel != nullptr) {
    *wheel = 3.0;
    testEqInt(g,
              GuiPanelLayout::applyWheelScroll(&input, 4, 18, 10, &scrolled),
              1,
              "a wheel up scrolls back by whole rows");
    testTrue(g, scrolled && *wheel == 0.0, "the wheel delta is consumed once");
    *wheel = -2.0;
    testEqInt(g,
              GuiPanelLayout::applyWheelScroll(&input, 4, 18, 10, &scrolled),
              6,
              "a wheel down scrolls forward by whole rows");
  }

  GuiPointerTracker pointer;
  pointer.reset(true);
  pointer.sample(&window, &input, 1.0f);
  testTrue(g,
           !pointer.clicked() && !pointer.pressed(),
           "a released button after opening is not a click");
  testTrue(g,
           pointer.released(),
           "letting go of the opening press reports a release edge");
  pointer.sample(&window, &input, 1.0f);
  testTrue(g, !pointer.moved(), "a stationary pointer reports no movement");
  testTrue(g,
           !pointer.released(),
           "a button that stays up reports no further release");

  pointer.reset(true);
  pointer.sample(&window, nullptr, 1.0f);
  testTrue(g,
           pointer.pressed() && !pointer.clicked() && !pointer.released(),
           "a frame without an input device invents no press edge");
  pointer.sample(&window, &input, 2.0f);
  testTrue(g,
           pointer.moved() || pointer.x() == 0.0f,
           "a changed layout scale moves the pointer in virtual space");
  pointer.forgetPosition();
  testTrue(g,
           pointer.x() == -1.0f && pointer.y() == -1.0f,
           "closing an overlay forgets the last pointer position");
}

static int
runGuiKitCase(void (*fn)())
{
  g.failures = 0;
  fn();
  return g.failures;
}

void
registerGuiKitTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.GuiKit.TextMetrics",
               []() { return runGuiKitCase(testTextMetricsAndLayout); });
  registry.add("Illumo.GuiKit.Primitives",
               []() { return runGuiKitCase(testGuiPrimitivesAndControls); });
  registry.add("Illumo.GuiKit.GridAtlas",
               []() { return runGuiKitCase(testGridAtlasCoordinates); });
  registry.add("Illumo.GuiKit.Dialog",
               []() { return runGuiKitCase(testGuiDialogModalFlow); });
  registry.add("Illumo.GuiKit.MenuShell",
               []() { return runGuiKitCase(testGuiMenuShellClocksAndLayout); });
}
