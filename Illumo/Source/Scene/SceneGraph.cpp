#include <Illumo/Scene/SceneGraph.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

static std::atomic<uint64_t> g_nextSceneGraphId{ 1 };

static uint64_t
allocateSceneGraphId()
{
  uint64_t graphId = g_nextSceneGraphId.fetch_add(1);
  while (graphId == 0) {
    graphId = g_nextSceneGraphId.fetch_add(1);
  }
  return graphId;
}

struct SceneGraph::Impl
{
  struct NodeSlot
  {
    uint32_t generation = 1;
    bool alive = false;
    SceneNodeHandle parent{};
    std::vector<SceneNodeHandle> children;
    Matrix4 localTransform = Matrix4(1.0f);
    Matrix4 worldTransform = Matrix4(1.0f);
    bool transformDirty = true;
    bool enabled = true;
    bool visible = true;
    ISceneRenderAttachment* renderAttachment = nullptr;
  };

  struct TransformVisit
  {
    SceneNodeHandle node{};
    bool ancestorDirty = false;
  };

  struct RenderVisit
  {
    SceneNodeHandle node{};
    bool ancestorDirty = false;
    bool ancestorsEnabled = true;
    bool ancestorsVisible = true;
  };

  struct RenderTraversalGuard
  {
    explicit RenderTraversalGuard(bool* activeFlag)
      : active(activeFlag)
    {
      *active = true;
    }

    ~RenderTraversalGuard() { *active = false; }

    RenderTraversalGuard(const RenderTraversalGuard&) = delete;
    RenderTraversalGuard& operator=(const RenderTraversalGuard&) = delete;

    bool* active;
  };

  uint64_t graphId = allocateSceneGraphId();
  std::vector<NodeSlot> slots{ NodeSlot{} };
  std::vector<uint32_t> freeSlots;
  std::vector<SceneNodeHandle> roots;
  std::vector<RenderVisit> renderTraversalScratch;
  size_t nodeCount = 0;
  bool renderTraversalActive = false;

  SceneNodeHandle makeHandle(uint32_t slot) const
  {
    return SceneNodeHandle{ graphId, slot, slots[slot].generation };
  }

  bool isCurrent(SceneNodeHandle node) const
  {
    return node.isValid() && node.graphId == graphId &&
           node.slot < slots.size() && slots[node.slot].alive &&
           slots[node.slot].generation == node.generation;
  }

  bool isAcceptedParent(SceneNodeHandle parent) const
  {
    return parent.isNull() || isCurrent(parent);
  }

  static void eraseHandle(std::vector<SceneNodeHandle>* handles,
                          SceneNodeHandle handle)
  {
    if (handles == nullptr) {
      return;
    }
    const std::vector<SceneNodeHandle>::iterator found =
      std::find(handles->begin(), handles->end(), handle);
    if (found != handles->end()) {
      handles->erase(found);
    }
  }

  void markSubtreeDirty(SceneNodeHandle root)
  {
    if (!isCurrent(root)) {
      return;
    }

    std::vector<SceneNodeHandle> pending;
    pending.push_back(root);
    while (!pending.empty()) {
      const SceneNodeHandle current = pending.back();
      pending.pop_back();
      if (!isCurrent(current)) {
        continue;
      }

      NodeSlot& slot = slots[current.slot];
      slot.transformDirty = true;
      for (std::vector<SceneNodeHandle>::const_reverse_iterator child =
             slot.children.rbegin();
           child != slot.children.rend();
           ++child) {
        pending.push_back(*child);
      }
    }
  }

  void detachFromParent(SceneNodeHandle node)
  {
    NodeSlot& slot = slots[node.slot];
    if (isCurrent(slot.parent)) {
      eraseHandle(&slots[slot.parent.slot].children, node);
    } else {
      eraseHandle(&roots, node);
    }
  }

  void releaseSlot(uint32_t slotIndex)
  {
    NodeSlot& slot = slots[slotIndex];
    slot.alive = false;
    slot.parent = SceneNodeHandle{};
    slot.children.clear();
    slot.localTransform = Matrix4(1.0f);
    slot.worldTransform = Matrix4(1.0f);
    slot.transformDirty = true;
    slot.enabled = true;
    slot.visible = true;
    slot.renderAttachment = nullptr;

    uint32_t nextGeneration = slot.generation + 1;
    if (nextGeneration == 0) {
      nextGeneration = 1;
    }
    slot.generation = nextGeneration;
    freeSlots.push_back(slotIndex);
  }

