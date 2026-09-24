#include "EditorAssetBrowser.h"

#include "EditorToolbar.h"
#include "IllEdPlatform.h"
#include <Illumo/Gui/GuiToolStyle.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/InputManager.h>
#include <algorithm>
#include <cmath>

EditorAssetBrowser::EditorAssetBrowser(IRenderWindow* window,
                                       Renderer* renderer)
  : m_window(window)
  , m_renderer(renderer)
  , m_visual(2048u)
  , m_tree("/")
  , m_fontSize(EditorToolbar::kDefaultFontSize)
{
  m_visual.setSpace(PrimitiveSpace::Pixels);
  m_visual.setLayerHint(RenderLayerId::UI);
  m_visual.setWindow(window);
  m_visual.setRenderer(renderer);
  m_visual.prepare(renderer);
  m_placement.visible = false;
}

EditorAssetBrowser::~EditorAssetBrowser()
{
  m_alive.reset();
}

void
EditorAssetBrowser::setFontSize(float sizePt)
{
  m_fontSize = std::clamp(sizePt, 8.0f, 48.0f);
}

void
EditorAssetBrowser::setPlacement(const GuiPanelPlacement& placement)
{
  if (placement.surface != m_placement.surface) {
    m_armedPath.clear();
    m_dragPath.clear();
  }
  m_placement = placement;
  m_visible = placement.visible && placement.area.h > rowHeight();
}

bool
EditorAssetBrowser::containsScreenPoint(float x, float y) const
{
  const GuiToolRect& area = m_placement.area;
  return m_visible && x >= area.x && x <= area.x + area.w && y >= area.y &&
         y <= area.y + area.h;
}

float
EditorAssetBrowser::rowHeight() const
{
  return std::max(
    20.0f, std::round(20.0f * m_fontSize / EditorToolbar::kDefaultFontSize));
}

float
EditorAssetBrowser::top() const
{
  return m_placement.area.y + 2.0f;
}

void
EditorAssetBrowser::refresh()
{
  // Keep what is open; listings are simply requested again.
  std::vector<std::string> open;
  for (const GuiFileTreeRow& row : m_tree.rows()) {
    if (row.expanded) {
      open.push_back(row.path);
    }
  }
  m_tree.setRoot("/");
  for (const std::string& path : open) {
    m_tree.setExpanded(path, true);
  }
}

void
EditorAssetBrowser::requestListings()
{
  for (const std::string& directory : m_tree.takePendingListings()) {
    const std::weak_ptr<bool> alive = m_alive;
    IllEdPlatform::current().listDirectory(
      directory,
      [this, alive, directory](bool success,
                               std::vector<IllEdPlatform::FileEntry> entries) {
        if (alive.expired()) {
          return;
        }
        std::vector<GuiFileTreeEntry> children;
        if (success) {
          for (IllEdPlatform::FileEntry& entry : entries) {
            children.push_back(
              { std::move(entry.name), entry.directory, entry.size });
          }
        }
        m_tree.setChildren(directory, std::move(children));
      });
  }
}

int
EditorAssetBrowser::rowAt(float x,
                          float y,
                          const std::vector<GuiFileTreeRow>& rows) const
{
  if (!containsScreenPoint(x, y) || y < top()) {
    return -1;
  }
  const int index =
    static_cast<int>(std::floor((y - top() + m_scroll) / rowHeight()));
  return index >= 0 && index < static_cast<int>(rows.size()) ? index : -1;
}

bool
EditorAssetBrowser::rowCenterForTesting(std::size_t index,
                                        float* x,
                                        float* y) const
{
  if (index >= m_tree.rows().size()) {
    return false;
  }
  *x = m_placement.area.x + m_placement.area.w * 0.5f;
  *y = top() + (static_cast<float>(index) + 0.5f) * rowHeight() - m_scroll;
  return true;
}

void
EditorAssetBrowser::press(const GuiFileTreeRow& row, bool doubleClick)
{
  if (row.directory) {
    m_tree.toggle(row.path);
    return;
  }
  m_selected = row.path;
  if (doubleClick) {
    m_activated = row.path;
  }
}

void
EditorAssetBrowser::clickRowForTesting(std::size_t index, bool doubleClick)
{
  const std::vector<GuiFileTreeRow> rows = m_tree.rows();
  if (index < rows.size()) {
    press(rows[index], doubleClick);
  }
  requestListings();
}

void
EditorAssetBrowser::dragRowOutForTesting(std::size_t index,
                                         float pixelX,
                                         float pixelY)
{
  const std::vector<GuiFileTreeRow> rows = m_tree.rows();
  if (index < rows.size() && !rows[index].directory) {
    m_drop = { rows[index].path, m_placement.surface, pixelX, pixelY };
  }
}

