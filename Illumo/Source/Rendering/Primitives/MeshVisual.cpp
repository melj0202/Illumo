#include <Illumo/Rendering/AssetManager.h>
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
#include <limits>

static bool
includeBoundsPoint(const glm::vec3& point,
                   glm::vec3* minimum,
                   glm::vec3* maximum,
                   bool* valid)
{
  if (minimum == nullptr || maximum == nullptr || valid == nullptr ||
      !std::isfinite(point.x) || !std::isfinite(point.y) ||
      !std::isfinite(point.z)) {
    return false;
  }
  if (!*valid) {
    *minimum = point;
    *maximum = point;
    *valid = true;
  } else {
    *minimum = glm::min(*minimum, point);
    *maximum = glm::max(*maximum, point);
  }
  return true;
}

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
MeshVisual::setLightDirection(const glm::vec3& direction)
{
  const float length = glm::length(direction);
  if (length <= 0.0001f) {
    return;
  }
  lightDirection = direction / length;
}

void
MeshVisual::setLightColor(const glm::vec3& color)
{
  lightColor = glm::max(color, glm::vec3(0.0f));
}

void
MeshVisual::setAmbientColor(const glm::vec3& color)
{
  ambientColor = glm::max(color, glm::vec3(0.0f));
}

static int
snapShadowMapSize(int size)
{
  const int options[4] = { 256, 512, 1024, 2048 };
  int best = 1024;
  int bestDelta = 100000;
  for (int i = 0; i < 4; ++i) {
    int delta = size - options[i];
    if (delta < 0) {
      delta = -delta;
    }
    if (delta < bestDelta) {
      best = options[i];
      bestDelta = delta;
    }
  }
  return best;
}

void
MeshVisual::setShadowMapSize(int size)
{
  shadowMapSize = snapShadowMapSize(size);
}

void
MeshVisual::setShadowRadius(float radius)
{
  shadowRadius = std::max(radius, 0.1f);
}

void
MeshVisual::setLightDistance(float distance)
{
  lightDistance = std::max(distance, 0.5f);
}

void
MeshVisual::setShadowCasterDistance(float distance)
{
  shadowCasterDistance =
    std::isfinite(distance) ? std::max(distance, 0.0f) : 0.0f;
}

void
MeshVisual::setShadowBias(float bias)
{
  shadowBias = std::max(bias, 0.0f);
}

void
MeshVisual::setShadowSlopeScale(float scale)
{
  shadowSlopeScale = std::max(scale, 0.0f);
}

void
MeshVisual::setShadowNormalOffset(float offset)
{
  shadowNormalOffset = std::max(offset, 0.0f);
}

void
MeshVisual::setMotionBlurAmount(float amount)
{
  motionBlurAmount = std::clamp(amount, 0.0f, 1.0f);
}

void
MeshVisual::setMotionBlurMax(float ndcUnits)
{
  motionBlurMax = std::clamp(ndcUnits, 0.0f, 2.0f);
}