  void updateWorldTransforms()
  {
    std::vector<TransformVisit> pending;
    for (std::vector<SceneNodeHandle>::const_reverse_iterator root =
           roots.rbegin();
         root != roots.rend();
         ++root) {
      pending.push_back(TransformVisit{ *root, false });
    }

    while (!pending.empty()) {
      const TransformVisit visit = pending.back();
      pending.pop_back();
      if (!isCurrent(visit.node)) {
        continue;
      }

      NodeSlot& slot = slots[visit.node.slot];
      const bool recompute = visit.ancestorDirty || slot.transformDirty;
      if (recompute) {
        if (isCurrent(slot.parent)) {
          slot.worldTransform =
            slots[slot.parent.slot].worldTransform * slot.localTransform;
        } else {
          slot.worldTransform = slot.localTransform;
        }
        slot.transformDirty = false;
      }

      for (std::vector<SceneNodeHandle>::const_reverse_iterator child =
             slot.children.rbegin();
           child != slot.children.rend();
           ++child) {
        pending.push_back(TransformVisit{ *child, recompute });
      }
    }
  }
};

SceneGraph::SceneGraph()
  : m_impl(std::make_unique<Impl>())
{
}

SceneGraph::~SceneGraph() = default;

SceneNodeHandle
SceneGraph::createNode(SceneNodeHandle parent)
{
  if (m_impl->renderTraversalActive) {
    return SceneNodeHandle{};
  }
  if (!m_impl->isAcceptedParent(parent)) {
    return SceneNodeHandle{};
  }

  uint32_t slotIndex = 0;
  if (!m_impl->freeSlots.empty()) {
    slotIndex = m_impl->freeSlots.back();
    m_impl->freeSlots.pop_back();
  } else {
    if (m_impl->slots.size() >=
        static_cast<size_t>(std::numeric_limits<uint32_t>::max())) {
      return SceneNodeHandle{};
    }
    slotIndex = static_cast<uint32_t>(m_impl->slots.size());
    m_impl->slots.push_back(Impl::NodeSlot{});
  }

  Impl::NodeSlot& slot = m_impl->slots[slotIndex];
  slot.alive = true;
  slot.parent = parent;
  slot.children.clear();
  slot.localTransform = Matrix4(1.0f);
  slot.worldTransform = Matrix4(1.0f);
  slot.transformDirty = true;
  slot.enabled = true;
  slot.visible = true;
  slot.renderAttachment = nullptr;

  const SceneNodeHandle node = m_impl->makeHandle(slotIndex);
  if (m_impl->isCurrent(parent)) {
    m_impl->slots[parent.slot].children.push_back(node);
  } else {
    m_impl->roots.push_back(node);
  }
  m_impl->nodeCount += 1;
  return node;
}

bool
SceneGraph::destroyNode(SceneNodeHandle node)
{
  if (m_impl->renderTraversalActive) {
    return false;
  }
  if (!m_impl->isCurrent(node)) {
    return false;
  }

  m_impl->detachFromParent(node);

  std::vector<SceneNodeHandle> pending;
  std::vector<SceneNodeHandle> subtree;
  pending.push_back(node);
  while (!pending.empty()) {
    const SceneNodeHandle current = pending.back();
    pending.pop_back();
    if (!m_impl->isCurrent(current)) {
      continue;
    }

    subtree.push_back(current);
    const Impl::NodeSlot& slot = m_impl->slots[current.slot];
    for (std::vector<SceneNodeHandle>::const_reverse_iterator child =
           slot.children.rbegin();
         child != slot.children.rend();
         ++child) {
      pending.push_back(*child);
    }
  }

  for (std::vector<SceneNodeHandle>::const_reverse_iterator current =
         subtree.rbegin();
       current != subtree.rend();
       ++current) {
    m_impl->releaseSlot(current->slot);
  }
  m_impl->nodeCount -= subtree.size();
  return true;
}

