#include <Illumo/Gui/GuiPanelDock.h>
#include <Illumo/Gui/PanelSurfaces.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cmath>
#include <sstream>

const GuiDockView GuiPanelDock::kHiddenView{};

void
GuiPanelDock::addPanel(const GuiDockPanelSpec& spec)
{
  m_specs.push_back(spec);
  Panel panel;
  panel.weight = spec.weight > 0.0f ? spec.weight : 1.0f;
  const float offset = 30.0f * static_cast<float>(m_panels.size());
  panel.windowX = 40.0f + offset;
  panel.windowY = 60.0f + offset;
  m_panels.push_back(panel);
}

bool
GuiPanelDock::canDetach() const
{
  return m_surfaces != nullptr && m_surfaces->available();
}

void
GuiPanelDock::setColumnWidth(GuiDockSide side, float width)
{
  (side == GuiDockSide::Left ? m_leftWidth : m_rightWidth) =
    std::max(kMinimumColumn, width);
}

float
GuiPanelDock::columnWidth(GuiDockSide side) const
{
  return side == GuiDockSide::Left ? m_leftWidth : m_rightWidth;
}

int
GuiPanelDock::find(const std::string& id) const
{
  for (std::size_t index = 0; index < m_specs.size(); ++index) {
    if (m_specs[index].id == id) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

int
GuiPanelDock::findSurface(std::uint32_t surface) const
{
  for (std::size_t index = 0; index < m_specs.size(); ++index) {
    if (m_specs[index].surface == surface) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

const GuiDockView&
GuiPanelDock::view(const std::string& id) const
{
  const int index = find(id);
  return index < 0 ? kHiddenView
                   : m_panels[static_cast<std::size_t>(index)].view;
}

GuiDockMode
GuiPanelDock::mode(const std::string& id) const
{
  const int index = find(id);
  return index < 0 ? GuiDockMode::Hidden
                   : m_panels[static_cast<std::size_t>(index)].mode;
}

std::vector<int>
GuiPanelDock::docked(GuiDockSide side) const
{
  std::vector<int> result;
  for (std::size_t index = 0; index < m_specs.size(); ++index) {
    const GuiDockMode mode = m_panels[index].mode;
    if (m_specs[index].side == side &&
        (mode == GuiDockMode::Docked || mode == GuiDockMode::Opening)) {
      result.push_back(static_cast<int>(index));
    }
  }
  return result;
}

std::vector<GuiToolTitleButton>
GuiPanelDock::buttons(int panel) const
{
  if (m_panels[static_cast<std::size_t>(panel)].mode == GuiDockMode::Detached) {
    return { GuiToolTitleButton::Dock };
  }
  if (canDetach()) {
    return { GuiToolTitleButton::Hide, GuiToolTitleButton::PopOut };
  }
  return { GuiToolTitleButton::Hide };
}

void
GuiPanelDock::layout(float width,
                     float height,
                     float top,
                     float bottom,
                     float scale)
{
  m_width = std::max(0.0f, width);
  m_height = std::max(0.0f, height);
  m_top = std::max(0.0f, top);
  m_bottom = std::max(0.0f, bottom);
  m_scale = scale > 0.0f ? scale : 1.0f;
  const float areaTop = m_top;
  const float areaHeight = std::max(0.0f, m_height - m_top - m_bottom);
  const std::vector<int> left = docked(GuiDockSide::Left);
  const std::vector<int> right = docked(GuiDockSide::Right);
  // Each column may take at most 45% of the window, and both together leave
  // a usable centre.
  const float limit = std::max(kMinimumColumn, m_width * 0.45f);
  float leftWidth =
    left.empty() ? 0.0f : std::clamp(m_leftWidth, kMinimumColumn, limit);
  float rightWidth =
    right.empty() ? 0.0f : std::clamp(m_rightWidth, kMinimumColumn, limit);
  const float room = std::max(0.0f, m_width - 120.0f);
  if (leftWidth + rightWidth > room && leftWidth + rightWidth > 0.0f) {
    const float shrink = room / (leftWidth + rightWidth);
    leftWidth *= shrink;
    rightWidth *= shrink;
  }
  for (Panel& panel : m_panels) {
    panel.view = GuiDockView{};
  }
  m_rowSplitters.clear();
  const float splitter = GuiToolStyle::kSplitter;
  for (int column = 0; column < 2; ++column) {
    const std::vector<int>& ids = column == 0 ? left : right;
    if (ids.empty()) {
      continue;
    }
    const float columnWidth = column == 0 ? leftWidth : rightWidth;
    const float x = column == 0 ? 0.0f : m_width - rightWidth;
    const float available = std::max(
      0.0f, areaHeight - splitter * static_cast<float>(ids.size() - 1));
    float totalWeight = 0.0f;
    for (int id : ids) {
      totalWeight += m_panels[static_cast<std::size_t>(id)].weight;
    }
    float y = areaTop;
    for (std::size_t position = 0; position < ids.size(); ++position) {
      Panel& panel = m_panels[static_cast<std::size_t>(ids[position])];
      const bool last = position + 1 == ids.size();
      const float share =
        totalWeight > 0.0f ? available * panel.weight / totalWeight : 0.0f;
      const float panelHeight =
        last ? std::max(0.0f, areaTop + areaHeight - y) : std::floor(share);
      panel.view.visible = true;
      panel.view.surface = IPanelSurfaces::kMainSurface;
      panel.view.frame = { x, y, columnWidth, panelHeight };
      panel.view.title = { x, y, columnWidth, GuiToolStyle::kTitleHeight };
      panel.view.content = {
        x,
        y + GuiToolStyle::kTitleHeight,
        columnWidth,
        std::max(0.0f, panelHeight - GuiToolStyle::kTitleHeight)
      };
      y += panelHeight;
      if (!last) {
        m_rowSplitters.push_back(
          { ids[position], { x, y, columnWidth, splitter } });
        y += splitter;
      }
    }
  }
  m_leftSplitter = left.empty()
                     ? GuiToolRect{}
                     : GuiToolRect{ leftWidth, areaTop, splitter, areaHeight };
  m_rightSplitter = right.empty()
                      ? GuiToolRect{}
                      : GuiToolRect{ m_width - rightWidth - splitter,
                                     areaTop,
                                     splitter,
                                     areaHeight };
  const float centreLeft = left.empty() ? 0.0f : leftWidth + splitter;
  const float centreRight =
    right.empty() ? m_width : m_width - rightWidth - splitter;
  m_center = {
    centreLeft, areaTop, std::max(0.0f, centreRight - centreLeft), areaHeight
  };
  for (std::size_t index = 0; index < m_specs.size(); ++index) {
    Panel& panel = m_panels[index];
    if (panel.mode != GuiDockMode::Detached || m_surfaces == nullptr) {
      continue;
    }
    const std::array<int, 2> size = m_surfaces->size(m_specs[index].surface);
    const float windowWidth = static_cast<float>(size[0]) / m_scale;
    const float windowHeight = static_cast<float>(size[1]) / m_scale;
    panel.view.visible = true;
    panel.view.surface = m_specs[index].surface;
    panel.view.frame = { 0.0f, 0.0f, windowWidth, windowHeight };
    panel.view.title = { 0.0f, 0.0f, windowWidth, GuiToolStyle::kTitleHeight };
    panel.view.content = { 0.0f,
                           GuiToolStyle::kTitleHeight,
                           windowWidth,
                           std::max(
                             0.0f, windowHeight - GuiToolStyle::kTitleHeight) };
  }
}

bool
GuiPanelDock::openWindow(int index, float x, float y)
{
  if (!canDetach()) {
    return false;
  }
  const GuiDockPanelSpec& spec = m_specs[static_cast<std::size_t>(index)];
  Panel& panel = m_panels[static_cast<std::size_t>(index)];
  const float width =
    panel.windowWidth > 0.0f ? panel.windowWidth : spec.detachedWidth;
  const float height =
    panel.windowHeight > 0.0f ? panel.windowHeight : spec.detachedHeight;
  if (!m_surfaces->open(spec.surface,
                        spec.title,
                        static_cast<int>(std::lround(x * m_scale)),
                        static_cast<int>(std::lround(y * m_scale)),
                        static_cast<int>(std::lround(width * m_scale)),
                        static_cast<int>(std::lround(height * m_scale)))) {
    return false;
  }
  panel.mode = GuiDockMode::Opening;
  panel.wantsWindow = false;
  panel.windowX = x;
  panel.windowY = y;
  panel.windowWidth = width;
  panel.windowHeight = height;
  return true;
}

bool
GuiPanelDock::detach(const std::string& id)
{
  const int index = find(id);
  if (index < 0) {
    return false;
  }
  const Panel& panel = m_panels[static_cast<std::size_t>(index)];
  if (panel.mode == GuiDockMode::Detached ||
      panel.mode == GuiDockMode::Opening) {
    return true;
  }
  return openWindow(index, panel.windowX, panel.windowY);
}

void
GuiPanelDock::dock(const std::string& id)
{
  const int index = find(id);
  if (index < 0) {
    return;
  }
  Panel& panel = m_panels[static_cast<std::size_t>(index)];
  if ((panel.mode == GuiDockMode::Detached ||
       panel.mode == GuiDockMode::Opening) &&
      m_surfaces != nullptr) {
    m_surfaces->close(m_specs[static_cast<std::size_t>(index)].surface);
    Logger::LogInfo(m_specs[static_cast<std::size_t>(index)].title +
                    " panel docked");
  }
  panel.mode = GuiDockMode::Docked;
  panel.wantsWindow = false;
}

void
GuiPanelDock::setHidden(const std::string& id, bool hidden)
{
  const int index = find(id);
  if (index < 0) {
    return;
  }
  Panel& panel = m_panels[static_cast<std::size_t>(index)];
  if (hidden) {
    dock(id);
    panel.mode = GuiDockMode::Hidden;
  } else if (panel.mode == GuiDockMode::Hidden) {
    panel.mode = GuiDockMode::Docked;
  }
}

void
GuiPanelDock::reset()
{
  for (std::size_t index = 0; index < m_specs.size(); ++index) {
    dock(m_specs[index].id);
    Panel& panel = m_panels[index];
    panel.weight = m_specs[index].weight > 0.0f ? m_specs[index].weight : 1.0f;
    const float offset = 30.0f * static_cast<float>(index);
    panel.windowX = 40.0f + offset;
    panel.windowY = 60.0f + offset;
    panel.windowWidth = 0.0f;
    panel.windowHeight = 0.0f;
  }
  m_leftWidth = kDefaultColumn;
  m_rightWidth = kDefaultColumn;
}

void
GuiPanelDock::update(const GuiDockPointer& main)
{
  m_consumed.clear();
  handleEvents();
  handleMain(main);
  handleDetached();
}

void
GuiPanelDock::handleEvents()
{
  if (m_surfaces == nullptr) {
    for (Panel& panel : m_panels) {
      if (panel.mode == GuiDockMode::Detached ||
          panel.mode == GuiDockMode::Opening) {
        panel.mode = GuiDockMode::Docked;
      }
      panel.wantsWindow = false;
    }
    return;
  }
  const std::array<int, 2> origin =
    m_surfaces->origin(IPanelSurfaces::kMainSurface);
  for (const PanelSurfaceEvent& event : m_surfaces->takeEvents()) {
    const int index = findSurface(event.surface);
    if (index < 0) {
      continue;
    }
    Panel& panel = m_panels[static_cast<std::size_t>(index)];
    const std::string& id = m_specs[static_cast<std::size_t>(index)].id;
    switch (event.kind) {
      case PanelSurfaceEvent::Kind::Opened:
        if (panel.mode == GuiDockMode::Opening) {
          panel.mode = GuiDockMode::Detached;
          Logger::LogInfo(m_specs[static_cast<std::size_t>(index)].title +
                          " panel detached into its own window");
        }
        break;
      case PanelSurfaceEvent::Kind::Failed:
        if (panel.mode == GuiDockMode::Opening) {
          panel.mode = GuiDockMode::Docked;
          Logger::LogWarning("The host could not open a window for the " +
                             m_specs[static_cast<std::size_t>(index)].title +
                             " panel; it stays docked");
        }
        break;
      case PanelSurfaceEvent::Kind::CloseRequested:
        dock(id);
        break;
      case PanelSurfaceEvent::Kind::Moved:
        panel.windowX = static_cast<float>(event.x - origin[0]) / m_scale;
        panel.windowY = static_cast<float>(event.y - origin[1]) / m_scale;
        break;
      case PanelSurfaceEvent::Kind::Resized:
        panel.windowWidth = static_cast<float>(event.x) / m_scale;
        panel.windowHeight = static_cast<float>(event.y) / m_scale;
        break;
      default:
        break;
    }
  }
  for (std::size_t index = 0; index < m_panels.size(); ++index) {
    Panel& panel = m_panels[index];
    const PanelSurfaceState state = m_surfaces->state(m_specs[index].surface);
    // The host may lose a window on its own; the panel comes home.
    if (panel.mode == GuiDockMode::Detached &&
        state != PanelSurfaceState::Open) {
      panel.mode = GuiDockMode::Docked;
      Logger::LogWarning(m_specs[index].title +
                         " panel window was lost; the panel docked again");
    }
    if (panel.wantsWindow) {
      if (!canDetach() ||
          !openWindow(static_cast<int>(index), panel.windowX, panel.windowY)) {
        panel.wantsWindow = false;
      }
    }
  }
}

void
GuiPanelDock::handleMain(const GuiDockPointer& pointer)
{
  m_mainPointer = pointer;
  const bool press = pointer.down && !m_previousDown;
  const bool release = !pointer.down && m_previousDown;
  m_previousDown = pointer.down;
  for (std::size_t index = 0; index < m_panels.size(); ++index) {
    Panel& panel = m_panels[index];
    if (panel.view.surface != IPanelSurfaces::kMainSurface) {
      continue;
    }
    panel.hoveredButton = -1;
    if (!panel.view.visible ||
        !panel.view.title.contains(pointer.x, pointer.y)) {
      continue;
    }
    const std::vector<GuiToolTitleButton> row =
      buttons(static_cast<int>(index));
    for (std::size_t button = 0; button < row.size(); ++button) {
      if (GuiToolStyle::titleButtonRect(panel.view.title,
                                        static_cast<int>(button))
            .contains(pointer.x, pointer.y)) {
        panel.hoveredButton = static_cast<int>(button);
      }
    }
  }
  if (press) {
    for (std::size_t index = 0; index < m_panels.size(); ++index) {
      Panel& panel = m_panels[index];
      if (!panel.view.visible ||
          panel.view.surface != IPanelSurfaces::kMainSurface ||
          !panel.view.title.contains(pointer.x, pointer.y)) {
        continue;
      }
      m_consumed.push_back(IPanelSurfaces::kMainSurface);
      const std::vector<GuiToolTitleButton> row =
        buttons(static_cast<int>(index));
      if (panel.hoveredButton >= 0) {
        const GuiToolTitleButton action =
          row[static_cast<std::size_t>(panel.hoveredButton)];
        if (action == GuiToolTitleButton::Hide) {
          setHidden(m_specs[index].id, true);
        } else if (action == GuiToolTitleButton::PopOut) {
          openWindow(static_cast<int>(index),
                     panel.view.frame.x + 24.0f,
                     panel.view.frame.y + 24.0f);
        }
        return;
      }
      m_drag = Drag::Title;
      m_dragPanel = static_cast<int>(index);
      m_grabX = pointer.x - panel.view.frame.x;
      m_grabY = pointer.y - panel.view.frame.y;
      return;
    }
    if (m_leftSplitter.w > 0.0f &&
        m_leftSplitter.contains(pointer.x, pointer.y)) {
      m_drag = Drag::Column;
      m_dragSide = GuiDockSide::Left;
      m_dragStart = pointer.x;
      m_dragValue = m_leftSplitter.x;
      m_consumed.push_back(IPanelSurfaces::kMainSurface);
      return;
    }
    if (m_rightSplitter.w > 0.0f &&
        m_rightSplitter.contains(pointer.x, pointer.y)) {
      m_drag = Drag::Column;
      m_dragSide = GuiDockSide::Right;
      m_dragStart = pointer.x;
      m_dragValue = m_width - m_rightSplitter.x - m_rightSplitter.w;
      m_consumed.push_back(IPanelSurfaces::kMainSurface);
      return;
    }
    for (const std::pair<int, GuiToolRect>& split : m_rowSplitters) {
      if (!split.second.contains(pointer.x, pointer.y)) {
        continue;
      }
      // Weights become the current heights, so the drag moves one boundary.
      const GuiDockSide side =
        m_specs[static_cast<std::size_t>(split.first)].side;
      const std::vector<int> column = docked(side);
      for (int id : column) {
        Panel& member = m_panels[static_cast<std::size_t>(id)];
        member.weight = std::max(1.0f, member.view.frame.h);
      }
      for (std::size_t position = 0; position + 1 < column.size(); ++position) {
        if (column[position] == split.first) {
          m_dragPanel = split.first;
          m_dragValue =
            m_panels[static_cast<std::size_t>(column[position])].view.frame.h;
          m_dragValueBelow =
            m_panels[static_cast<std::size_t>(column[position + 1])]
              .view.frame.h;
        }
      }
      m_drag = Drag::Row;
      m_dragSide = side;
      m_dragStart = pointer.y;
      m_consumed.push_back(IPanelSurfaces::kMainSurface);
      return;
    }
  }
  if (m_drag != Drag::None && pointer.down) {
    m_consumed.push_back(IPanelSurfaces::kMainSurface);
    if (m_drag == Drag::Column) {
      const float delta = pointer.x - m_dragStart;
      if (m_dragSide == GuiDockSide::Left) {
        m_leftWidth = std::max(kMinimumColumn, m_dragValue + delta);
      } else {
        m_rightWidth = std::max(kMinimumColumn, m_dragValue - delta);
      }
    } else if (m_drag == Drag::Row && m_dragPanel >= 0) {
      const std::vector<int> column = docked(m_dragSide);
      for (std::size_t position = 0; position + 1 < column.size(); ++position) {
        if (column[position] != m_dragPanel) {
          continue;
        }
        const GuiDockPanelSpec& above =
          m_specs[static_cast<std::size_t>(column[position])];
        const GuiDockPanelSpec& below =
          m_specs[static_cast<std::size_t>(column[position + 1])];
        const float total = m_dragValue + m_dragValueBelow;
        const float wanted = m_dragValue + pointer.y - m_dragStart;
        const float lower = std::min(above.minHeight, total * 0.5f);
        const float upper =
          std::max(lower, total - std::min(below.minHeight, total * 0.5f));
        const float height = std::clamp(wanted, lower, upper);
        m_panels[static_cast<std::size_t>(column[position])].weight =
          std::max(1.0f, height);
        m_panels[static_cast<std::size_t>(column[position + 1])].weight =
          std::max(1.0f, total - height);
      }
    } else if (m_drag == Drag::Title && m_dragPanel >= 0) {
      // Past the window's edge the panel tears off into its own window,
      // keeping the grabbed point under the cursor.
      const bool outside = pointer.x < -kTearOffMargin ||
                           pointer.y < -kTearOffMargin ||
                           pointer.x > m_width + kTearOffMargin ||
                           pointer.y > m_height + kTearOffMargin;
      if (outside && canDetach()) {
        openWindow(m_dragPanel, pointer.x - m_grabX, pointer.y - m_grabY);
        m_drag = Drag::None;
        m_dragPanel = -1;
      }
    }
  }
  if (release || !pointer.down) {
    m_drag = Drag::None;
    m_dragPanel = -1;
  }
}

void
GuiPanelDock::handleDetached()
{
  if (m_surfaces == nullptr) {
    return;
  }
  for (std::size_t index = 0; index < m_panels.size(); ++index) {
    Panel& panel = m_panels[index];
    if (panel.mode != GuiDockMode::Detached) {
      panel.previousDown = false;
      continue;
    }
    const PanelSurfacePointer source =
      m_surfaces->pointer(m_specs[index].surface);
    const float x = static_cast<float>(source.x) / m_scale;
    const float y = static_cast<float>(source.y) / m_scale;
    const bool press = source.left && !panel.previousDown;
    panel.previousDown = source.left;
    panel.hoveredButton =
      GuiToolStyle::titleButtonRect(panel.view.title, 0).contains(x, y) ? 0
                                                                        : -1;
    if (!press || !panel.view.title.contains(x, y)) {
      continue;
    }
    m_consumed.push_back(m_specs[index].surface);
    if (panel.hoveredButton == 0) {
      dock(m_specs[index].id);
    }
  }
}

bool
GuiPanelDock::consumedPress(std::uint32_t surface) const
{
  return std::find(m_consumed.begin(), m_consumed.end(), surface) !=
         m_consumed.end();
}

bool
GuiPanelDock::overPanels(float x, float y) const
{
  for (const Panel& panel : m_panels) {
    if (panel.view.visible &&
        panel.view.surface == IPanelSurfaces::kMainSurface &&
        panel.view.frame.contains(x, y)) {
      return true;
    }
  }
  if ((m_leftSplitter.w > 0.0f && m_leftSplitter.contains(x, y)) ||
      (m_rightSplitter.w > 0.0f && m_rightSplitter.contains(x, y))) {
    return true;
  }
  for (const std::pair<int, GuiToolRect>& split : m_rowSplitters) {
    if (split.second.contains(x, y)) {
      return true;
    }
  }
  return false;
}

void
GuiPanelDock::drawDocked(GameVisual& visual) const
{
  for (std::size_t index = 0; index < m_panels.size(); ++index) {
    const Panel& panel = m_panels[index];
    if (!panel.view.visible ||
        panel.view.surface != IPanelSurfaces::kMainSurface) {
      continue;
    }
    GuiToolStyle::panel(visual, panel.view.frame);
    const std::string title = panel.mode == GuiDockMode::Opening
                                ? m_specs[index].title + " (opening)"
                                : m_specs[index].title;
    GuiToolStyle::titleBar(visual,
                           panel.view.title,
                           title,
                           buttons(static_cast<int>(index)),
                           panel.hoveredButton);
  }
  const bool leftActive =
    m_drag == Drag::Column && m_dragSide == GuiDockSide::Left;
  const bool rightActive =
    m_drag == Drag::Column && m_dragSide == GuiDockSide::Right;
  if (m_leftSplitter.w > 0.0f) {
    GuiToolStyle::splitter(
      visual,
      m_leftSplitter,
      m_leftSplitter.contains(m_mainPointer.x, m_mainPointer.y),
      leftActive);
  }
  if (m_rightSplitter.w > 0.0f) {
    GuiToolStyle::splitter(
      visual,
      m_rightSplitter,
      m_rightSplitter.contains(m_mainPointer.x, m_mainPointer.y),
      rightActive);
  }
  for (const std::pair<int, GuiToolRect>& split : m_rowSplitters) {
    GuiToolStyle::splitter(
      visual,
      split.second,
      split.second.contains(m_mainPointer.x, m_mainPointer.y),
      m_drag == Drag::Row && m_dragPanel == split.first);
  }
}

void
GuiPanelDock::drawDetached(const std::string& id, GameVisual& visual) const
{
  const int index = find(id);
  if (index < 0) {
    return;
  }
  const Panel& panel = m_panels[static_cast<std::size_t>(index)];
  if (panel.mode != GuiDockMode::Detached || !panel.view.visible) {
    return;
  }
  visual.addFilledRect(panel.view.frame.x,
                       panel.view.frame.y,
                       panel.view.frame.w,
                       panel.view.frame.h,
                       GuiToolPalette::panel);
  GuiToolStyle::titleBar(visual,
                         panel.view.title,
                         m_specs[static_cast<std::size_t>(index)].title,
                         buttons(index),
                         panel.hoveredButton);
}

static const char* const kDockHeader = "illumo-dock 1";

static const char*
modeName(GuiDockMode mode, bool wantsWindow)
{
  if (mode == GuiDockMode::Hidden) {
    return "hidden";
  }
  return mode == GuiDockMode::Detached || mode == GuiDockMode::Opening ||
             wantsWindow
           ? "detached"
           : "docked";
}

std::string
GuiPanelDock::serialize() const
{
  std::ostringstream output;
  output << kDockHeader << '\n';
  output << "column left " << std::lround(m_leftWidth) << '\n';
  output << "column right " << std::lround(m_rightWidth) << '\n';
  for (std::size_t index = 0; index < m_specs.size(); ++index) {
    const Panel& panel = m_panels[index];
    output << "panel " << m_specs[index].id << ' '
           << modeName(panel.mode, panel.wantsWindow) << ' '
           << std::lround(panel.weight) << ' ' << std::lround(panel.windowX)
           << ' ' << std::lround(panel.windowY) << ' '
           << std::lround(panel.windowWidth) << ' '
           << std::lround(panel.windowHeight) << '\n';
  }
  return output.str();
}

bool
GuiPanelDock::restore(const std::string& text)
{
  std::istringstream input(text);
  std::string line;
  if (!std::getline(input, line) || line != kDockHeader) {
    return false;
  }
  float leftWidth = m_leftWidth;
  float rightWidth = m_rightWidth;
  struct Saved
  {
    int index = -1;
    GuiDockMode mode = GuiDockMode::Docked;
    bool detached = false;
    float weight = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
  };
  std::vector<Saved> saved;
  int lines = 0;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    if (++lines > 64) {
      return false;
    }
    std::istringstream fields(line);
    std::string kind;
    fields >> kind;
    if (kind == "column") {
      std::string side;
      float width = 0.0f;
      if (!(fields >> side >> width) || !std::isfinite(width) || width < 0.0f ||
          width > 8192.0f || (side != "left" && side != "right")) {
        return false;
      }
      (side == "left" ? leftWidth : rightWidth) =
        std::max(kMinimumColumn, width);
      continue;
    }
    if (kind != "panel") {
      return false;
    }
    std::string id;
    std::string mode;
    Saved entry;
    if (!(fields >> id >> mode >> entry.weight >> entry.x >> entry.y >>
          entry.width >> entry.height) ||
        !std::isfinite(entry.weight) || entry.weight < 0.0f ||
        entry.weight > 100000.0f || !std::isfinite(entry.x) ||
        !std::isfinite(entry.y) || std::abs(entry.x) > 100000.0f ||
        std::abs(entry.y) > 100000.0f || !std::isfinite(entry.width) ||
        !std::isfinite(entry.height) || entry.width < 0.0f ||
        entry.height < 0.0f || entry.width > 8192.0f ||
        entry.height > 8192.0f ||
        (mode != "docked" && mode != "detached" && mode != "hidden")) {
      return false;
    }
    entry.index = find(id);
    if (entry.index < 0) {
      continue; // a panel this build no longer has
    }
    entry.mode = mode == "hidden" ? GuiDockMode::Hidden : GuiDockMode::Docked;
    entry.detached = mode == "detached";
    saved.push_back(entry);
  }
  m_leftWidth = leftWidth;
  m_rightWidth = rightWidth;
  for (const Saved& entry : saved) {
    Panel& panel = m_panels[static_cast<std::size_t>(entry.index)];
    dock(m_specs[static_cast<std::size_t>(entry.index)].id);
    panel.mode = entry.mode;
    panel.wantsWindow = entry.detached;
    panel.weight = std::max(1.0f, entry.weight);
    panel.windowX = entry.x;
    panel.windowY = entry.y;
    panel.windowWidth = entry.width;
    panel.windowHeight = entry.height;
  }
  return true;
}
