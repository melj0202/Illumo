#include "SceneGraphInternal.h"

void
SceneGraph::raycastCandidates(const Vector3& origin,
                              const Vector3& direction,
                              std::vector<SceneRayHit>* hits,
                              bool useIndex) const
{
  if (hits == nullptr) {
    return;
  }
  hits->clear();
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
  const bool compiled = m_impl->resolve(true);
  if (compiled && useIndex && m_impl->ensureQueryIndex()) {
    m_impl->queryIndex.raycastCandidates(
      m_impl->worldBounds, origin, direction, &m_impl->queryScratch);
    for (uint32_t index : m_impl->queryScratch) {
      float distance = 0.0f;
      SceneQueryIndex::intersectRay(
        m_impl->worldBounds[index], origin, direction, &distance);
      hits->push_back(
        SceneRayHit{ m_impl->handle(m_impl->preorder[index]), distance });
    }
    return;
  }
  size_t index = 0;
  for (uint32_t slot = m_impl->firstChild[0]; slot != 0;
       slot = m_impl->nextSlot(slot), ++index) {
    AxisAlignedBounds3 bounds;
    bool valid = false;
    if (compiled) {
      bounds = m_impl->worldBounds[index];
      valid = m_impl->boundsValid[index] != 0 && m_impl->effective[index] != 0;
    } else if (isEffectivelyVisible(m_impl->handle(slot))) {
      Matrix4 world(1.0f);
      valid = m_impl->worldTransform(slot, &world) &&
              m_impl->boundsAtWorld(slot, world, &bounds, true);
    }
    float distance = 0.0f;
    if (valid &&
        SceneQueryIndex::intersectRay(bounds, origin, direction, &distance)) {
      hits->push_back(SceneRayHit{ m_impl->handle(slot), distance });
    }
  }
}

bool
SceneGraph::Impl::ensureQueryIndex()
{
  if (queryRevision == contentRevision) {
    return true;
  }
  queryRevision = 0;
  if (!queryIndex.build(worldBounds, boundsValid, effective)) {
    return false;
  }
  queryRevision = contentRevision;
  return true;
}

bool
SceneGraph::raycast(const Vector3& origin,
                    const Vector3& direction,
                    SceneRayHit* hit,
                    bool useIndex) const
{
  if (hit == nullptr || m_impl->extractionActive) {
    return false;
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
  const bool compiled = m_impl->resolve(true);
  if (compiled && useIndex && m_impl->ensureQueryIndex()) {
    uint32_t index = 0;
    float distance = 0.0f;
    if (!m_impl->queryIndex.raycast(
          m_impl->worldBounds, origin, direction, &index, &distance)) {
      return false;
    }
    *hit = SceneRayHit{ m_impl->handle(m_impl->preorder[index]), distance };
    return true;
  }
  float closest = std::numeric_limits<float>::infinity();
  SceneNodeHandle best;
  size_t index = 0;
  for (uint32_t slot = m_impl->firstChild[0]; slot != 0;
       slot = m_impl->nextSlot(slot), ++index) {
    AxisAlignedBounds3 bounds;
    bool valid = false;
    if (compiled) {
      valid = m_impl->effective[index] != 0 && m_impl->boundsValid[index] != 0;
      bounds = m_impl->worldBounds[index];
    } else if (isEffectivelyVisible(m_impl->handle(slot))) {
      Matrix4 world(1.0f);
      valid = m_impl->worldTransform(slot, &world) &&
              m_impl->boundsAtWorld(slot, world, &bounds, true);
    }
    float distance = 0.0f;
    if (valid &&
        SceneQueryIndex::intersectRay(bounds, origin, direction, &distance) &&
        distance < closest) {
      closest = distance;
      best = m_impl->handle(slot);
    }
  }
  if (best.isNull()) {
    return false;
  }
  *hit = SceneRayHit{ best, closest };
  return true;
}

void
SceneGraph::queryBounds(const AxisAlignedBounds3& bounds,
                        std::vector<SceneNodeHandle>* results,
                        bool useIndex) const
{
  if (results == nullptr) {
    return;
  }
  results->clear();
  if (!bounds.isValid() || m_impl->extractionActive) {
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
  const bool compiled = m_impl->resolve(true);
  if (compiled && useIndex && m_impl->ensureQueryIndex()) {
    m_impl->queryIndex.queryBounds(
      m_impl->worldBounds, bounds, &m_impl->queryScratch);
    for (uint32_t index : m_impl->queryScratch) {
      results->push_back(m_impl->handle(m_impl->preorder[index]));
    }
    return;
  }
  size_t index = 0;
  for (uint32_t slot = m_impl->firstChild[0]; slot != 0;
       slot = m_impl->nextSlot(slot), ++index) {
    AxisAlignedBounds3 worldBounds;
    bool valid = false;
    if (compiled) {
      valid = m_impl->effective[index] != 0 && m_impl->boundsValid[index] != 0;
      worldBounds = m_impl->worldBounds[index];
    } else if (isEffectivelyVisible(m_impl->handle(slot))) {
      Matrix4 world(1.0f);
      valid = m_impl->worldTransform(slot, &world) &&
              m_impl->boundsAtWorld(slot, world, &worldBounds, true);
    }
    if (valid && worldBounds.intersects(bounds)) {
      results->push_back(m_impl->handle(slot));
    }
  }
}
