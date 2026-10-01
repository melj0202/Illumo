#pragma once

#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

// A browsable file tree drawn from primitives (no retained widgets). It holds
// the children listed so far for each directory and which directories are
// expanded; the owner lists directories on demand (pendingListings) from
// whatever file source it has, such as the guest's virtual file tree.
//
// Paths are '/'-separated; the root may be "/" or any directory.
struct GuiFileTreeEntry
{
  std::string name;
  bool directory = false;
  std::uint64_t size = 0;
};

struct GuiFileTreeRow
{
  std::string path;
  std::string name;
  int depth = 0;
  bool directory = false;
  bool expanded = false;
  // An expanded directory whose listing has not arrived yet.
  bool loading = false;
  std::uint64_t size = 0;
};

class GuiFileTree
{
public:
  // Rows beyond this are not flattened (a huge tree stays responsive).
  static constexpr std::size_t kMaximumRows = 20000;

  explicit GuiFileTree(std::string root = "/");

  const std::string& root() const { return m_root; }
  // Changes the root and forgets every listing and expansion.
  void setRoot(std::string root);

  // Replaces a directory's children (sorted: directories first, then by
  // name) once its listing arrives.
  void setChildren(const std::string& directory,
                   std::vector<GuiFileTreeEntry> children);
  bool listed(const std::string& directory) const;
  // Forgets one directory's listing (for example after an import) so it is
  // requested again.
  void invalidate(const std::string& directory);

  void setExpanded(const std::string& directory, bool expanded);
  void toggle(const std::string& directory);
  bool expanded(const std::string& directory) const;

  // Display order, depth first; the root itself is not a row.
  std::vector<GuiFileTreeRow> rows() const;
  // Expanded directories (the root included) whose listing is missing and
  // not yet requested; marks them requested.
  std::vector<std::string> takePendingListings();

  static std::string childPath(const std::string& directory,
                               const std::string& name);

  // Draws rows [first, first + count) top to bottom.
  static void draw(GameVisual& visual,
                   const std::vector<GuiFileTreeRow>& rows,
                   std::size_t first,
                   std::size_t count,
                   float x,
                   float y,
                   float width,
                   float rowHeight,
                   const std::string& selected,
                   int hovered);

private:
  std::string m_root;
  std::map<std::string, std::vector<GuiFileTreeEntry>> m_children;
  std::set<std::string> m_expanded;
  std::set<std::string> m_requested;
};
