#include "MeshViewerUi.h"

#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>

MeshViewerUi::MeshViewerUi(IRenderWindow* window, Renderer* renderer)
  : m_window(window)
  , m_renderer(renderer)
  , m_visual(2048u)
  , m_fontSize(kDefaultFontSize)
  , m_mouseWasDown(false)
  , m_consumedPress(false)
  , m_showGrid(true)
  , m_showWireframe(false)
  , m_showAxes(true)
  , m_yawDeg(45.0f)
  , m_pitchDeg(25.0f)
  , m_distance(3.5f)
  , m_toastColor{ 60, 220, 120, 255 }
  , m_toastTimer(0.0f)
  , m_hoveredButton(-1)
{
  m_visual.setSpace(PrimitiveSpace::Pixels);
  m_visual.setLayerHint(RenderLayerId::UI);
  m_visual.setWindow(window);
  m_visual.setRenderer(renderer);
}

void
MeshViewerUi::setMeshMetadata(const MeshMetadata& metadata)
{
  m_metadata = metadata;
}

void
MeshViewerUi::setDisplayOptions(bool showGrid,
                                bool showWireframe,
                                bool showAxes)
{
  m_showGrid = showGrid;
  m_showWireframe = showWireframe;
  m_showAxes = showAxes;
}

void
MeshViewerUi::setCameraInfo(float yawDegrees,
                            float pitchDegrees,
                            float distance)
{
  m_yawDeg = yawDegrees;
  m_pitchDeg = pitchDegrees;
  m_distance = distance;
}

void
MeshViewerUi::showToast(const std::string& message, ColorRgba color)
{
  m_toastMessage = message;
  m_toastColor = color;
  m_toastTimer = 3.5f;
}

bool
MeshViewerUi::containsScreenPoint(float x, float y) const
{
  if (m_window == nullptr) {
    return false;
  }
  const std::array<int, 2> dimensions = m_window->getWindowDimensions();
  const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
  const float virtualWidth =
    static_cast<float>(dimensions[0]) / (scale > 0.0f ? scale : 1.0f);
  const float virtualHeight =
    static_cast<float>(dimensions[1]) / (scale > 0.0f ? scale : 1.0f);

  const float fontScale = m_fontSize / kDefaultFontSize;

  // Top header bar
  if (y <= kHeaderHeight) {
    return true;
  }
  // Bottom status bar
  if (y >= virtualHeight - kStatusHeight) {
    return true;
  }
  // Info card area (top-left below header)
  if (m_metadata.hasMesh && x <= 260.0f * fontScale &&
      y <= kHeaderHeight + 170.0f * fontScale) {
    return true;
  }
  // Empty state card area
  const bool isCompact = virtualWidth < 800.0f;
  const bool isVeryCompact = virtualWidth < 580.0f;
  const std::string emptyHint =
    isVeryCompact
      ? "Press [O] or 'Open' to load .obj"
      : (isCompact
           ? "Press [O] or click 'Open' to view .obj"
           : "Press [O] or click 'Open Mesh' to view a Wavefront (.obj) file");

  const float emptyW =
    std::min(virtualWidth - 24.0f,
             std::max(300.0f * fontScale,
                      GuiKit::estimateTextWidth(emptyHint, m_fontSize) +
                        40.0f * fontScale));
  const float emptyH = 76.0f * fontScale;
  const float emptyX = (virtualWidth - emptyW) * 0.5f;
  const float emptyY = (virtualHeight - emptyH) * 0.45f;
  if (!m_metadata.hasMesh && x >= emptyX && x <= emptyX + emptyW &&
      y >= emptyY && y <= emptyY + emptyH) {
    return true;
  }

  return false;
}

