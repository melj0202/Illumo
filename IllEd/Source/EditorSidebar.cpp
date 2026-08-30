#include "EditorSidebar.h"
#include "EditorUiAtlas.h"

#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/UiTheme.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <queue>
#include <sstream>

EditorSidebar::EditorSidebar(IRenderWindow* window, Renderer* renderer)
  : m_window(window)
  , m_renderer(renderer)
  , m_visual(1024u)
  , m_activeTool(EditorCommand::SelectTool)
  , m_mouseWasDown(false)
  , m_consumedPress(false)
  , m_x(1080.0f)
  , m_y(EditorToolbar::kBarHeight)
  , m_height(670.0f)
  , m_modeY(0.0f)
  , m_inspectorY(0.0f)
  , m_animTime(0.0f)
  , m_modeAnim(0.0f)
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
  m_x = virtualWidth - kWidth;
  m_y = EditorToolbar::kBarHeight;
  m_height = std::max(80.0f,
                      virtualHeight - EditorToolbar::kBarHeight -
                        EditorToolbar::kStatusHeight);
  m_modeY = m_y + 24.0f;
  float toolY = m_modeY + 30.0f;
  for (ToolRow& row : m_tools) {
    row.y = toolY;
    toolY += 24.0f;
  }
  m_inspectorY = toolY + 8.0f;
}

bool
EditorSidebar::containsScreenPoint(float x, float y) const
{
  return x >= m_x && y >= m_y && y <= m_y + m_height;
}

