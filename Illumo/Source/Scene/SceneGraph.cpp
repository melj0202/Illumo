#include "SceneGraphInternal.h"

#include <algorithm>
#include <atomic>
#include <new>

static std::atomic<uint64_t> g_nextSceneGraphId{ 1 };

static uint64_t
allocateGraphId()
{
  uint64_t id = g_nextSceneGraphId.fetch_add(1);
  while (id == 0) {
    id = g_nextSceneGraphId.fetch_add(1);
  }
  return id;
}

bool
SceneGraph::Impl::current(SceneNodeHandle node) const
{
  return node.isValid() && node.graphId == graphId &&
         node.slot < flags.size() && (flags[node.slot] & kAlive) != 0 &&
         generation[node.slot] == node.generation;
}
SceneNodeHandle
SceneGraph::Impl::handle(uint32_t slot) const
{
  return slot == 0 ? SceneNodeHandle{}
                   : SceneNodeHandle{ graphId, slot, generation[slot] };
}
uint32_t
SceneGraph::Impl::nextSlot(uint32_t slot) const
{
  if (firstChild[slot] != 0) {
    return firstChild[slot];
  }
  while (slot != 0 && nextSibling[slot] == 0) {
    slot = parent[slot];
  }
  return nextSibling[slot];
}
void
SceneGraph::Impl::appendChild(uint32_t slot, uint32_t parentSlot)
{
  parent[slot] = parentSlot;
  previousSibling[slot] = lastChild[parentSlot];
  nextSibling[slot] = 0;
  if (lastChild[parentSlot] != 0) {
    nextSibling[lastChild[parentSlot]] = slot;
  } else {
    firstChild[parentSlot] = slot;
  }
  lastChild[parentSlot] = slot;
  ++childCount[parentSlot];
}
void
SceneGraph::Impl::detach(uint32_t slot)
{
  const uint32_t p = parent[slot];
  const uint32_t before = previousSibling[slot];
  const uint32_t after = nextSibling[slot];
  if (before != 0) {
    nextSibling[before] = after;
  } else {
    firstChild[p] = after;
  }
  if (after != 0) {
    previousSibling[after] = before;
  } else {
    lastChild[p] = before;
  }
  --childCount[p];
}
void
SceneGraph::Impl::record(SceneChangeKind kind, uint32_t slot)
{
  ++sequence;
  journal[(sequence - 1) % kJournalCapacity] =
    SceneChange{ sequence, kind, handle(slot) };
}
void
SceneGraph::Impl::structuralChange()
{
  ++structuralRevision;
  ++contentRevision;
  lowestDirtyIndex = 0;
  stateDirty = true;
  boundsDirty = true;
}
void
SceneGraph::Impl::dirty(uint32_t slot)
{
  flags[slot] |= kDirty;
  if (compiledRevision == structuralRevision) {
    lowestDirtyIndex =
      std::min(lowestDirtyIndex, static_cast<size_t>(slotToIndex[slot]));
  } else {
    lowestDirtyIndex = 0;
  }
  ++contentRevision;
  boundsDirty = true;
}
uint32_t
SceneGraph::Impl::intern(std::string_view name)
{
  if (name.empty()) {
    return 0;
  }
  const std::string value(name);
  const Impl::NameMap::const_iterator found = nameIds.find(value);
  if (found != nameIds.end()) {
    return found->second;
  }
  const uint32_t id = static_cast<uint32_t>(names.size());
  if (nameFirst.capacity() <= names.size()) {
    nameFirst.reserve(std::max<size_t>(16, names.size() * 2));
  }
  names.push_back(value);
  try {
    nameIds.emplace(names.back(), id);
  } catch (...) {
    names.pop_back();
    throw;
  }
  nameFirst.push_back(0);
  return id;
}
void
SceneGraph::Impl::assignName(uint32_t slot, uint32_t name)
{
  const uint32_t old = nameId[slot];
  if (old != 0) {
    if (namePrevious[slot] != 0) {
      nameNext[namePrevious[slot]] = nameNext[slot];
    } else {
      nameFirst[old] = nameNext[slot];
    }
    if (nameNext[slot] != 0) {
      namePrevious[nameNext[slot]] = namePrevious[slot];
    }
  }
  nameId[slot] = name;
  namePrevious[slot] = 0;
  nameNext[slot] = 0;
  if (name != 0) {
    nameNext[slot] = nameFirst[name];
    if (nameFirst[name] != 0) {
      namePrevious[nameFirst[name]] = slot;
    }
    nameFirst[name] = slot;
  }
}
uint32_t
SceneGraph::Impl::allocateSlot()
{
  if (!freeSlots.empty()) {
    const uint32_t slot = freeSlots.back();
    freeSlots.pop_back();
    return slot;
  }
  const size_t size = flags.size();
  if (size >= kNoIndex) {
    return 0;
  }
  // Reserve every array before resizing any: allocation failure leaves the
  // authoritative parallel-array lengths and live hierarchy unchanged.
  const size_t capacity = std::max(size + 1, size * 2);
  if (flags.capacity() < size + 1) {
    generation.reserve(capacity);
    parent.reserve(capacity);
    firstChild.reserve(capacity);
    lastChild.reserve(capacity);
    nextSibling.reserve(capacity);
    previousSibling.reserve(capacity);
    childCount.reserve(capacity);
    local.reserve(capacity);
    nameId.reserve(capacity);
    nameNext.reserve(capacity);
    namePrevious.reserve(capacity);
    userData.reserve(capacity);
    attachmentFirst.reserve(capacity);
    attachmentLast.reserve(capacity);
    attachmentCount.reserve(capacity);
    freeSlots.reserve(capacity);
    pathScratch.reserve(capacity);
    destroyScratch.reserve(capacity);
    flags.reserve(capacity);
  }
  generation.push_back(1);
  parent.push_back(0);
  firstChild.push_back(0);
  lastChild.push_back(0);
  nextSibling.push_back(0);
  previousSibling.push_back(0);
  childCount.push_back(0);
  local.emplace_back();
  nameId.push_back(0);
  nameNext.push_back(0);
  namePrevious.push_back(0);
  userData.push_back(0);
  attachmentFirst.push_back(0);
  attachmentLast.push_back(0);
  attachmentCount.push_back(0);
  flags.push_back(0);
  return static_cast<uint32_t>(size);
}
SceneGraph::Impl::Attachment&
SceneGraph::Impl::attachment(uint32_t index)
{
  return attachmentChunks[index / kAttachmentChunkSize]
                         [index % kAttachmentChunkSize];
}
const SceneGraph::Impl::Attachment&
SceneGraph::Impl::attachment(uint32_t index) const
{
  return attachmentChunks[index / kAttachmentChunkSize]
                         [index % kAttachmentChunkSize];
}
uint32_t
SceneGraph::Impl::allocateAttachment(ISceneRenderAttachment* pointer)
{
  uint32_t index = freeAttachment;
  if (index != 0) {
    freeAttachment = attachment(index).next;
  } else {
    index = attachmentSlots;
    if (index == kNoIndex) {
      return 0;
    }
    if (index / kAttachmentChunkSize >= attachmentChunks.size()) {
      attachmentChunks.emplace_back();
    }
    ++attachmentSlots;
  }
  attachment(index) = Attachment{};
  attachment(index).pointer = pointer;
  return index;
}
void
SceneGraph::Impl::releaseAttachments(uint32_t slot)
{
  uint32_t index = attachmentFirst[slot];
  while (index != 0) {
    Attachment& entry = attachment(index);
    const uint32_t next = entry.next;
    entry = Attachment{};
    entry.next = freeAttachment;
    freeAttachment = index;
    index = next;
  }
  attachmentFirst[slot] = attachmentLast[slot] = attachmentCount[slot] = 0;
}
void
SceneGraph::Impl::releaseSlot(uint32_t slot)
{
  record(SceneChangeKind::Destroyed, slot);
  releaseAttachments(slot);
  flags[slot] = 0;
  parent[slot] = firstChild[slot] = lastChild[slot] = 0;
  nextSibling[slot] = previousSibling[slot] = childCount[slot] = 0;
  local[slot] = Transform3D{};
  assignName(slot, 0);
  userData[slot] = 0;
  ++generation[slot];
  if (generation[slot] == 0) {
    ++generation[slot];
  }
  freeSlots.push_back(slot);
  --nodeCount;
}

