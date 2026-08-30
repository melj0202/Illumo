#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/MeshVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/WorldLook.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <glm/gtc/type_ptr.hpp>

MeshVisual::MeshVisual() = default;

MeshVisual::~MeshVisual()
{
  releaseMeshes();
  releaseStyles();
}

void
MeshVisual::prepare(Renderer* value)
{
  if (value == nullptr || (renderer != nullptr && renderer != value)) {
    return;
  }
  renderer = value;
  ensureStyles();
}

void
MeshVisual::setModelMatrix(const glm::mat4& value)
{
  modelMatrix = value;
}

void
MeshVisual::clearPrimitives()
{
  lineVertices.clear();
  lineIndices.clear();
  triangleVertices.clear();
  triangleIndices.clear();
  sprites.clear();
  geometryDirty = true;
}

size_t
MeshVisual::addQuad(const glm::vec3& center,
                    const glm::vec2& size,
                    ColorRgba color)
{
  const float halfW = std::max(size.x, 0.0f) * 0.5f;
  const float halfH = std::max(size.y, 0.0f) * 0.5f;
  const glm::vec3 corners[4] = {
    center + glm::vec3(-halfW, -halfH, 0.0f),
    center + glm::vec3(halfW, -halfH, 0.0f),
    center + glm::vec3(halfW, halfH, 0.0f),
    center + glm::vec3(-halfW, halfH, 0.0f),
  };
  const unsigned int vertexOffset =
    static_cast<unsigned int>(triangleVertices.size());
  for (int i = 0; i < 4; ++i) {
    triangleVertices.push_back({ corners[i].x,
                                 corners[i].y,
                                 corners[i].z,
                                 color.r,
                                 color.g,
                                 color.b,
                                 color.a });
  }
  triangleIndices.push_back(vertexOffset + 0);
  triangleIndices.push_back(vertexOffset + 1);
  triangleIndices.push_back(vertexOffset + 2);
  triangleIndices.push_back(vertexOffset + 0);
  triangleIndices.push_back(vertexOffset + 2);
  triangleIndices.push_back(vertexOffset + 3);
  geometryDirty = true;
  return triangleIndices.size() / 6;
}

size_t
MeshVisual::addSprite(TextureHandle textureHandle,
                      const glm::vec3& center,
                      const glm::vec2& size,
                      ColorRgba tint,
                      MeshFacing facing,
                      const TextureRegion& region)
{
  SpriteItem item;
  item.textureHandle = textureHandle;
  item.local.position = Vector3(center.x, center.y, center.z);
  item.local.scale =
    Vector3(std::max(size.x, 0.0f), std::max(size.y, 0.0f), 1.0f);
  item.tint = tint;
  item.facing = facing;
  item.region = region;
  sprites.push_back(item);
  geometryDirty = true;
  return sprites.size() - 1;
}

void
MeshVisual::addAxes(const glm::vec3& origin, float length)
{
  const float safeLength = std::max(length, 0.0f);
  addLine(origin,
          origin + glm::vec3(safeLength, 0.0f, 0.0f),
          ColorRgba{ 255, 64, 64, 255 });
  addLine(origin,
          origin + glm::vec3(0.0f, safeLength, 0.0f),
          ColorRgba{ 64, 255, 64, 255 });
  addLine(origin,
          origin + glm::vec3(0.0f, 0.0f, safeLength),
          ColorRgba{ 64, 128, 255, 255 });
}

void
MeshVisual::addGrid(int halfLineCount, float spacing, ColorRgba color)
{
  const int safeHalfLineCount = std::max(halfLineCount, 0);
  const float safeSpacing = std::max(spacing, 0.0001f);
  const float extent = static_cast<float>(safeHalfLineCount) * safeSpacing;
  for (int line = -safeHalfLineCount; line <= safeHalfLineCount; ++line) {
    const float coordinate = static_cast<float>(line) * safeSpacing;
    addLine(glm::vec3(-extent, 0.0f, coordinate),
            glm::vec3(extent, 0.0f, coordinate),
            color);
    addLine(glm::vec3(coordinate, 0.0f, -extent),
            glm::vec3(coordinate, 0.0f, extent),
            color);
  }
}

