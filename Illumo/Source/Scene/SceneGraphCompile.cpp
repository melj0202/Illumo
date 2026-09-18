#include "SceneGraphInternal.h"

#include <Illumo/Rendering/Renderer.h>
#include <algorithm>
#include <new>

bool
SceneGraph::Impl::worldTransform(uint32_t slot, Matrix4* transform)
{
  pathScratch.clear();
  for (uint32_t currentSlot = slot; currentSlot != 0;
       currentSlot = parent[currentSlot]) {
    pathScratch.push_back(currentSlot);
  }
  Matrix4 result(1.0f);
  for (size_t i = pathScratch.size(); i-- > 0;) {
    const Matrix4 localMatrix = local[pathScratch[i]].toMatrix();
    result = i == pathScratch.size() - 1 ? localMatrix : result * localMatrix;
  }
  *transform = result;
  return true;
}

bool
SceneGraph::Impl::refreshBounds(uint32_t index)
{
  Attachment& entry = attachment(index);
  const uint64_t revision = entry.pointer->getSceneBoundsRevision();
  if (revision != 0 && entry.cached && entry.revision == revision) {
    return false;
  }
  AxisAlignedBounds3 next;
  const bool valid =
    entry.pointer->getSceneLocalBounds(&next) && next.isValid();
  ++statistics.boundsQueries;
  const bool changed = !entry.cached || entry.valid != valid ||
                       (valid && (entry.localBounds.minimum != next.minimum ||
                                  entry.localBounds.maximum != next.maximum));
  entry.localBounds = next;
  entry.valid = valid;
  entry.revision = revision;
  entry.cached = true;
  if (changed) {
    boundsDirty = true;
    flags[entry.owner] |= kBoundsDirty;
    ++contentRevision;
  }
  return changed;
}

bool
SceneGraph::Impl::localBounds(uint32_t slot, AxisAlignedBounds3* bounds)
{
  bool present = false;
  bool valid = true;
  AxisAlignedBounds3 result;
  for (uint32_t index = attachmentFirst[slot]; index != 0;
       index = attachment(index).next) {
    refreshBounds(index);
    const Attachment& entry = attachment(index);
    if (!entry.valid) {
      valid = false;
      continue;
    }
    if (!present) {
      result = entry.localBounds;
      present = true;
    } else {
      result.include(entry.localBounds);
    }
  }
  if (!valid || !present) {
    return false;
  }
  *bounds = result;
  return true;
}

bool
SceneGraph::Impl::compile()
{
  if (compiledRevision == structuralRevision) {
    return true;
  }
  compiledRevision = 0;
  try {
    preorder.resize(nodeCount);
    parentIndex.resize(nodeCount);
    subtreeSize.assign(nodeCount, 1);
    depth.resize(nodeCount);
    slotToIndex.assign(flags.size(), kNoIndex);
    world.resize(nodeCount);
    worldBounds.resize(nodeCount);
    subtreeBounds.resize(nodeCount);
    effective.resize(nodeCount);
    recomputed.resize(nodeCount);
    boundsValid.resize(nodeCount);
    subtreeValid.resize(nodeCount);
    subtreePresent.resize(nodeCount);
    uint32_t index = 0;
    for (uint32_t slot = firstChild[0]; slot != 0; slot = nextSlot(slot)) {
      preorder[index] = slot;
      slotToIndex[slot] = index;
      const uint32_t p =
        parent[slot] == 0 ? kNoIndex : slotToIndex[parent[slot]];
      parentIndex[index] = p;
      depth[index] = p == kNoIndex ? 0 : depth[p] + 1;
      ++index;
    }
    for (size_t i = nodeCount; i-- > 0;) {
      if (parentIndex[i] != kNoIndex) {
        subtreeSize[parentIndex[i]] += subtreeSize[i];
      }
    }
    // All compiled indices may have changed. Dirtiness in authoritative slots
    // survives queries; only this forward resolve consumes it.
    for (uint32_t slot : preorder) {
      flags[slot] |= kDirty | kBoundsDirty;
    }
    lowestDirtyIndex = 0;
    stateDirty = true;
    boundsDirty = true;
    compiledRevision = structuralRevision;
    statistics.compiledNodes = nodeCount;
    ++statistics.compilations;
    return true;
  } catch (const std::bad_alloc&) {
    return false;
  }
}

