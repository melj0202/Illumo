#include <Illumo/Gui/GridAtlas.h>
#include <Illumo/Gui/GuiDialog.h>
#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/UiScale.h>
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

static bool
alphaFadesKeepColor(const ShapePrimitive& shape)
{
  // A straight-alpha fade must keep RGB on the transparent vertices.
  for (int corner = 1; corner < 4; ++corner) {
    const ColorRgba& vertex = shape.vertexColors[static_cast<size_t>(corner)];
    if (vertex.a == 0 && (vertex.r != shape.vertexColors[0].r ||
                          vertex.g != shape.vertexColors[0].g ||
                          vertex.b != shape.vertexColors[0].b)) {
      return false;
    }
  }
  return true;
}

static void
testGuiGlassChrome()
{
  testSection("GuiKit: living-glass chrome helpers");
  const ColorRgba cyan = UiTheme::accentCool();

  GameVisual rounded(256u);
  GuiKit::drawRoundedRect(rounded, 10.0f, 20.0f, 36.0f, 16.0f, 8.0f, cyan);
  testEqSize(
    g, rounded.shapeCount(), 15u, "a rounded rect packs two wedges per quad");
  const ShapePrimitive* middle = rounded.getShape(0);
  testTrue(g,
           middle != nullptr && middle->kind == ShapeKind::FilledRect &&
             std::abs(middle->rect.w - 20.0f) < 0.01f &&
             std::abs(middle->rect.h - 16.0f) < 0.01f,
           "the center rect still leads the rounded rect");
  float minX = 1000.0f;
  float maxX = -1000.0f;
  float minY = 1000.0f;
  float maxY = -1000.0f;
  for (size_t i = 0; i < rounded.shapeCount(); ++i) {
    const Rect2& bounds = rounded.getShape(i)->rect;
    minX = std::min(minX, bounds.x);
    minY = std::min(minY, bounds.y);
    maxX = std::max(maxX, bounds.x + bounds.w);
    maxY = std::max(maxY, bounds.y + bounds.h);
  }
  testTrue(g,
           std::abs(minX - 10.0f) < 0.01f && std::abs(maxX - 46.0f) < 0.01f &&
             std::abs(minY - 20.0f) < 0.01f && std::abs(maxY - 36.0f) < 0.01f,
           "the packed corners stay inside the rect bounds");

  GameVisual bubble(256u);
  GuiKit::drawRoundedRect(bubble, 0.0f, 0.0f, 60.0f, 56.0f, 27.0f, cyan);
  testEqSize(g,
             bubble.shapeCount(),
             3u + 4u * 6u,
             "a large radius subdivides each corner finer (12 wedges)");
  bool onArc = true;
  for (size_t i = 3; i < bubble.shapeCount(); ++i) {
    const ShapePrimitive* wedge = bubble.getShape(i);
    const float rimX[3] = { wedge->x1, wedge->x2, wedge->x3 };
    const float rimY[3] = { wedge->y1, wedge->y2, wedge->y3 };
    for (int vertex = 0; vertex < 3; ++vertex) {
      const float dx = rimX[vertex] - wedge->x0;
      const float dy = rimY[vertex] - wedge->y0;
      onArc = onArc && std::abs(std::sqrt(dx * dx + dy * dy) - 27.0f) < 0.01f;
    }
  }
  testTrue(g, onArc, "every finer wedge's rim lies on its corner's arc");

  GameVisual chevron(16u);
  GuiKit::drawChevron(chevron, 50.0f, 10.0f, 5.0f, 5.0f, 2.0f, cyan);
  testEqSize(g, chevron.shapeCount(), 2u, "a chevron is two arm quads");
  const ShapePrimitive* rightArm = chevron.getShape(0);
  const ShapePrimitive* leftArm = chevron.getShape(1);
  // Each arm is (inner end, outer end, outer tip, inner tip).
  testTrue(g,
           rightArm != nullptr && leftArm != nullptr &&
             std::abs(rightArm->x2 - 50.0f) < 0.001f &&
             std::abs(rightArm->x2 - leftArm->x2) < 0.001f &&
             std::abs(rightArm->y2 - leftArm->y2) < 0.001f &&
             std::abs(rightArm->y3 - leftArm->y3) < 0.001f &&
             rightArm->y2 < 10.0f && rightArm->y3 > 10.0f,
           "both arms share one mitered tip, so the point has no notch");
  GameVisual square(16u);
  const GuiPoint2 corners[4] = {
    { 10.0f, 10.0f }, { 30.0f, 10.0f }, { 30.0f, 30.0f }, { 10.0f, 30.0f }
  };
  GuiKit::drawPolyline(square, corners, 4, 2.0f, cyan, true);
  bool joined = square.shapeCount() == 4u;
  for (std::size_t side = 0; joined && side < 4u; ++side) {
    const ShapePrimitive* current = square.getShape(side);
    const ShapePrimitive* next = square.getShape((side + 1u) % 4u);
    // A side ends (x2..x3) exactly where the next begins (x1..x0).
    joined = std::abs(current->x2 - next->x1) < 0.001f &&
             std::abs(current->y2 - next->y1) < 0.001f &&
             std::abs(current->x3 - next->x0) < 0.001f &&
             std::abs(current->y3 - next->y0) < 0.001f;
  }
  testTrue(g, joined, "a closed polyline's sides share mitered corners");
  const ShapePrimitive* top = square.getShape(0);
  const float outerCornerX = std::min(top->x0, top->x1);
  const float outerCornerY = std::min(top->y0, top->y1);
  testTrue(g,
           std::abs(outerCornerX - 9.0f) < 0.001f &&
             std::abs(outerCornerY - 9.0f) < 0.001f,
           "a square's outer corner sits half the stroke outside it");
  GameVisual open(16u);
  const GuiPoint2 line[2] = { { 0.0f, 5.0f }, { 10.0f, 5.0f } };
  GuiKit::drawPolyline(open, line, 2, 2.0f, cyan, false, true);
  testTrue(g,
           open.shapeCount() == 1u &&
             std::abs(std::min(open.getShape(0)->x0, open.getShape(0)->x1) +
                      1.0f) < 0.001f,
           "square caps extend an open stroke by half its width");

  GameVisual downChevron(16u);
  GuiKit::drawChevron(downChevron, 50.0f, 10.0f, 5.0f, -5.0f, 2.0f, cyan);
  testTrue(g,
           downChevron.shapeCount() == 2u &&
             downChevron.getShape(0)->y2 > 10.0f,
           "a negative depth points the chevron down");

  GameVisual band(256u);
  GuiKit::drawSoftShadow(
    band, 0.0f, 0.0f, 100.0f, 60.0f, 12.0f, 20.0f, 6.0f, UiTheme::glowShadow());
  testEqSize(g,
             band.shapeCount(),
             43u,
             "a soft shadow is a filled core plus one 28-quad band");
  bool fadesOut = true;
  bool keepsColor = true;
  // The core is a 15-quad rounded rect; the band follows it.
  for (size_t i = 15; i < band.shapeCount(); ++i) {
    const ShapePrimitive* shape = band.getShape(i);
    fadesOut = fadesOut && shape->kind == ShapeKind::GradientQuad &&
               shape->vertexColors[1].a == 0 && shape->vertexColors[0].a > 0;
    keepsColor = keepsColor && alphaFadesKeepColor(*shape);
  }
  testTrue(g, fadesOut, "shadow bands fade to zero alpha at their outer edge");
  testTrue(g, keepsColor, "shadow fades keep their color channels");

  GameVisual glow(256u);
  GuiKit::drawSoftGlow(glow, 50.0f, 50.0f, 40.0f, 30.0f, cyan, 20);
  testTrue(g,
           glow.shapeCount() > 0u && glow.shapeCount() <= 64u,
           "a soft glow stays within its quad budget");
  bool glowKeepsColor = true;
  for (size_t i = 0; i < glow.shapeCount(); ++i) {
    glowKeepsColor = glowKeepsColor && alphaFadesKeepColor(*glow.getShape(i));
  }
  testTrue(g, glowKeepsColor, "glow rims fade without darkening");

  GameVisual panel(4096u);
  GuiGlassStyle style;
  style.ambientPhase = 3.0f;
  GuiKit::drawGlassPanel(panel, 20.0f, 20.0f, 400.0f, 300.0f, style);
  testTrue(g,
           panel.shapeCount() > 0u && panel.shapeCount() <= 160u,
           "a glass panel fits a modest primitive budget");
  GameVisual roundedPanel(4096u);
  GuiKit::drawRoundedPanel(roundedPanel, 20.0f, 20.0f, 400.0f, 300.0f);
  testEqSize(g,
             roundedPanel.shapeCount() + 2u,
             panel.shapeCount(),
             "the rounded panel is the glass panel before its glint travels");
  GameVisual tiltedPanel(4096u);
  GuiGlassStyle tiltedStyle = style;
  tiltedStyle.tiltX = 0.8f;
  tiltedStyle.tiltY = -0.5f;
  GuiKit::drawGlassPanel(
    tiltedPanel, 20.0f, 20.0f, 400.0f, 300.0f, tiltedStyle);
  const ShapePrimitive* levelShadow = panel.getShape(0);
  const ShapePrimitive* tiltedShadow = tiltedPanel.getShape(0);
  testTrue(g,
           tiltedPanel.shapeCount() > panel.shapeCount() &&
             tiltedShadow->rect.x < levelShadow->rect.x &&
             tiltedShadow->rect.y > levelShadow->rect.y,
           "a tilted pane adds glare and edge light and throws its shadow "
           "away from the pointer");

  GameVisual keys(256u);
  const float advance =
    GuiKit::drawKeyHint(keys, 10.0f, 10.0f, "UPDOWN", "Select", 10.0f);
  testTrue(g,
           advance > 20.0f &&
             std::abs(advance - GuiKit::measureKeyHint(
                                  "UPDOWN", "Select", 10.0f)) < 0.01f,
           "key hints advance by their measured width");
  testEqSize(g, keys.textCount(), 1u, "arrow keycaps draw glyphs, not text");
  GameVisual enter(256u);
  GuiKit::drawKeycap(enter, 0.0f, 0.0f, "ENTER", 10.0f);
  testEqSize(g, enter.textCount(), 1u, "word keycaps draw their label");

  GameVisual sheen(64u);
  GuiKit::drawSheen(sheen, 0.0f, 0.0f, 200.0f, 40.0f, 10.0f, 0.5f, cyan);
  testTrue(g, sheen.shapeCount() > 0u, "a mid-sweep sheen draws");
  bool sheenInside = true;
  for (size_t i = 0; i < sheen.shapeCount(); ++i) {
    const Rect2& bounds = sheen.getShape(i)->rect;
    sheenInside = sheenInside && bounds.x >= 10.0f - 0.01f &&
                  bounds.x + bounds.w <= 190.0f + 0.01f;
  }
  testTrue(g, sheenInside, "the sheen stays inside its inset");
  GameVisual idle(64u);
  GuiKit::drawSheen(idle, 0.0f, 0.0f, 200.0f, 40.0f, 10.0f, -1.0f, cyan);
  testEqSize(g, idle.shapeCount(), 0u, "an idle sheen draws nothing");

  // Liquid selection: a rounded rect at rest; stretched, it necks in the
  // middle and tapers toward its tail while the head keeps full width.
  GuiLiquidSelection drop;
  drop.crossStart = 10.0f;
  drop.crossSize = 200.0f;
  drop.headStart = 20.0f;
  drop.tailStart = 20.0f;
  drop.cellLength = 40.0f;
  drop.radius = 8.0f;
  drop.rim = cyan;
  drop.faceTop = UiTheme::selectionTop();
  drop.faceBottom = UiTheme::selectionBottom();
  GameVisual resting(512u);
  GuiKit::drawLiquidSelection(resting, drop);
  bool restInside = resting.shapeCount() > 0u;
  for (size_t i = 0; i < resting.shapeCount(); ++i) {
    const ShapePrimitive* shape = resting.getShape(i);
    const float xs[4] = { shape->x0, shape->x1, shape->x2, shape->x3 };
    const float ys[4] = { shape->y0, shape->y1, shape->y2, shape->y3 };
    for (int corner = 0; corner < 4; ++corner) {
      restInside = restInside && xs[corner] >= 10.0f - 0.01f &&
                   xs[corner] <= 210.0f + 0.01f &&
                   ys[corner] >= 20.0f - 0.01f && ys[corner] <= 60.0f + 0.01f;
    }
  }
  testTrue(g, restInside, "a resting drop fills exactly its cell");

  drop.headStart = 100.0f;
  GameVisual stretched(512u);
  GuiKit::drawLiquidSelection(stretched, drop);
  // The rim is drawn first; its slices report the width at each level.
  const size_t rimSlices = stretched.shapeCount() / 2u;
  float headWidth = 0.0f;
  float neckWidth = 1000.0f;
  float tailWidth = 1000.0f;
  for (size_t i = 0; i < rimSlices; ++i) {
    const ShapePrimitive* shape = stretched.getShape(i);
    const float width = shape->x1 - shape->x0;
    if (shape->y0 >= 110.0f && shape->y0 <= 130.0f) {
      headWidth = std::max(headWidth, width);
    } else if (shape->y0 >= 60.0f && shape->y0 <= 100.0f) {
      neckWidth = std::min(neckWidth, width);
    } else if (shape->y0 >= 28.0f && shape->y0 <= 50.0f) {
      tailWidth = std::min(tailWidth, width);
    }
  }
  testTrue(g,
           std::abs(headWidth - 200.0f) < 0.01f && neckWidth < 185.0f &&
             tailWidth < headWidth,
           "a stretched drop necks and tapers behind a full-width head");

  drop.horizontal = true;
  drop.crossStart = 30.0f;
  drop.crossSize = 50.0f;
  drop.headStart = 10.0f;
  drop.tailStart = 130.0f;
  drop.cellLength = 100.0f;
  drop.squash = 0.8f;
  drop.glow = cyan;
  drop.sheen = 0.5f;
  drop.sheenColor = cyan;
  GameVisual sideways(512u);
  GuiKit::drawLiquidSelection(sideways, drop);
  bool sidewaysFinite = sideways.shapeCount() > 0u;
  for (size_t i = 0; i < sideways.shapeCount(); ++i) {
    const ShapePrimitive* shape = sideways.getShape(i);
    sidewaysFinite = sidewaysFinite && std::isfinite(shape->x0) &&
                     std::isfinite(shape->y2) && shape->y0 > 0.0f &&
                     shape->y2 < 110.0f;
  }
  testTrue(g,
           sidewaysFinite,
           "a squashed horizontal drop with glow and sheen stays near its row");

  GameVisual splash(256u);
  GuiKit::drawSplash(splash, 50.0f, 50.0f, 0.4f, 1.0f, cyan);
  testTrue(g, splash.shapeCount() > 0u, "a splash in flight draws");
  GameVisual splashDone(64u);
  GuiKit::drawSplash(splashDone, 50.0f, 50.0f, 1.0f, 1.0f, cyan);
  GuiKit::drawSplash(splashDone, 50.0f, 50.0f, 0.0f, 1.0f, cyan);
  testEqSize(g,
             splashDone.shapeCount(),
             0u,
             "a splash outside its flight draws nothing");

  GameVisual degenerate(64u);
  const float nan = std::numeric_limits<float>::quiet_NaN();
  drop.crossStart = nan;
  GuiKit::drawLiquidSelection(degenerate, drop);
  drop.crossStart = 0.0f;
  drop.cellLength = 0.0f;
  GuiKit::drawLiquidSelection(degenerate, drop);
  GuiKit::drawRoundedGradientRect(
    degenerate, nan, 0.0f, 10.0f, 10.0f, 2.0f, cyan, cyan);
  GuiKit::drawRoundedBand(
    degenerate, 0.0f, 0.0f, 0.0f, 10.0f, 2.0f, 0.0f, 4.0f, cyan, cyan);
  GuiKit::drawSoftGlow(degenerate, 0.0f, 0.0f, nan, 10.0f, cyan);
  GuiKit::drawGlassPanel(degenerate, 0.0f, 0.0f, -5.0f, 10.0f, style);
  testEqSize(
    g, degenerate.shapeCount(), 0u, "degenerate chrome input draws nothing");
}

