#include "EditorToolsPanel.h"

#include "EditorToolbar.h"
#include "EditorUiAtlas.h"
#include <Illumo/Gui/GuiToolStyle.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

struct ToolEntry
{
  const char* label;
  EditorCommand command;
};

static const std::array<ToolEntry, 8> kTools = { {
  { "Select", EditorCommand::SelectTool },
  { "Empty", EditorCommand::CreateEmpty },
  { "Rect", EditorCommand::CreateRect },
  { "Ellipse", EditorCommand::CreateEllipse },
  { "Triangle", EditorCommand::CreateTriangle },
  { "Cube", EditorCommand::CreateCube },
  { "Pyramid", EditorCommand::CreatePyramid },
  { "Sphere", EditorCommand::CreateSphere },
} };

EditorToolsPanel::EditorToolsPanel(IRenderWindow* window, Renderer* renderer)
  : m_window(window)
  , m_renderer(renderer)
  , m_visual(1024u)
  , m_fontSize(EditorToolbar::kDefaultFontSize)
{
  m_visual.setSpace(PrimitiveSpace::Pixels);
  m_visual.setLayerHint(RenderLayerId::UI);
  m_visual.setWindow(window);
  m_visual.setRenderer(renderer);
  m_visual.prepare(renderer);
  // Until the module places it: the top of a right dock column.
  m_placement.area = { 1030.0f,
                       GuiToolStyle::kMenuHeight + GuiToolStyle::kTitleHeight,
                       250.0f,
                       320.0f };
  layout();
  rebuildVisual();
}

void
EditorToolsPanel::setFontSize(float sizePt)
{
  const float clamped = std::clamp(sizePt, 8.0f, 48.0f);
  if (std::abs(m_fontSize - clamped) > 0.001f) {
    m_fontSize = clamped;
    layout();
  }
}

void
EditorToolsPanel::setPlacement(const GuiPanelPlacement& placement)
{
  m_placement = placement;
  layout();
}

void
EditorToolsPanel::setAtlas(TextureHandle atlas)
{
  m_atlas = atlas;
  rebuildVisual();
}

bool
EditorToolsPanel::containsScreenPoint(float x, float y) const
{
  return m_placement.visible && m_placement.area.contains(x, y);
}

float
EditorToolsPanel::rowHeight() const
{
  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  return std::max(20.0f, std::round(22.0f * fontScale));
}

float
EditorToolsPanel::addSection(const std::string& label, float y)
{
  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  const float header = std::max(18.0f, std::round(20.0f * fontScale));
  m_sections.push_back(
    { label, { m_placement.area.x, y, m_placement.area.w, header } });
  return y + header + 4.0f;
}

float
EditorToolsPanel::addButtons(
  const std::vector<std::pair<std::string, EditorCommand>>& entries,
  float y)
{
  const float gap = 4.0f;
  const float left = m_placement.area.x + GuiToolStyle::kPad;
  const float width =
    std::max(40.0f, m_placement.area.w - GuiToolStyle::kPad * 2.0f);
  const float count = static_cast<float>(entries.size());
  const float each = (width - gap * (count - 1.0f)) / count;
  float x = left;
  for (const std::pair<std::string, EditorCommand>& entry : entries) {
    m_controls.push_back(
      { ControlKind::Button,
        entry.first,
        entry.second,
        { std::round(x), y, std::round(each), rowHeight() } });
    x += each + gap;
  }
  return y + rowHeight() + gap;
}

void
EditorToolsPanel::layout()
{
  const GuiToolRect& area = m_placement.area;
  const float row = rowHeight();
  m_controls.clear();
  m_sections.clear();
  float y = area.y + 4.0f - m_scroll;
  y = addSection("Mode", y);
  y = addButtons(
    { { "2D", EditorCommand::SetMode2D }, { "3D", EditorCommand::SetMode3D } },
    y);
  y = addSection("Transform", y);
  y = addButtons({ { "Move", EditorCommand::TranslateMode },
                   { "Rotate", EditorCommand::RotateMode },
                   { "Scale", EditorCommand::ScaleMode } },
                 y);
  m_controls.push_back({ ControlKind::Toggle,
                         "Local axes",
                         EditorCommand::ToggleGizmoSpace,
                         { area.x, y, area.w * 0.5f, row } });
  m_controls.push_back({ ControlKind::Toggle,
                         "Snap",
                         EditorCommand::ToggleSnap,
                         { area.x + area.w * 0.5f, y, area.w * 0.5f, row } });
  y += row + 4.0f;
  y = addSection("Create", y);
  for (const ToolEntry& entry : kTools) {
    m_controls.push_back({ ControlKind::Tool,
                           entry.label,
                           entry.command,
                           { area.x, y, area.w, row } });
    y += row;
  }
  m_contentHeight = y + m_scroll - area.y + 4.0f;
}
bool
EditorToolsPanel::active(const Control& control) const
{
  switch (control.command) {
    case EditorCommand::SetMode2D:
      return !m_state.is3D;
    case EditorCommand::SetMode3D:
      return m_state.is3D;
    case EditorCommand::TranslateMode:
      return m_state.gizmoMode == GizmoMode::Translate;
    case EditorCommand::RotateMode:
      return m_state.gizmoMode == GizmoMode::Rotate;
    case EditorCommand::ScaleMode:
      return m_state.gizmoMode == GizmoMode::Scale;
    case EditorCommand::ToggleGizmoSpace:
      return m_state.gizmoSpace == GizmoSpace::Local;
    case EditorCommand::ToggleSnap:
      return m_state.snap;
    default:
      return control.command == m_state.activeTool;
  }
}