void
SceneGraph::clear()
{
  if (m_impl->renderTraversalActive) {
    return;
  }
  m_impl->roots.clear();
  m_impl->freeSlots.clear();
  for (uint32_t slotIndex = 1; slotIndex < m_impl->slots.size(); ++slotIndex) {
    Impl::NodeSlot& slot = m_impl->slots[slotIndex];
    if (slot.alive) {
      uint32_t nextGeneration = slot.generation + 1;
      if (nextGeneration == 0) {
        nextGeneration = 1;
      }
      slot.generation = nextGeneration;
    }
    slot.alive = false;
    slot.parent = SceneNodeHandle{};
    slot.children.clear();
    slot.localTransform = Matrix4(1.0f);
    slot.worldTransform = Matrix4(1.0f);
    slot.transformDirty = true;
    slot.enabled = true;
    slot.visible = true;
    slot.renderAttachment = nullptr;
    m_impl->freeSlots.push_back(slotIndex);
  }
  m_impl->nodeCount = 0;
}

bool
SceneGraph::isNodeValid(SceneNodeHandle node) const
{
  return m_impl->isCurrent(node);
}

size_t
SceneGraph::getNodeCount() const
{
  return m_impl->nodeCount;
}

size_t
SceneGraph::getRootCount() const
{
  return m_impl->roots.size();
}

SceneNodeHandle
SceneGraph::getRoot(size_t index) const
{
  if (index >= m_impl->roots.size()) {
    return SceneNodeHandle{};
  }
  return m_impl->roots[index];
}

SceneNodeHandle
SceneGraph::getParent(SceneNodeHandle node) const
{
  if (!m_impl->isCurrent(node)) {
    return SceneNodeHandle{};
  }
  return m_impl->slots[node.slot].parent;
}

size_t
SceneGraph::getChildCount(SceneNodeHandle node) const
{
  if (!m_impl->isCurrent(node)) {
    return 0;
  }
  return m_impl->slots[node.slot].children.size();
}

SceneNodeHandle
SceneGraph::getChild(SceneNodeHandle node, size_t index) const
{
  if (!m_impl->isCurrent(node) ||
      index >= m_impl->slots[node.slot].children.size()) {
    return SceneNodeHandle{};
  }
  return m_impl->slots[node.slot].children[index];
}

bool
SceneGraph::setParent(SceneNodeHandle node, SceneNodeHandle parent)
{
  if (m_impl->renderTraversalActive) {
    return false;
  }
  if (!m_impl->isCurrent(node) || !m_impl->isAcceptedParent(parent)) {
    return false;
  }
  if (node == parent) {
    return false;
  }

  SceneNodeHandle ancestor = parent;
  while (m_impl->isCurrent(ancestor)) {
    if (ancestor == node) {
      return false;
    }
    ancestor = m_impl->slots[ancestor.slot].parent;
  }

  Impl::NodeSlot& slot = m_impl->slots[node.slot];
  if (slot.parent == parent) {
    return true;
  }

  m_impl->detachFromParent(node);
  slot.parent = parent;
  if (m_impl->isCurrent(parent)) {
    m_impl->slots[parent.slot].children.push_back(node);
  } else {
    m_impl->roots.push_back(node);
  }
  m_impl->markSubtreeDirty(node);
  return true;
}

bool
SceneGraph::setLocalTransform(SceneNodeHandle node, const Matrix4& transform)
{
  if (m_impl->renderTraversalActive) {
    return false;
  }
  if (!m_impl->isCurrent(node)) {
    return false;
  }
  m_impl->slots[node.slot].localTransform = transform;
  m_impl->markSubtreeDirty(node);
  return true;
}

bool
SceneGraph::setLocalTransform(SceneNodeHandle node,
                              const Transform3D& transform)
{
  return setLocalTransform(node, transform.toMatrix());
}

bool
SceneGraph::getLocalTransform(SceneNodeHandle node, Matrix4* transform) const
{
  if (!m_impl->isCurrent(node) || transform == nullptr) {
    return false;
  }
  *transform = m_impl->slots[node.slot].localTransform;
  return true;
}