void
MeshVisual::addWireCube(const glm::vec3& center,
                        const glm::vec3& halfExtent,
                        ColorRgba color)
{
  const glm::vec3 extent = glm::max(halfExtent, glm::vec3(0.0f));
  const glm::vec3 corners[8] = {
    center + glm::vec3(-extent.x, -extent.y, -extent.z),
    center + glm::vec3(extent.x, -extent.y, -extent.z),
    center + glm::vec3(extent.x, extent.y, -extent.z),
    center + glm::vec3(-extent.x, extent.y, -extent.z),
    center + glm::vec3(-extent.x, -extent.y, extent.z),
    center + glm::vec3(extent.x, -extent.y, extent.z),
    center + glm::vec3(extent.x, extent.y, extent.z),
    center + glm::vec3(-extent.x, extent.y, extent.z),
  };
  static const unsigned int kEdges[24] = {
    0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7,
  };
  for (unsigned int edge = 0; edge < 24; edge += 2) {
    addLine(corners[kEdges[edge]], corners[kEdges[edge + 1]], color);
  }
}

void
MeshVisual::addSolidCube(const glm::vec3& center,
                         const glm::vec3& halfExtent,
                         ColorRgba color)
{
  const glm::vec3 extent = glm::max(halfExtent, glm::vec3(0.0f));
  const glm::vec3 corners[8] = {
    center + glm::vec3(-extent.x, -extent.y, -extent.z),
    center + glm::vec3(extent.x, -extent.y, -extent.z),
    center + glm::vec3(extent.x, extent.y, -extent.z),
    center + glm::vec3(-extent.x, extent.y, -extent.z),
    center + glm::vec3(-extent.x, -extent.y, extent.z),
    center + glm::vec3(extent.x, -extent.y, extent.z),
    center + glm::vec3(extent.x, extent.y, extent.z),
    center + glm::vec3(-extent.x, extent.y, extent.z),
  };
  const unsigned int vertexOffset =
    static_cast<unsigned int>(triangleVertices.size());
  for (const glm::vec3& corner : corners) {
    triangleVertices.push_back(
      { corner.x, corner.y, corner.z, color.r, color.g, color.b, color.a });
  }
  static const unsigned int kCubeIndices[36] = {
    0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
    1, 2, 6, 1, 6, 5, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2,
  };
  for (unsigned int index : kCubeIndices) {
    triangleIndices.push_back(vertexOffset + index);
  }
  geometryDirty = true;
}

void
MeshVisual::addSolidTriangle(const glm::vec3& a,
                             const glm::vec3& b,
                             const glm::vec3& c,
                             ColorRgba color)
{
  const unsigned int vertexOffset =
    static_cast<unsigned int>(triangleVertices.size());
  triangleVertices.push_back(
    { a.x, a.y, a.z, color.r, color.g, color.b, color.a });
  triangleVertices.push_back(
    { b.x, b.y, b.z, color.r, color.g, color.b, color.a });
  triangleVertices.push_back(
    { c.x, c.y, c.z, color.r, color.g, color.b, color.a });
  triangleIndices.push_back(vertexOffset + 0);
  triangleIndices.push_back(vertexOffset + 1);
  triangleIndices.push_back(vertexOffset + 2);
  geometryDirty = true;
}

void
MeshVisual::addSolidPyramid(const glm::vec3& center,
                            const glm::vec3& halfExtent,
                            ColorRgba color)
{
  const glm::vec3 extent = glm::max(halfExtent, glm::vec3(0.0f));
  const glm::vec3 apex = center + glm::vec3(0.0f, extent.y, 0.0f);
  const glm::vec3 b0 = center + glm::vec3(-extent.x, -extent.y, -extent.z);
  const glm::vec3 b1 = center + glm::vec3(extent.x, -extent.y, -extent.z);
  const glm::vec3 b2 = center + glm::vec3(extent.x, -extent.y, extent.z);
  const glm::vec3 b3 = center + glm::vec3(-extent.x, -extent.y, extent.z);
  addSolidTriangle(b0, b1, b2, color);
  addSolidTriangle(b0, b2, b3, color);
  addSolidTriangle(apex, b0, b1, color);
  addSolidTriangle(apex, b1, b2, color);
  addSolidTriangle(apex, b2, b3, color);
  addSolidTriangle(apex, b3, b0, color);
}

