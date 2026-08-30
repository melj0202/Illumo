#include "EditorSidebar.h"
#include "EditorUiAtlas.h"

#include <Illumo/Gui/GuiKit.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <queue>
#include <sstream>

namespace {
float
estimateTextWidth(const std::string& text, float sizePt)
{
  return GuiKit::estimateTextWidth(text, sizePt);
}
} // namespace

EditorSidebar::EditorSidebar(IRenderWindow* window, Renderer* renderer)
  : m_window(window)
  , m_renderer(renderer)
  , m_visual(1024u)
  , m_activeTool(EditorCommand::SelectTool)
  , m_mouseWasDown(false)
  , m_consumedPress(false)
  , m_fontSize(EditorToolbar::kDefaultFontSize)
  , m_width(kDefaultWidth)
  , m_barHeight(EditorToolbar::kDefaultBarHeight)
  , m_statusHeight(EditorToolbar::kDefaultStatusHeight)
  , m_toolRowHeight(22.0f)
  , m_modeButtonHeight(22.0f)
  , m_x(1080.0f)
  , m_y(EditorToolbar::kDefaultBarHeight)
  , m_height(670.0f)
  , m_modeY(0.0f)
  , m_inspectorY(0.0f)
  , m_animTime(0.0f)
  , m_modeAnim(0.0f)
  , m_hoverTool(-1)
  , m_hoverMode2D(false)
  , m_hoverMode3D(false)
  , m_hoverNudge(false)
  , m_hoverColor(false)
  , m_mouseX(0.0f)
  , m_mouseY(0.0f)
{
  m_visual.setSpace(PrimitiveSpace::Pixels);
  m_visual.setLayerHint(RenderLayerId::UI);
  m_visual.setWindow(window);
  m_visual.setRenderer(renderer);
  m_visual.prepare(renderer);
  m_tools.push_back({ "Select", EditorCommand::SelectTool });
  m_tools.push_back({ "Empty", EditorCommand::CreateEmpty });
  m_tools.push_back({ "Rect", EditorCommand::CreateRect });
  m_tools.push_back({ "Ellipse", EditorCommand::CreateEllipse });
  m_tools.push_back({ "Triangle", EditorCommand::CreateTriangle });
  m_tools.push_back({ "Cube", EditorCommand::CreateCube });
  m_tools.push_back({ "Pyramid", EditorCommand::CreatePyramid });
  m_tools.push_back({ "Sphere", EditorCommand::CreateSphere });
  updateLayout();
  rebuildVisual();
}

void
EditorSidebar::setFontSize(float sizePt)
{
  const float clamped = std::clamp(sizePt, 8.0f, 48.0f);
  if (std::abs(m_fontSize - clamped) > 0.001f) {
    m_fontSize = clamped;
    updateLayout();
    rebuildVisual();
  }
}

void
EditorSidebar::setToolbarDimensions(float barHeight, float statusHeight)
{
  m_barHeight = barHeight;
  m_statusHeight = statusHeight;
  updateLayout();
  rebuildVisual();
}

void
EditorSidebar::setAtlas(TextureHandle atlas)
{
  m_atlas = atlas;
  rebuildVisual();
}

void
EditorSidebar::setDetail(const EditorSceneDetail& detail)
{
  m_detail = detail;
}

void
EditorSidebar::setActiveTool(EditorCommand tool)
{
  m_activeTool = tool;
}

void
EditorSidebar::updateLayout()
{
  int width = 1280;
  int height = 720;
  if (m_window != nullptr) {
    const std::array<int, 2> dimensions = m_window->getWindowDimensions();
    width = std::max(1, dimensions[0]);
    height = std::max(1, dimensions[1]);
  }
  const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
  const float virtualWidth =
    static_cast<float>(width) / (scale > 0.0f ? scale : 1.0f);
  const float virtualHeight =
    static_cast<float>(height) / (scale > 0.0f ? scale : 1.0f);

  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  m_width = std::max(200.0f, std::round(200.0f * fontScale));
  m_x = virtualWidth - m_width;
  m_y = m_barHeight;
  m_height = std::max(80.0f, virtualHeight - m_barHeight - m_statusHeight);

  m_modeButtonHeight = std::max(22.0f, std::round(22.0f * fontScale));
  m_modeY = m_y + std::round(24.0f * fontScale);
  m_toolRowHeight = std::max(22.0f, std::round(22.0f * fontScale));
  const float toolStep =
    m_toolRowHeight + std::max(2.0f, std::round(2.0f * fontScale));
  float toolY = m_modeY + m_modeButtonHeight + std::round(8.0f * fontScale);
  for (ToolRow& row : m_tools) {
    row.y = toolY;
    toolY += toolStep;
  }
  m_inspectorY = toolY + std::round(8.0f * fontScale);
}