void
MeshVisual::clearPrimitives()
{
  lineVertices.clear();
  lineIndices.clear();
  triangleVertices.clear();
  triangleIndices.clear();
  sprites.clear();
  lineBoundsMin = glm::vec3(0.0f);
  lineBoundsMax = glm::vec3(0.0f);
  lineBoundsValid = false;
  triangleBoundsMin = glm::vec3(0.0f);
  triangleBoundsMax = glm::vec3(0.0f);
  triangleBoundsValid = false;
  spriteBoundsMin = glm::vec3(0.0f);
  spriteBoundsMax = glm::vec3(0.0f);
  spriteBoundsValid = false;
  geometryBoundsReliable = true;
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
                                 0.0f,
                                 0.0f,
                                 1.0f,
                                 color.r,
                                 color.g,
                                 color.b,
                                 color.a,
                                 0.0f,
                                 0.0f });
  }
  triangleIndices.push_back(vertexOffset + 0);
  triangleIndices.push_back(vertexOffset + 1);
  triangleIndices.push_back(vertexOffset + 2);
  triangleIndices.push_back(vertexOffset + 0);
  triangleIndices.push_back(vertexOffset + 2);
  triangleIndices.push_back(vertexOffset + 3);
  for (const glm::vec3& corner : corners) {
    if (!includeBoundsPoint(corner,
                            &triangleBoundsMin,
                            &triangleBoundsMax,
                            &triangleBoundsValid)) {
      geometryBoundsReliable = false;
    }
  }
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
  const float radius =
    0.5f * std::sqrt(item.local.scale.x * item.local.scale.x +
                     item.local.scale.y * item.local.scale.y);
  const glm::vec3 extent(radius);
  if (!includeBoundsPoint(center - extent,
                          &spriteBoundsMin,
                          &spriteBoundsMax,
                          &spriteBoundsValid) ||
      !includeBoundsPoint(center + extent,
                          &spriteBoundsMin,
                          &spriteBoundsMax,
                          &spriteBoundsValid)) {
    geometryBoundsReliable = false;
  }
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
  struct FaceDef
  {
    glm::vec3 normal;
    glm::vec3 corners[4];
  };

  const FaceDef faces[6] = {
    // Front (+Z)
    { glm::vec3(0, 0, 1),
      { center + glm::vec3(-extent.x, -extent.y, extent.z),
        center + glm::vec3(extent.x, -extent.y, extent.z),
        center + glm::vec3(extent.x, extent.y, extent.z),
        center + glm::vec3(-extent.x, extent.y, extent.z) } },
    // Back (-Z)
    { glm::vec3(0, 0, -1),
      { center + glm::vec3(extent.x, -extent.y, -extent.z),
        center + glm::vec3(-extent.x, -extent.y, -extent.z),
        center + glm::vec3(-extent.x, extent.y, -extent.z),
        center + glm::vec3(extent.x, extent.y, -extent.z) } },
    // Top (+Y)
    { glm::vec3(0, 1, 0),
      { center + glm::vec3(-extent.x, extent.y, extent.z),
        center + glm::vec3(extent.x, extent.y, extent.z),
        center + glm::vec3(extent.x, extent.y, -extent.z),
        center + glm::vec3(-extent.x, extent.y, -extent.z) } },
    // Bottom (-Y)
    { glm::vec3(0, -1, 0),
      { center + glm::vec3(-extent.x, -extent.y, -extent.z),
        center + glm::vec3(extent.x, -extent.y, -extent.z),
        center + glm::vec3(extent.x, -extent.y, extent.z),
        center + glm::vec3(-extent.x, -extent.y, extent.z) } },
    // Right (+X)
    { glm::vec3(1, 0, 0),
      { center + glm::vec3(extent.x, -extent.y, extent.z),
        center + glm::vec3(extent.x, -extent.y, -extent.z),
        center + glm::vec3(extent.x, extent.y, -extent.z),
        center + glm::vec3(extent.x, extent.y, extent.z) } },
    // Left (-X)
    { glm::vec3(-1, 0, 0),
      { center + glm::vec3(-extent.x, -extent.y, -extent.z),
        center + glm::vec3(-extent.x, -extent.y, extent.z),
        center + glm::vec3(-extent.x, extent.y, extent.z),
        center + glm::vec3(-extent.x, extent.y, -extent.z) } },
  };

  for (const FaceDef& f : faces) {
    const unsigned int offset =
      static_cast<unsigned int>(triangleVertices.size());
    for (int i = 0; i < 4; ++i) {
      triangleVertices.push_back({ f.corners[i].x,
                                   f.corners[i].y,
                                   f.corners[i].z,
                                   f.normal.x,
                                   f.normal.y,
                                   f.normal.z,
                                   color.r,
                                   color.g,
                                   color.b,
                                   color.a,
                                   0.0f,
                                   0.0f });
    }
    triangleIndices.push_back(offset + 0);
    triangleIndices.push_back(offset + 1);
    triangleIndices.push_back(offset + 2);
    triangleIndices.push_back(offset + 0);
    triangleIndices.push_back(offset + 2);
    triangleIndices.push_back(offset + 3);
  }
  if (!includeBoundsPoint(center - extent,
                          &triangleBoundsMin,
                          &triangleBoundsMax,
                          &triangleBoundsValid) ||
      !includeBoundsPoint(center + extent,
                          &triangleBoundsMin,
                          &triangleBoundsMax,
                          &triangleBoundsValid)) {
    geometryBoundsReliable = false;
  }
  geometryDirty = true;
}