void
MeshVisual::addWireSphere(const glm::vec3& center,
                          float radius,
                          ColorRgba color)
{
  const float safeRadius = std::max(radius, 0.0f);
  const int kSegments = 16;
  const float step = 6.28318530718f / static_cast<float>(kSegments);
  for (int ring = 0; ring < 3; ++ring) {
    for (int i = 0; i < kSegments; ++i) {
      const float a0 = step * static_cast<float>(i);
      const float a1 = step * static_cast<float>(i + 1);
      const float c0 = std::cos(a0) * safeRadius;
      const float s0 = std::sin(a0) * safeRadius;
      const float c1 = std::cos(a1) * safeRadius;
      const float s1 = std::sin(a1) * safeRadius;
      glm::vec3 p0 = center;
      glm::vec3 p1 = center;
      if (ring == 0) {
        p0 += glm::vec3(c0, s0, 0.0f);
        p1 += glm::vec3(c1, s1, 0.0f);
      } else if (ring == 1) {
        p0 += glm::vec3(c0, 0.0f, s0);
        p1 += glm::vec3(c1, 0.0f, s1);
      } else {
        p0 += glm::vec3(0.0f, c0, s0);
        p1 += glm::vec3(0.0f, c1, s1);
      }
      addLine(p0, p1, color);
    }
  }
}

void
MeshVisual::addSolidEllipse(const glm::vec3& center,
                            const glm::vec2& radius,
                            ColorRgba color,
                            int segments)
{
  const int count = std::max(segments, 3);
  const float step = 6.28318530718f / static_cast<float>(count);
  const unsigned int centerIndex =
    static_cast<unsigned int>(triangleVertices.size());
  triangleVertices.push_back(
    { center.x, center.y, center.z, color.r, color.g, color.b, color.a });

  for (int i = 0; i < count; ++i) {
    const float angle = step * static_cast<float>(i);
    const float x = center.x + std::cos(angle) * std::max(radius.x, 0.0f);
    const float y = center.y + std::sin(angle) * std::max(radius.y, 0.0f);
    triangleVertices.push_back(
      { x, y, center.z, color.r, color.g, color.b, color.a });
  }

  for (int i = 0; i < count; ++i) {
    const unsigned int current = centerIndex + 1 + static_cast<unsigned int>(i);
    const unsigned int next =
      centerIndex + 1 + static_cast<unsigned int>((i + 1) % count);
    triangleIndices.push_back(centerIndex);
    triangleIndices.push_back(current);
    triangleIndices.push_back(next);
  }
  geometryDirty = true;
}

bool
MeshVisual::AppendCommands(Renderer* value)
{
  return appendCommandsWithWorld(value, glm::mat4(1.0f));
}

void
MeshVisual::appendSceneCommands(Renderer* value, const Matrix4& worldTransform)
{
  (void)appendCommandsWithWorld(value, worldTransform);
}

void
MeshVisual::addLine(const glm::vec3& start,
                    const glm::vec3& end,
                    ColorRgba color)
{
  const unsigned int firstVertex =
    static_cast<unsigned int>(lineVertices.size());
  lineVertices.push_back(
    { start.x, start.y, start.z, color.r, color.g, color.b, color.a });
  lineVertices.push_back(
    { end.x, end.y, end.z, color.r, color.g, color.b, color.a });
  lineIndices.push_back(firstVertex);
  lineIndices.push_back(firstVertex + 1);
  geometryDirty = true;
}

void
MeshVisual::ensureStyles()
{
  if (renderer == nullptr) {
    return;
  }
  const RenderStyle* shapeStyle = renderer->getStyle(RenderStyleId::Shape);
  const RenderStyle* spriteStyle = renderer->getStyle(RenderStyleId::Sprite);
  if (shapeStyle != nullptr && !lineStyleHandle.isValid()) {
    RenderStyle lineStyle = *shapeStyle;
    lineStyle.pipeline.depthTestEnabled = true;
    lineStyle.pipeline.blendEnabled = false;
    lineStyle.pipeline.faceCullingEnabled = false;
    lineStyle.pipeline.primitives = Primitives::Lines;
    lineStyleHandle = renderer->createStyle(lineStyle);
  }
  if (shapeStyle != nullptr && !triangleStyleHandle.isValid()) {
    RenderStyle triangleStyle = *shapeStyle;
    triangleStyle.pipeline.depthTestEnabled = true;
    triangleStyle.pipeline.blendEnabled = false;
    triangleStyle.pipeline.faceCullingEnabled = false;
    triangleStyle.pipeline.primitives = Primitives::Triangles;
    triangleStyleHandle = renderer->createStyle(triangleStyle);
  }
  if (spriteStyle != nullptr && !spriteStyleHandle.isValid()) {
    RenderStyle worldSprite = *spriteStyle;
    worldSprite.pipeline.depthTestEnabled = true;
    worldSprite.pipeline.blendEnabled = true;
    worldSprite.pipeline.blendSrc = BlendFactor::SrcAlpha;
    worldSprite.pipeline.blendDst = BlendFactor::OneMinusSrcAlpha;
    worldSprite.pipeline.faceCullingEnabled = false;
    worldSprite.pipeline.primitives = Primitives::Triangles;
    spriteStyleHandle = renderer->createStyle(worldSprite);
  }
}