bool
EditorSidebar::containsScreenPoint(float x, float y) const
{
  return x >= m_x && x <= m_x + m_width && y >= m_y && y <= m_y + m_height;
}

EditorCommand
EditorSidebar::clickAt(float x, float y)
{
  updateLayout();
  if (!containsScreenPoint(x, y)) {
    return EditorCommand::None;
  }
  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  const float tabWidth = std::round((m_width - 24.0f * fontScale) * 0.5f);
  const float tab1X = m_x + 8.0f * fontScale;
  const float tab2X = tab1X + tabWidth + 8.0f * fontScale;

  if (y >= m_modeY && y <= m_modeY + m_modeButtonHeight) {
    if (x >= tab1X && x < tab1X + tabWidth) {
      return EditorCommand::SetMode2D;
    }
    if (x >= tab2X && x <= tab2X + tabWidth) {
      return EditorCommand::SetMode3D;
    }
  }
  for (const ToolRow& row : m_tools) {
    if (y >= row.y && y < row.y + m_toolRowHeight) {
      return row.command;
    }
  }
  const float itemH = std::max(20.0f, std::round(20.0f * fontScale));
  const float extentY = m_inspectorY + std::round(108.0f * fontScale);
  const float colorY = extentY + itemH;
  if (m_detail.hasSelection && y >= extentY && y < extentY + itemH) {
    return EditorCommand::NudgeExtent;
  }
  if (m_detail.hasSelection && y >= colorY && y < colorY + itemH) {
    return EditorCommand::CycleColor;
  }
  return EditorCommand::None;
}

EditorCommand
EditorSidebar::clickAtForTesting(float x, float y)
{
  return clickAt(x, y);
}

