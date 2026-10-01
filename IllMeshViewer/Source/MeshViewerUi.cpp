#include "MeshViewerUi.h"

#include <Illumo/Gui/GuiMenuShell.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

MeshViewerUi::MeshViewerUi(IRenderWindow* window, Renderer* renderer)
  : m_window(window)
  , m_renderer(renderer)
  , m_visual(2048u)
  , m_fontSize(kDefaultFontSize)
  , m_consumedPress(false)
  , m_showGrid(true)
  , m_showWireframe(false)
  , m_showAxes(true)
  , m_showSkybox(true)
  , m_yawDeg(45.0f)
  , m_pitchDeg(25.0f)
  , m_distance(3.5f)
  , m_toastColor(GuiToolPalette::good)
  , m_toastTimer(0.0f)
{
  m_visual.setSpace(PrimitiveSpace::Pixels);
  m_visual.setLayerHint(RenderLayerId::UI);
  m_visual.setWindow(window);
  m_visual.setRenderer(renderer);
  updateLayout();
  m_viewport = {
    0.0f, kHeaderHeight, m_width, m_height - kHeaderHeight - kStatusHeight
  };
  rebuildMenus();
}

void
MeshViewerUi::setMeshMetadata(const MeshMetadata& metadata)
{
  m_metadata = metadata;
}