bool
MeshVisual::resolveViewProjection(Renderer* value,
                                  glm::mat4* viewProjection,
                                  glm::mat4* view) const
{
  if (viewProjection == nullptr || view == nullptr) {
    return false;
  }
  *viewProjection = glm::mat4(1.0f);
  *view = glm::mat4(1.0f);
  if (value == nullptr) {
    return false;
  }

  const Renderer::FrameContext& frameContext = value->getFrameContext();
  Camera* activeCamera = nullptr;
  if (frameContext.active) {
    activeCamera = frameContext.worldCamera;
  }
  if (activeCamera == nullptr) {
    activeCamera = value->getCamera();
  }
  if (frameContext.active && frameContext.hasWorldMvp) {
    std::memcpy(glm::value_ptr(*viewProjection),
                frameContext.worldMvp.data(),
                16 * sizeof(float));
  } else if (activeCamera != nullptr) {
    std::array<int, 2> dimensions{ 1280, 720 };
    if (value->getWindow() != nullptr) {
      dimensions = value->getWindow()->getWindowDimensions();
    }
    const float aspect =
      static_cast<float>(dimensions[0]) /
      static_cast<float>(dimensions[1] > 0 ? dimensions[1] : 1);
    *viewProjection = activeCamera->GetMVPMatrix(aspect);
  }
  if (activeCamera != nullptr) {
    *view = activeCamera->GetViewMatrix();
  }
  return true;
}

