#pragma once

#include <Illumo/Gui/GuiFileTree.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Services/FileTreeSource.h>
#include <Illumo/Services/KeyCode.h>
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

// DebugModule's browser over the host's mounted file tree (the `files`
// command). Listings are read synchronously from the IFileTreeSource when a
// directory expands; GuiFileTree flattens and draws the rows. Input is the
// keyboard (Up/Down/PageUp/PageDown/Home/End select, Right or Enter expands,
// Left collapses or selects the parent, Escape closes) and the mouse wheel,
// all of which DebugModule consumes before the product sees them.
class FileTreeOverlay
{
public:
  static constexpr float kWidth = 460.0f;
  static constexpr float kRowHeight = 18.0f;
  static constexpr float kHeaderHeight = 40.0f;
  static constexpr float kFooterHeight = 50.0f;
  // Below product header bars and the FPS label.
  static constexpr float kTop = 48.0f;

  FileTreeOverlay()
  {
    m_visual.setSpace(PrimitiveSpace::Pixels);
    m_visual.setLayerHint(RenderLayerId::Debug);
  }
  FileTreeOverlay(const FileTreeOverlay&) = delete;
  FileTreeOverlay& operator=(const FileTreeOverlay&) = delete;
  FileTreeOverlay(FileTreeOverlay&&) = delete;
  FileTreeOverlay& operator=(FileTreeOverlay&&) = delete;
  ~FileTreeOverlay() = default;

  void prepare(Renderer* renderer, IRenderWindow* window, Camera* camera)
  {
    m_visual.setWindow(window);
    m_visual.setCamera(camera);
    m_visual.prepare(renderer);
  }

  bool visible() const { return m_source != nullptr; }

  // Opens the browser at root (expanded) over source.
  void show(const IFileTreeSource* source, const std::string& root)
  {
    m_source = source;
    m_tree.setRoot(root);
    m_tree.setExpanded(root, true);
    m_selected = 0;
    m_first = 0;
    refresh();
  }

  void hide()
  {
    m_source = nullptr;
    m_rows.clear();
    m_status.clear();
    m_visual.clearPrimitives();
  }

  // True when the key belongs to the browser (it is then consumed).
  bool handleKey(KeyCode key, InputAction action, bool consoleOpen)
  {
    if (!visible() || consoleOpen) {
      return false;
    }
    const bool navigation = key == KeyCode::Up || key == KeyCode::Down ||
                            key == KeyCode::Left || key == KeyCode::Right ||
                            key == KeyCode::Enter || key == KeyCode::PageUp ||
                            key == KeyCode::PageDown || key == KeyCode::Home ||
                            key == KeyCode::End || key == KeyCode::Escape;
    if (!navigation) {
      return false;
    }
    if (action == InputAction::Release) {
      return true;
    }
    if (key == KeyCode::Escape) {
      if (action == InputAction::Press) {
        hide();
      }
      return true;
    }
    const std::size_t page = visibleRows();
    const std::size_t last = m_rows.empty() ? 0 : m_rows.size() - 1;
    if (key == KeyCode::Up) {
      m_selected = m_selected > 0 ? m_selected - 1 : 0;
    } else if (key == KeyCode::Down) {
      m_selected = std::min(last, m_selected + 1);
    } else if (key == KeyCode::PageUp) {
      m_selected = m_selected > page ? m_selected - page : 0;
    } else if (key == KeyCode::PageDown) {
      m_selected = std::min(last, m_selected + page);
    } else if (key == KeyCode::Home) {
      m_selected = 0;
    } else if (key == KeyCode::End) {
      m_selected = last;
    } else if (!m_rows.empty()) {
      const GuiFileTreeRow row = m_rows[m_selected];
      if (key == KeyCode::Left) {
        if (row.directory && row.expanded) {
          m_tree.setExpanded(row.path, false);
        } else {
          selectParent(row);
        }
      } else if (row.directory) {
        m_tree.setExpanded(row.path, true);
      }
    }
    refresh();
    return true;
  }

  // Wheel steps (positive scrolls up); true when consumed.
  bool scroll(double offset)
  {
    if (!visible() || offset == 0.0 || m_rows.empty()) {
      return false;
    }
    const std::size_t steps = 3;
    const std::size_t last = m_rows.size() - 1;
    m_selected = offset > 0.0 ? (m_selected > steps ? m_selected - steps : 0)
                              : std::min(last, m_selected + steps);
    refresh();
    return true;
  }

  void update(float width, float height)
  {
    if (!visible()) {
      return;
    }
    if (width == m_width && height == m_height && !m_dirty) {
      return;
    }
    m_width = width;
    m_height = height;
    m_dirty = false;
    keepSelectionVisible();
    rebuild();
  }

