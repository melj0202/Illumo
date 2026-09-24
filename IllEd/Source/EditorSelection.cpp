#include "EditorSelection.h"

#include <Illumo/Content/SceneInstance.h>
#include <algorithm>
#include <unordered_set>

static const std::string kNoSelection;

const std::string&
EditorSelection::primary() const
{
  return m_ids.empty() ? kNoSelection : m_ids.back();
}

bool
EditorSelection::contains(std::string_view id) const
{
  return std::find(m_ids.begin(), m_ids.end(), id) != m_ids.end();
}

void
EditorSelection::set(std::string_view id)
{
  m_ids.clear();
  if (!id.empty()) {
    m_ids.emplace_back(id);
  }
}

void
EditorSelection::set(const std::vector<std::string>& ids)
{
  m_ids.clear();
  for (const std::string& id : ids) {
    add(id);
  }
}

void
EditorSelection::add(std::string_view id)
{
  if (id.empty()) {
    return;
  }
  remove(id);
  m_ids.emplace_back(id);
}

void
EditorSelection::toggle(std::string_view id)
{
  if (contains(id)) {
    remove(id);
  } else {
    add(id);
  }
}

void
EditorSelection::remove(std::string_view id)
{
  m_ids.erase(std::remove(m_ids.begin(), m_ids.end(), id), m_ids.end());
}

void
EditorSelection::clear()
{
  m_ids.clear();
}

void
EditorSelection::prune(const SceneInstance& scene)
{
  m_ids.erase(std::remove_if(m_ids.begin(),
                             m_ids.end(),
                             [&scene](const std::string& id) {
                               return scene.findNode(id) == nullptr;
                             }),
              m_ids.end());
}

std::vector<std::string>
EditorSelection::topLevel(const SceneInstance& scene) const
{
  std::vector<std::string> roots;
  if (m_ids.empty()) {
    return roots;
  }
  const std::unordered_set<std::string> selected(m_ids.begin(), m_ids.end());
  const SceneGraph& graph = scene.graph();
  for (SceneNodeHandle node = graph.firstNode(); !node.isNull();
       node = graph.nextNode(node)) {
    const std::string id(graph.getName(node));
    if (selected.find(id) == selected.end()) {
      continue;
    }
    bool covered = false;
    for (SceneNodeHandle ancestor = graph.getParent(node); !ancestor.isNull();
         ancestor = graph.getParent(ancestor)) {
      if (selected.find(std::string(graph.getName(ancestor))) !=
          selected.end()) {
        covered = true;
        break;
      }
    }
    if (!covered) {
      roots.push_back(id);
    }
  }
  return roots;
}