void
EditorAssetBrowser::update(InputManager* input, float dt)
{
  m_animTime += std::max(0.0f, dt);
  m_consumedPress = false;
  if (m_visible) {
    requestListings();
  }
  std::vector<GuiFileTreeRow> rows = m_tree.rows();
  m_pointer.sample(m_placement, m_window, m_renderer, input);
  const float mouseX = m_pointer.x();
  const float mouseY = m_pointer.y();
  const bool inside = containsScreenPoint(mouseX, mouseY);
  m_hover = rowAt(mouseX, mouseY, rows);
  if (input != nullptr && m_visible) {
    if (inside) {
      const float wheel = m_pointer.takeWheel();
      if (wheel != 0.0f) {
        m_scroll -= wheel * rowHeight() * 3.0f;
      }
    }
    if (m_pointer.clicked() && inside) {
      m_consumedPress = true;
      if (m_hover >= 0) {
        const GuiFileTreeRow& row = rows[static_cast<std::size_t>(m_hover)];
        const bool doubleClick =
          row.path == m_lastClick && m_animTime - m_lastClickTime < 0.35f;
        m_lastClick = doubleClick ? std::string() : row.path;
        m_lastClickTime = m_animTime;
        press(row, doubleClick);
        if (!row.directory) {
          m_armedPath = row.path;
          m_pressX = mouseX;
          m_pressY = mouseY;
        }
      }
    }
    if (m_pointer.pressed() && !m_armedPath.empty()) {
      const float dx = mouseX - m_pressX;
      const float dy = mouseY - m_pressY;
      if (dx * dx + dy * dy > 16.0f) {
        m_dragPath = m_armedPath;
      }
      if (!m_dragPath.empty()) {
        m_consumedPress = true;
      }
    }
    if (m_pointer.released()) {
      if (!m_dragPath.empty() && !inside) {
        // A held button keeps reporting this window's pointer even past its
        // edge, so the release point may lie over another window.
        m_drop = { m_dragPath,
                   m_placement.surface,
                   m_pointer.pixelX(),
                   m_pointer.pixelY() };
      }
      m_armedPath.clear();
      m_dragPath.clear();
    }
  }
  const float window = std::max(0.0f, m_placement.area.h - 4.0f);
  const float content = static_cast<float>(rows.size()) * rowHeight();
  m_scroll = std::clamp(m_scroll, 0.0f, std::max(0.0f, content - window));
  rows = m_tree.rows();
  rebuild(rows);
}

void
EditorAssetBrowser::rebuild(const std::vector<GuiFileTreeRow>& rows)
{
  m_visual.clearPrimitives();
  if (!m_visible) {
    return;
  }
  const GuiToolRect& area = m_placement.area;
  m_visual.setPixelClipRect(Rect2{ area.x, area.y, area.w, area.h });
  const float fontScale = m_fontSize / EditorToolbar::kDefaultFontSize;
  const float font = std::max(10.0f, std::round(13.0f * fontScale));
  const float smallFont = std::max(9.0f, std::round(11.0f * fontScale));
  const float height = rowHeight();
  if (rows.empty()) {
    GuiToolStyle::text(m_visual,
                       m_tree.listed("/") ? "No mounted files" : "Listing...",
                       area.x + GuiToolStyle::kPad,
                       top() + 4.0f,
                       smallFont,
                       GuiToolPalette::faint);
    return;
  }
  const std::size_t first = static_cast<std::size_t>(m_scroll / height);
  const float offset = m_scroll - static_cast<float>(first) * height;
  const std::size_t count =
    static_cast<std::size_t>((area.h + offset) / height) + 1u;
  const float indent = std::round(14.0f * fontScale);
  const float rowWidth = area.w - 4.0f - GuiToolStyle::kScrollbar;
  for (std::size_t index = first; index < rows.size() && index < first + count;
       ++index) {
    const GuiFileTreeRow& row = rows[index];
    const float y = top() - offset + static_cast<float>(index - first) * height;
    const GuiToolRect rect{ area.x + 2.0f, y, rowWidth, height - 1.0f };
    const bool selected = row.path == m_selected;
    GuiToolStyle::row(m_visual,
                      rect,
                      selected || row.path == m_dragPath,
                      static_cast<int>(index) == m_hover && !selected);
    const float x = rect.x + 6.0f + static_cast<float>(row.depth) * indent;
    const float textY =
      y + std::max(0.0f, std::round((height - font) * 0.5f)) - 1.0f;
    if (row.directory) {
      // A fold arrow drawn with lines, like the hierarchy's.
      const float centreY = y + height * 0.5f;
      const float size = std::max(6.0f, std::round(7.0f * fontScale));
      if (row.expanded) {
        m_visual.addLine(x,
                         centreY - size * 0.3f,
                         x + size * 0.5f,
                         centreY + size * 0.3f,
                         GuiToolPalette::dim,
                         1.0f);
        m_visual.addLine(x + size * 0.5f,
                         centreY + size * 0.3f,
                         x + size,
                         centreY - size * 0.3f,
                         GuiToolPalette::dim,
                         1.0f);
      } else {
        m_visual.addLine(x + 1.0f,
                         centreY - size * 0.5f,
                         x + 1.0f + size * 0.6f,
                         centreY,
                         GuiToolPalette::dim,
                         1.0f);
        m_visual.addLine(x + 1.0f + size * 0.6f,
                         centreY,
                         x + 1.0f,
                         centreY + size * 0.5f,
                         GuiToolPalette::dim,
                         1.0f);
      }
    }
    const float labelX = x + 12.0f * fontScale;
    const std::string label =
      row.directory && row.loading ? row.name + "  ..." : row.name;
    GuiToolStyle::fittedText(m_visual,
                             label,
                             labelX,
                             textY,
                             rect.x + rect.w - labelX - 4.0f,
                             font,
                             row.directory ? GuiToolPalette::text
                                           : GuiToolPalette::dim);
  }
  GuiToolStyle::scrollbar(
    m_visual,
    GuiToolRect{ area.x + area.w - GuiToolStyle::kScrollbar,
                 top(),
                 GuiToolStyle::kScrollbar - 1.0f,
                 area.h - 4.0f },
    m_scroll,
    area.h - 4.0f,
    static_cast<float>(rows.size()) * height);
}

EditorAssetBrowser::Drop
EditorAssetBrowser::takeDrop()
{
  Drop drop = std::move(m_drop);
  m_drop = Drop{};
  return drop;
}

std::string
EditorAssetBrowser::takeActivated()
{
  std::string activated = std::move(m_activated);
  m_activated.clear();
  return activated;
}

bool
EditorAssetBrowser::AppendCommands(Renderer* renderer)
{
  if (!m_visible) {
    return true;
  }
  return m_visual.AppendCommands(renderer);
}
