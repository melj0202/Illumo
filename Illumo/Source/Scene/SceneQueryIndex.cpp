#include "SceneQueryIndex.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <new>

static double
surfaceArea(const AxisAlignedBounds3& bounds)
{
  const Vector3 extent = bounds.maximum - bounds.minimum;
  return 2.0 * (static_cast<double>(extent.x) * extent.y +
                static_cast<double>(extent.y) * extent.z +
                static_cast<double>(extent.z) * extent.x);
}
static float
centerAxis(const AxisAlignedBounds3& bounds, int axis)
{
  return bounds.minimum[axis] * 0.5f + bounds.maximum[axis] * 0.5f;
}

void
SceneQueryIndex::raycastCandidates(
  const std::vector<AxisAlignedBounds3>& bounds,
  const Vector3& origin,
  const Vector3& direction,
  std::vector<uint32_t>* results) const
{
  results->clear();
  if (m_nodes.size() < 2) {
    return;
  }
  uint32_t current = 1;
  while (current != 0) {
    const Node& node = m_nodes[current];
    float distance = 0.0f;
    if (!intersectRay(node.bounds, origin, direction, &distance)) {
      current = node.escape;
      continue;
    }
    if (node.count == 0) {
      current = node.left;
      continue;
    }
    for (uint32_t i = node.begin; i < node.begin + node.count; ++i) {
      if (intersectRay(bounds[m_indices[i]], origin, direction, &distance)) {
        results->push_back(m_indices[i]);
      }
    }
    current = node.escape;
  }
  std::sort(results->begin(), results->end());
}

bool
SceneQueryIndex::build(const std::vector<AxisAlignedBounds3>& bounds,
                       const std::vector<unsigned char>& valid,
                       const std::vector<unsigned char>& enabled)
{
  m_nodes.clear();
  m_indices.clear();
  m_buildScratch.clear();
  try {
    m_indices.reserve(bounds.size());
    for (uint32_t i = 0; i < bounds.size(); ++i) {
      if (valid[i] != 0 && enabled[i] != 0) {
        m_indices.push_back(i);
      }
    }
    if (m_indices.empty()) {
      return true;
    }
    m_nodes.reserve(m_indices.size() * 2 + 1);
    m_buildScratch.reserve(m_indices.size());
    m_nodes.emplace_back(); // zero terminates threaded traversal
    Node root;
    root.count = static_cast<uint32_t>(m_indices.size());
    m_nodes.push_back(root);
    m_buildScratch.push_back(1);
    constexpr int kBins = 12;
    struct Bin
    {
      AxisAlignedBounds3 bounds;
      uint32_t count = 0;
    };
    while (!m_buildScratch.empty()) {
      const uint32_t nodeIndex = m_buildScratch.back();
      m_buildScratch.pop_back();
      Node node = m_nodes[nodeIndex];
      node.bounds = bounds[m_indices[node.begin]];
      AxisAlignedBounds3 centers{ Vector3(centerAxis(node.bounds, 0),
                                          centerAxis(node.bounds, 1),
                                          centerAxis(node.bounds, 2)),
                                  Vector3(centerAxis(node.bounds, 0),
                                          centerAxis(node.bounds, 1),
                                          centerAxis(node.bounds, 2)) };
      for (uint32_t i = node.begin + 1; i < node.begin + node.count; ++i) {
        const AxisAlignedBounds3& box = bounds[m_indices[i]];
        node.bounds.include(box);
        centers.include(
          Vector3(centerAxis(box, 0), centerAxis(box, 1), centerAxis(box, 2)));
      }
      m_nodes[nodeIndex].bounds = node.bounds;
      if (node.count <= 4) {
        continue;
      }
      double bestCost = std::numeric_limits<double>::infinity();
      int bestAxis = -1;
      int bestBin = -1;
      for (int axis = 0; axis < 3; ++axis) {
        const double extent =
          static_cast<double>(centers.maximum[axis]) - centers.minimum[axis];
        if (extent <= 0.0) {
          continue;
        }
        std::array<Bin, kBins> bins{};
        for (uint32_t i = node.begin; i < node.begin + node.count; ++i) {
          const AxisAlignedBounds3& box = bounds[m_indices[i]];
          const int bin = std::clamp(
            static_cast<int>((centerAxis(box, axis) -
                              static_cast<double>(centers.minimum[axis])) /
                             extent * kBins),
            0,
            kBins - 1);
          if (bins[bin].count == 0) {
            bins[bin].bounds = box;
          } else {
            bins[bin].bounds.include(box);
          }
          ++bins[bin].count;
        }
        for (int split = 0; split < kBins - 1; ++split) {
          Bin left, right;
          for (int bin = 0; bin < kBins; ++bin) {
            if (bins[bin].count == 0) {
              continue;
            }
            Bin& side = bin <= split ? left : right;
            if (side.count == 0) {
              side.bounds = bins[bin].bounds;
            } else {
              side.bounds.include(bins[bin].bounds);
            }
            side.count += bins[bin].count;
          }
          if (left.count == 0 || right.count == 0) {
            continue;
          }
          const double cost = surfaceArea(left.bounds) * left.count +
                              surfaceArea(right.bounds) * right.count;
          if (cost < bestCost) {
            bestCost = cost;
            bestAxis = axis;
            bestBin = split;
          }
        }
      }
      uint32_t middle = node.begin + node.count / 2;
      if (bestAxis >= 0) {
        const double extent = static_cast<double>(centers.maximum[bestAxis]) -
                              centers.minimum[bestAxis];
        const std::vector<uint32_t>::iterator split = std::partition(
          m_indices.begin() + node.begin,
          m_indices.begin() + node.begin + node.count,
          [&bounds, &centers, bestAxis, bestBin, extent](uint32_t index) {
            const int bin =
              std::clamp(static_cast<int>(
                           (centerAxis(bounds[index], bestAxis) -
                            static_cast<double>(centers.minimum[bestAxis])) /
                           extent * kBins),
                         0,
                         kBins - 1);
            return bin <= bestBin;
          });
        middle = static_cast<uint32_t>(split - m_indices.begin());
      }
      if (middle == node.begin || middle == node.begin + node.count) {
        middle = node.begin + node.count / 2;
      }
      const uint32_t leftIndex = static_cast<uint32_t>(m_nodes.size());
      const uint32_t rightIndex = leftIndex + 1;
      Node left;
      left.begin = node.begin;
      left.count = middle - node.begin;
      left.escape = rightIndex;
      Node right;
      right.begin = middle;
      right.count = node.begin + node.count - middle;
      right.escape = node.escape;
      m_nodes.push_back(left);
      m_nodes.push_back(right);
      m_nodes[nodeIndex].left = leftIndex;
      m_nodes[nodeIndex].right = rightIndex;
      m_nodes[nodeIndex].count = 0;
      m_buildScratch.push_back(rightIndex);
      m_buildScratch.push_back(leftIndex);
    }
    return true;
  } catch (const std::bad_alloc&) {
    m_nodes.clear();
    m_indices.clear();
    return false;
  }
}