MeshViewerAction
MeshViewerUi::update(InputManager* inputManager, float dt)
{
  m_consumedPress = false;
  if (m_toastTimer > 0.0f) {
    m_toastTimer = std::max(0.0f, m_toastTimer - dt);
  }

  if (m_window == nullptr) {
    return MeshViewerAction::None;
  }

  const std::array<int, 2> dimensions = m_window->getWindowDimensions();
  const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
  const float virtualWidth =
    static_cast<float>(dimensions[0]) / (scale > 0.0f ? scale : 1.0f);
  const float virtualHeight =
    static_cast<float>(dimensions[1]) / (scale > 0.0f ? scale : 1.0f);

  float mouseX = 0.0f;
  float mouseY = 0.0f;
  bool isDown = false;
  if (inputManager != nullptr) {
    const std::array<double, 2> coords = m_window->getMouseCoords();
    mouseX = static_cast<float>(coords[0]) / (scale > 0.0f ? scale : 1.0f);
    mouseY = static_cast<float>(coords[1]) / (scale > 0.0f ? scale : 1.0f);
    isDown = inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
  }

  // Update button hover and clicks
  m_hoveredButton = -1;
  MeshViewerAction triggeredAction = MeshViewerAction::None;

  for (size_t i = 0; i < m_buttonDefs.size(); ++i) {
    const UiButton& btn = m_buttonDefs[i];
    if (GuiKit::isPointInRect(
          mouseX, mouseY, btn.x, btn.y, btn.width, btn.height)) {
      m_hoveredButton = static_cast<int>(i);
      if (isDown && !m_mouseWasDown) {
        m_consumedPress = true;
        triggeredAction = btn.action;
      }
      break;
    }
  }

  if (isDown && !m_mouseWasDown && containsScreenPoint(mouseX, mouseY)) {
    m_consumedPress = true;
  }

  m_mouseWasDown = isDown;

  rebuildVisual(virtualWidth, virtualHeight);
  return triggeredAction;
}

void
MeshViewerUi::clickAtForTesting(float x, float y)
{
  for (const UiButton& btn : m_buttonDefs) {
    if (GuiKit::isPointInRect(x, y, btn.x, btn.y, btn.width, btn.height)) {
      m_consumedPress = true;
      break;
    }
  }
}

