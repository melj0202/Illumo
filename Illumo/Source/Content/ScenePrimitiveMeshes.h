#pragma once

#include <Illumo/Content/SceneDocument.h>
#include <Illumo/Rendering/MeshData.h>

// Unit meshes for the solid primitive shapes: each spans [-1, 1] on its used
// axes (extent is applied as scale), has white vertex colors (the primitive
// color is the tint) and outward normals. Flat shapes lie in the XY plane and
// are double-sided. Returns false for wire shapes, which stay procedural.
bool
buildScenePrimitiveMesh(ScenePrimitiveShape shape, MeshData& mesh);

// Index into a per-instance cache of solid shapes, or -1 for wire shapes.
int
scenePrimitiveMeshSlot(ScenePrimitiveShape shape);

constexpr int kScenePrimitiveMeshSlots = 6;