SceneGraph::SceneGraph()
  : m_impl(std::make_unique<Impl>(allocateGraphId()))
{
}
SceneGraph::~SceneGraph()
{
  m_impl->lifetime->alive = false;
}
SceneNodeHandle
SceneGraph::createNode(SceneNodeHandle parent)
{
  SceneNodeDesc description;
  description.parent = parent;
  return createNode(description);
}
SceneNodeHandle
SceneGraph::createNode(const SceneNodeDesc& description)
{
  if (m_impl->extractionActive ||
      (!description.parent.isNull() && !m_impl->current(description.parent))) {
    return {};
  }
  const uint32_t name = m_impl->intern(description.name);
  const uint32_t slot = m_impl->allocateSlot();
  if (slot == 0) {
    return {};
  }
  m_impl->flags[slot] = Impl::kAlive | Impl::kDirty |
                        (description.enabled ? Impl::kEnabled : 0u) |
                        (description.visible ? Impl::kVisible : 0u);
  m_impl->local[slot] = description.transform;
  m_impl->assignName(slot, name);
  m_impl->userData[slot] = description.userData;
  m_impl->appendChild(slot, description.parent.slot);
  ++m_impl->nodeCount;
  m_impl->structuralChange();
  m_impl->record(SceneChangeKind::Created, slot);
  return m_impl->handle(slot);
}
bool
SceneGraph::destroyNode(SceneNodeHandle node)
{
  if (m_impl->extractionActive || !m_impl->current(node)) {
    return false;
  }
  std::vector<uint32_t>& subtree = m_impl->destroyScratch;
  subtree.clear();
  uint32_t current = node.slot;
  // Retained scratch is reserved with slot storage. Collect before unlinking;
  // reverse release preserves the v1 parent-first slot reuse contract.
  for (;;) {
    subtree.push_back(current);
    if (m_impl->firstChild[current] != 0) {
      current = m_impl->firstChild[current];
      continue;
    }
    while (current != node.slot && m_impl->nextSibling[current] == 0) {
      current = m_impl->parent[current];
    }
    if (current == node.slot) {
      break;
    }
    current = m_impl->nextSibling[current];
  }
  m_impl->detach(node.slot);
  invalidateSnapshots();
  for (size_t i = subtree.size(); i-- > 0;) {
    m_impl->releaseSlot(subtree[i]);
  }
  m_impl->structuralChange();
  return true;
}
void
SceneGraph::clear()
{
  if (m_impl->extractionActive) {
    return;
  }
  invalidateSnapshots();
  m_impl->freeSlots.clear();
  for (uint32_t slot = 1; slot < m_impl->flags.size(); ++slot) {
    if ((m_impl->flags[slot] & Impl::kAlive) != 0) {
      m_impl->releaseSlot(slot);
    } else {
      m_impl->freeSlots.push_back(slot);
    }
  }
  m_impl->firstChild[0] = m_impl->lastChild[0] = m_impl->childCount[0] = 0;
  m_impl->structuralChange();
}
bool
SceneGraph::isNodeValid(SceneNodeHandle node) const
{
  return m_impl->current(node);
}
size_t
SceneGraph::getNodeCount() const
{
  return m_impl->nodeCount;
}
size_t
SceneGraph::getRootCount() const
{
  return m_impl->childCount[0];
}
SceneNodeHandle
SceneGraph::getRoot(size_t index) const
{
  uint32_t slot = m_impl->firstChild[0];
  while (slot != 0 && index-- != 0) {
    slot = m_impl->nextSibling[slot];
  }
  return m_impl->handle(slot);
}
SceneNodeHandle
SceneGraph::getParent(SceneNodeHandle node) const
{
  return m_impl->current(node) ? m_impl->handle(m_impl->parent[node.slot])
                               : SceneNodeHandle{};
}
size_t
SceneGraph::getChildCount(SceneNodeHandle node) const
{
  return m_impl->current(node) ? m_impl->childCount[node.slot] : 0;
}
SceneNodeHandle
SceneGraph::getNextSibling(SceneNodeHandle node) const
{
  return m_impl->current(node) ? m_impl->handle(m_impl->nextSibling[node.slot])
                               : SceneNodeHandle{};
}
SceneNodeHandle
SceneGraph::getChild(SceneNodeHandle node, size_t index) const
{
  if (!m_impl->current(node)) {
    return {};
  }
  uint32_t slot = m_impl->firstChild[node.slot];
  while (slot != 0 && index-- != 0) {
    slot = m_impl->nextSibling[slot];
  }
  return m_impl->handle(slot);
}
bool
SceneGraph::canSetParent(SceneNodeHandle node, SceneNodeHandle parent) const
{
  if (!m_impl->current(node) ||
      (!parent.isNull() && !m_impl->current(parent))) {
    return false;
  }
  for (uint32_t ancestor = parent.slot; ancestor != 0;
       ancestor = m_impl->parent[ancestor]) {
    if (ancestor == node.slot) {
      return false;
    }
  }
  return true;
}
bool
SceneGraph::setParent(SceneNodeHandle node, SceneNodeHandle parent)
{
  if (m_impl->extractionActive || !canSetParent(node, parent)) {
    return false;
  }
  if (m_impl->parent[node.slot] == parent.slot) {
    return true;
  }
  m_impl->detach(node.slot);
  m_impl->appendChild(node.slot, parent.slot);
  m_impl->structuralChange();
  m_impl->record(SceneChangeKind::Reparented, node.slot);
  return true;
}
bool
SceneGraph::setLocalTransform(SceneNodeHandle node, const Matrix4& transform)
{
  return setLocalTransform(node, Transform3D::fromMatrix(transform));
}
bool
SceneGraph::setLocalTransform(SceneNodeHandle node,
                              const Transform3D& transform)
{
  if (m_impl->extractionActive || !m_impl->current(node)) {
    return false;
  }
  m_impl->local[node.slot] = transform;
  m_impl->dirty(node.slot);
  m_impl->record(SceneChangeKind::Transform, node.slot);
  return true;
}
bool
SceneGraph::setLocalTransforms(const SceneNodeHandle* nodes,
                               const Transform3D* transforms,
                               size_t count)
{
  if (m_impl->extractionActive ||
      (count != 0 && (nodes == nullptr || transforms == nullptr))) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    if (!m_impl->current(nodes[i])) {
      return false;
    }
  }
  for (size_t i = 0; i < count; ++i) {
    setLocalTransform(nodes[i], transforms[i]);
  }
  return true;
}
bool
SceneGraph::getLocalTransform(SceneNodeHandle node, Matrix4* transform) const
{
  if (!m_impl->current(node) || transform == nullptr) {
    return false;
  }
  *transform = m_impl->local[node.slot].toMatrix();
  return true;
}
bool
SceneGraph::getLocalTransform(SceneNodeHandle node,
                              Transform3D* transform) const
{
  if (!m_impl->current(node) || transform == nullptr) {
    return false;
  }
  *transform = m_impl->local[node.slot];
  return true;
}
bool
SceneGraph::getWorldTransform(SceneNodeHandle node, Matrix4* transform) const
{
  return m_impl->current(node) && transform != nullptr &&
         m_impl->worldTransform(node.slot, transform);
}
bool
SceneGraph::getWorldBounds(SceneNodeHandle node,
                           AxisAlignedBounds3* bounds) const
{
  if (!m_impl->current(node) || bounds == nullptr || m_impl->extractionActive) {
    return false;
  }
  // Bounds callbacks may attempt mutations too. Guard every callback boundary.
  struct Guard
  {
    bool& active;
    explicit Guard(bool& value)
      : active(value)
    {
      active = true;
    }
    ~Guard() { active = false; }
  };
  Guard guard(m_impl->extractionActive);
  Matrix4 world(1.0f);
  return m_impl->worldTransform(node.slot, &world) &&
         m_impl->boundsAtWorld(node.slot, world, bounds, true);
}
void
SceneGraph::updateWorldTransforms()
{
  if (!m_impl->extractionActive) {
    m_impl->resolve(false);
  }
}
bool
SceneGraph::setEnabled(SceneNodeHandle node, bool enabled)
{
  if (m_impl->extractionActive || !m_impl->current(node)) {
    return false;
  }
  if (enabled) {
    m_impl->flags[node.slot] |= Impl::kEnabled;
  } else {
    m_impl->flags[node.slot] &= ~Impl::kEnabled;
  }
  m_impl->stateDirty = true;
  ++m_impl->contentRevision;
  m_impl->record(SceneChangeKind::State, node.slot);
  return true;
}
bool
SceneGraph::getEnabled(SceneNodeHandle node, bool* enabled) const
{
  if (!m_impl->current(node) || enabled == nullptr) {
    return false;
  }
  *enabled = (m_impl->flags[node.slot] & Impl::kEnabled) != 0;
  return true;
}
bool
SceneGraph::setVisible(SceneNodeHandle node, bool visible)
{
  if (m_impl->extractionActive || !m_impl->current(node)) {
    return false;
  }
  if (visible) {
    m_impl->flags[node.slot] |= Impl::kVisible;
  } else {
    m_impl->flags[node.slot] &= ~Impl::kVisible;
  }
  m_impl->stateDirty = true;
  ++m_impl->contentRevision;
  m_impl->record(SceneChangeKind::State, node.slot);
  return true;
}
bool
SceneGraph::getVisible(SceneNodeHandle node, bool* visible) const
{
  if (!m_impl->current(node) || visible == nullptr) {
    return false;
  }
  *visible = (m_impl->flags[node.slot] & Impl::kVisible) != 0;
  return true;
}
bool
SceneGraph::isEffectivelyVisible(SceneNodeHandle node) const
{
  if (!m_impl->current(node)) {
    return false;
  }
  for (uint32_t slot = node.slot; slot != 0; slot = m_impl->parent[slot]) {
    if ((m_impl->flags[slot] & (Impl::kEnabled | Impl::kVisible)) !=
        (Impl::kEnabled | Impl::kVisible)) {
      return false;
    }
  }
  return true;
}
bool
SceneGraph::setName(SceneNodeHandle node, std::string_view name)
{
  if (m_impl->extractionActive || !m_impl->current(node)) {
    return false;
  }
  m_impl->assignName(node.slot, m_impl->intern(name));
  m_impl->record(SceneChangeKind::Name, node.slot);
  return true;
}
std::string_view
SceneGraph::getName(SceneNodeHandle node) const
{
  return m_impl->current(node)
           ? std::string_view(m_impl->names[m_impl->nameId[node.slot]])
           : std::string_view{};
}
SceneNodeHandle
SceneGraph::findByName(std::string_view name) const
{
  if (!name.empty()) {
    const Impl::NameMap::const_iterator found = m_impl->nameIds.find(name);
    if (found == m_impl->nameIds.end()) {
      return {};
    }
    const uint32_t slot = m_impl->nameFirst[found->second];
    if (slot == 0 || m_impl->nameNext[slot] == 0) {
      return m_impl->handle(slot);
    }
  }
  for (uint32_t slot = m_impl->firstChild[0]; slot != 0;
       slot = m_impl->nextSlot(slot)) {
    if (m_impl->names[m_impl->nameId[slot]] == name) {
      return m_impl->handle(slot);
    }
  }
  return {};
}
bool
SceneGraph::setUserData(SceneNodeHandle node, uint64_t data)
{
  if (m_impl->extractionActive || !m_impl->current(node)) {
    return false;
  }
  m_impl->userData[node.slot] = data;
  m_impl->record(SceneChangeKind::UserData, node.slot);
  return true;
}
bool
SceneGraph::getUserData(SceneNodeHandle node, uint64_t* data) const
{
  if (!m_impl->current(node) || data == nullptr) {
    return false;
  }
  *data = m_impl->userData[node.slot];
  return true;
}
SceneNodeHandle
SceneGraph::firstNode() const
{
  return m_impl->handle(m_impl->firstChild[0]);
}
SceneNodeHandle
SceneGraph::nextNode(SceneNodeHandle node) const
{
  return m_impl->current(node) ? m_impl->handle(m_impl->nextSlot(node.slot))
                               : SceneNodeHandle{};
}
bool
SceneGraph::addAttachment(SceneNodeHandle node, ISceneRenderAttachment* pointer)
{
  if (m_impl->extractionActive || !m_impl->current(node) ||
      pointer == nullptr) {
    return false;
  }
  for (uint32_t i = m_impl->attachmentFirst[node.slot]; i != 0;
       i = m_impl->attachment(i).next) {
    if (m_impl->attachment(i).pointer == pointer) {
      return false;
    }
  }
  const uint32_t index = m_impl->allocateAttachment(pointer);
  if (index == 0) {
    return false;
  }
  const uint32_t last = m_impl->attachmentLast[node.slot];
  if (last != 0) {
    m_impl->attachment(last).next = index;
  } else {
    m_impl->attachmentFirst[node.slot] = index;
  }
  m_impl->attachment(index).previous = last;
  m_impl->attachment(index).owner = node.slot;
  m_impl->attachmentLast[node.slot] = index;
  ++m_impl->attachmentCount[node.slot];
  m_impl->boundsDirty = true;
  m_impl->flags[node.slot] |= Impl::kBoundsDirty;
  ++m_impl->contentRevision;
  m_impl->record(SceneChangeKind::Attachments, node.slot);
  return true;
}
bool
SceneGraph::removeAttachment(SceneNodeHandle node,
                             ISceneRenderAttachment* pointer)
{
  if (m_impl->extractionActive || !m_impl->current(node) ||
      pointer == nullptr) {
    return false;
  }
  for (uint32_t i = m_impl->attachmentFirst[node.slot]; i != 0;
       i = m_impl->attachment(i).next) {
    Impl::Attachment& entry = m_impl->attachment(i);
    if (entry.pointer != pointer) {
      continue;
    }
    if (entry.previous != 0) {
      m_impl->attachment(entry.previous).next = entry.next;
    } else {
      m_impl->attachmentFirst[node.slot] = entry.next;
    }
    if (entry.next != 0) {
      m_impl->attachment(entry.next).previous = entry.previous;
    } else {
      m_impl->attachmentLast[node.slot] = entry.previous;
    }
    entry = Impl::Attachment{};
    entry.next = m_impl->freeAttachment;
    m_impl->freeAttachment = i;
    --m_impl->attachmentCount[node.slot];
    invalidateSnapshots();
    m_impl->boundsDirty = true;
    m_impl->flags[node.slot] |= Impl::kBoundsDirty;
    ++m_impl->contentRevision;
    m_impl->record(SceneChangeKind::Attachments, node.slot);
    return true;
  }
  return false;
}
size_t
SceneGraph::getAttachmentCount(SceneNodeHandle node) const
{
  return m_impl->current(node) ? m_impl->attachmentCount[node.slot] : 0;
}
ISceneRenderAttachment*
SceneGraph::getAttachment(SceneNodeHandle node, size_t index) const
{
  if (!m_impl->current(node)) {
    return nullptr;
  }
  uint32_t i = m_impl->attachmentFirst[node.slot];
  while (i != 0 && index-- != 0) {
    i = m_impl->attachment(i).next;
  }
  return i == 0 ? nullptr : m_impl->attachment(i).pointer;
}
bool
SceneGraph::setRenderAttachment(SceneNodeHandle node,
                                ISceneRenderAttachment* pointer)
{
  if (m_impl->extractionActive || !m_impl->current(node)) {
    return false;
  }
  if (m_impl->attachmentCount[node.slot] == 1 &&
      getAttachment(node, 0) == pointer) {
    return true;
  }
  // Allocate before dropping the old range, preserving it on allocation
  // failure.
  uint32_t replacement = 0;
  if (pointer != nullptr) {
    replacement = m_impl->allocateAttachment(pointer);
    if (replacement == 0) {
      return false;
    }
  }
  m_impl->releaseAttachments(node.slot);
  invalidateSnapshots();
  m_impl->attachmentFirst[node.slot] = m_impl->attachmentLast[node.slot] =
    replacement;
  m_impl->attachmentCount[node.slot] = replacement != 0 ? 1u : 0u;
  if (replacement != 0) {
    m_impl->attachment(replacement).owner = node.slot;
  }
  m_impl->boundsDirty = true;
  m_impl->flags[node.slot] |= Impl::kBoundsDirty;
  ++m_impl->contentRevision;
  m_impl->record(SceneChangeKind::Attachments, node.slot);
  return true;
}
ISceneRenderAttachment*
SceneGraph::getRenderAttachment(SceneNodeHandle node) const
{
  return getAttachment(node, 0);
}
uint64_t
SceneGraph::getStructuralRevision() const
{
  return m_impl->structuralRevision;
}
uint64_t
SceneGraph::getChangeSequence() const
{
  return m_impl->sequence;
}
bool
SceneGraph::readChanges(uint64_t after, std::vector<SceneChange>* changes) const
{
  if (changes == nullptr) {
    return false;
  }
  changes->clear();
  if (after > m_impl->sequence ||
      m_impl->sequence - after > Impl::kJournalCapacity) {
    return false;
  }
  for (uint64_t i = after; i < m_impl->sequence; ++i) {
    changes->push_back(m_impl->journal[i % Impl::kJournalCapacity]);
  }
  return true;
}
SceneGraphStatistics
SceneGraph::getStatistics() const
{
  return m_impl->statistics;
}
void
SceneGraph::invalidateSnapshots()
{
  if (!m_impl->extractionActive) {
    ++m_impl->lifetime->epoch;
  }
}
bool
SceneGraph::notifyAttachmentChanged(SceneNodeHandle node)
{
  if (m_impl->extractionActive || !m_impl->current(node)) {
    return false;
  }
  for (uint32_t index = m_impl->attachmentFirst[node.slot]; index != 0;
       index = m_impl->attachment(index).next) {
    m_impl->attachment(index).cached = false;
  }
  m_impl->boundsDirty = true;
  m_impl->flags[node.slot] |= Impl::kBoundsDirty;
  ++m_impl->contentRevision;
  invalidateSnapshots();
  m_impl->record(SceneChangeKind::Attachments, node.slot);
  return true;
}