void
MeshViewerUi::rebuildVisual(float virtualWidth, float virtualHeight)
{
  m_visual.clearPrimitives();
  m_buttonDefs.clear();

  // 1. Top Header Bar
  GuiPanelChrome headerChrome;
  headerChrome.background = ColorRgba{ 14, 20, 30, 255 };
  headerChrome.border = ColorRgba{ 38, 54, 76, 255 };
  headerChrome.drawShadow = false;
  GuiKit::drawPanel(
    m_visual, 0.0f, 0.0f, virtualWidth, kHeaderHeight, headerChrome);
  GuiKit::drawDivider(
    m_visual, 0.0f, kHeaderHeight - 1.0f, virtualWidth, false);

  // App Title
  const std::string title = "IllMeshViewer";
  GuiKit::drawTextAligned(m_visual,
                          title,
                          14.0f,
                          8.0f,
                          160.0f,
                          m_fontSize + 2.0f,
                          UiTheme::accent(),
                          GuiAlignment::Left);

  // Header Buttons
  const float fontScale = m_fontSize / kDefaultFontSize;
  const float titleWidth =
    GuiKit::estimateTextWidth(title, m_fontSize + 2.0f) + 24.0f * fontScale;
  float btnX = std::max(180.0f, titleWidth);
  const float btnY = 5.0f;
  const float btnH = 26.0f;

  auto addButton = [&](const std::string& label,
                       MeshViewerAction action,
                       float minWidth,
                       bool active = false) {
    const size_t index = m_buttonDefs.size();
    const bool hovered = (static_cast<int>(index) == m_hoveredButton);
    GuiButtonState state = GuiButtonState::Normal;
    if (active) {
      state = GuiButtonState::Pressed;
    } else if (hovered) {
      state = GuiButtonState::Hover;
    }
    const float textW = GuiKit::estimateTextWidth(label, m_fontSize);
    const float width =
      std::max(minWidth * fontScale, textW + 20.0f * fontScale);
    GuiKit::drawButton(m_visual,
                       btnX,
                       btnY,
                       width,
                       btnH,
                       label,
                       m_fontSize,
                       state,
                       UiTheme::accent());
    m_buttonDefs.push_back({ label, action, btnX, btnY, width, btnH });
    btnX += width + 8.0f;
  };

  // Header Buttons: calculate compact vs full labels based on virtualWidth
  const bool isCompact = virtualWidth < 800.0f;
  const bool isVeryCompact = virtualWidth < 580.0f;

  const std::string openLabel =
    isVeryCompact ? "Open" : (isCompact ? "Open [O]" : "Open Mesh... [O]");
  const std::string resetLabel =
    isVeryCompact ? "Reset" : (isCompact ? "Reset [R]" : "Reset Camera [R/F]");
  const std::string gridLabel =
    isVeryCompact ? (m_showGrid ? "G:ON" : "G:OFF")
                  : (std::string("Grid: ") + (m_showGrid ? "ON" : "OFF"));
  const std::string wireLabel =
    isVeryCompact ? (m_showWireframe ? "W:ON" : "W:OFF")
                  : (std::string("Wire: ") + (m_showWireframe ? "ON" : "OFF"));
  const std::string axesLabel =
    isVeryCompact ? (m_showAxes ? "A:ON" : "A:OFF")
                  : (std::string("Axes: ") + (m_showAxes ? "ON" : "OFF"));

  addButton(openLabel,
            MeshViewerAction::OpenMesh,
            isVeryCompact ? 50.0f : (isCompact ? 80.0f : 135.0f));
  addButton(resetLabel,
            MeshViewerAction::ResetView,
            isVeryCompact ? 55.0f : (isCompact ? 85.0f : 145.0f));
  addButton(gridLabel,
            MeshViewerAction::ToggleGrid,
            isVeryCompact ? 50.0f : 80.0f,
            m_showGrid);
  addButton(wireLabel,
            MeshViewerAction::ToggleWireframe,
            isVeryCompact ? 50.0f : 80.0f,
            m_showWireframe);
  addButton(axesLabel,
            MeshViewerAction::ToggleAxes,
            isVeryCompact ? 50.0f : 80.0f,
            m_showAxes);

  // 2. Info Card / HUD (Top-Left under header)
  if (m_metadata.hasMesh) {
    const float cardX = 12.0f * fontScale;
    const float cardY = kHeaderHeight + 12.0f;
    const float cardW = 250.0f * fontScale;
    const float cardH = 154.0f * fontScale;
    const float headerH = 26.0f * fontScale;

    GuiKit::drawShadow(m_visual, cardX, cardY, cardW, cardH);
    GuiKit::drawCard(m_visual, cardX, cardY, cardW, cardH);
    GuiKit::drawHeaderBar(m_visual,
                          cardX,
                          cardY,
                          cardW,
                          headerH,
                          "Mesh Info",
                          m_fontSize,
                          UiTheme::panelRaised(),
                          UiTheme::textPrimary(),
                          UiTheme::accent());

    float lineY = cardY + headerH + 8.0f * fontScale;
    const float lineSpacing = 20.0f * fontScale;

    GuiKit::drawLabelValue(m_visual,
                           "File:",
                           m_metadata.filename,
                           cardX + 10.0f * fontScale,
                           lineY,
                           cardW - 20.0f * fontScale,
                           m_fontSize);
    lineY += lineSpacing;

    GuiKit::drawLabelValue(m_visual,
                           "Vertices:",
                           std::to_string(m_metadata.vertexCount),
                           cardX + 10.0f * fontScale,
                           lineY,
                           cardW - 20.0f * fontScale,
                           m_fontSize);
    lineY += lineSpacing;

    GuiKit::drawLabelValue(m_visual,
                           "Triangles:",
                           std::to_string(m_metadata.triangleCount),
                           cardX + 10.0f * fontScale,
                           lineY,
                           cardW - 20.0f * fontScale,
                           m_fontSize);
    lineY += lineSpacing;

    GuiKit::drawLabelValue(m_visual,
                           "Submeshes:",
                           std::to_string(m_metadata.submeshCount),
                           cardX + 10.0f * fontScale,
                           lineY,
                           cardW - 20.0f * fontScale,
                           m_fontSize);
    lineY += lineSpacing;

    std::ostringstream dimStream;
    dimStream << std::fixed << std::setprecision(1) << m_metadata.dimensions.x
              << " x " << m_metadata.dimensions.y << " x "
              << m_metadata.dimensions.z;
    GuiKit::drawLabelValue(m_visual,
                           "Size:",
                           dimStream.str(),
                           cardX + 10.0f * fontScale,
                           lineY,
                           cardW - 20.0f * fontScale,
                           m_fontSize);
  } else {
    // Empty state card
    const std::string emptyHint =
      isVeryCompact ? "Press [O] or 'Open' to load .obj"
                    : (isCompact ? "Press [O] or click 'Open' to view .obj"
                                 : "Press [O] or click 'Open Mesh' to view a "
                                   "Wavefront (.obj) file");

    const float emptyW =
      std::min(virtualWidth - 24.0f,
               std::max(300.0f * fontScale,
                        GuiKit::estimateTextWidth(emptyHint, m_fontSize) +
                          40.0f * fontScale));
    const float emptyH = 76.0f * fontScale;
    const float emptyX = (virtualWidth - emptyW) * 0.5f;
    const float emptyY = (virtualHeight - emptyH) * 0.45f;

    GuiKit::drawShadow(m_visual, emptyX, emptyY, emptyW, emptyH);
    GuiKit::drawCard(m_visual, emptyX, emptyY, emptyW, emptyH);
    GuiKit::drawTextCentered(m_visual,
                             "No 3D Mesh Loaded",
                             emptyX + emptyW * 0.5f,
                             emptyY + 16.0f * fontScale,
                             m_fontSize + 2.0f,
                             UiTheme::textPrimary());
    GuiKit::drawTextCentered(m_visual,
                             emptyHint,
                             emptyX + emptyW * 0.5f,
                             emptyY + 42.0f * fontScale,
                             m_fontSize,
                             UiTheme::textMuted());
  }

  // 3. Toast notification (if active)
  if (m_toastTimer > 0.0f && !m_toastMessage.empty()) {
    const float toastAlpha =
      std::min(1.0f, m_toastTimer > 0.5f ? 1.0f : m_toastTimer / 0.5f);
    const float toastW =
      GuiKit::estimateTextWidth(m_toastMessage, m_fontSize) + 32.0f;
    const float toastH = 30.0f;
    const float toastX = (virtualWidth - toastW) * 0.5f;
    const float toastY = kHeaderHeight + 16.0f;

    ColorRgba bg = UiTheme::panelRaised();
    bg.a = static_cast<unsigned char>(230.0f * toastAlpha);
    ColorRgba textCol = m_toastColor;
    textCol.a = static_cast<unsigned char>(255.0f * toastAlpha);

    GuiKit::drawShadow(m_visual, toastX, toastY, toastW, toastH, 3.0f);
    m_visual.addFilledRect(toastX, toastY, toastW, toastH, bg);
    m_visual.addLine(toastX, toastY, toastX + toastW, toastY, textCol, 1.0f);
    GuiKit::drawTextCentered(m_visual,
                             m_toastMessage,
                             toastX + toastW * 0.5f,
                             toastY + 7.0f,
                             m_fontSize,
                             textCol);
  }

  // 4. Bottom Status Bar
  const float statusY = virtualHeight - kStatusHeight;
  GuiPanelChrome statusChrome;
  statusChrome.background = ColorRgba{ 12, 18, 28, 255 };
  statusChrome.border = ColorRgba{ 34, 48, 68, 255 };
  statusChrome.drawShadow = false;
  GuiKit::drawPanel(
    m_visual, 0.0f, statusY, virtualWidth, kStatusHeight, statusChrome);
  GuiKit::drawDivider(m_visual, 0.0f, statusY, virtualWidth, false);

  const std::string hintText =
    isVeryCompact
      ? "[LMB: Orbit  MMB: Pan  Scroll: Zoom]"
      : (isCompact
           ? "[LMB: Orbit  MMB/WASD: Pan  Scroll: Zoom  O: Open  R: Reset]"
           : "[LMB/RMB: Orbit  MMB/WASD: Pan  Scroll: Zoom  Q/E: Roll  O: "
             "Open  F/R: Reset  ~: Console]");

  const float camInfoWidth = isVeryCompact ? 160.0f : 240.0f;
  GuiKit::drawTextAligned(m_visual,
                          hintText,
                          12.0f,
                          statusY + 5.0f,
                          std::max(50.0f, virtualWidth - camInfoWidth - 24.0f),
                          m_fontSize - 1.0f,
                          UiTheme::textMuted(),
                          GuiAlignment::Left);

  std::ostringstream camStream;
  if (isVeryCompact) {
    camStream << "D: " << std::fixed << std::setprecision(1) << m_distance;
  } else {
    camStream << "Yaw: " << static_cast<int>(std::round(m_yawDeg))
              << " deg | Dist: " << std::fixed << std::setprecision(1)
              << m_distance;
  }
  GuiKit::drawTextAligned(m_visual,
                          camStream.str(),
                          virtualWidth - camInfoWidth - 12.0f,
                          statusY + 5.0f,
                          camInfoWidth,
                          m_fontSize - 1.0f,
                          UiTheme::textPrimary(),
                          GuiAlignment::Right);
}

bool
MeshViewerUi::AppendCommands(Renderer* renderer)
{
  if (renderer == nullptr) {
    return false;
  }
  m_visual.prepare(renderer);
  return m_visual.AppendCommands(renderer);
}