bool
SceneGraph::Impl::boundsAtWorld(uint32_t slot,
                                const Matrix4& matrix,
                                AxisAlignedBounds3* bounds,
                                bool refresh)
{
  bool present = false;
  bool valid = true;
  AxisAlignedBounds3 result;
  for (uint32_t index = attachmentFirst[slot]; index != 0;
       index = attachment(index).next) {
    if (refresh) {
      refreshBounds(index);
    }
    const Attachment& entry = attachment(index);
    AxisAlignedBounds3 transformed;
    if (!entry.valid || !entry.localBounds.transformed(matrix, &transformed)) {
      valid = false;
      continue;
    }
    if (!present) {
      result = transformed;
      present = true;
    } else {
      result.include(transformed);
    }
  }
  if (!valid || !present) {
    return false;
  }
  *bounds = result;
  return true;
}

bool
SceneGraph::Impl::resolve(bool withBounds)
{
  if (!compile()) {
    return false;
  }
  statistics.recomputedTransforms = 0;
  const size_t start = std::min(lowestDirtyIndex, nodeCount);
  for (size_t i = start; i < nodeCount; ++i) {
    const uint32_t slot = preorder[i];
    const uint32_t p = parentIndex[i];
    const bool changed = (flags[slot] & kDirty) != 0 ||
                         (p != kNoIndex && p >= start && recomputed[p] != 0);
    recomputed[i] = changed ? 1 : 0;
    if (changed) {
      const Matrix4 matrix = local[slot].toMatrix();
      world[i] = p == kNoIndex ? matrix : world[p] * matrix;
      flags[slot] &= ~kDirty;
      flags[slot] |= kBoundsDirty;
      ++statistics.recomputedTransforms;
    }
  }
  lowestDirtyIndex = nodeCount;
  if (stateDirty) {
    for (size_t i = 0; i < nodeCount; ++i) {
      const uint32_t slot = preorder[i];
      const uint32_t p = parentIndex[i];
      effective[i] =
        (flags[slot] & (kEnabled | kVisible)) == (kEnabled | kVisible) &&
            (p == kNoIndex || effective[p] != 0)
          ? 1
          : 0;
    }
    stateDirty = false;
    boundsDirty = true;
  }
  if (!withBounds) {
    return true;
  }
  for (uint32_t slot : preorder) {
    for (uint32_t index = attachmentFirst[slot]; index != 0;
         index = attachment(index).next) {
      refreshBounds(index);
    }
  }
  if (!boundsDirty) {
    return true;
  }
  for (size_t i = 0; i < nodeCount; ++i) {
    const uint32_t slot = preorder[i];
    if ((flags[slot] & kBoundsDirty) != 0) {
      bool present = false;
      bool valid = true;
      AxisAlignedBounds3 combined;
      for (uint32_t index = attachmentFirst[slot]; index != 0;
           index = attachment(index).next) {
        Attachment& entry = attachment(index);
        AxisAlignedBounds3 transformed;
        entry.worldValid =
          entry.valid && entry.localBounds.transformed(world[i], &transformed);
        entry.worldBounds = transformed;
        if (!entry.worldValid) {
          valid = false;
          continue;
        }
        if (!present) {
          combined = transformed;
          present = true;
        } else {
          combined.include(transformed);
        }
      }
      worldBounds[i] = combined;
      boundsValid[i] = valid && present ? 1 : 0;
      flags[slot] &= ~kBoundsDirty;
    }
    subtreeBounds[i] = worldBounds[i];
    subtreeValid[i] =
      effective[i] == 0 || attachmentCount[slot] == 0 || boundsValid[i] != 0
        ? 1
        : 0;
    subtreePresent[i] = effective[i] != 0 && boundsValid[i] != 0 ? 1 : 0;
  }
  for (size_t i = nodeCount; i-- > 0;) {
    const uint32_t p = parentIndex[i];
    if (p == kNoIndex) {
      continue;
    }
    subtreeValid[p] = subtreeValid[p] != 0 && subtreeValid[i] != 0 ? 1 : 0;
    if (subtreePresent[i] == 0) {
      continue;
    }
    if (subtreePresent[p] == 0) {
      subtreeBounds[p] = subtreeBounds[i];
    } else {
      subtreeBounds[p].include(subtreeBounds[i]);
    }
    subtreePresent[p] = 1;
  }
  boundsDirty = false;
  return true;
}

const SceneSnapshot*
SceneSnapshotView::get() const
{
  return m_lifetime && m_lifetime->alive && m_lifetime->epoch == m_epoch &&
             m_snapshot && m_snapshot->publication == m_publication
           ? m_snapshot.get()
           : nullptr;
}