void
MeshVisual::addSolidTriangle(const glm::vec3& a,
                             const glm::vec3& b,
                             const glm::vec3& c,
                             ColorRgba color)
{
  glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
  if (std::isnan(normal.x) || std::isnan(normal.y) || std::isnan(normal.z)) {
    normal = glm::vec3(0.0f, 1.0f, 0.0f);
  }
  const unsigned int vertexOffset =
    static_cast<unsigned int>(triangleVertices.size());
  triangleVertices.push_back({ a.x,
                               a.y,
                               a.z,
                               normal.x,
                               normal.y,
                               normal.z,
                               color.r,
                               color.g,
                               color.b,
                               color.a,
                               0.0f,
                               0.0f });
  triangleVertices.push_back({ b.x,
                               b.y,
                               b.z,
                               normal.x,
                               normal.y,
                               normal.z,
                               color.r,
                               color.g,
                               color.b,
                               color.a,
                               0.0f,
                               0.0f });
  triangleVertices.push_back({ c.x,
                               c.y,
                               c.z,
                               normal.x,
                               normal.y,
                               normal.z,
                               color.r,
                               color.g,
                               color.b,
                               color.a,
                               0.0f,
                               0.0f });
  triangleIndices.push_back(vertexOffset + 0);
  triangleIndices.push_back(vertexOffset + 1);
  triangleIndices.push_back(vertexOffset + 2);
  for (const glm::vec3& point : { a, b, c }) {
    if (!includeBoundsPoint(point,
                            &triangleBoundsMin,
                            &triangleBoundsMax,
                            &triangleBoundsValid)) {
      geometryBoundsReliable = false;
    }
  }
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
  triangleVertices.push_back({ center.x,
                               center.y,
                               center.z,
                               0.0f,
                               0.0f,
                               1.0f,
                               color.r,
                               color.g,
                               color.b,
                               color.a,
                               0.0f,
                               0.0f });

  for (int i = 0; i < count; ++i) {
    const float angle = step * static_cast<float>(i);
    const float x = center.x + std::cos(angle) * std::max(radius.x, 0.0f);
    const float y = center.y + std::sin(angle) * std::max(radius.y, 0.0f);
    triangleVertices.push_back({ x,
                                 y,
                                 center.z,
                                 0.0f,
                                 0.0f,
                                 1.0f,
                                 color.r,
                                 color.g,
                                 color.b,
                                 color.a,
                                 0.0f,
                                 0.0f });
  }

  for (int i = 0; i < count; ++i) {
    const unsigned int current = centerIndex + 1 + static_cast<unsigned int>(i);
    const unsigned int next =
      centerIndex + 1 + static_cast<unsigned int>((i + 1) % count);
    triangleIndices.push_back(centerIndex);
    triangleIndices.push_back(current);
    triangleIndices.push_back(next);
  }
  const glm::vec3 extent(
    std::max(radius.x, 0.0f), std::max(radius.y, 0.0f), 0.0f);
  if (!includeBoundsPoint(center - extent,
                          &triangleBoundsMin,
                          &triangleBoundsMax,
                          &triangleBoundsValid) ||
      !includeBoundsPoint(center + extent,
                          &triangleBoundsMin,
                          &triangleBoundsMax,
                          &triangleBoundsValid)) {
    geometryBoundsReliable = false;
  }
  geometryDirty = true;
}

void
MeshVisual::addMesh(const MeshData& mesh, ColorRgba tint)
{
  if (mesh.vertices.empty() || mesh.indices.empty()) {
    return;
  }
  for (uint32_t index : mesh.indices) {
    if (index >= mesh.vertices.size()) {
      return;
    }
  }

  const unsigned int vertexOffset =
    static_cast<unsigned int>(triangleVertices.size());
  for (size_t i = 0; i < mesh.vertices.size(); ++i) {
    const MeshVertex& vertex = mesh.vertices[i];
    unsigned char r = static_cast<unsigned char>(
      std::clamp(vertex.color.r * static_cast<float>(tint.r), 0.0f, 255.0f));
    unsigned char g = static_cast<unsigned char>(
      std::clamp(vertex.color.g * static_cast<float>(tint.g), 0.0f, 255.0f));
    unsigned char b = static_cast<unsigned char>(
      std::clamp(vertex.color.b * static_cast<float>(tint.b), 0.0f, 255.0f));
    unsigned char a = static_cast<unsigned char>(std::clamp(
      vertex.color.a * (static_cast<float>(tint.a) / 255.0f) * 255.0f,
      0.0f,
      255.0f));

    triangleVertices.push_back({ vertex.position.x,
                                 vertex.position.y,
                                 vertex.position.z,
                                 vertex.normal.x,
                                 vertex.normal.y,
                                 vertex.normal.z,
                                 r,
                                 g,
                                 b,
                                 a,
                                 vertex.texCoords.x,
                                 vertex.texCoords.y });
    if (!includeBoundsPoint(vertex.position,
                            &triangleBoundsMin,
                            &triangleBoundsMax,
                            &triangleBoundsValid)) {
      geometryBoundsReliable = false;
    }
  }

  for (size_t i = 0; i < mesh.indices.size(); ++i) {
    triangleIndices.push_back(vertexOffset + mesh.indices[i]);
  }
  geometryDirty = true;
}