EditorCommand
EditorSidebar::clickAt(float x, float y)
{
  updateLayout();
  if (!containsScreenPoint(x, y)) {
    return EditorCommand::None;
  }
  if (y >= m_modeY && y <= m_modeY + 22.0f) {
    if (x >= m_x + 8.0f && x < m_x + 96.0f) {
      return EditorCommand::SetMode2D;
    }
    if (x >= m_x + 104.0f && x <= m_x + 192.0f) {
      return EditorCommand::SetMode3D;
    }
  }
  for (const ToolRow& row : m_tools) {
    if (y >= row.y && y < row.y + 22.0f) {
      return row.command;
    }
  }
  if (m_detail.hasSelection && y >= m_inspectorY + 108.0f &&
      y < m_inspectorY + 128.0f) {
    return EditorCommand::NudgeExtent;
  }
  if (m_detail.hasSelection && y >= m_inspectorY + 128.0f &&
      y < m_inspectorY + 148.0f) {
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

  const ColorRgba sideBg{ 13, 19, 29, 255 };
  const ColorRgba sideBorder{ 40, 56, 78, 255 };
  const ColorRgba text = UiTheme::textPrimary();
  const ColorRgba muted = UiTheme::textMuted();
  const ColorRgba cyanAccent{ 66, 214, 210, 255 };

  // Sidebar background and left border
  m_visual.addFilledRect(m_x, m_y, kWidth, m_height, sideBg);
  m_visual.addLine(m_x, m_y, m_x, m_y + m_height, sideBorder, 1.0f);

  // Section: MODE
  m_visual.addFilledRect(m_x + 6.0f, m_y + 8.0f, 2.0f, 8.0f, cyanAccent);
  m_visual.addText("MODE", m_x + 12.0f, m_y + 6.0f, 11.0f, muted);

  const bool mode3d = m_detail.worldMode == IlscWorldMode::World3D;

  // Segmented control container
  m_visual.addFilledRect(m_x + 6.0f,
                         m_modeY - 2.0f,
                         kWidth - 12.0f,
                         26.0f,
                         ColorRgba{ 8, 12, 18, 220 });
  m_visual.addOutlineRect(m_x + 6.0f,
                          m_modeY - 2.0f,
                          kWidth - 12.0f,
                          26.0f,
                          ColorRgba{ 35, 48, 66, 255 },
                          1.0f);

  // 2D Button
  const ColorRgba activeTabBg{ 25, 75, 105, 255 };
  const ColorRgba inactiveTabBg{ 16, 24, 36, 180 };
  m_visual.addFilledRect(
    m_x + 8.0f, m_modeY, 88.0f, 22.0f, mode3d ? inactiveTabBg : activeTabBg);
  if (!mode3d) {
    m_visual.addOutlineRect(
      m_x + 8.0f, m_modeY, 88.0f, 22.0f, ColorRgba{ 66, 214, 210, 200 }, 1.0f);
  }
  if (m_atlas.isValid()) {
    m_visual.addCenteredSprite(
      m_atlas,
      m_x + 28.0f,
      m_modeY + 11.0f,
      EditorUiAtlas::kIconSize,
      EditorUiAtlas::kIconSize,
      EditorUiAtlas::regionFor(EditorCommand::SetMode2D));
  }
  m_visual.addText("2D",
                   m_x + 44.0f,
                   m_modeY + 5.0f,
                   13.0f,
                   mode3d ? muted : ColorRgba{ 255, 255, 255, 255 });

  // 3D Button
  m_visual.addFilledRect(
    m_x + 104.0f, m_modeY, 88.0f, 22.0f, mode3d ? activeTabBg : inactiveTabBg);
  if (mode3d) {
    m_visual.addOutlineRect(m_x + 104.0f,
                            m_modeY,
                            88.0f,
                            22.0f,
                            ColorRgba{ 66, 214, 210, 200 },
                            1.0f);
  }
  if (m_atlas.isValid()) {
    m_visual.addCenteredSprite(
      m_atlas,
      m_x + 124.0f,
      m_modeY + 11.0f,
      EditorUiAtlas::kIconSize,
      EditorUiAtlas::kIconSize,
      EditorUiAtlas::regionFor(EditorCommand::SetMode3D));
  }
  m_visual.addText("3D",
                   m_x + 140.0f,
                   m_modeY + 5.0f,
                   13.0f,
                   mode3d ? ColorRgba{ 255, 255, 255, 255 } : muted);

  // Divider between Mode and Tools
  m_visual.addLine(m_x + 8.0f,
                   m_modeY + 25.0f,
                   m_x + kWidth - 8.0f,
                   m_modeY + 25.0f,
                   ColorRgba{ 35, 48, 66, 255 },
                   1.0f);

  const float toolPulse = 0.75f + 0.25f * std::sin(m_animTime * 4.0f);
  const ColorRgba pulsedCyan{
    66, 214, 210, static_cast<unsigned char>(255.0f * toolPulse)
  };

  for (const ToolRow& row : m_tools) {
    const bool active = row.command == m_activeTool;
    if (active) {
      m_visual.addFilledRect(m_x + 8.0f,
                             row.y,
                             kWidth - 16.0f,
                             22.0f,
                             ColorRgba{ 28, 80, 115, 250 });
      m_visual.addOutlineRect(m_x + 8.0f,
                              row.y,
                              kWidth - 16.0f,
                              22.0f,
                              ColorRgba{ 66, 214, 210, 160 },
                              1.0f);
      m_visual.addFilledRect(m_x + 8.0f, row.y, 3.0f, 22.0f, pulsedCyan);
    } else {
      m_visual.addFilledRect(
        m_x + 8.0f, row.y, kWidth - 16.0f, 22.0f, ColorRgba{ 17, 25, 38, 220 });
      m_visual.addOutlineRect(m_x + 8.0f,
                              row.y,
                              kWidth - 16.0f,
                              22.0f,
                              ColorRgba{ 32, 45, 62, 200 },
                              1.0f);
    }
    float labelX = m_x + 16.0f;
    if (m_atlas.isValid()) {
      m_visual.addCenteredSprite(m_atlas,
                                 m_x + 22.0f,
                                 row.y + 11.0f,
                                 EditorUiAtlas::kIconSize,
                                 EditorUiAtlas::kIconSize,
                                 EditorUiAtlas::regionFor(row.command));
      labelX = m_x + 36.0f;
    }
    m_visual.addText(row.label,
                     labelX,
                     row.y + 4.0f,
                     13.0f,
                     active ? ColorRgba{ 255, 255, 255, 255 }
                            : ColorRgba{ 190, 208, 228, 255 });
  }

  // Section: INSPECTOR
  m_visual.addLine(m_x + 8.0f,
                   m_inspectorY - 6.0f,
                   m_x + kWidth - 8.0f,
                   m_inspectorY - 6.0f,
                   ColorRgba{ 35, 48, 66, 255 },
                   1.0f);
  m_visual.addFilledRect(
    m_x + 6.0f, m_inspectorY + 2.0f, 2.0f, 8.0f, cyanAccent);
  m_visual.addText("INSPECTOR", m_x + 12.0f, m_inspectorY, 11.0f, muted);

  // Summary badges
  m_visual.addFilledRect(m_x + 8.0f,
                         m_inspectorY + 16.0f,
                         88.0f,
                         18.0f,
                         ColorRgba{ 18, 27, 40, 255 });
  m_visual.addOutlineRect(m_x + 8.0f,
                          m_inspectorY + 16.0f,
                          88.0f,
                          18.0f,
                          ColorRgba{ 38, 54, 75, 255 },
                          1.0f);
  std::ostringstream summary;
  summary << "Nodes: " << m_detail.nodeCount;
  m_visual.addText(summary.str(),
                   m_x + 14.0f,
                   m_inspectorY + 19.0f,
                   11.0f,
                   ColorRgba{ 210, 225, 240, 255 });

  const char* modeLabel = IlscCodec::worldModeName(m_detail.worldMode);
  std::string modeText = "Mode: ";
  modeText += modeLabel;
  m_visual.addFilledRect(m_x + 104.0f,
                         m_inspectorY + 16.0f,
                         88.0f,
                         18.0f,
                         ColorRgba{ 18, 27, 40, 255 });
  m_visual.addOutlineRect(m_x + 104.0f,
                          m_inspectorY + 16.0f,
                          88.0f,
                          18.0f,
                          ColorRgba{ 38, 54, 75, 255 },
                          1.0f);
  m_visual.addText(modeText,
                   m_x + 110.0f,
                   m_inspectorY + 19.0f,
                   11.0f,
                   ColorRgba{ 210, 225, 240, 255 });

  if (!m_detail.hasSelection) {
    m_visual.addFilledRect(m_x + 8.0f,
                           m_inspectorY + 40.0f,
                           kWidth - 16.0f,
                           70.0f,
                           ColorRgba{ 10, 15, 24, 200 });
    m_visual.addOutlineRect(m_x + 8.0f,
                            m_inspectorY + 40.0f,
                            kWidth - 16.0f,
                            70.0f,
                            ColorRgba{ 32, 45, 62, 255 },
                            1.0f);
    m_visual.addText("No Selection",
                     m_x + 14.0f,
                     m_inspectorY + 48.0f,
                     12.0f,
                     ColorRgba{ 140, 165, 190, 255 });
    m_visual.addText("Click node in canvas",
                     m_x + 14.0f,
                     m_inspectorY + 68.0f,
                     11.0f,
                     ColorRgba{ 90, 112, 135, 255 });
    m_visual.addText("or choose tool to create",
                     m_x + 14.0f,
                     m_inspectorY + 84.0f,
                     11.0f,
                     ColorRgba{ 90, 112, 135, 255 });
  } else {
    const float selPulse = 0.80f + 0.20f * std::sin(m_animTime * 3.5f);
    const unsigned char selBorderA =
      static_cast<unsigned char>(255.0f * selPulse);

    // Header card for selection
    m_visual.addFilledRect(m_x + 8.0f,
                           m_inspectorY + 38.0f,
                           kWidth - 16.0f,
                           20.0f,
                           ColorRgba{ 22, 34, 52, 255 });
    m_visual.addOutlineRect(m_x + 8.0f,
                            m_inspectorY + 38.0f,
                            kWidth - 16.0f,
                            20.0f,
                            ColorRgba{ 66, 120, 180, selBorderA },
                            1.0f);
    std::string idLine =
      m_detail.selectedName + " (#" + m_detail.selectedId + ")";
    m_visual.addText(idLine,
                     m_x + 12.0f,
                     m_inspectorY + 42.0f,
                     12.0f,
                     ColorRgba{ 255, 255, 255, 255 });

    std::string kindLine =
      "Kind: " + std::string(IlscCodec::kindName(m_detail.selectedKind));
    m_visual.addText(kindLine,
                     m_x + 10.0f,
                     m_inspectorY + 66.0f,
                     12.0f,
                     ColorRgba{ 66, 214, 210, 255 });

    char transformText[96];
    std::snprintf(transformText,
                  sizeof(transformText),
                  "Pos %.2f, %.2f, %.2f",
                  m_detail.transform.position.x,
                  m_detail.transform.position.y,
                  m_detail.transform.position.z);
    m_visual.addText(
      transformText, m_x + 10.0f, m_inspectorY + 86.0f, 12.0f, text);

    char extentText[96];
    std::snprintf(extentText,
                  sizeof(extentText),
                  "Size %.2f %.2f %.2f",
                  m_detail.extent.x,
                  m_detail.extent.y,
                  m_detail.extent.z);
    m_visual.addFilledRect(m_x + 8.0f,
                           m_inspectorY + 108.0f,
                           kWidth - 16.0f,
                           20.0f,
                           ColorRgba{ 16, 24, 37, 255 });
    m_visual.addOutlineRect(m_x + 8.0f,
                            m_inspectorY + 108.0f,
                            kWidth - 16.0f,
                            20.0f,
                            ColorRgba{ 44, 62, 86, 255 },
                            1.0f);
    m_visual.addText(
      extentText, m_x + 12.0f, m_inspectorY + 111.0f, 12.0f, text);
    m_visual.addText(
      "[+]", m_x + kWidth - 32.0f, m_inspectorY + 111.0f, 11.0f, cyanAccent);

    char colorText[64];
    std::snprintf(colorText,
                  sizeof(colorText),
                  "RGB %u %u %u",
                  static_cast<unsigned>(m_detail.color.r),
                  static_cast<unsigned>(m_detail.color.g),
                  static_cast<unsigned>(m_detail.color.b));
    m_visual.addFilledRect(m_x + 8.0f,
                           m_inspectorY + 128.0f,
                           kWidth - 16.0f,
                           20.0f,
                           ColorRgba{ 16, 24, 37, 255 });
    m_visual.addOutlineRect(m_x + 8.0f,
                            m_inspectorY + 128.0f,
                            kWidth - 16.0f,
                            20.0f,
                            ColorRgba{ 44, 62, 86, 255 },
                            1.0f);

    // Live color swatch preview tile with subtle breathing outline
    m_visual.addFilledRect(
      m_x + 12.0f, m_inspectorY + 132.0f, 12.0f, 12.0f, m_detail.color);
    m_visual.addOutlineRect(m_x + 12.0f,
                            m_inspectorY + 132.0f,
                            12.0f,
                            12.0f,
                            ColorRgba{ 255, 255, 255, selBorderA },
                            1.0f);

    m_visual.addText(
      colorText, m_x + 28.0f, m_inspectorY + 131.0f, 12.0f, text);
    m_visual.addText("[Cycle]",
                     m_x + kWidth - 54.0f,
                     m_inspectorY + 131.0f,
                     11.0f,
                     cyanAccent);
  }
}

bool
EditorSidebar::AppendCommands(Renderer* renderer)
{
  return m_visual.AppendCommands(renderer);
}
