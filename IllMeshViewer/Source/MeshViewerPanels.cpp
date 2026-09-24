#include "MeshViewerPanels.h"

#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

static void
prepareVisual(GameVisual& visual, IRenderWindow* window, Renderer* renderer)
{
  visual.setSpace(PrimitiveSpace::Pixels);
  visual.setLayerHint(RenderLayerId::UI);
  visual.setWindow(window);
  visual.setRenderer(renderer);
  visual.prepare(renderer);
}

MeshViewerInfoPanel::MeshViewerInfoPanel(IRenderWindow* window,
                                         Renderer* renderer)
  : m_visual(512u)
{
  prepareVisual(m_visual, window, renderer);
}

void
MeshViewerInfoPanel::setPlacement(const GuiPanelPlacement& placement)
{
  m_placement = placement;
}

void
MeshViewerInfoPanel::update()
{
  m_visual.clearPrimitives();
  if (!m_placement.visible) {
    return;
  }
  const GuiToolRect& area = m_placement.area;
  m_visual.setPixelClipRect(Rect2{ area.x, area.y, area.w, area.h });
  if (!m_metadata.hasMesh) {
    GuiToolStyle::fittedText(m_visual,
                             "Nothing open",
                             area.x + GuiToolStyle::kPad,
                             area.y + 8.0f,
                             area.w - GuiToolStyle::kPad * 2.0f,
                             GuiToolStyle::kSmallFontSize,
                             GuiToolPalette::faint);
    return;
  }
  std::vector<std::pair<std::string, std::string>> rows;
  rows.push_back(
    { m_metadata.isScene ? "Scene" : "Mesh", m_metadata.filename });
  if (m_metadata.isScene) {
    rows.push_back({ "Nodes", std::to_string(m_metadata.nodeCount) });
    rows.push_back({ "Assets", std::to_string(m_metadata.assetCount) });
    rows.push_back({ "Missing", std::to_string(m_metadata.missingCount) });
  } else {
    rows.push_back({ "Vertices", std::to_string(m_metadata.vertexCount) });
    rows.push_back({ "Triangles", std::to_string(m_metadata.triangleCount) });
    rows.push_back({ "Submeshes", std::to_string(m_metadata.submeshCount) });
    rows.push_back({ "Materials", std::to_string(m_metadata.materialCount) });
  }
  std::ostringstream size;
  size << std::fixed << std::setprecision(2) << m_metadata.dimensions.x << " x "
       << m_metadata.dimensions.y << " x " << m_metadata.dimensions.z;
  rows.push_back({ "Size", size.str() });
  float y = area.y + 4.0f;
  for (const std::pair<std::string, std::string>& row : rows) {
    GuiToolStyle::labelValue(
      m_visual,
      GuiToolRect{ area.x, y, area.w, GuiToolStyle::kRowHeight },
      row.first,
      row.second);
    y += GuiToolStyle::kRowHeight;
  }
  if (m_metadata.missingCount > 0) {
    GuiToolStyle::fittedText(m_visual,
                             "Missing assets draw as placeholders",
                             area.x + GuiToolStyle::kPad,
                             y + 4.0f,
                             area.w - GuiToolStyle::kPad * 2.0f,
                             GuiToolStyle::kSmallFontSize,
                             GuiToolPalette::warning);
  }
}

bool
MeshViewerInfoPanel::AppendCommands(Renderer* renderer)
{
  if (!m_placement.visible) {
    return true;
  }
  return m_visual.AppendCommands(renderer);
}

MeshViewerDisplayPanel::MeshViewerDisplayPanel(IRenderWindow* window,
                                               Renderer* renderer)
  : m_window(window)
  , m_renderer(renderer)
  , m_visual(1024u)
{
  prepareVisual(m_visual, window, renderer);
  m_placement.area = { 1030.0f, 46.0f, 250.0f, 480.0f };
  layout();
}

void
MeshViewerDisplayPanel::setPlacement(const GuiPanelPlacement& placement)
{
  if (placement.surface != m_placement.surface || !placement.visible) {
    m_activeSlider = -1;
  }
  m_placement = placement;
  layout();
}

void
MeshViewerDisplayPanel::setSettings(const MeshViewerDisplaySettings& settings)
{
  m_settings = settings;
  layout();
}