EditorCommand
EditorSidebar::update(InputManager* inputManager, float dt)
{
  m_animTime += std::max(0.0f, dt);
  const float targetMode =
    (m_detail.worldMode == IlscWorldMode::World3D) ? 1.0f : 0.0f;
  m_modeAnim +=
    (targetMode - m_modeAnim) * std::min(1.0f, std::max(0.0f, dt) * 16.0f);

  updateLayout();
  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  const float tabWidth = std::round((m_width - 24.0f * fontScale) * 0.5f);
  const float tab1X = m_x + 8.0f * fontScale;
  const float tab2X = tab1X + tabWidth + 8.0f * fontScale;

  // Mouse hover tracking
  m_hoverTool = -1;
  m_hoverMode2D = false;
  m_hoverMode3D = false;
  m_hoverNudge = false;
  m_hoverColor = false;

  if (m_window != nullptr) {
    const std::array<double, 2> mouseCoords = m_window->getMouseCoords();
    const float scale = m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
    m_mouseX =
      static_cast<float>(mouseCoords[0]) / (scale > 0.0f ? scale : 1.0f);
    m_mouseY =
      static_cast<float>(mouseCoords[1]) / (scale > 0.0f ? scale : 1.0f);

    if (containsScreenPoint(m_mouseX, m_mouseY)) {
      if (m_mouseY >= m_modeY && m_mouseY <= m_modeY + m_modeButtonHeight) {
        if (m_mouseX >= tab1X && m_mouseX < tab1X + tabWidth) {
          m_hoverMode2D = true;
        } else if (m_mouseX >= tab2X && m_mouseX <= tab2X + tabWidth) {
          m_hoverMode3D = true;
        }
      }
      for (size_t i = 0; i < m_tools.size(); ++i) {
        if (m_mouseY >= m_tools[i].y &&
            m_mouseY < m_tools[i].y + m_toolRowHeight) {
          m_hoverTool = static_cast<int>(i);
          break;
        }
      }
      if (m_detail.hasSelection) {
        const float itemH = std::max(20.0f, std::round(20.0f * fontScale));
        const float extentY = m_inspectorY + std::round(108.0f * fontScale);
        const float colorY = extentY + itemH;
        if (m_mouseY >= extentY && m_mouseY < extentY + itemH) {
          m_hoverNudge = true;
        } else if (m_mouseY >= colorY && m_mouseY < colorY + itemH) {
          m_hoverColor = true;
        }
      }
    }
  }

  EditorCommand command = EditorCommand::None;
  m_consumedPress = false;
  if (inputManager != nullptr) {
    const bool mouseDown =
      inputManager->isMouseButtonPressed(KeyCode::MouseLeft);
    if (mouseDown && !m_mouseWasDown) {
      std::array<double, 2> mouse{ 0.0, 0.0 };
      if (m_window != nullptr) {
        mouse = m_window->getMouseCoords();
      }
      const float scale =
        m_renderer != nullptr ? m_renderer->getUiScale() : 1.0f;
      const float uiX =
        static_cast<float>(mouse[0]) / (scale > 0.0f ? scale : 1.0f);
      const float uiY =
        static_cast<float>(mouse[1]) / (scale > 0.0f ? scale : 1.0f);
      command = clickAt(uiX, uiY);
      m_consumedPress =
        command != EditorCommand::None || containsScreenPoint(uiX, uiY);
    }
    m_mouseWasDown = mouseDown;
  }
  rebuildVisual();
  return command;
}