static void
testGuiMotionVocabulary()
{
  testSection("GuiMenuShell: springs, stretching selection, press pulse");
  testTrue(g,
           GuiEasing::outBack(0.0f) == 0.0f &&
             std::abs(GuiEasing::outBack(1.0f) - 1.0f) < 0.0001f,
           "outBack starts at zero and lands on one");
  float peak = 0.0f;
  for (int step = 0; step <= 100; ++step) {
    peak =
      std::max(peak, GuiEasing::outBack(static_cast<float>(step) / 100.0f));
  }
  testTrue(g, peak > 1.02f && peak < 1.1f, "outBack overshoots gently");
  testTrue(g,
           GuiEasing::inOutCubic(0.0f) == 0.0f &&
             GuiEasing::inOutCubic(1.0f) == 1.0f &&
             std::abs(GuiEasing::inOutCubic(0.5f) - 0.5f) < 0.0001f,
           "inOutCubic is symmetric");

  GuiSpring spring;
  spring.configure(3.0f, 0.5f);
  spring.setTarget(1.0f);
  float highest = 0.0f;
  for (int frame = 0; frame < 120; ++frame) {
    spring.tick(1.0f / 60.0f, false);
    highest = std::max(highest, spring.value());
  }
  testTrue(g, highest > 1.0f, "an underdamped spring overshoots");
  testTrue(g,
           spring.settled() && spring.value() == 1.0f,
           "the spring snaps exactly onto its target at rest");
  spring.setTarget(0.0f);
  spring.tick(std::numeric_limits<float>::quiet_NaN(), false);
  testTrue(g, spring.value() == 1.0f, "a non-finite step is ignored");
  spring.tick(0.25f, false);
  const float afterLongStep = spring.value();
  testTrue(g,
           std::isfinite(afterLongStep) && afterLongStep < 1.0f,
           "a long step stays finite and moves toward the target");
  spring.tick(5.0f, true);
  testTrue(g,
           spring.value() == 0.0f && spring.settled(),
           "reduced motion snaps the spring");

  GuiSpring critical;
  critical.configure(4.0f, 1.0f);
  critical.setTarget(1.0f);
  float criticalPeak = 0.0f;
  for (int frame = 0; frame < 240; ++frame) {
    critical.tick(1.0f / 120.0f, false);
    criticalPeak = std::max(criticalPeak, critical.value());
  }
  testTrue(
    g, criticalPeak <= 1.0f, "a critically damped spring never overshoots");

  GuiSpringArray bank;
  bank.configure(3.0f, 0.7f);
  bank.focusOnly(2, 4);
  bank.tick(0.05f, false);
  testTrue(g,
           bank.value(2) > 0.0f && bank.value(1) == 0.0f,
           "focus lifts only the focused row");
  testTrue(g,
           bank.value(-1) == 0.0f &&
             bank.value(GuiSpringArray::kCapacity) == 0.0f,
           "out-of-range rows read as zero");
  bank.snapAll();
  testTrue(g, bank.value(2) == 1.0f, "snapAll settles every row");

  testTrue(g,
           GuiEasing::springStep(0.0f, 3.0f, 0.5f) == 0.0f &&
             GuiEasing::springStep(-1.0f, 3.0f, 0.5f) == 0.0f,
           "a spring step rests at zero before release");
  float stepPeak = 0.0f;
  for (int frame = 1; frame <= 120; ++frame) {
    stepPeak = std::max(
      stepPeak,
      GuiEasing::springStep(static_cast<float>(frame) / 120.0f, 3.0f, 0.5f));
  }
  testTrue(g,
           stepPeak > 1.05f && stepPeak < 1.3f,
           "an underdamped spring step bounces past one");
  testTrue(g,
           GuiEasing::springStep(3.0f, 3.0f, 0.5f) == 1.0f &&
             GuiEasing::springStep(3.0f, 3.0f, 1.0f) == 1.0f,
           "a spring step lands exactly on one");
  testTrue(g,
           GuiEasing::wobble(0.0f, 3.4f, 0.3f) == 1.0f &&
             GuiEasing::wobble(-0.1f, 3.4f, 0.3f) == 0.0f &&
             GuiEasing::wobble(3.0f, 3.4f, 0.3f) == 0.0f,
           "a wobble starts at one and rings out to zero");
  float wobbleLow = 0.0f;
  for (int frame = 0; frame < 60; ++frame) {
    wobbleLow = std::min(
      wobbleLow,
      GuiEasing::wobble(static_cast<float>(frame) / 120.0f, 3.4f, 0.3f));
  }
  testTrue(g, wobbleLow < -0.2f, "a wobble swings through the opposite sign");

  GuiSpring tuned;
  tuned.configure(GuiMotion::kJelly);
  tuned.setTarget(1.0f);
  float jellyPeak = 0.0f;
  for (int frame = 0; frame < 120; ++frame) {
    tuned.tick(1.0f / 60.0f, false);
    jellyPeak = std::max(jellyPeak, tuned.value());
  }
  testTrue(g,
           jellyPeak > 1.1f && tuned.settled(),
           "the jelly preset overshoots visibly, then rests");

  GuiPanelTilt tilt;
  tilt.aim(620.0f, 60.0f, 640.0f, 480.0f, true);
  for (int frame = 0; frame < 120; ++frame) {
    tilt.tick(1.0f / 60.0f, false);
  }
  testTrue(g,
           tilt.x() > 0.9f && tilt.y() < -0.7f,
           "a panel tilts toward the pointer's corner");
  testTrue(g,
           std::abs(tilt.bodyShiftX() - tilt.x() * GuiPanelTilt::kBodyDepth) <
               0.0001f &&
             std::abs(tilt.layerX(GuiPanelTilt::kGlassDepth) -
                      (tilt.shiftX(GuiPanelTilt::kGlassDepth) -
                       tilt.bodyShiftX())) < 0.0001f &&
             tilt.layerX(GuiPanelTilt::kHeaderDepth) > 0.0f &&
             tilt.layerX(GuiPanelTilt::kGlassDepth) < 0.0f,
           "layers nearer than the body swing further, the glass less");
  GuiGlassStyle tiltedGlass;
  tilt.applyTo(tiltedGlass);
  testTrue(g,
           tiltedGlass.tiltX > 0.9f && tiltedGlass.tiltX <= 1.0f,
           "the glass takes the tilt for its shadow and glare");
  tilt.aim(-1.0f, -1.0f, 640.0f, 480.0f, true);
  for (int frame = 0; frame < 240; ++frame) {
    tilt.tick(1.0f / 60.0f, false);
  }
  testTrue(g,
           tilt.x() == 0.0f && tilt.y() == 0.0f,
           "a panel with no pointer swings back exactly level");
  tilt.aim(620.0f, 60.0f, 640.0f, 480.0f, false);
  tilt.tick(0.1f, false);
  testTrue(g, tilt.x() == 0.0f, "an inactive panel stays level");
  tilt.aim(620.0f, 60.0f, 640.0f, 480.0f, true);
  tilt.tick(0.1f, true);
  testTrue(g,
           tilt.x() == 0.0f && tilt.y() == 0.0f,
           "reduced motion keeps the panel level");

  GuiMenuAnimator animator;
  animator.restart();
  testTrue(g,
           animator.selectionSheen() < 0.0f && animator.pressPulse() == 0.0f &&
             animator.pressWobble() == 0.0f && animator.valueWobble() == 0.0f,
           "a fresh menu has no sheen, press or wobble");
  animator.beginSelectionTravel(0.0f, 2.0f);
  const GuiSelectionSpan leaving = animator.selectionSpan(2.0f);
  testTrue(g,
           leaving.leading == 0.0f && leaving.trailing == 0.0f,
           "a travel from rest starts on the row it leaves");
  animator.tick(0.05f);
  const GuiSelectionSpan moving = animator.selectionSpan(2.0f);
  testTrue(g,
           moving.leading > moving.trailing && moving.trailing > 0.0f &&
             moving.squash < 0.0f,
           "the head races ahead and the drop pulls thin");
  float overshoot = 0.0f;
  float bulge = 0.0f;
  for (int frame = 0; frame < 30; ++frame) {
    animator.tick(0.01f);
    const GuiSelectionSpan frameSpan = animator.selectionSpan(2.0f);
    overshoot = std::max(overshoot, frameSpan.leading);
    bulge = std::max(bulge, frameSpan.squash);
  }
  testTrue(g, overshoot > 2.2f, "the head overshoots its row");
  testTrue(g, bulge > 0.0f, "the drop bulges as its tail catches up");
  testTrue(g,
           animator.selectionSheen() >= 0.0f,
           "a sheen sweeps once the highlight arrives");
  animator.tick(2.0f);
  const GuiSelectionSpan settled = animator.selectionSpan(2.0f);
  testTrue(g,
           settled.leading == 2.0f && settled.trailing == 2.0f &&
             settled.squash == 0.0f && animator.selectionSheen() < 0.0f,
           "the span settles exactly on its row");

  animator.beginSelectionTravel(2.0f, 12.0f);
  float longPeak = 0.0f;
  for (int frame = 0; frame < 60; ++frame) {
    animator.tick(0.01f);
    longPeak = std::max(longPeak, animator.selectionSpan(12.0f).leading);
  }
  testTrue(g,
           longPeak > 12.0f &&
             longPeak <= 12.0f + GuiMenuAnimator::kSelectionOvershootRows,
           "a long jump splashes against its row within the soft limit");
  animator.tick(2.0f);

  animator.beginSelectionTravel(12.0f, 0.0f);
  animator.tick(0.04f);
  const float headBefore = animator.selectionSpan(0.0f).leading;
  animator.beginSelectionTravel(0.0f, 3.0f);
  const float headAfter = animator.selectionSpan(3.0f).leading;
  testTrue(g,
           headBefore < 12.0f && headAfter == headBefore,
           "redirecting mid-flight keeps the drop where it is");
  testTrue(g,
           animator.selectionSpan(1.0f).leading == 1.0f,
           "a row the drop is not travelling to reads as itself");
  animator.tick(2.0f);

  animator.triggerPress();
  testTrue(g,
           animator.pressPulse() == 1.0f && animator.pressProgress() == 0.0f &&
             animator.pressWobble() == 1.0f,
           "a press starts at full strength, fully squashed");
  float pressLow = 0.0f;
  for (int frame = 0; frame < 40; ++frame) {
    animator.tick(0.01f);
    pressLow = std::min(pressLow, animator.pressWobble());
  }
  testTrue(g, pressLow < -0.1f, "the pressed drop rebounds into a stretch");
  animator.tick(1.0f);
  testTrue(g,
           animator.pressPulse() == 0.0f && animator.pressProgress() < 0.0f &&
             animator.pressWobble() == 0.0f,
           "the press decays away");
  animator.triggerValuePulse(-3);
  testEqInt(g, animator.valuePulseDirection(), -1, "value pulses keep a sign");
  testTrue(g, animator.valueWobble() == 0.0f, "a value nudge starts from rest");
  float nudgePeak = 0.0f;
  for (int frame = 0; frame < 20; ++frame) {
    animator.tick(0.005f);
    nudgePeak = std::min(nudgePeak, animator.valueWobble());
  }
  testTrue(g,
           nudgePeak < -0.9f && nudgePeak >= -1.001f,
           "a value nudge leaps the way the value moved");
  animator.tick(1.0f);
  testTrue(g, animator.valueWobble() == 0.0f, "the value nudge rings out");

  GuiMenuAnimator still;
  still.setReducedMotion(true);
  still.restart();
  still.beginSelectionTravel(0.0f, 3.0f);
  still.triggerPress();
  still.triggerValuePulse(1);
  still.tick(0.01f);
  const GuiSelectionSpan snapped = still.selectionSpan(3.0f);
  testTrue(g,
           snapped.leading == 3.0f && snapped.trailing == 3.0f &&
             snapped.squash == 0.0f && still.selectionSheen() < 0.0f &&
             still.pressPulse() == 0.0f && still.pressWobble() == 0.0f &&
             still.valueWobble() == 0.0f && still.rowDrop(5, 0) == 0.0f,
           "reduced motion snaps span, sheen, press and wobbles");
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
  testTrue(g,
           animator.rowDrop(4, 0) > animator.rowDrop(0, 0) &&
             animator.rowDrop(4, 4) == animator.rowDrop(0, 0),
           "later rows are still falling behind earlier ones");
  float panelLow = 0.0f;
  float rowLow = 0.0f;
  for (int frame = 0; frame < 60; ++frame) {
    animator.tick(0.02f);
    panelLow = std::min(panelLow, animator.panelOffsetY());
    rowLow = std::min(rowLow, animator.rowDrop(0, 0));
  }
  testTrue(g,
           panelLow < 0.0f && rowLow < 0.0f,
           "the panel and rows bounce just past their resting places");
  animator.tick(5.0f);
  testTrue(g, animator.openProgress() == 1.0f, "the reveal clamps at one");
  testTrue(g,
           animator.panelReveal() == 1.0f && animator.panelOffsetY() == 0.0f &&
             animator.rowDrop(12, 0) == 0.0f,
           "a settled panel sits at its resting position");

  animator.beginSelectionTravel(0.0f, 1.0f);
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
  still.beginSelectionTravel(0.0f, 3.0f);
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

static EnvVar
scaleSetting(EnvVars& env, const std::string& text)
{
  env.setVar("uiScale", text);
  return env.getVar("uiScale");
}

static void
testUiScaleSetting()
{
  testSection("UiScale: fractional and automatic interface scale");
  EnvVars env;
  const EnvVar automatic = scaleSetting(env, "auto");
  testTrue(g,
           UiScale::isAutomatic(automatic) &&
             UiScale::isAutomatic(scaleSetting(env, "AUTO")) &&
             UiScale::isAutomatic(scaleSetting(env, "0")),
           "\"auto\" and 0 select automatic scale");
  testTrue(g,
           !UiScale::isAutomatic(scaleSetting(env, "1.5")) &&
             !UiScale::isAutomatic(EnvVar{}),
           "a factor or an unset setting is not automatic");
  testTrue(g,
           UiScale::resolve(automatic, 1280, 720) == 1.0f &&
             UiScale::resolve(automatic, 1600, 900) == 1.25f &&
             UiScale::resolve(automatic, 1920, 1080) == 1.5f &&
             UiScale::resolve(automatic, 2560, 1440) == 2.0f &&
             UiScale::resolve(automatic, 3840, 2160) == 3.0f,
           "automatic scale steps by a quarter with the window");
  testTrue(g,
           UiScale::resolve(automatic, 3440, 1440) == 2.0f &&
             UiScale::resolve(automatic, 1920, 600) == 1.0f,
           "automatic scale follows the tighter axis");
  testTrue(g,
           UiScale::resolve(automatic, 640, 480) == 1.0f &&
             UiScale::resolve(automatic, 0, 0) == 1.0f &&
             UiScale::resolve(automatic, 7680, 4320) == 4.0f,
           "automatic scale stays within 1x to 4x");
  testTrue(g,
           UiScale::resolve(scaleSetting(env, "1.25"), 3840, 2160) == 1.25f &&
             UiScale::resolve(scaleSetting(env, "0.5"), 3840, 2160) == 0.5f &&
             UiScale::resolve(EnvVar{}, 3840, 2160) == 1.0f &&
             UiScale::resolve(scaleSetting(env, "junk"), 3840, 2160) == 1.0f,
           "explicit factors pass through; unset or junk reads as 1x");
  testTrue(g,
           UiScale::stored(automatic) == 0.0f &&
             UiScale::stored(scaleSetting(env, "1.75")) == 1.75f &&
             UiScale::text(0.0f) == "auto" && UiScale::text(1.25f) == "1.25" &&
             UiScale::text(2.0f) == "2" && UiScale::text(1.5f) == "1.5",
           "preferences round trip through their persisted text");

  // The renderer re-resolves automatic scale for each frame's window.
  NullRenderWindow window(1280, 720);
  env.setVar("uiScale", "auto");
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  testTrue(g,
           renderer.getUiScale() == 1.0f,
           "automatic scale is 1x at the reference window");
  window.width = 1920;
  window.height = 1080;
  testTrue(g,
           renderer.getUiScale() == 1.5f,
           "resizing the window changes the automatic scale");
  renderer.BeginFrame();
  const float framed = renderer.getUiScale();
  renderer.EndFrame();
  testTrue(g, framed == 1.5f, "a frame uses the scale for its window size");
  env.setVar("uiScale", "1.75");
  testTrue(
    g, renderer.getUiScale() == 1.75f, "a fractional factor applies as set");
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
  registry.add("Illumo.GuiKit.MotionVocabulary",
               []() { return runGuiKitCase(testGuiMotionVocabulary); });
  registry.add("Illumo.GuiKit.GlassChrome",
               []() { return runGuiKitCase(testGuiGlassChrome); });
  registry.add("Illumo.GuiKit.UiScale",
               []() { return runGuiKitCase(testUiScaleSetting); });
}