void
MeshViewerUi::setDisplayOptions(bool showGrid,
                                bool showWireframe,
                                bool showAxes,
                                bool showSkybox)
{
  if (showGrid == m_showGrid && showWireframe == m_showWireframe &&
      showAxes == m_showAxes && showSkybox == m_showSkybox) {
    return;
  }
  m_showGrid = showGrid;
  m_showWireframe = showWireframe;
  m_showAxes = showAxes;
  m_showSkybox = showSkybox;
  rebuildMenus();
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
MeshViewerUi::setPanels(const std::vector<MeshViewerPanelMenuEntry>& panels,
                        bool canDetach)
{
  bool same = panels.size() == m_panels.size() && canDetach == m_canDetach;
  for (std::size_t index = 0; same && index < panels.size(); ++index) {
    same = panels[index].visible == m_panels[index].visible &&
           panels[index].detached == m_panels[index].detached;
  }
  if (same) {
    return;
  }
  m_panels = panels;
  m_canDetach = canDetach;
  rebuildMenus();
}

void
MeshViewerUi::showToast(const std::string& message, ColorRgba color)
{
  m_toastMessage = message;
  m_toastColor = color;
  m_toastTimer = 3.5f;
}

void
MeshViewerUi::rebuildMenus()
{
  m_menus.clear();
  Menu file;
  file.title = "File";
  file.items.push_back({ "Open...", "O", MeshViewerAction::OpenMesh });
  m_menus.push_back(file);

  Menu view;
  view.title = "View";
  view.items.push_back({ "Reset Camera", "R", MeshViewerAction::ResetView });
  MenuItem rule;
  rule.separator = true;
  view.items.push_back(rule);
  view.items.push_back(
    { "Grid", "G", MeshViewerAction::ToggleGrid, true, m_showGrid });
  view.items.push_back(
    { "Axes", "", MeshViewerAction::ToggleAxes, true, m_showAxes });
  view.items.push_back(
    { "Sky", "B", MeshViewerAction::ToggleSkybox, true, m_showSkybox });
  view.items.push_back({ "Wireframe",
                         "X",
                         MeshViewerAction::ToggleWireframe,
                         true,
                         m_showWireframe });
  if (!m_panels.empty()) {
    view.items.push_back(rule);
    for (const MeshViewerPanelMenuEntry& panel : m_panels) {
      view.items.push_back(
        { panel.title, "", panel.toggle, true, panel.visible });
    }
    view.items.push_back(rule);
    for (const MeshViewerPanelMenuEntry& panel : m_panels) {
      view.items.push_back(
        { (panel.detached ? "Dock " : "Pop Out ") + panel.title,
          "",
          panel.popOut,
          panel.detached || m_canDetach,
          false });
    }
    view.items.push_back(rule);
    view.items.push_back({ "Reset Layout", "", MeshViewerAction::ResetLayout });
  }
  m_menus.push_back(view);
}

std::vector<std::string>
MeshViewerUi::titles() const
{
  std::vector<std::string> result;
  for (const Menu& menu : m_menus) {
    result.push_back(menu.title);
  }
  return result;
}

std::vector<GuiToolStyle::MenuItem>
MeshViewerUi::styleItems(const Menu& menu) const
{
  std::vector<GuiToolStyle::MenuItem> items;
  for (const MenuItem& entry : menu.items) {
    GuiToolStyle::MenuItem styled;
    styled.label = entry.label;
    styled.hint = entry.hint;
    styled.enabled = entry.enabled;
    styled.checked = entry.checked;
    styled.separator = entry.separator;
    items.push_back(styled);
  }
  return items;
}

void
MeshViewerUi::updateLayout()
{
  const GuiPanelFit view = GuiPanelLayout::viewport(m_window, m_renderer);
  m_width = view.virtualWidth;
  m_height = view.virtualHeight;
}

GuiToolRect
MeshViewerUi::barRect() const
{
  return { 0.0f, 0.0f, m_width, kHeaderHeight };
}

GuiToolRect
MeshViewerUi::dropdownRect() const
{
  if (m_openMenu < 0 ||
      static_cast<std::size_t>(m_openMenu) >= m_menus.size()) {
    return {};
  }
  const GuiToolRect title =
    GuiToolStyle::menuRect(barRect(), titles(), m_openMenu);
  const std::vector<GuiToolStyle::MenuItem> items =
    styleItems(m_menus[static_cast<std::size_t>(m_openMenu)]);
  float height = 6.0f;
  for (const GuiToolStyle::MenuItem& entry : items) {
    height += entry.separator ? 7.0f : GuiToolStyle::kRowHeight;
  }
  return { title.x, kHeaderHeight, GuiToolStyle::dropdownWidth(items), height };
}

GuiToolRect
MeshViewerUi::emptyCardRect() const
{
  if (m_metadata.hasMesh) {
    return {};
  }
  const float width = std::min(m_viewport.w - 24.0f, 380.0f);
  const float height = 64.0f;
  return { m_viewport.x + (m_viewport.w - width) * 0.5f,
           m_viewport.y + (m_viewport.h - height) * 0.45f,
           width,
           height };
}

int
MeshViewerUi::menuAt(float x, float y) const
{
  const GuiToolRect bar = barRect();
  if (!bar.contains(x, y)) {
    return -1;
  }
  const std::vector<std::string> names = titles();
  for (int index = 0; index < static_cast<int>(names.size()); ++index) {
    if (GuiToolStyle::menuRect(bar, names, index).contains(x, y)) {
      return index;
    }
  }
  return -1;
}

int
MeshViewerUi::itemAt(float x, float y) const
{
  if (m_openMenu < 0 ||
      static_cast<std::size_t>(m_openMenu) >= m_menus.size()) {
    return -1;
  }
  const GuiToolRect drop = dropdownRect();
  return GuiToolStyle::dropdownItemAt(
    drop.x,
    drop.y,
    styleItems(m_menus[static_cast<std::size_t>(m_openMenu)]),
    x,
    y);
}

bool
MeshViewerUi::containsScreenPoint(float x, float y) const
{
  if (barRect().contains(x, y)) {
    return true;
  }
  if (m_openMenu >= 0 && dropdownRect().contains(x, y)) {
    return true;
  }
  if (y >= m_height - kStatusHeight) {
    return true;
  }
  return emptyCardRect().contains(x, y);
}

MeshViewerAction
MeshViewerUi::clickAt(float x, float y)
{
  if (m_openMenu >= 0) {
    const int index = itemAt(x, y);
    if (index >= 0) {
      const MenuItem& chosen = m_menus[static_cast<std::size_t>(m_openMenu)]
                                 .items[static_cast<std::size_t>(index)];
      if (!chosen.enabled) {
        return MeshViewerAction::None;
      }
      m_openMenu = -1;
      return chosen.action;
    }
  }
  const int menu = menuAt(x, y);
  if (menu >= 0) {
    m_openMenu = m_openMenu == menu ? -1 : menu;
    return MeshViewerAction::None;
  }
  m_openMenu = -1;
  return MeshViewerAction::None;
}

MeshViewerAction
MeshViewerUi::clickAtForTesting(float x, float y)
{
  updateLayout();
  return clickAt(x, y);
}

bool
MeshViewerUi::menuItemCenterForTesting(MeshViewerAction action,
                                       float* x,
                                       float* y)
{
  updateLayout();
  for (std::size_t menu = 0; menu < m_menus.size(); ++menu) {
    float top = kHeaderHeight + 3.0f;
    for (const MenuItem& entry : m_menus[menu].items) {
      const float height = entry.separator ? 7.0f : GuiToolStyle::kRowHeight;
      if (!entry.separator && entry.action == action) {
        m_openMenu = static_cast<int>(menu);
        const GuiToolRect drop = dropdownRect();
        *x = drop.x + drop.w * 0.5f;
        *y = top + height * 0.5f;
        return true;
      }
      top += height;
    }
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
  updateLayout();
  MeshViewerAction action = MeshViewerAction::None;
  if (m_window != nullptr && inputManager != nullptr) {
    m_pointer.sample(m_placement, m_window, m_renderer, inputManager);
    const float x = m_pointer.x();
    const float y = m_pointer.y();
    m_hoverMenu = menuAt(x, y);
    m_hoverItem = itemAt(x, y);
    if (m_openMenu >= 0 && m_hoverMenu >= 0) {
      m_openMenu = m_hoverMenu;
    }
    if (m_pointer.clicked()) {
      const bool menuWasOpen = m_openMenu >= 0;
      action = clickAt(x, y);
      m_consumedPress = action != MeshViewerAction::None || menuWasOpen ||
                        containsScreenPoint(x, y);
    }
  }
  rebuildVisual();
  return action;
}

void
MeshViewerUi::rebuildVisual()
{
  m_visual.clearPrimitives();
  GuiToolStyle::menuBar(m_visual, barRect(), titles(), m_openMenu, m_hoverMenu);
  GuiToolStyle::text(
    m_visual,
    "IllMeshViewer",
    m_width - GuiToolStyle::kPad -
      GuiToolStyle::textWidth("IllMeshViewer", GuiToolStyle::kSmallFontSize),
    6.0f,
    GuiToolStyle::kSmallFontSize,
    GuiToolPalette::faint);

  const GuiToolRect card = emptyCardRect();
  if (card.w > 0.0f) {
    m_visual.addFilledRect(card.x, card.y, card.w, card.h, GuiToolPalette::bar);
    m_visual.addOutlineRect(
      card.x, card.y, card.w, card.h, GuiToolPalette::border, 1.0f);
    GuiToolStyle::fittedText(m_visual,
                             "Nothing open",
                             card.x + 14.0f,
                             card.y + 12.0f,
                             card.w - 28.0f,
                             GuiToolStyle::kFontSize,
                             GuiToolPalette::text);
    GuiToolStyle::fittedText(m_visual,
                             "Press O or use File > Open (.obj or .ilsc)",
                             card.x + 14.0f,
                             card.y + 36.0f,
                             card.w - 28.0f,
                             GuiToolStyle::kSmallFontSize,
                             GuiToolPalette::dim);
  }

  if (m_toastTimer > 0.0f && !m_toastMessage.empty()) {
    const float fade = std::min(1.0f, m_toastTimer / 0.3f);
    const unsigned char alpha = static_cast<unsigned char>(255.0f * fade);
    const float width =
      GuiToolStyle::textWidth(m_toastMessage, GuiToolStyle::kFontSize) + 28.0f;
    const float toastX = m_viewport.x + (m_viewport.w - width) * 0.5f;
    const float toastY = m_viewport.y + 12.0f;
    ColorRgba background = GuiToolPalette::bar;
    background.a = alpha;
    ColorRgba border = GuiToolPalette::border;
    border.a = alpha;
    ColorRgba stripe = m_toastColor;
    stripe.a = alpha;
    ColorRgba text = GuiToolPalette::text;
    text.a = alpha;
    m_visual.addFilledRect(toastX, toastY, width, 26.0f, background);
    m_visual.addOutlineRect(toastX, toastY, width, 26.0f, border, 1.0f);
    m_visual.addFilledRect(toastX, toastY, 3.0f, 26.0f, stripe);
    GuiToolStyle::text(m_visual,
                       m_toastMessage,
                       toastX + 14.0f,
                       toastY + 5.0f,
                       GuiToolStyle::kFontSize,
                       text);
  }

  std::ostringstream camera;
  camera << "Yaw " << static_cast<int>(std::round(m_yawDeg)) << "  Pitch "
         << static_cast<int>(std::round(m_pitchDeg)) << "  Dist " << std::fixed
         << std::setprecision(1) << m_distance;
  GuiToolStyle::statusBar(
    m_visual,
    GuiToolRect{ 0.0f, m_height - kStatusHeight, m_width, kStatusHeight },
    "LMB/RMB orbit  MMB/WASD pan  Wheel zoom  Q/E roll  O open  R reset  "
    "~ console",
    camera.str());

  if (m_openMenu >= 0 &&
      static_cast<std::size_t>(m_openMenu) < m_menus.size()) {
    const GuiToolRect drop = dropdownRect();
    GuiToolStyle::dropdown(
      m_visual,
      drop.x,
      drop.y,
      styleItems(m_menus[static_cast<std::size_t>(m_openMenu)]),
      m_hoverItem);
  }
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