void
MeshVisual::setMeshAsset(const MeshAssetInfo& asset, ColorRgba tint)
{
  if (!asset.isValid()) {
    clearMeshAsset();
    return;
  }
  meshAssetHandle = asset.handle;
  meshAssetIndexCount = asset.indexCount;
  meshAssetTint = tint;
  meshAssetBoundsMin = asset.minBounds;
  meshAssetBoundsMax = asset.maxBounds;
  meshAssetBoundsValid = true;
  for (int axis = 0; axis < 3; ++axis) {
    if (!std::isfinite(meshAssetBoundsMin[axis]) ||
        !std::isfinite(meshAssetBoundsMax[axis]) ||
        meshAssetBoundsMin[axis] > meshAssetBoundsMax[axis]) {
      meshAssetBoundsValid = false;
      break;
    }
  }
}

void
MeshVisual::clearMeshAsset()
{
  meshAssetHandle = MeshHandle{};
  meshAssetIndexCount = 0;
  meshAssetTint = ColorRgba{ 255, 255, 255, 255 };
  meshAssetBoundsMin = glm::vec3(0.0f);
  meshAssetBoundsMax = glm::vec3(0.0f);
  meshAssetBoundsValid = false;
}

bool
MeshVisual::AppendCommands(Renderer* value)
{
  return appendCommandsWithWorld(value, glm::mat4(1.0f));
}

void
MeshVisual::CollectShadowCasters(Renderer* value)
{
  collectShadowCasterWithWorld(value, glm::mat4(1.0f));
}

void
MeshVisual::AppendShadowCommands(Renderer* value)
{
  appendShadowCommandsWithWorld(value, glm::mat4(1.0f));
}

void
MeshVisual::appendSceneCommands(Renderer* value, const Matrix4& worldTransform)
{
  if (!appendCommandsWithWorld(value, worldTransform) && value != nullptr) {
    value->reportFrameError(
      "MeshVisual scene attachment could not emit its resources");
  }
}

bool
MeshVisual::getSceneLocalBounds(AxisAlignedBounds3* bounds) const
{
  if (bounds == nullptr || !geometryBoundsReliable) {
    return false;
  }

  bool boundsValid = false;
  AxisAlignedBounds3 combined;
  const bool hasLines = !lineVertices.empty();
  const bool hasTriangles =
    !triangleVertices.empty() && !triangleIndices.empty();
  const bool hasSprites = !sprites.empty();
  const bool hasMeshAsset =
    meshAssetHandle.isValid() && meshAssetIndexCount > 0;
  if ((hasLines && !lineBoundsValid) ||
      (hasTriangles && !triangleBoundsValid) ||
      (hasSprites && !spriteBoundsValid) ||
      (hasMeshAsset && !meshAssetBoundsValid)) {
    return false;
  }

  const AxisAlignedBounds3 candidates[] = {
    AxisAlignedBounds3{ lineBoundsMin, lineBoundsMax },
    AxisAlignedBounds3{ triangleBoundsMin, triangleBoundsMax },
    AxisAlignedBounds3{ spriteBoundsMin, spriteBoundsMax },
    AxisAlignedBounds3{ meshAssetBoundsMin, meshAssetBoundsMax },
  };
  const bool present[] = { hasLines, hasTriangles, hasSprites, hasMeshAsset };
  for (size_t index = 0; index < 4; ++index) {
    if (!present[index]) {
      continue;
    }
    if (!boundsValid) {
      combined = candidates[index];
      boundsValid = true;
    } else {
      combined.include(candidates[index]);
    }
  }
  if (!boundsValid) {
    return false;
  }
  return combined.transformed(modelMatrix, bounds);
}

void
MeshVisual::collectSceneShadowCasters(Renderer* value,
                                      const Matrix4& worldTransform)
{
  collectShadowCasterWithWorld(value, worldTransform);
}

void
MeshVisual::appendSceneShadowCommands(Renderer* value,
                                      const Matrix4& worldTransform)
{
  appendShadowCommandsWithWorld(value, worldTransform);
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
  if (!includeBoundsPoint(
        start, &lineBoundsMin, &lineBoundsMax, &lineBoundsValid) ||
      !includeBoundsPoint(
        end, &lineBoundsMin, &lineBoundsMax, &lineBoundsValid)) {
    geometryBoundsReliable = false;
  }
  geometryDirty = true;
}

