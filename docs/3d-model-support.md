# Future 3D Model Support (OBJ/GLTF/FBX)

**Date**: August 2026

## Current State

Illumo currently does **not** support loading external 3D models like `.obj`, `.gltf`, or `.fbx`.

The rendering primitives available in the engine (such as pyramids, cubes, spheres, and ellipses) are generated mathematically on the fly by `MeshVisual` (located in `Illumo/Include/Illumo/Rendering/Primitives/MeshVisual.h`). `IllEd` (the editor) simply maps its file format definitions (`SceneNodeKind` in `.ilsc` files) to these built-in geometric generator functions.

`AssetManager` currently only supports loading two `AssetKind`s:
- `Texture`
- `Shader`

## Theoretical Loading with Current Architecture

If one were to write a parser for an `.obj` file right now, the resulting triangles *could* technically be fed directly into `MeshVisual::addSolidTriangle(a, b, c, color)` to render the shape. However, this approach has severe limitations:

1. **No Lighting/Normals**: The vertex structure used by `MeshVisual` (`ColorVertex`) only stores `Position (x,y,z)` and `Color (r,g,b,a)`. Because it lacks surface `Normals (nx,ny,nz)`, the shader cannot perform lighting calculations (like Phong shading). The 3D model would appear as a flat, unlit silhouette unless you manually calculated and assigned different colors to each face.
2. **No Textures for 3D Geometry**: The `MeshVisual` vertex layout that supports UV coordinates (`SpriteVertex`) is hardcoded to be used exclusively for 2D quads (`addSprite`). You cannot pass UV coordinates to `addSolidTriangle()`, meaning you could not wrap a texture around the loaded 3D model.
3. **Severe Memory Inefficiency**: `MeshVisual` is designed as an immediate-mode-like builder for simple debug shapes (like grid lines and editor gizmos). When you add a triangle, it unrolls the indices into a flat triangle list (`expandIndexedVertices`). For a dense 50,000-triangle model, this would cause massive vertex duplication, wasting GPU memory and vertex cache.

## Required Implementation for Proper Support

To properly support 3D models in the future, the following architectural additions would be needed:

1. **New Asset Type**: Add `AssetKind::Model` to `AssetManager` to handle asynchronous file loading and caching of parsed mesh data.
2. **Model Parser Integration**: Integrate a lightweight parsing library (like `tinyobjloader` or `cgltf`) inside the `AssetManager` worker thread.
3. **New Vertex Layout**: Define a new vertex layout, such as `MeshVertexLayout::Pos3Norm3Uv2`, to support normals and texture coordinates for 3D geometry.
4. **New Visual Component**: Create a `ModelVisual` class (similar to `MeshVisual`) that takes a `ModelHandle` and uploads the properly indexed geometry to the GPU without unrolling the indices, and binds the appropriate textured/lit shader pipeline.
