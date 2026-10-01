#pragma once

#include <Illumo/Foundation/AxisAlignedBounds3.h>
#include <cstdint>
#include <vector>

// Replaceable, derived query accelerator. Indices refer to caller-owned cached
// bounds; it never owns scene nodes or invokes attachment/user callbacks.
class SceneQueryIndex
{
public:
  bool build(const std::vector<AxisAlignedBounds3>& bounds,
             const std::vector<unsigned char>& valid,
             const std::vector<unsigned char>& enabled);
  bool raycast(const std::vector<AxisAlignedBounds3>& bounds,
               const Vector3& origin,
               const Vector3& direction,
               uint32_t* index,
               float* distance) const;
  void queryBounds(const std::vector<AxisAlignedBounds3>& bounds,
                   const AxisAlignedBounds3& query,
                   std::vector<uint32_t>* results) const;
  static bool intersectRay(const AxisAlignedBounds3& bounds,
                           const Vector3& origin,
                           const Vector3& direction,
                           float* distance);
  void raycastCandidates(const std::vector<AxisAlignedBounds3>& bounds,
                         const Vector3& origin,
                         const Vector3& direction,
                         std::vector<uint32_t>* results) const;

private:
  struct Node
  {
    AxisAlignedBounds3 bounds;
    uint32_t begin = 0;
    uint32_t count = 0;
    uint32_t left = 0;
    uint32_t right = 0;
    uint32_t escape = 0;
  };
  std::vector<Node> m_nodes;
  std::vector<uint32_t> m_indices;
  std::vector<uint32_t> m_buildScratch;
};