bool
MeshVisual::getLocalShadowBounds(AxisAlignedBounds3* bounds) const
{
  if (bounds == nullptr || !geometryBoundsReliable) {
    return false;
  }
  const bool hasTriangles =
    !triangleVertices.empty() && !triangleIndices.empty();
  const bool hasMeshAsset =
    meshAssetHandle.isValid() && meshAssetIndexCount > 0;
  if ((hasTriangles && !triangleBoundsValid) ||
      (hasMeshAsset && !meshAssetBoundsValid) ||
      (!hasTriangles && !hasMeshAsset)) {
    return false;
  }
  if (hasTriangles) {
    *bounds = AxisAlignedBounds3{ triangleBoundsMin, triangleBoundsMax };
  } else {
    *bounds = AxisAlignedBounds3{ meshAssetBoundsMin, meshAssetBoundsMax };
  }
  if (hasTriangles && hasMeshAsset) {
    bounds->include(
      AxisAlignedBounds3{ meshAssetBoundsMin, meshAssetBoundsMax });
  }
  return bounds->isValid();
}

bool
MeshVisual::getWorldShadowBounds(const glm::mat4& nodeWorld,
                                 AxisAlignedBounds3* bounds) const
{
  AxisAlignedBounds3 localBounds;
  return getLocalShadowBounds(&localBounds) &&
         localBounds.transformed(nodeWorld * modelMatrix, bounds);
}

