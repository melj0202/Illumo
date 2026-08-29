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
EditorSidebar::update(InputManager* inputManager)
{
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
  m_visual.addFilledRect(m_x, m_y, kWidth, m_height, UiTheme::panelSurface());
  m_visual.addLine(m_x, m_y, m_x, m_y + m_height, UiTheme::panelBorder(), 1.0f);
  m_visual.addText(
    "Tools", m_x + 10.0f, m_y + 6.0f, 12.0f, UiTheme::textMuted());

  const ColorRgba modeOn = UiTheme::selection();
  const ColorRgba modeOff = UiTheme::panelRaised();
  const bool mode3d = m_detail.worldMode == IlscWorldMode::World3D;
  m_visual.addFilledRect(
    m_x + 8.0f, m_modeY, 88.0f, 22.0f, mode3d ? modeOff : modeOn);
  if (m_atlas.isValid()) {
    m_visual.addCenteredSprite(
      m_atlas,
      m_x + 26.0f,
      m_modeY + 11.0f,
      EditorUiAtlas::kIconSize,
      EditorUiAtlas::kIconSize,
      EditorUiAtlas::regionFor(EditorCommand::SetMode2D));
  }
  m_visual.addText(
    "2D", m_x + 40.0f, m_modeY + 4.0f, 13.0f, UiTheme::textPrimary());
  m_visual.addFilledRect(
    m_x + 104.0f, m_modeY, 88.0f, 22.0f, mode3d ? modeOn : modeOff);
  if (m_atlas.isValid()) {
    m_visual.addCenteredSprite(
      m_atlas,
      m_x + 122.0f,
      m_modeY + 11.0f,
      EditorUiAtlas::kIconSize,
      EditorUiAtlas::kIconSize,
      EditorUiAtlas::regionFor(EditorCommand::SetMode3D));
  }
  m_visual.addText(
    "3D", m_x + 136.0f, m_modeY + 4.0f, 13.0f, UiTheme::textPrimary());

  for (const ToolRow& row : m_tools) {
    const bool active = row.command == m_activeTool;
    m_visual.addFilledRect(m_x + 8.0f,
                           row.y,
                           kWidth - 16.0f,
                           22.0f,
                           active ? UiTheme::selection()
                                  : UiTheme::panelRaised());
    float labelX = m_x + 16.0f;
    if (m_atlas.isValid()) {
      m_visual.addCenteredSprite(m_atlas,
                                 m_x + 20.0f,
                                 row.y + 11.0f,
                                 EditorUiAtlas::kIconSize,
                                 EditorUiAtlas::kIconSize,
                                 EditorUiAtlas::regionFor(row.command));
      labelX = m_x + 32.0f;
    }
    m_visual.addText(
      row.label, labelX, row.y + 4.0f, 13.0f, UiTheme::textPrimary());
  }

  m_visual.addText(
    "Scene", m_x + 10.0f, m_inspectorY, 12.0f, UiTheme::textMuted());
  std::ostringstream summary;
  summary << "Nodes: " << m_detail.nodeCount;
  m_visual.addText(summary.str(),
                   m_x + 10.0f,
                   m_inspectorY + 18.0f,
                   12.0f,
                   UiTheme::textPrimary());
  const char* modeLabel = IlscCodec::worldModeName(m_detail.worldMode);
  std::string modeText = "Mode: ";
  modeText += modeLabel;
  m_visual.addText(
    modeText, m_x + 10.0f, m_inspectorY + 36.0f, 12.0f, UiTheme::textPrimary());

  if (!m_detail.hasSelection) {
    m_visual.addText("No selection",
                     m_x + 10.0f,
                     m_inspectorY + 58.0f,
                     12.0f,
                     UiTheme::textMuted());
  } else {
    std::string idLine =
      m_detail.selectedName + " (" + m_detail.selectedId + ")";
    m_visual.addText(
      idLine, m_x + 10.0f, m_inspectorY + 58.0f, 12.0f, UiTheme::textPrimary());
    std::string kindLine = "Kind: ";
    kindLine += IlscCodec::kindName(m_detail.selectedKind);
    m_visual.addText(kindLine,
                     m_x + 10.0f,
                     m_inspectorY + 74.0f,
                     12.0f,
                     UiTheme::textPrimary());
    char transformText[96];
    std::snprintf(transformText,
                  sizeof(transformText),
                  "Pos %.2f %.2f %.2f",
                  m_detail.transform.position.x,
                  m_detail.transform.position.y,
                  m_detail.transform.position.z);
    m_visual.addText(transformText,
                     m_x + 10.0f,
                     m_inspectorY + 90.0f,
                     12.0f,
                     UiTheme::textPrimary());
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
                           UiTheme::panelInset());
    m_visual.addText(extentText,
                     m_x + 10.0f,
                     m_inspectorY + 110.0f,
                     12.0f,
                     UiTheme::textPrimary());
    char colorText[64];
    std::snprintf(colorText,
                  sizeof(colorText),
                  "Color %u %u %u",
                  static_cast<unsigned>(m_detail.color.r),
                  static_cast<unsigned>(m_detail.color.g),
                  static_cast<unsigned>(m_detail.color.b));
    m_visual.addFilledRect(m_x + 8.0f,
                           m_inspectorY + 128.0f,
                           kWidth - 16.0f,
                           20.0f,
                           UiTheme::panelInset());
    m_visual.addText(colorText,
                     m_x + 10.0f,
                     m_inspectorY + 130.0f,
                     12.0f,
                     UiTheme::textPrimary());
  }
}

bool
EditorSidebar::AppendCommands(Renderer* renderer)
{
  return m_visual.AppendCommands(renderer);
}
