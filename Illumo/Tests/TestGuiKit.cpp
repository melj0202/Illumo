#include <Illumo/Gui/GridAtlas.h>
#include <Illumo/Gui/GuiDialog.h>
#include <Illumo/Gui/GuiKit.h>
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
  const float clickBtn0X = dialog.panelWidth() * 0.4f;
  const float clickBtnY = 720.0f * 0.5f + 40.0f;
  const int clickedAction = dialog.clickAt(clickBtn0X, clickBtnY);
  testTrue(g,
           clickedAction >= 0 && clickedAction <= 2,
           "clickAt returns valid action");

  dialog.close();
  testTrue(g, !dialog.isOpen(), "Dialog closed");
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
}