bool
MeshViewerDisplayPanel::containsScreenPoint(float x, float y) const
{
  return m_placement.visible && m_placement.area.contains(x, y);
}

float
MeshViewerDisplayPanel::addSection(const std::string& label, float y)
{
  m_sections.push_back(
    { label, { m_placement.area.x, y, m_placement.area.w, 20.0f } });
  return y + 22.0f;
}

float
MeshViewerDisplayPanel::addToggle(const std::string& label,
                                  MeshViewerAction action,
                                  const std::string& key,
                                  bool on,
                                  float y)
{
  Control control;
  control.kind = ControlKind::Toggle;
  control.label = label;
  control.action = action;
  control.key = key;
  control.on = on;
  control.rect = { m_placement.area.x,
                   y,
                   m_placement.area.w - GuiToolStyle::kScrollbar,
                   GuiToolStyle::kRowHeight };
  m_controls.push_back(control);
  return y + GuiToolStyle::kRowHeight;
}

float
MeshViewerDisplayPanel::addSlider(const std::string& label,
                                  const std::string& key,
                                  float value,
                                  float minimum,
                                  float maximum,
                                  int digits,
                                  float y)
{
  Control control;
  control.kind = ControlKind::Slider;
  control.label = label;
  control.key = key;
  control.value = value;
  control.minimum = minimum;
  control.maximum = maximum;
  control.digits = digits;
  control.rect = { m_placement.area.x,
                   y,
                   m_placement.area.w - GuiToolStyle::kScrollbar,
                   GuiToolStyle::kRowHeight + 2.0f };
  m_controls.push_back(control);
  return y + GuiToolStyle::kRowHeight + 2.0f;
}

void
MeshViewerDisplayPanel::layout()
{
  m_controls.clear();
  m_sections.clear();
  const MeshViewerDisplaySettings& s = m_settings;
  float y = m_placement.area.y + 4.0f - m_scroll;
  y = addSection("View", y);
  y = addToggle("Grid", MeshViewerAction::ToggleGrid, "", s.grid, y);
  y = addToggle("Axes", MeshViewerAction::ToggleAxes, "", s.axes, y);
  y = addToggle("Sky", MeshViewerAction::ToggleSkybox, "", s.sky, y);
  y = addToggle(
    "Wireframe", MeshViewerAction::ToggleWireframe, "", s.wireframe, y);
  y = addSection("Lighting", y + 4.0f);
  y = addToggle(
    "Lighting", MeshViewerAction::None, "lightingEnabled", s.lighting, y);
  y = addSlider("Light X", "lightDirX", s.lightDirection.x, -1.0f, 1.0f, 2, y);
  y = addSlider("Light Y", "lightDirY", s.lightDirection.y, -1.0f, 1.0f, 2, y);
  y = addSlider("Light Z", "lightDirZ", s.lightDirection.z, -1.0f, 1.0f, 2, y);
  y = addSlider("Ambient", "ambient", s.ambient, 0.0f, 1.0f, 2, y);
  y = addSection("Shadows", y + 4.0f);
  y = addToggle(
    "Shadows", MeshViewerAction::None, "shadowsEnabled", s.shadows, y);
  y = addToggle(
    "Soft edges", MeshViewerAction::None, "shadowPcf", s.softShadows, y);
  y = addSlider("Radius", "shadowRadius", s.shadowRadius, 0.5f, 20.0f, 1, y);
  y = addSlider("Bias", "shadowBias", s.shadowBias, 0.0f, 0.01f, 4, y);
  y = addSection("Motion blur", y + 4.0f);
  y = addToggle("Motion blur",
                MeshViewerAction::None,
                "motionBlurEnabled",
                s.motionBlur,
                y);
  y = addSlider(
    "Amount", "motionBlurAmount", s.motionBlurAmount, 0.0f, 2.0f, 2, y);
  m_contentHeight = y + m_scroll - m_placement.area.y + 4.0f;
}

int
MeshViewerDisplayPanel::controlAt(float x, float y) const
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

std::string
MeshViewerDisplayPanel::format(float value, int digits)
{
  std::ostringstream text;
  text << std::fixed << std::setprecision(digits) << value;
  return text.str();
}