bool
SceneGraph::getWorldTransform(SceneNodeHandle node, Matrix4* transform)
{
  if (!m_impl->isCurrent(node) || transform == nullptr) {
    return false;
  }
  m_impl->updateWorldTransforms();
  *transform = m_impl->slots[node.slot].worldTransform;
  return true;
}

void
SceneGraph::updateWorldTransforms()
{
  m_impl->updateWorldTransforms();
}

bool
SceneGraph::setEnabled(SceneNodeHandle node, bool enabled)
{
  if (m_impl->renderTraversalActive) {
    return false;
  }
  if (!m_impl->isCurrent(node)) {
    return false;
  }
  m_impl->slots[node.slot].enabled = enabled;
  return true;
}

bool
SceneGraph::getEnabled(SceneNodeHandle node, bool* enabled) const
{
  if (!m_impl->isCurrent(node) || enabled == nullptr) {
    return false;
  }
  *enabled = m_impl->slots[node.slot].enabled;
  return true;
}

bool
SceneGraph::setVisible(SceneNodeHandle node, bool nodeVisible)
{
  if (m_impl->renderTraversalActive) {
    return false;
  }
  if (!m_impl->isCurrent(node)) {
    return false;
  }
  m_impl->slots[node.slot].visible = nodeVisible;
  return true;
}

bool
SceneGraph::getVisible(SceneNodeHandle node, bool* nodeVisible) const
{
  if (!m_impl->isCurrent(node) || nodeVisible == nullptr) {
    return false;
  }
  *nodeVisible = m_impl->slots[node.slot].visible;
  return true;
}

bool
SceneGraph::setRenderAttachment(SceneNodeHandle node,
                                ISceneRenderAttachment* attachment)
{
  if (m_impl->renderTraversalActive) {
    return false;
  }
  if (!m_impl->isCurrent(node)) {
    return false;
  }
  m_impl->slots[node.slot].renderAttachment = attachment;
  return true;
}

ISceneRenderAttachment*
SceneGraph::getRenderAttachment(SceneNodeHandle node) const
{
  if (!m_impl->isCurrent(node)) {
    return nullptr;
  }
  return m_impl->slots[node.slot].renderAttachment;
}

bool
SceneGraph::AppendCommands(Renderer* renderer)
{
  if (!isVisible()) {
    return true;
  }
  if (m_impl->renderTraversalActive) {
    return true;
  }

  Impl::RenderTraversalGuard traversalGuard(&m_impl->renderTraversalActive);

  std::vector<Impl::RenderVisit>& pending = m_impl->renderTraversalScratch;
  pending.clear();
  if (pending.capacity() < m_impl->nodeCount) {
    pending.reserve(m_impl->nodeCount);
  }
  for (std::vector<SceneNodeHandle>::const_reverse_iterator root =
         m_impl->roots.rbegin();
       root != m_impl->roots.rend();
       ++root) {
    pending.push_back(Impl::RenderVisit{ *root, false, true, true });
  }

  while (!pending.empty()) {
    const Impl::RenderVisit visit = pending.back();
    pending.pop_back();
    if (!m_impl->isCurrent(visit.node)) {
      continue;
    }

    Impl::NodeSlot& slot = m_impl->slots[visit.node.slot];
    const bool recompute = visit.ancestorDirty || slot.transformDirty;
    if (recompute) {
      if (m_impl->isCurrent(slot.parent)) {
        slot.worldTransform =
          m_impl->slots[slot.parent.slot].worldTransform * slot.localTransform;
      } else {
        slot.worldTransform = slot.localTransform;
      }
      slot.transformDirty = false;
    }

    const bool enabled = visit.ancestorsEnabled && slot.enabled;
    const bool nodeVisible = visit.ancestorsVisible && slot.visible;
    if (!enabled || !nodeVisible) {
      continue;
    }

    if (slot.renderAttachment != nullptr) {
      slot.renderAttachment->appendSceneCommands(renderer, slot.worldTransform);
    }
    for (std::vector<SceneNodeHandle>::const_reverse_iterator child =
           slot.children.rbegin();
         child != slot.children.rend();
         ++child) {
      pending.push_back(
        Impl::RenderVisit{ *child, recompute, enabled, nodeVisible });
    }
  }
  return true;
}
