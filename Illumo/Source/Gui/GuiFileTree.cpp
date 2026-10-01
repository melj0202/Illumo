#include <Illumo/Gui/GuiFileTree.h>

#include <Illumo/Gui/GuiKit.h>
#include <algorithm>

GuiFileTree::GuiFileTree(std::string root)
  : m_root(std::move(root))
{
}

void
GuiFileTree::setRoot(std::string root)
{
  m_root = std::move(root);
  m_children.clear();
  m_expanded.clear();
  m_requested.clear();
}

std::string
GuiFileTree::childPath(const std::string& directory, const std::string& name)
{
  if (directory.empty() || directory.back() == '/') {
    return directory + name;
  }
  return directory + "/" + name;
}

void
GuiFileTree::setChildren(const std::string& directory,
                         std::vector<GuiFileTreeEntry> children)
{
  std::sort(children.begin(),
            children.end(),
            [](const GuiFileTreeEntry& a, const GuiFileTreeEntry& b) {
              if (a.directory != b.directory) {
                return a.directory;
              }
              return a.name < b.name;
            });
  m_children[directory] = std::move(children);
  m_requested.erase(directory);
}

bool
GuiFileTree::listed(const std::string& directory) const
{
  return m_children.find(directory) != m_children.end();
}

void
GuiFileTree::invalidate(const std::string& directory)
{
  m_children.erase(directory);
  m_requested.erase(directory);
}

void
GuiFileTree::setExpanded(const std::string& directory, bool expanded)
{
  if (expanded) {
    m_expanded.insert(directory);
  } else {
    m_expanded.erase(directory);
  }
}

void
GuiFileTree::toggle(const std::string& directory)
{
  setExpanded(directory, !expanded(directory));
}

bool
GuiFileTree::expanded(const std::string& directory) const
{
  return m_expanded.find(directory) != m_expanded.end();
}

std::vector<GuiFileTreeRow>
GuiFileTree::rows() const
{
  struct Pending
  {
    std::string directory;
    std::size_t next;
    int depth;
  };
  std::vector<GuiFileTreeRow> rows;
  std::vector<Pending> stack;
  stack.push_back({ m_root, 0, 0 });
  while (!stack.empty() && rows.size() < kMaximumRows) {
    Pending& top = stack.back();
    const std::map<std::string, std::vector<GuiFileTreeEntry>>::const_iterator
      children = m_children.find(top.directory);
    if (children == m_children.end() || top.next >= children->second.size()) {
      stack.pop_back();
      continue;
    }
    const GuiFileTreeEntry& entry = children->second[top.next++];
    GuiFileTreeRow row;
    row.path = childPath(top.directory, entry.name);
    row.name = entry.name;
    row.depth = top.depth;
    row.directory = entry.directory;
    row.size = entry.size;
    row.expanded = entry.directory && expanded(row.path);
    row.loading = row.expanded && !listed(row.path);
    const int depth = top.depth + 1;
    const bool descend = row.expanded && !row.loading;
    const std::string path = row.path;
    rows.push_back(std::move(row));
    if (descend) {
      stack.push_back({ path, 0, depth });
    }
  }
  return rows;
}

std::vector<std::string>
GuiFileTree::takePendingListings()
{
  std::vector<std::string> pending;
  const bool rootMissing = !listed(m_root) && !m_requested.count(m_root);
  if (rootMissing) {
    pending.push_back(m_root);
  }
  for (const GuiFileTreeRow& row : rows()) {
    if (row.loading && !m_requested.count(row.path)) {
      pending.push_back(row.path);
    }
  }
  for (const std::string& path : pending) {
    m_requested.insert(path);
  }
  return pending;
}

void
GuiFileTree::draw(GameVisual& visual,
                  const std::vector<GuiFileTreeRow>& rows,
                  std::size_t first,
                  std::size_t count,
                  float x,
                  float y,
                  float width,
                  float rowHeight,
                  const std::string& selected,
                  int hovered)
{
  for (std::size_t index = first; index < rows.size() && index < first + count;
       ++index) {
    const GuiFileTreeRow& row = rows[index];
    std::string label = row.name;
    if (row.directory) {
      label = (row.expanded ? "v " : "> ") + label +
              (row.loading ? "  ..." : std::string());
    }
    GuiKit::drawTreeRow(visual,
                        x,
                        y + static_cast<float>(index - first) * rowHeight,
                        width,
                        rowHeight,
                        row.depth,
                        label,
                        row.path == selected,
                        static_cast<int>(index) == hovered);
  }
}