bool
MeshVisual::appendCommandsWithWorld(Renderer* value, const glm::mat4& nodeWorld)
{
  if (!isVisible()) {
    return true;
  }
  if (value == nullptr || (renderer != nullptr && renderer != value)) {
    return false;
  }
  if (renderer == nullptr) {
    renderer = value;
  }
  ensureStyles();
  if (geometryDirty) {
    rebuildMeshes();
  }

  const bool hasLines = !lineDrawVertices.empty();
  const bool hasTriangles = !triangleDrawVertices.empty();
  const bool hasSprites = !sprites.empty();
  if (hasLines && !lineStyleHandle.isValid()) {
    return false;
  }
  if (hasTriangles && !triangleStyleHandle.isValid()) {
    return false;
  }
  if (hasSprites && !spriteStyleHandle.isValid()) {
    return false;
  }

  if (hasLines && !ensureMeshCapacity(&lineMeshHandle,
                                      &lineMeshIndices,
                                      &lineMeshCapacity,
                                      lineDrawVertices.size(),
                                      MeshVertexLayout::Pos3Color4U8)) {
    return false;
  }
  if (hasTriangles && !ensureMeshCapacity(&triangleMeshHandle,
                                          &triangleMeshIndices,
                                          &triangleMeshCapacity,
                                          triangleDrawVertices.size(),
                                          MeshVertexLayout::Pos3Color4U8)) {
    return false;
  }
  if (hasSprites && !ensureMeshCapacity(&spriteMeshHandle,
                                        &spriteMeshIndices,
                                        &spriteMeshCapacity,
                                        spriteVertices.size(),
                                        MeshVertexLayout::Pos3Color4U8Uv2)) {
    return false;
  }

  if (lineUploadPending && lineMeshHandle.isValid()) {
    value->pushUpdateBuffer(
      lineMeshHandle,
      0,
      static_cast<unsigned int>(lineDrawVertices.size() * sizeof(ColorVertex)),
      lineDrawVertices.data());
    lineUploadPending = false;
  }
  if (triangleUploadPending && triangleMeshHandle.isValid()) {
    value->pushUpdateBuffer(
      triangleMeshHandle,
      0,
      static_cast<unsigned int>(triangleDrawVertices.size() *
                                sizeof(ColorVertex)),
      triangleDrawVertices.data());
    triangleUploadPending = false;
  }
  if (spriteUploadPending && spriteMeshHandle.isValid()) {
    value->pushUpdateBuffer(
      spriteMeshHandle,
      0,
      static_cast<unsigned int>(spriteVertices.size() * sizeof(SpriteVertex)),
      spriteVertices.data());
    spriteUploadPending = false;
  }

  glm::mat4 viewProjection(1.0f);
  glm::mat4 view(1.0f);
  if (!resolveViewProjection(value, &viewProjection, &view)) {
    return false;
  }
  const glm::mat4 coloredWorld = nodeWorld * modelMatrix;
  const glm::mat4 coloredMvp = viewProjection * coloredWorld;
  const float* coloredMvpPtr = glm::value_ptr(coloredMvp);

  if (hasLines && lineMeshHandle.isValid()) {
    if (!value->bindStyle(lineStyleHandle)) {
      return false;
    }
    value->pushSetMesh(lineMeshHandle);
    value->pushUniformMat4(WorldLook::kMvpUniform, coloredMvpPtr);
    value->pushDrawIndexed(static_cast<unsigned int>(lineDrawVertices.size()));
  }
  if (hasTriangles && triangleMeshHandle.isValid()) {
    if (!value->bindStyle(triangleStyleHandle)) {
      return false;
    }
    value->pushSetMesh(triangleMeshHandle);
    value->pushUniformMat4(WorldLook::kMvpUniform, coloredMvpPtr);
    value->pushDrawIndexed(
      static_cast<unsigned int>(triangleDrawVertices.size()));
  }
  if (hasSprites && spriteMeshHandle.isValid()) {
    if (!value->bindStyle(spriteStyleHandle)) {
      return false;
    }
    value->pushSetMesh(spriteMeshHandle);
    value->pushUniformInt(WorldLook::kTextureUniform, WorldLook::kTextureUnit);
    for (size_t i = 0; i < sprites.size(); ++i) {
      const SpriteItem& sprite = sprites[i];
      if (!sprite.textureHandle.isValid()) {
        continue;
      }
      glm::mat4 spriteWorld = coloredWorld * sprite.local.toMatrix();
      if (sprite.facing == MeshFacing::Billboard) {
        spriteWorld = WorldLook::billboardWorld(spriteWorld, view);
      }
      const glm::mat4 spriteMvp = viewProjection * spriteWorld;
      value->pushSetTexture(sprite.textureHandle, WorldLook::kTextureUnit);
      value->pushUniformMat4(WorldLook::kMvpUniform, glm::value_ptr(spriteMvp));
      value->pushDrawIndexed(6, static_cast<unsigned int>(i * 6));
    }
  }
  return true;
}

void
MeshVisual::rebuildMeshes()
{
  expandIndexedVertices(lineVertices, lineIndices, &lineDrawVertices);
  expandIndexedVertices(
    triangleVertices, triangleIndices, &triangleDrawVertices);
  spriteVertices.clear();
  spriteVertices.reserve(sprites.size() * 6);
  for (size_t i = 0; i < sprites.size(); ++i) {
    const SpriteItem& sprite = sprites[i];
    const float u0 = sprite.region.u0;
    const float v0 = sprite.region.v0;
    const float u1 = sprite.region.u1;
    const float v1 = sprite.region.v1;
    const SpriteVertex corners[4] = {
      { -0.5f,
        -0.5f,
        0.0f,
        sprite.tint.r,
        sprite.tint.g,
        sprite.tint.b,
        sprite.tint.a,
        u0,
        v0 },
      { 0.5f,
        -0.5f,
        0.0f,
        sprite.tint.r,
        sprite.tint.g,
        sprite.tint.b,
        sprite.tint.a,
        u1,
        v0 },
      { 0.5f,
        0.5f,
        0.0f,
        sprite.tint.r,
        sprite.tint.g,
        sprite.tint.b,
        sprite.tint.a,
        u1,
        v1 },
      { -0.5f,
        0.5f,
        0.0f,
        sprite.tint.r,
        sprite.tint.g,
        sprite.tint.b,
        sprite.tint.a,
        u0,
        v1 },
    };
    static const int kTriangle[6] = { 0, 1, 2, 0, 2, 3 };
    for (int vertex = 0; vertex < 6; ++vertex) {
      spriteVertices.push_back(corners[kTriangle[vertex]]);
    }
  }
  lineUploadPending = !lineDrawVertices.empty();
  triangleUploadPending = !triangleDrawVertices.empty();
  spriteUploadPending = !spriteVertices.empty();
  geometryDirty = false;
}