int
EditorToolsPanel::controlAt(float x, float y) const
{
  if (!containsScreenPoint(x, y)) {
    return -1;
  }
  for (std::size_t index = 0; index < m_controls.size(); ++index) {
    if (m_controls[index].rect.contains(x, y)) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

EditorCommand
EditorToolsPanel::clickAtForTesting(float x, float y)
{
  layout();
  const int hit = controlAt(x, y);
  return hit >= 0 ? m_controls[static_cast<std::size_t>(hit)].command
                  : EditorCommand::None;
}

bool
EditorToolsPanel::controlCenterForTesting(EditorCommand command,
                                          float* x,
                                          float* y) const
{
  for (const Control& control : m_controls) {
    if (control.command == command) {
      *x = control.rect.x + control.rect.w * 0.5f;
      *y = control.rect.y + control.rect.h * 0.5f;
      return true;
    }
  }
  return false;
}

EditorCommand
EditorToolsPanel::update(InputManager* inputManager, float dt)
{
  (void)dt;
  m_consumedPress = false;
  layout();
  m_pointer.sample(m_placement, m_window, m_renderer, inputManager);
  const float x = m_pointer.x();
  const float y = m_pointer.y();
  EditorCommand command = EditorCommand::None;
  if (containsScreenPoint(x, y)) {
    const float wheel = m_pointer.takeWheel();
    if (wheel != 0.0f) {
      const float maximum =
        std::max(0.0f, m_contentHeight - m_placement.area.h);
      m_scroll = std::clamp(m_scroll - wheel * 40.0f, 0.0f, maximum);
      layout();
    }
  }
  m_hover = controlAt(x, y);
  if (m_pointer.clicked() && containsScreenPoint(x, y)) {
    m_consumedPress = true;
    if (m_hover >= 0) {
      command = m_controls[static_cast<std::size_t>(m_hover)].command;
    }
  }
  rebuildVisual();
  return command;
}

void
EditorToolsPanel::rebuildVisual()
{
  m_visual.clearPrimitives();
  if (!m_placement.visible) {
    return;
  }
  const GuiToolRect& area = m_placement.area;
  m_visual.setPixelClipRect(Rect2{ area.x, area.y, area.w, area.h });
  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  const float font = std::max(10.0f, std::round(13.0f * fontScale));
  const float iconSize =
    std::max(14.0f, std::round(EditorUiAtlas::kIconSize * fontScale));
  for (const Section& section : m_sections) {
    GuiToolStyle::sectionHeader(m_visual, section.rect, section.label);
  }
  for (std::size_t index = 0; index < m_controls.size(); ++index) {
    const Control& control = m_controls[index];
    const bool hovered = m_hover == static_cast<int>(index);
    const bool on = active(control);
    if (control.kind == ControlKind::Button) {
      GuiToolStyle::button(m_visual, control.rect, control.label, hovered, on);
      continue;
    }
    if (control.kind == ControlKind::Toggle) {
      GuiToolStyle::toggle(m_visual, control.rect, control.label, on, hovered);
      continue;
    }
    GuiToolStyle::row(m_visual, control.rect, on, hovered && !on);
    float labelX = control.rect.x + GuiToolStyle::kPad;
    if (m_atlas.isValid()) {
      m_visual.addCenteredSprite(m_atlas,
                                 labelX + iconSize * 0.5f,
                                 control.rect.y + control.rect.h * 0.5f,
                                 iconSize,
                                 iconSize,
                                 EditorUiAtlas::regionFor(control.command));
      labelX += iconSize + 6.0f;
    }
    GuiToolStyle::text(
      m_visual,
      control.label,
      labelX,
      control.rect.y +
        std::max(0.0f, std::round((control.rect.h - font) * 0.5f)) - 1.0f,
      font,
      on ? GuiToolPalette::text : GuiToolPalette::dim);
  }
  GuiToolStyle::scrollbar(
    m_visual,
    GuiToolRect{ area.x + area.w - GuiToolStyle::kScrollbar,
                 area.y,
                 GuiToolStyle::kScrollbar - 1.0f,
                 area.h },
    m_scroll,
    area.h,
    m_contentHeight);
}

bool
EditorToolsPanel::AppendCommands(Renderer* renderer)
{
  if (!m_placement.visible) {
    return true;
  }
  return m_visual.AppendCommands(renderer);
}