void
MeshVisual::ensureStyles()
{
  if (renderer == nullptr) {
    return;
  }
  const RenderStyle* shapeStyle = renderer->getStyle(RenderStyleId::Shape);
  const RenderStyle* spriteStyle = renderer->getStyle(RenderStyleId::Sprite);
  const RenderStyle* litStyle = renderer->getStyle(RenderStyleId::LitMesh);

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
  if (litStyle != nullptr && !litMeshStyleHandle.isValid()) {
    litMeshStyleHandle =
      renderer->getBuiltinStyleHandle(RenderStyleId::LitMesh);
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
MeshVisual::prepareForCommands(Renderer* value)
{
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
  const bool hasTriangles =
    !triangleVertices.empty() && !triangleIndices.empty();
  const bool hasSprites = !sprites.empty();
  const bool hasMeshAsset =
    meshAssetHandle.isValid() && meshAssetIndexCount > 0;
  if (hasLines && !lineStyleHandle.isValid()) {
    return false;
  }
  if (hasTriangles && !triangleStyleHandle.isValid() &&
      !litMeshStyleHandle.isValid()) {
    return false;
  }
  if (hasMeshAsset && !litMeshStyleHandle.isValid()) {
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
  if (hasTriangles && !ensureTriangleMeshCapacity(triangleVertices.size(),
                                                  triangleIndices.size())) {
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
      static_cast<unsigned int>(triangleVertices.size() * sizeof(LitVertex)),
      triangleVertices.data());
    value->pushUpdateIndexBuffer(
      triangleMeshHandle,
      0,
      static_cast<unsigned int>(triangleIndices.size() * sizeof(unsigned int)),
      triangleIndices.data());
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

  return true;
}

void
MeshVisual::collectShadowCasterWithWorld(Renderer* value,
                                         const glm::mat4& nodeWorld)
{
  if (!isVisible() || !lightingEnabled || !shadowsEnabled ||
      !prepareForCommands(value)) {
    return;
  }

  AxisAlignedBounds3 worldBounds;
  if (!getWorldShadowBounds(nodeWorld, &worldBounds)) {
    return;
  }

  Renderer::ShadowCasterDesc caster;
  caster.boundsMin = { worldBounds.minimum.x,
                       worldBounds.minimum.y,
                       worldBounds.minimum.z };
  caster.boundsMax = { worldBounds.maximum.x,
                       worldBounds.maximum.y,
                       worldBounds.maximum.z };
  caster.lightDirection = { lightDirection.x,
                            lightDirection.y,
                            lightDirection.z };
  caster.mapSize = shadowMapSize;
  caster.minimumRadius = shadowRadius;
  caster.lightDistance = lightDistance;
  caster.casterDistance = shadowCasterDistance;
  value->registerShadowCaster(caster);
}

void
MeshVisual::appendShadowCommandsWithWorld(Renderer* value,
                                          const glm::mat4& nodeWorld)
{
  if (!isVisible() || !lightingEnabled || !shadowsEnabled || value == nullptr) {
    return;
  }

  AxisAlignedBounds3 worldBounds;
  if (getWorldShadowBounds(nodeWorld, &worldBounds) &&
      !value->isShadowCasterRelevant(worldBounds)) {
    return;
  }
  if (!prepareForCommands(value)) {
    return;
  }

  const bool hasTriangles =
    !triangleIndices.empty() && triangleMeshHandle.isValid();
  const bool hasMeshAsset =
    meshAssetHandle.isValid() && meshAssetIndexCount > 0;
  if (!hasTriangles && !hasMeshAsset) {
    return;
  }

  const Renderer::ShadowFrameContext& shadow = value->getShadowFrameContext();
  if (!shadow.active || !shadow.depthTexture.isValid()) {
    return;
  }

  glm::mat4 lightSpaceMatrix(1.0f);
  std::memcpy(glm::value_ptr(lightSpaceMatrix),
              shadow.lightSpaceMatrix.data(),
              shadow.lightSpaceMatrix.size() * sizeof(float));
  const glm::mat4 lightMvp = lightSpaceMatrix * nodeWorld * modelMatrix;
  value->pushUniformMat4(WorldLook::kMvpUniform, glm::value_ptr(lightMvp));
  if (hasTriangles) {
    value->pushSetMesh(triangleMeshHandle);
    value->pushDrawIndexed(static_cast<unsigned int>(triangleIndices.size()));
  }
  if (hasMeshAsset) {
    value->pushSetMesh(meshAssetHandle);
    value->pushDrawIndexed(meshAssetIndexCount);
  }
}

bool
MeshVisual::appendCommandsWithWorld(Renderer* value, const glm::mat4& nodeWorld)
{
  if (!isVisible()) {
    return true;
  }
  AxisAlignedBounds3 localBounds;
  AxisAlignedBounds3 worldBounds;
  if (value != nullptr && getSceneLocalBounds(&localBounds) &&
      localBounds.transformed(nodeWorld, &worldBounds) &&
      !value->isWorldBoundsVisible(worldBounds)) {
    return true;
  }
  if (!prepareForCommands(value)) {
    return false;
  }

  const bool hasLines = !lineDrawVertices.empty();
  const bool hasTriangles =
    !triangleVertices.empty() && !triangleIndices.empty();
  const bool hasSprites = !sprites.empty();
  const bool hasMeshAsset =
    meshAssetHandle.isValid() && meshAssetIndexCount > 0;

  glm::mat4 viewProjection(1.0f);
  glm::mat4 view(1.0f);
  if (!resolveViewProjection(value, &viewProjection, &view)) {
    return false;
  }
  const glm::mat4 coloredWorld = nodeWorld * modelMatrix;
  const glm::mat4 coloredMvp = viewProjection * coloredWorld;
  const float* coloredMvpPtr = glm::value_ptr(coloredMvp);
  const Renderer::FrameContext& frame = value->getFrameContext();
  bool previousMvpUsable = hasPreviousMvp;
  if (frame.active && frame.frameSerial != 0) {
    const bool sameFrame = previousFrameSerial == frame.frameSerial;
    const bool previousFrame = previousFrameSerial + 1 == frame.frameSerial;
    previousMvpUsable = previousMvpUsable && (sameFrame || previousFrame);
  }
  const glm::mat4 previousMvp =
    previousMvpUsable ? previousColoredMvp : coloredMvp;

  const Renderer::ShadowFrameContext& shadow = value->getShadowFrameContext();
  const bool shadowMapBound = lightingEnabled && shadowsEnabled &&
                              shadow.active && shadow.depthTexture.isValid();
  glm::mat4 lightSpaceMatrix(1.0f);
  glm::vec3 activeLightDirection = lightDirection;
  if (shadowMapBound) {
    std::memcpy(glm::value_ptr(lightSpaceMatrix),
                shadow.lightSpaceMatrix.data(),
                shadow.lightSpaceMatrix.size() * sizeof(float));
    activeLightDirection = glm::vec3(shadow.lightDirection[0],
                                     shadow.lightDirection[1],
                                     shadow.lightDirection[2]);
  }

  // The renderer has already emitted the one shared scene shadow pass.
  if (hasLines && lineMeshHandle.isValid()) {
    if (!value->bindStyle(lineStyleHandle)) {
      return false;
    }
    value->pushSetMesh(lineMeshHandle);
    value->pushUniformMat4(WorldLook::kMvpUniform, coloredMvpPtr);
    value->pushUniformMat4(WorldLook::kPrevMvpUniform,
                           glm::value_ptr(previousMvp));
    value->pushUniformInt(WorldLook::kMotionBlurEnabledUniform,
                          motionBlurEnabled ? 1 : 0);
    value->pushDrawIndexed(static_cast<unsigned int>(lineDrawVertices.size()));
  }
  if (hasTriangles && triangleMeshHandle.isValid()) {
    if (lightingEnabled && litMeshStyleHandle.isValid()) {
      if (!value->bindStyle(litMeshStyleHandle)) {
        return false;
      }
      value->pushSetMesh(triangleMeshHandle);
      value->pushUniformMat4(WorldLook::kMvpUniform, coloredMvpPtr);
      value->pushUniformMat4(WorldLook::kModelUniform,
                             glm::value_ptr(coloredWorld));
      value->pushUniformMat4(WorldLook::kLightSpaceMatrixUniform,
                             glm::value_ptr(lightSpaceMatrix));
      value->pushUniformVec3(WorldLook::kLightDirUniform,
                             activeLightDirection.x,
                             activeLightDirection.y,
                             activeLightDirection.z);
      value->pushUniformVec3(WorldLook::kLightColorUniform,
                             lightColor.x,
                             lightColor.y,
                             lightColor.z);
      value->pushUniformVec3(WorldLook::kAmbientColorUniform,
                             ambientColor.x,
                             ambientColor.y,
                             ambientColor.z);
      value->pushUniformInt(WorldLook::kShadowsEnabledUniform,
                            shadowMapBound ? 1 : 0);
      value->pushUniformFloat(WorldLook::kShadowBiasUniform, shadowBias);
      value->pushUniformFloat(WorldLook::kShadowSlopeScaleUniform,
                              shadowSlopeScale);
      value->pushUniformFloat(WorldLook::kShadowNormalOffsetUniform,
                              shadowNormalOffset);
      value->pushUniformInt(WorldLook::kShadowPcfUniform,
                            shadowPcfEnabled ? 1 : 0);
      value->pushUniformMat4(WorldLook::kPrevMvpUniform,
                             glm::value_ptr(previousMvp));
      value->pushUniformInt(WorldLook::kMotionBlurEnabledUniform,
                            motionBlurEnabled ? 1 : 0);
      value->pushUniformFloat(WorldLook::kMotionBlurAmountUniform,
                              motionBlurAmount);
      value->pushUniformFloat(WorldLook::kMotionBlurMaxUniform, motionBlurMax);
      value->pushUniformVec4(WorldLook::kTintUniform, 1.0f, 1.0f, 1.0f, 1.0f);

      if (shadowMapBound) {
        value->pushSetTexture(shadow.depthTexture,
                              WorldLook::kShadowTextureUnit);
        value->pushUniformInt(WorldLook::kShadowMapUniform,
                              WorldLook::kShadowTextureUnit);
      }
      value->pushDrawIndexed(static_cast<unsigned int>(triangleIndices.size()));
    } else {
      if (!value->bindStyle(triangleStyleHandle)) {
        return false;
      }
      value->pushSetMesh(triangleMeshHandle);
      value->pushUniformMat4(WorldLook::kMvpUniform, coloredMvpPtr);
      value->pushUniformMat4(WorldLook::kPrevMvpUniform,
                             glm::value_ptr(previousMvp));
      value->pushUniformInt(WorldLook::kMotionBlurEnabledUniform,
                            motionBlurEnabled ? 1 : 0);
      value->pushDrawIndexed(static_cast<unsigned int>(triangleIndices.size()));
    }
  }
  if (hasMeshAsset) {
    if (!value->bindStyle(litMeshStyleHandle)) {
      return false;
    }
    value->pushSetMesh(meshAssetHandle);
    value->pushUniformMat4(WorldLook::kMvpUniform, coloredMvpPtr);
    value->pushUniformMat4(WorldLook::kModelUniform,
                           glm::value_ptr(coloredWorld));
    value->pushUniformMat4(WorldLook::kLightSpaceMatrixUniform,
                           glm::value_ptr(lightSpaceMatrix));
    value->pushUniformVec3(WorldLook::kLightDirUniform,
                           activeLightDirection.x,
                           activeLightDirection.y,
                           activeLightDirection.z);
    value->pushUniformVec3(WorldLook::kLightColorUniform,
                           lightingEnabled ? lightColor.x : 0.0f,
                           lightingEnabled ? lightColor.y : 0.0f,
                           lightingEnabled ? lightColor.z : 0.0f);
    value->pushUniformVec3(WorldLook::kAmbientColorUniform,
                           lightingEnabled ? ambientColor.x : 1.0f,
                           lightingEnabled ? ambientColor.y : 1.0f,
                           lightingEnabled ? ambientColor.z : 1.0f);
    value->pushUniformInt(WorldLook::kShadowsEnabledUniform,
                          lightingEnabled && shadowMapBound ? 1 : 0);
    value->pushUniformFloat(WorldLook::kShadowBiasUniform, shadowBias);
    value->pushUniformFloat(WorldLook::kShadowSlopeScaleUniform,
                            shadowSlopeScale);
    value->pushUniformFloat(WorldLook::kShadowNormalOffsetUniform,
                            shadowNormalOffset);
    value->pushUniformInt(WorldLook::kShadowPcfUniform,
                          shadowPcfEnabled ? 1 : 0);
    value->pushUniformMat4(WorldLook::kPrevMvpUniform,
                           glm::value_ptr(previousMvp));
    value->pushUniformInt(WorldLook::kMotionBlurEnabledUniform,
                          motionBlurEnabled ? 1 : 0);
    value->pushUniformFloat(WorldLook::kMotionBlurAmountUniform,
                            motionBlurAmount);
    value->pushUniformFloat(WorldLook::kMotionBlurMaxUniform, motionBlurMax);
    value->pushUniformVec4(WorldLook::kTintUniform,
                           static_cast<float>(meshAssetTint.r) / 255.0f,
                           static_cast<float>(meshAssetTint.g) / 255.0f,
                           static_cast<float>(meshAssetTint.b) / 255.0f,
                           static_cast<float>(meshAssetTint.a) / 255.0f);
    if (lightingEnabled && shadowMapBound) {
      value->pushSetTexture(shadow.depthTexture, WorldLook::kShadowTextureUnit);
      value->pushUniformInt(WorldLook::kShadowMapUniform,
                            WorldLook::kShadowTextureUnit);
    }
    value->pushDrawIndexed(meshAssetIndexCount);
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
  previousColoredMvp = coloredMvp;
  hasPreviousMvp = true;
  if (frame.active) {
    previousFrameSerial = frame.frameSerial;
  }
  return true;
}

void
MeshVisual::rebuildMeshes()
{
  expandIndexedVertices(lineVertices, lineIndices, &lineDrawVertices);
  triangleBoundsValid = !triangleVertices.empty() && !triangleIndices.empty();
  if (triangleBoundsValid) {
    const LitVertex& first = triangleVertices[0];
    triangleBoundsMin = glm::vec3(first.x, first.y, first.z);
    triangleBoundsMax = triangleBoundsMin;
    for (size_t i = 1; i < triangleVertices.size(); ++i) {
      const LitVertex& vertex = triangleVertices[i];
      const glm::vec3 position(vertex.x, vertex.y, vertex.z);
      triangleBoundsMin = glm::min(triangleBoundsMin, position);
      triangleBoundsMax = glm::max(triangleBoundsMax, position);
    }
  }
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
  triangleUploadPending = !triangleVertices.empty() && !triangleIndices.empty();
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

  size_t vertexStride = sizeof(ColorVertex);
  if (layout == MeshVertexLayout::Pos3Color4U8Uv2) {
    vertexStride = sizeof(SpriteVertex);
  } else if (layout == MeshVertexLayout::Pos3Norm3Color4U8Uv2) {
    vertexStride = sizeof(LitVertex);
  }

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

bool
MeshVisual::ensureTriangleMeshCapacity(size_t requiredVertices,
                                       size_t requiredIndices)
{
  if (requiredVertices == 0 || requiredIndices == 0) {
    return true;
  }
  if (renderer == nullptr) {
    return false;
  }
  if (triangleMeshHandle.isValid() &&
      triangleVertexCapacity >= requiredVertices &&
      triangleIndexCapacity >= requiredIndices) {
    return true;
  }

  size_t nextVertexCapacity =
    triangleVertexCapacity == 0 ? 64u : triangleVertexCapacity;
  while (nextVertexCapacity < requiredVertices) {
    nextVertexCapacity *= 2u;
  }
  size_t nextIndexCapacity =
    triangleIndexCapacity == 0 ? 64u : triangleIndexCapacity;
  while (nextIndexCapacity < requiredIndices) {
    nextIndexCapacity *= 2u;
  }

  const size_t vertexCapacityBytes = nextVertexCapacity * sizeof(LitVertex);
  const size_t indexCapacityBytes = nextIndexCapacity * sizeof(unsigned int);
  if (triangleMeshHandle.isValid()) {
    if (!renderer->replaceDynamicMesh(triangleMeshHandle,
                                      vertexCapacityBytes,
                                      nullptr,
                                      indexCapacityBytes,
                                      MeshVertexLayout::Pos3Norm3Color4U8Uv2)) {
      return false;
    }
  } else {
    triangleMeshHandle =
      renderer->enrollDynamicMesh(vertexCapacityBytes,
                                  nullptr,
                                  indexCapacityBytes,
                                  MeshVertexLayout::Pos3Norm3Color4U8Uv2);
    if (!triangleMeshHandle.isValid()) {
      return false;
    }
  }
  triangleVertexCapacity = nextVertexCapacity;
  triangleIndexCapacity = nextIndexCapacity;
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
  triangleVertexCapacity = 0;
  triangleIndexCapacity = 0;
  spriteMeshCapacity = 0;
  lineMeshIndices.clear();
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
  litMeshStyleHandle = RenderStyleHandle{};
}
