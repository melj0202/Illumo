#pragma once

#include <cstdint>

// Graph-local generational identity. Slot zero, generation zero, and graph ID
// zero are reserved for the null handle.
struct SceneNodeHandle
{
  uint64_t graphId = 0;
  uint32_t slot = 0;
  uint32_t generation = 0;

  bool isNull() const { return graphId == 0 && slot == 0 && generation == 0; }

  bool isValid() const { return graphId != 0 && slot != 0 && generation != 0; }
};

inline bool
operator==(SceneNodeHandle left, SceneNodeHandle right)
{
  return left.graphId == right.graphId && left.slot == right.slot &&
         left.generation == right.generation;
}

inline bool
operator!=(SceneNodeHandle left, SceneNodeHandle right)
{
  return !(left == right);
}