bool
SceneQueryIndex::intersectRay(const AxisAlignedBounds3& bounds,
                              const Vector3& origin,
                              const Vector3& direction,
                              float* distance)
{
  if (distance == nullptr || !bounds.isValid()) {
    return false;
  }
  double near = 0.0;
  double far = std::numeric_limits<double>::infinity();
  bool nonzero = false;
  for (int axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(origin[axis]) || !std::isfinite(direction[axis])) {
      return false;
    }
    if (direction[axis] == 0.0f) {
      if (origin[axis] < bounds.minimum[axis] ||
          origin[axis] > bounds.maximum[axis]) {
        return false;
      }
      continue;
    }
    nonzero = true;
    double a = (static_cast<double>(bounds.minimum[axis]) - origin[axis]) /
               direction[axis];
    double b = (static_cast<double>(bounds.maximum[axis]) - origin[axis]) /
               direction[axis];
    if (a > b) {
      std::swap(a, b);
    }
    near = std::max(near, a);
    far = std::min(far, b);
    if (near > far) {
      return false;
    }
  }
  if (!nonzero || !std::isfinite(near) ||
      near > std::numeric_limits<float>::max()) {
    return false;
  }
  *distance = static_cast<float>(near);
  return true;
}

bool
SceneQueryIndex::raycast(const std::vector<AxisAlignedBounds3>& bounds,
                         const Vector3& origin,
                         const Vector3& direction,
                         uint32_t* index,
                         float* distance) const
{
  if (m_nodes.size() < 2 || index == nullptr || distance == nullptr) {
    return false;
  }
  float closest = std::numeric_limits<float>::infinity();
  uint32_t best = std::numeric_limits<uint32_t>::max();
  uint32_t current = 1;
  while (current != 0) {
    const Node& node = m_nodes[current];
    float entry = 0.0f;
    if (!intersectRay(node.bounds, origin, direction, &entry) ||
        entry > closest) {
      current = node.escape;
      continue;
    }
    if (node.count == 0) {
      current = node.left;
      continue;
    }
    for (uint32_t i = node.begin; i < node.begin + node.count; ++i) {
      const uint32_t candidate = m_indices[i];
      float hit = 0.0f;
      if (intersectRay(bounds[candidate], origin, direction, &hit) &&
          (hit < closest || (hit == closest && candidate < best))) {
        closest = hit;
        best = candidate;
      }
    }
    current = node.escape;
  }
  if (best == std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  *index = best;
  *distance = closest;
  return true;
}

void
SceneQueryIndex::queryBounds(const std::vector<AxisAlignedBounds3>& bounds,
                             const AxisAlignedBounds3& query,
                             std::vector<uint32_t>* results) const
{
  results->clear();
  if (m_nodes.size() < 2 || !query.isValid()) {
    return;
  }
  uint32_t current = 1;
  while (current != 0) {
    const Node& node = m_nodes[current];
    if (!node.bounds.intersects(query)) {
      current = node.escape;
      continue;
    }
    if (node.count == 0) {
      current = node.left;
      continue;
    }
    for (uint32_t i = node.begin; i < node.begin + node.count; ++i) {
      if (bounds[m_indices[i]].intersects(query)) {
        results->push_back(m_indices[i]);
      }
    }
    current = node.escape;
  }
  std::sort(results->begin(), results->end());
}