bool
MeshVisual::ensureMeshCapacity(MeshHandle* meshHandle,
                               std::vector<unsigned int>* meshIndices,
                               size_t* capacity,
                               size_t required,
                               MeshVertexLayout layout)
{
  if (required == 0) {
    return true;
  }
  if (renderer == nullptr || meshHandle == nullptr || meshIndices == nullptr ||
      capacity == nullptr) {
    return false;
  }
  if (meshHandle->isValid() && *capacity >= required) {
    return true;
  }

  size_t nextCapacity = *capacity == 0 ? 64u : *capacity;
  while (nextCapacity < required) {
    nextCapacity *= 2u;
  }
  meshIndices->resize(nextCapacity);
  for (size_t index = 0; index < nextCapacity; ++index) {
    (*meshIndices)[index] = static_cast<unsigned int>(index);
  }

  const size_t vertexStride = layout == MeshVertexLayout::Pos3Color4U8Uv2
                                ? sizeof(SpriteVertex)
                                : sizeof(ColorVertex);
  const size_t vertexCapacityBytes = nextCapacity * vertexStride;
  const size_t indexBytes = meshIndices->size() * sizeof(unsigned int);
  if (meshHandle->isValid()) {
    if (!renderer->replaceDynamicMesh(*meshHandle,
                                      vertexCapacityBytes,
                                      meshIndices->data(),
                                      indexBytes,
                                      layout)) {
      return false;
    }
  } else {
    *meshHandle = renderer->enrollDynamicMesh(
      vertexCapacityBytes, meshIndices->data(), indexBytes, layout);
    if (!meshHandle->isValid()) {
      return false;
    }
  }
  *capacity = nextCapacity;
  return true;
}

void
MeshVisual::expandIndexedVertices(const std::vector<ColorVertex>& vertices,
                                  const std::vector<unsigned int>& indices,
                                  std::vector<ColorVertex>* drawVertices)
{
  if (drawVertices == nullptr) {
    return;
  }
  drawVertices->clear();
  drawVertices->reserve(indices.size());
  for (unsigned int index : indices) {
    if (index < vertices.size()) {
      drawVertices->push_back(vertices[index]);
    }
  }
}

void
MeshVisual::releaseMeshes()
{
  if (renderer == nullptr) {
    return;
  }
  if (lineMeshHandle.isValid()) {
    renderer->destroyMesh(lineMeshHandle);
    lineMeshHandle = MeshHandle{};
  }
  if (triangleMeshHandle.isValid()) {
    renderer->destroyMesh(triangleMeshHandle);
    triangleMeshHandle = MeshHandle{};
  }
  if (spriteMeshHandle.isValid()) {
    renderer->destroyMesh(spriteMeshHandle);
    spriteMeshHandle = MeshHandle{};
  }
  lineMeshCapacity = 0;
  triangleMeshCapacity = 0;
  spriteMeshCapacity = 0;
  lineMeshIndices.clear();
  triangleMeshIndices.clear();
  spriteMeshIndices.clear();
  lineUploadPending = false;
  triangleUploadPending = false;
  spriteUploadPending = false;
}

void
MeshVisual::releaseStyles()
{
  if (renderer == nullptr) {
    return;
  }
  if (lineStyleHandle.isValid()) {
    renderer->destroyStyle(lineStyleHandle);
    lineStyleHandle = RenderStyleHandle{};
  }
  if (triangleStyleHandle.isValid()) {
    renderer->destroyStyle(triangleStyleHandle);
    triangleStyleHandle = RenderStyleHandle{};
  }
  if (spriteStyleHandle.isValid()) {
    renderer->destroyStyle(spriteStyleHandle);
    spriteStyleHandle = RenderStyleHandle{};
  }
}