void
EditorSidebar::rebuildVisual()
{
  m_visual.clearPrimitives();
  updateLayout();

  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  const float iconSize =
    std::max(16.0f, std::round(EditorUiAtlas::kIconSize * fontScale));

  const ColorRgba sideBg{ 13, 19, 29, 255 };
  const ColorRgba sideBorder{ 40, 56, 78, 255 };
  const ColorRgba text = UiTheme::textPrimary();
  const ColorRgba muted = UiTheme::textMuted();
  const ColorRgba cyanAccent{ 66, 214, 210, 255 };

  // Sidebar background and left border
  m_visual.addFilledRect(m_x, m_y, m_width, m_height, sideBg);
  m_visual.addLine(m_x, m_y, m_x, m_y + m_height, sideBorder, 1.0f);

  // Section: MODE Header
  const float headerGlow = 0.70f + 0.30f * std::sin(m_animTime * 3.0f);
  const float headerFontSize = std::max(9.0f, std::round(11.0f * fontScale));
  m_visual.addFilledRect(
    m_x + 6.0f * fontScale,
    m_y + 8.0f * fontScale,
    2.0f,
    8.0f * fontScale,
    ColorRgba{ 66, 214, 210, static_cast<unsigned char>(255.0f * headerGlow) });
  m_visual.addText("MODE",
                   m_x + 12.0f * fontScale,
                   m_y + 6.0f * fontScale,
                   headerFontSize,
                   muted);

  const bool mode3d = m_detail.worldMode == IlscWorldMode::World3D;
  const float tabWidth = std::round((m_width - 24.0f * fontScale) * 0.5f);
  const float trackHeight = m_modeButtonHeight + 4.0f;

  // Segmented control track container
  m_visual.addFilledRect(m_x + 6.0f * fontScale,
                         m_modeY - 2.0f,
                         m_width - 12.0f * fontScale,
                         trackHeight,
                         ColorRgba{ 8, 12, 18, 220 });
  m_visual.addOutlineRect(m_x + 6.0f * fontScale,
                          m_modeY - 2.0f,
                          m_width - 12.0f * fontScale,
                          trackHeight,
                          ColorRgba{ 35, 48, 66, 255 },
                          1.0f);

  // Animated sliding selection indicator pill
  const float slideX =
    m_x + 8.0f * fontScale + m_modeAnim * (tabWidth + 8.0f * fontScale);
  const ColorRgba activeTabBg{ 25, 75, 105, 255 };
  const ColorRgba activeTabBorder{ 66, 214, 210, 220 };
  m_visual.addFilledRect(
    slideX, m_modeY, tabWidth, m_modeButtonHeight, activeTabBg);
  m_visual.addOutlineRect(
    slideX, m_modeY, tabWidth, m_modeButtonHeight, activeTabBorder, 1.0f);

  const float tab1X = m_x + 8.0f * fontScale;
  const float tab2X = tab1X + tabWidth + 8.0f * fontScale;
  const float tabTextY =
    m_modeY +
    std::max(0.0f, std::round((m_modeButtonHeight - m_fontSize) * 0.5f));

  // 2D Button content
  if (m_hoverMode2D && mode3d) {
    m_visual.addFilledRect(tab1X,
                           m_modeY,
                           tabWidth,
                           m_modeButtonHeight,
                           ColorRgba{ 20, 32, 48, 180 });
  }
  float tab1TextX = tab1X + 16.0f * fontScale;
  if (m_atlas.isValid()) {
    m_visual.addCenteredSprite(
      m_atlas,
      tab1X + 14.0f * fontScale,
      m_modeY + m_modeButtonHeight * 0.5f,
      iconSize,
      iconSize,
      EditorUiAtlas::regionFor(EditorCommand::SetMode2D));
    tab1TextX = tab1X + 14.0f * fontScale + iconSize * 0.5f + 6.0f * fontScale;
  }
  m_visual.addText("2D",
                   tab1TextX,
                   tabTextY,
                   m_fontSize,
                   (!mode3d || m_hoverMode2D) ? ColorRgba{ 255, 255, 255, 255 }
                                              : muted);

  // 3D Button content
  if (m_hoverMode3D && !mode3d) {
    m_visual.addFilledRect(tab2X,
                           m_modeY,
                           tabWidth,
                           m_modeButtonHeight,
                           ColorRgba{ 20, 32, 48, 180 });
  }
  float tab2TextX = tab2X + 16.0f * fontScale;
  if (m_atlas.isValid()) {
    m_visual.addCenteredSprite(
      m_atlas,
      tab2X + 14.0f * fontScale,
      m_modeY + m_modeButtonHeight * 0.5f,
      iconSize,
      iconSize,
      EditorUiAtlas::regionFor(EditorCommand::SetMode3D));
    tab2TextX = tab2X + 14.0f * fontScale + iconSize * 0.5f + 6.0f * fontScale;
  }
  m_visual.addText("3D",
                   tab2TextX,
                   tabTextY,
                   m_fontSize,
                   (mode3d || m_hoverMode3D) ? ColorRgba{ 255, 255, 255, 255 }
                                             : muted);

  // Divider between Mode and Tools
  const float dividerY = m_modeY + m_modeButtonHeight + 3.0f * fontScale;
  m_visual.addLine(m_x + 8.0f * fontScale,
                   dividerY,
                   m_x + m_width - 8.0f * fontScale,
                   dividerY,
                   ColorRgba{ 35, 48, 66, 255 },
                   1.0f);

  const float toolPulse = 0.75f + 0.25f * std::sin(m_animTime * 4.0f);
  const ColorRgba pulsedCyan{
    66, 214, 210, static_cast<unsigned char>(255.0f * toolPulse)
  };

  for (size_t i = 0; i < m_tools.size(); ++i) {
    const ToolRow& row = m_tools[i];
    const bool active = (row.command == m_activeTool);
    const bool hovered = (m_hoverTool == static_cast<int>(i));

    if (active) {
      m_visual.addFilledRect(m_x + 8.0f * fontScale,
                             row.y,
                             m_width - 16.0f * fontScale,
                             m_toolRowHeight,
                             ColorRgba{ 28, 80, 115, 250 });
      m_visual.addOutlineRect(m_x + 8.0f * fontScale,
                              row.y,
                              m_width - 16.0f * fontScale,
                              m_toolRowHeight,
                              ColorRgba{ 66, 214, 210, 180 },
                              1.0f);
      m_visual.addFilledRect(
        m_x + 8.0f * fontScale, row.y, 3.0f, m_toolRowHeight, pulsedCyan);
    } else if (hovered) {
      m_visual.addFilledRect(m_x + 8.0f * fontScale,
                             row.y,
                             m_width - 16.0f * fontScale,
                             m_toolRowHeight,
                             ColorRgba{ 24, 38, 56, 240 });
      m_visual.addOutlineRect(m_x + 8.0f * fontScale,
                              row.y,
                              m_width - 16.0f * fontScale,
                              m_toolRowHeight,
                              ColorRgba{ 52, 75, 105, 220 },
                              1.0f);
      m_visual.addFilledRect(m_x + 8.0f * fontScale,
                             row.y,
                             2.0f,
                             m_toolRowHeight,
                             ColorRgba{ 66, 160, 200, 200 });
    } else {
      m_visual.addFilledRect(m_x + 8.0f * fontScale,
                             row.y,
                             m_width - 16.0f * fontScale,
                             m_toolRowHeight,
                             ColorRgba{ 17, 25, 38, 220 });
      m_visual.addOutlineRect(m_x + 8.0f * fontScale,
                              row.y,
                              m_width - 16.0f * fontScale,
                              m_toolRowHeight,
                              ColorRgba{ 32, 45, 62, 200 },
                              1.0f);
    }

    float labelX = m_x + 16.0f * fontScale;
    if (m_atlas.isValid()) {
      m_visual.addCenteredSprite(m_atlas,
                                 m_x + 14.0f * fontScale + iconSize * 0.5f,
                                 row.y + m_toolRowHeight * 0.5f,
                                 iconSize,
                                 iconSize,
                                 EditorUiAtlas::regionFor(row.command));
      labelX = m_x + 14.0f * fontScale + iconSize + 6.0f * fontScale;
    }
    const float rowTextY =
      row.y + std::max(0.0f, std::round((m_toolRowHeight - m_fontSize) * 0.5f));
    m_visual.addText(row.label,
                     labelX,
                     rowTextY,
                     m_fontSize,
                     (active || hovered) ? ColorRgba{ 255, 255, 255, 255 }
                                         : ColorRgba{ 190, 208, 228, 255 });
  }

  // Section: INSPECTOR
  m_visual.addLine(m_x + 8.0f * fontScale,
                   m_inspectorY - 6.0f * fontScale,
                   m_x + m_width - 8.0f * fontScale,
                   m_inspectorY - 6.0f * fontScale,
                   ColorRgba{ 35, 48, 66, 255 },
                   1.0f);
  m_visual.addFilledRect(
    m_x + 6.0f * fontScale,
    m_inspectorY + 2.0f * fontScale,
    2.0f,
    8.0f * fontScale,
    ColorRgba{ 66, 214, 210, static_cast<unsigned char>(255.0f * headerGlow) });
  m_visual.addText(
    "INSPECTOR", m_x + 12.0f * fontScale, m_inspectorY, headerFontSize, muted);

  // Summary badges with glossy borders
  const float badgeWidth = tabWidth;
  const float badgeHeight = std::max(18.0f, std::round(18.0f * fontScale));
  const float badgeFontSize = std::max(9.0f, std::round(11.0f * fontScale));
  const float badgeY = m_inspectorY + 16.0f * fontScale;

  m_visual.addFilledRect(
    tab1X, badgeY, badgeWidth, badgeHeight, ColorRgba{ 18, 27, 40, 255 });
  m_visual.addOutlineRect(
    tab1X, badgeY, badgeWidth, badgeHeight, ColorRgba{ 44, 62, 86, 255 }, 1.0f);
  std::ostringstream summary;
  summary << "Nodes: " << m_detail.nodeCount;
  m_visual.addText(
    summary.str(),
    tab1X + 6.0f * fontScale,
    badgeY + std::max(0.0f, std::round((badgeHeight - badgeFontSize) * 0.5f)),
    badgeFontSize,
    ColorRgba{ 210, 225, 240, 255 });

  const char* modeLabel = IlscCodec::worldModeName(m_detail.worldMode);
  std::string modeText = "Mode: ";
  modeText += modeLabel;
  m_visual.addFilledRect(
    tab2X, badgeY, badgeWidth, badgeHeight, ColorRgba{ 18, 27, 40, 255 });
  m_visual.addOutlineRect(
    tab2X, badgeY, badgeWidth, badgeHeight, ColorRgba{ 44, 62, 86, 255 }, 1.0f);
  m_visual.addText(
    modeText,
    tab2X + 6.0f * fontScale,
    badgeY + std::max(0.0f, std::round((badgeHeight - badgeFontSize) * 0.5f)),
    badgeFontSize,
    ColorRgba{ 210, 225, 240, 255 });

  const float itemH = std::max(20.0f, std::round(20.0f * fontScale));
  const float itemFontSize = std::max(10.0f, std::round(12.0f * fontScale));
  const float smallFontSize = std::max(9.0f, std::round(11.0f * fontScale));

  if (!m_detail.hasSelection) {
    const float noSelH = std::max(70.0f, std::round(70.0f * fontScale));
    const float noSelY = m_inspectorY + 40.0f * fontScale;
    m_visual.addFilledRect(m_x + 8.0f * fontScale,
                           noSelY,
                           m_width - 16.0f * fontScale,
                           noSelH,
                           ColorRgba{ 10, 15, 24, 200 });
    m_visual.addOutlineRect(m_x + 8.0f * fontScale,
                            noSelY,
                            m_width - 16.0f * fontScale,
                            noSelH,
                            ColorRgba{ 32, 45, 62, 255 },
                            1.0f);
    m_visual.addText("No Selection",
                     m_x + 14.0f * fontScale,
                     noSelY + 8.0f * fontScale,
                     itemFontSize,
                     ColorRgba{ 140, 165, 190, 255 });
    m_visual.addText("Click node in canvas",
                     m_x + 14.0f * fontScale,
                     noSelY + 28.0f * fontScale,
                     smallFontSize,
                     ColorRgba{ 90, 112, 135, 255 });
    m_visual.addText("or choose tool to create",
                     m_x + 14.0f * fontScale,
                     noSelY + 44.0f * fontScale,
                     smallFontSize,
                     ColorRgba{ 90, 112, 135, 255 });
  } else {
    const float selPulse = 0.80f + 0.20f * std::sin(m_animTime * 3.5f);
    const unsigned char selBorderA =
      static_cast<unsigned char>(255.0f * selPulse);

    // Header card for selection with left glowing accent
    const float selHeaderY = m_inspectorY + 38.0f * fontScale;
    m_visual.addFilledRect(m_x + 8.0f * fontScale,
                           selHeaderY,
                           m_width - 16.0f * fontScale,
                           itemH,
                           ColorRgba{ 22, 34, 52, 255 });
    m_visual.addOutlineRect(m_x + 8.0f * fontScale,
                            selHeaderY,
                            m_width - 16.0f * fontScale,
                            itemH,
                            ColorRgba{ 66, 120, 180, selBorderA },
                            1.0f);
    m_visual.addFilledRect(
      m_x + 8.0f * fontScale, selHeaderY, 3.0f, itemH, cyanAccent);

    std::string idLine =
      m_detail.selectedName + " (#" + m_detail.selectedId + ")";
    m_visual.addText(
      idLine,
      m_x + 15.0f * fontScale,
      selHeaderY + std::max(0.0f, std::round((itemH - itemFontSize) * 0.5f)),
      itemFontSize,
      ColorRgba{ 255, 255, 255, 255 });

    std::string kindLine =
      "Kind: " + std::string(IlscCodec::kindName(m_detail.selectedKind));
    m_visual.addText(kindLine,
                     m_x + 10.0f * fontScale,
                     m_inspectorY + 66.0f * fontScale,
                     itemFontSize,
                     ColorRgba{ 66, 214, 210, 255 });

    char transformText[96];
    std::snprintf(transformText,
                  sizeof(transformText),
                  "Pos %.2f, %.2f, %.2f",
                  m_detail.transform.position.x,
                  m_detail.transform.position.y,
                  m_detail.transform.position.z);
    m_visual.addText(transformText,
                     m_x + 10.0f * fontScale,
                     m_inspectorY + 86.0f * fontScale,
                     itemFontSize,
                     text);

    const float extentY = m_inspectorY + std::round(108.0f * fontScale);
    char extentText[96];
    std::snprintf(extentText,
                  sizeof(extentText),
                  "Size %.2f %.2f %.2f",
                  m_detail.extent.x,
                  m_detail.extent.y,
                  m_detail.extent.z);
    m_visual.addFilledRect(m_x + 8.0f * fontScale,
                           extentY,
                           m_width - 16.0f * fontScale,
                           itemH,
                           m_hoverNudge ? ColorRgba{ 24, 38, 56, 255 }
                                        : ColorRgba{ 16, 24, 37, 255 });
    m_visual.addOutlineRect(m_x + 8.0f * fontScale,
                            extentY,
                            m_width - 16.0f * fontScale,
                            itemH,
                            m_hoverNudge ? ColorRgba{ 66, 120, 180, 255 }
                                         : ColorRgba{ 44, 62, 86, 255 },
                            1.0f);
    m_visual.addText(
      extentText,
      m_x + 12.0f * fontScale,
      extentY + std::max(0.0f, std::round((itemH - itemFontSize) * 0.5f)),
      itemFontSize,
      text);
    const float plusTagW = estimateTextWidth("[+]", smallFontSize);
    m_visual.addText(
      "[+]",
      m_x + m_width - plusTagW - 10.0f * fontScale,
      extentY + std::max(0.0f, std::round((itemH - smallFontSize) * 0.5f)),
      smallFontSize,
      m_hoverNudge ? ColorRgba{ 255, 255, 255, 255 } : cyanAccent);

    const float colorY = extentY + itemH;
    char colorText[64];
    std::snprintf(colorText,
                  sizeof(colorText),
                  "RGB %u %u %u",
                  static_cast<unsigned>(m_detail.color.r),
                  static_cast<unsigned>(m_detail.color.g),
                  static_cast<unsigned>(m_detail.color.b));
    m_visual.addFilledRect(m_x + 8.0f * fontScale,
                           colorY,
                           m_width - 16.0f * fontScale,
                           itemH,
                           m_hoverColor ? ColorRgba{ 24, 38, 56, 255 }
                                        : ColorRgba{ 16, 24, 37, 255 });
    m_visual.addOutlineRect(m_x + 8.0f * fontScale,
                            colorY,
                            m_width - 16.0f * fontScale,
                            itemH,
                            m_hoverColor ? ColorRgba{ 66, 120, 180, 255 }
                                         : ColorRgba{ 44, 62, 86, 255 },
                            1.0f);

    // Live color swatch preview tile with subtle breathing outline
    const float swatchSize = std::max(12.0f, std::round(12.0f * fontScale));
    const float swatchY = colorY + std::round((itemH - swatchSize) * 0.5f);
    m_visual.addFilledRect(
      m_x + 12.0f * fontScale, swatchY, swatchSize, swatchSize, m_detail.color);
    m_visual.addOutlineRect(m_x + 12.0f * fontScale,
                            swatchY,
                            swatchSize,
                            swatchSize,
                            ColorRgba{ 255, 255, 255, selBorderA },
                            1.0f);

    m_visual.addText(
      colorText,
      m_x + 16.0f * fontScale + swatchSize,
      colorY + std::max(0.0f, std::round((itemH - itemFontSize) * 0.5f)),
      itemFontSize,
      text);
    const float cycleTagW = estimateTextWidth("[Cycle]", smallFontSize);
    m_visual.addText(
      "[Cycle]",
      m_x + m_width - cycleTagW - 10.0f * fontScale,
      colorY + std::max(0.0f, std::round((itemH - smallFontSize) * 0.5f)),
      smallFontSize,
      m_hoverColor ? ColorRgba{ 255, 255, 255, 255 } : cyanAccent);
  }
}

bool
EditorSidebar::AppendCommands(Renderer* renderer)
{
  return m_visual.AppendCommands(renderer);
}