  const std::vector<GuiFileTreeRow>& rows() const { return m_rows; }
  std::string selectedPath() const
  {
    return m_rows.empty() ? std::string() : m_rows[m_selected].path;
  }
  const std::string& status() const { return m_status; }
  const std::string& root() const { return m_tree.root(); }
  GameVisual& visual() { return m_visual; }

private:
  std::size_t visibleRows() const
  {
    const float height = std::max(m_height, 200.0f) - kTop - 8.0f;
    const float rows = (height - kHeaderHeight - kFooterHeight) / kRowHeight;
    return rows < 1.0f ? 1u : static_cast<std::size_t>(rows);
  }

  void selectParent(const GuiFileTreeRow& row)
  {
    const std::size_t slash = row.path.rfind('/');
    const std::string parent = slash == 0 || slash == std::string::npos
                                 ? "/"
                                 : row.path.substr(0, slash);
    for (std::size_t index = 0; index < m_rows.size(); ++index) {
      if (m_rows[index].path == parent) {
        m_selected = index;
        return;
      }
    }
  }

  // Lists every newly expanded directory, then re-flattens the rows.
  void refresh()
  {
    if (m_source == nullptr) {
      return;
    }
    std::string failure;
    for (const std::string& directory : m_tree.takePendingListings()) {
      std::vector<FileTreeEntry> listed;
      std::vector<GuiFileTreeEntry> children;
      if (m_source->list(directory, listed)) {
        children.reserve(listed.size());
        for (const FileTreeEntry& entry : listed) {
          children.push_back({ entry.name, entry.directory, entry.size });
        }
      } else {
        failure = "Cannot list " + directory;
      }
      m_tree.setChildren(directory, std::move(children));
    }
    m_rows = m_tree.rows();
    if (m_selected >= m_rows.size()) {
      m_selected = m_rows.empty() ? 0 : m_rows.size() - 1;
    }
    m_status = failure.empty() ? describeSelection() : failure;
    m_dirty = true;
  }

  std::string describeSelection() const
  {
    if (m_rows.empty()) {
      return "(empty)";
    }
    const GuiFileTreeRow& row = m_rows[m_selected];
    FileTreeStatus status;
    if (!m_source->stat(row.path, status)) {
      return row.path;
    }
    std::string text = row.path;
    text += status.directory ? "  directory"
                             : "  " + std::to_string(status.size) + " bytes";
    if (!status.packageId.empty()) {
      text += "  from " + status.packageId;
    }
    return text;
  }

  void keepSelectionVisible()
  {
    const std::size_t page = visibleRows();
    if (m_selected < m_first) {
      m_first = m_selected;
    } else if (m_selected >= m_first + page) {
      m_first = m_selected + 1 - page;
    }
    const std::size_t maximumFirst =
      m_rows.size() > page ? m_rows.size() - page : 0;
    m_first = std::min(m_first, maximumFirst);
  }

  void rebuild()
  {
    m_visual.clearPrimitives();
    const float width = std::max(160.0f, std::min(kWidth, m_width - 16.0f));
    const float height = std::max(200.0f, m_height) - kTop - 8.0f;
    const float x = 8.0f;
    const float y = kTop;
    m_visual.addFilledRect(x, y, width, height, { 24, 27, 34, 235 });
    m_visual.addOutlineRect(x, y, width, height, { 108, 120, 139, 255 });
    m_visual.addText(
      "Files  " + m_tree.root(), x + 10.0f, y + 10.0f, 16, ColorRgba{});
    const std::size_t count =
      std::min(visibleRows(), m_rows.size() - std::min(m_first, m_rows.size()));
    GuiFileTree::draw(m_visual,
                      m_rows,
                      m_first,
                      count,
                      x + 4.0f,
                      y + kHeaderHeight,
                      width - 8.0f,
                      kRowHeight,
                      selectedPath(),
                      -1);
    const float footer = y + height - kFooterHeight + 6.0f;
    m_visual.addText(m_status, x + 10.0f, footer, 13, { 178, 190, 209, 255 });
    m_visual.addText("[Up/Down] select  [Right/Left] open/close  [Esc] close",
                     x + 10.0f,
                     footer + 20.0f,
                     12,
                     { 205, 213, 226, 255 });
  }

  const IFileTreeSource* m_source = nullptr;
  GuiFileTree m_tree;
  std::vector<GuiFileTreeRow> m_rows;
  std::size_t m_selected = 0;
  std::size_t m_first = 0;
  std::string m_status;
  GameVisual m_visual{ 2048 };
  bool m_dirty = true;
  float m_width = 0.0f;
  float m_height = 0.0f;
};