void
SceneGraph::emitDirect(Renderer* renderer, unsigned pass)
{
  if (m_impl->extractionActive) {
    return;
  }
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
  for (uint32_t slot = m_impl->firstChild[0]; slot != 0;
       slot = m_impl->nextSlot(slot)) {
    if (!isEffectivelyVisible(m_impl->handle(slot))) {
      continue;
    }
    Matrix4 world(1.0f);
    m_impl->worldTransform(slot, &world);
    for (uint32_t index = m_impl->attachmentFirst[slot]; index != 0;
         index = m_impl->attachment(index).next) {
      m_impl->refreshBounds(index);
      const Impl::Attachment& entry = m_impl->attachment(index);
      AxisAlignedBounds3 bounds;
      const bool valid =
        entry.valid && entry.localBounds.transformed(world, &bounds);
      if (pass == 0) {
        entry.pointer->collectSceneShadowCasters(renderer, world);
      } else if (pass == 1) {
        if (renderer == nullptr || !valid ||
            renderer->isShadowCasterRelevant(bounds)) {
          entry.pointer->appendSceneShadowCommands(renderer, world);
        }
      } else if (renderer == nullptr || !valid ||
                 renderer->isWorldBoundsVisible(bounds)) {
        entry.pointer->appendSceneCommands(renderer, world);
      }
    }
  }
}

SceneSnapshotView
SceneGraph::extract(Renderer* renderer)
{
  if (m_impl->extractionActive) {
    return {};
  }
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
  m_impl->statistics.boundsQueries = 0;
  m_impl->statistics.extractionVisits = 0;
  const bool compiled = m_impl->resolve(true);
  const uint64_t publication = ++m_impl->publication;
  std::shared_ptr<SceneSnapshot>& snapshot =
    m_impl->snapshots[publication % m_impl->snapshots.size()];
  snapshot->publication = publication; // Retire views before touching storage.
  snapshot->items.clear();
  snapshot->graphId = m_impl->graphId;
  snapshot->structuralRevision = m_impl->structuralRevision;
  snapshot->frameSerial =
    renderer != nullptr ? renderer->getFrameContext().frameSerial : 0;
  size_t cameraRejectedUntil = 0;
  if (compiled) {
    for (size_t i = 0; i < m_impl->nodeCount;) {
      ++m_impl->statistics.extractionVisits;
      if (m_impl->effective[i] == 0) {
        i += m_impl->subtreeSize[i];
        continue;
      }
      const uint32_t slot = m_impl->preorder[i];
      // The shadow fit does not exist yet. A camera-missed subtree skips camera
      // tests, but its casters remain available for the shared light fit.
      if (i >= cameraRejectedUntil && renderer != nullptr &&
          m_impl->subtreeValid[i] != 0 && m_impl->subtreePresent[i] != 0 &&
          !renderer->isWorldBoundsVisible(m_impl->subtreeBounds[i])) {
        cameraRejectedUntil = i + m_impl->subtreeSize[i];
      }
      for (uint32_t index = m_impl->attachmentFirst[slot]; index != 0;
           index = m_impl->attachment(index).next) {
        const Impl::Attachment& entry = m_impl->attachment(index);
        SceneRenderItem item;
        item.worldTransform = m_impl->world[i];
        item.attachment = entry.pointer;
        item.node = m_impl->handle(slot);
        item.boundsValid = entry.worldValid;
        item.worldBounds = entry.worldBounds;
        item.cameraVisible = i >= cameraRejectedUntil &&
                             (renderer == nullptr || !item.boundsValid ||
                              renderer->isWorldBoundsVisible(item.worldBounds));
        snapshot->items.push_back(item);
      }
      ++i;
    }
  } else {
    // A failed compile changes cost, not correctness. Intrusive traversal and
    // the authoritative O(depth) query require no compiled arrays.
    for (uint32_t slot = m_impl->firstChild[0]; slot != 0;
         slot = m_impl->nextSlot(slot)) {
      ++m_impl->statistics.extractionVisits;
      if (!isEffectivelyVisible(m_impl->handle(slot))) {
        continue;
      }
      Matrix4 world(1.0f);
      m_impl->worldTransform(slot, &world);
      for (uint32_t index = m_impl->attachmentFirst[slot]; index != 0;
           index = m_impl->attachment(index).next) {
        m_impl->refreshBounds(index);
        const Impl::Attachment& entry = m_impl->attachment(index);
        SceneRenderItem item;
        item.worldTransform = world;
        item.attachment = entry.pointer;
        item.node = m_impl->handle(slot);
        item.boundsValid = entry.valid && entry.localBounds.transformed(
                                            world, &item.worldBounds);
        item.cameraVisible = renderer == nullptr || !item.boundsValid ||
                             renderer->isWorldBoundsVisible(item.worldBounds);
        snapshot->items.push_back(item);
      }
    }
  }
  ++m_impl->statistics.extractions;
  SceneSnapshotView view;
  view.m_snapshot = snapshot;
  view.m_lifetime = m_impl->lifetime;
  view.m_epoch = m_impl->lifetime->epoch;
  view.m_publication = publication;
  return view;
}