void
MeshViewerDisplayPanel::sliderEdits(
  const Control& control,
  float value,
  std::vector<MeshViewerDisplayEdit>& edits) const
{
  if (control.key == "ambient") {
    // Brightness of the default blue-grey tint (0.2, 0.22, 0.25).
    edits.push_back(
      { MeshViewerAction::None, "ambientColorR", format(value * 0.909f, 3) });
    edits.push_back(
      { MeshViewerAction::None, "ambientColorG", format(value, 3) });
    edits.push_back(
      { MeshViewerAction::None, "ambientColorB", format(value * 1.136f, 3) });
    return;
  }
  edits.push_back(
    { MeshViewerAction::None, control.key, format(value, control.digits + 1) });
}

bool
MeshViewerDisplayPanel::controlPointForTesting(const std::string& label,
                                               float fraction,
                                               float* x,
                                               float* y) const
{
  for (const Control& control : m_controls) {
    if (control.label != label) {
      continue;
    }
    if (control.kind == ControlKind::Slider) {
      const GuiToolRect track = GuiToolStyle::sliderTrack(control.rect);
      *x = track.x + track.w * std::clamp(fraction, 0.0f, 1.0f);
    } else {
      *x = control.rect.x + 20.0f;
    }
    *y = control.rect.y + control.rect.h * 0.5f;
    return true;
  }
  return false;
}

std::vector<MeshViewerDisplayEdit>
MeshViewerDisplayPanel::update(InputManager* input, float dt)
{
  (void)dt;
  std::vector<MeshViewerDisplayEdit> edits;
  m_consumedPress = false;
  layout();
  m_pointer.sample(m_placement, m_window, m_renderer, input);
  const float x = m_pointer.x();
  const float y = m_pointer.y();
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
      const Control& control = m_controls[static_cast<std::size_t>(m_hover)];
      if (control.kind == ControlKind::Toggle) {
        edits.push_back(
          { control.action,
            control.key,
            control.key.empty() ? std::string() : (control.on ? "0" : "1") });
      } else {
        m_activeSlider = m_hover;
      }
    }
  }
  if (m_activeSlider >= 0 &&
      static_cast<std::size_t>(m_activeSlider) < m_controls.size()) {
    if (!m_pointer.pressed()) {
      m_activeSlider = -1;
    } else {
      m_consumedPress = true;
      const Control& control =
        m_controls[static_cast<std::size_t>(m_activeSlider)];
      const GuiToolRect track = GuiToolStyle::sliderTrack(control.rect);
      const float fraction =
        track.w > 0.0f ? std::clamp((x - track.x) / track.w, 0.0f, 1.0f) : 0.0f;
      const float value =
        control.minimum + (control.maximum - control.minimum) * fraction;
      if (format(value, control.digits + 1) !=
          format(control.value, control.digits + 1)) {
        sliderEdits(control, value, edits);
      }
    }
  }
  rebuildVisual();
  return edits;
}

void
MeshViewerDisplayPanel::rebuildVisual()
{
  m_visual.clearPrimitives();
  if (!m_placement.visible) {
    return;
  }
  const GuiToolRect& area = m_placement.area;
  m_visual.setPixelClipRect(Rect2{ area.x, area.y, area.w, area.h });
  for (const Section& section : m_sections) {
    GuiToolStyle::sectionHeader(m_visual, section.rect, section.label);
  }
  for (std::size_t index = 0; index < m_controls.size(); ++index) {
    const Control& control = m_controls[index];
    const bool hovered = m_hover == static_cast<int>(index);
    if (control.kind == ControlKind::Toggle) {
      GuiToolStyle::toggle(
        m_visual, control.rect, control.label, control.on, hovered);
      continue;
    }
    const float span = control.maximum - control.minimum;
    GuiToolStyle::slider(m_visual,
                         control.rect,
                         control.label,
                         span > 0.0f ? (control.value - control.minimum) / span
                                     : 0.0f,
                         format(control.value, control.digits),
                         hovered,
                         m_activeSlider == static_cast<int>(index));
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
MeshViewerDisplayPanel::AppendCommands(Renderer* renderer)
{
  if (!m_placement.visible) {
    return true;
  }
  return m_visual.AppendCommands(renderer);
}
