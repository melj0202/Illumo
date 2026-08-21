#include "thirdparty/stb/stb_easy_font.h"
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/IMesh.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>

static void
includeBounds(bool& any,
              float& minX,
              float& minY,
              float& maxX,
              float& maxY,
              float x0,
              float y0,
              float x1,
              float y1)
{
  if (!any) {
    minX = x0;
    minY = y0;
    maxX = x1;
    maxY = y1;
    any = true;
    return;
  }
  minX = std::min(minX, x0);
  minY = std::min(minY, y0);
  maxX = std::max(maxX, x1);
  maxY = std::max(maxY, y1);
}

GameVisual::GameVisual(unsigned int maxQuads)
  : maxQuadCount(maxQuads == 0 ? 1 : maxQuads)
{
  quadCapacity = std::min(kInitialQuadCapacity, maxQuadCount);
  shapeVerts.resize(static_cast<size_t>(quadCapacity) * 4);
  spriteVerts.resize(static_cast<size_t>(quadCapacity) * 4);
}

GameVisual::~GameVisual()
{
  if (renderer != nullptr) {
    if (shapeMeshHandle.isValid()) {
      renderer->destroyMesh(shapeMeshHandle);
    }
    if (spriteMeshHandle.isValid()) {
      renderer->destroyMesh(spriteMeshHandle);
    }
  }
  gpuReady = false;
}

void
GameVisual::setRenderer(Renderer* value)
{
  renderer = value;
  if (!gpuReady && renderer != nullptr) {
    enrollGpuResources();
  }
}

void
GameVisual::setWindow(IRenderWindow* value)
{
  window = value;
}

void
GameVisual::setCamera(Camera* value)
{
  camera = value;
}

void
GameVisual::setSpace(PrimitiveSpace value)
{
  if (space != value) {
    space = value;
    markDirty();
  }
}

void
GameVisual::setTransform(const Transform2D& value)
{
  transform = value;
  markDirty();
}

void
GameVisual::prepare(Renderer* value)
{
  if (value != nullptr) {
    renderer = value;
  }
  enrollGpuResources();
}

void
GameVisual::clearPrimitives()
{
  shapes.clear();
  sprites.clear();
  texts.clear();
  items.clear();
  nextSequence = 0;
  markDirty();
}

size_t
GameVisual::addFilledRect(float x, float y, float w, float h, ColorRgba color)
{
  ShapePrimitive shape;
  shape.kind = ShapeKind::FilledRect;
  shape.rect = { x, y, w, h };
  shape.color = color;
  shapes.push_back(shape);
  VisualItem item;
  item.kind = VisualItemKind::Shape;
  item.index = shapes.size() - 1;
  item.sequence = nextSequence++;
  items.push_back(item);
  markDirty();
  return shapeCount() - 1;
}

size_t
GameVisual::addOutlineRect(float x,
                           float y,
                           float w,
                           float h,
                           ColorRgba color,
                           float lineWidth)
{
  ShapePrimitive shape;
  shape.kind = ShapeKind::OutlineRect;
  shape.rect = { x, y, w, h };
  shape.color = color;
  shape.lineWidth = lineWidth;
  shapes.push_back(shape);
  VisualItem item;
  item.kind = VisualItemKind::Shape;
  item.index = shapes.size() - 1;
  item.sequence = nextSequence++;
  items.push_back(item);
  markDirty();
  return shapeCount() - 1;
}

size_t
GameVisual::addLine(float x0,
                    float y0,
                    float x1,
                    float y1,
                    ColorRgba color,
                    float lineWidth)
{
  ShapePrimitive shape;
  shape.kind = ShapeKind::Line;
  shape.x0 = x0;
  shape.y0 = y0;
  shape.x1 = x1;
  shape.y1 = y1;
  shape.color = color;
  shape.lineWidth = lineWidth;
  shapes.push_back(shape);
  VisualItem item;
  item.kind = VisualItemKind::Shape;
  item.index = shapes.size() - 1;
  item.sequence = nextSequence++;
  items.push_back(item);
  markDirty();
  return shapeCount() - 1;
}

size_t
GameVisual::addSprite(TextureHandle textureHandle,
                      float x,
                      float y,
                      float w,
                      float h,
                      ColorRgba tint,
                      float u0,
                      float v0,
                      float u1,
                      float v1)
{
  Rect2 rect{ x, y, w, h };
  TextureRegion region{ u0, v0, u1, v1 };
  return addSprite(textureHandle, rect, region, tint);
}

size_t
GameVisual::addSprite(TextureHandle textureHandle,
                      const Rect2& rect,
                      const TextureRegion& region,
                      ColorRgba tint)
{
  SpritePrimitive sprite;
  sprite.textureHandle = textureHandle;
  sprite.rect = rect;
  sprite.region = region;
  sprite.tint = tint;
  sprites.push_back(sprite);
  VisualItem item;
  item.kind = VisualItemKind::Sprite;
  item.index = sprites.size() - 1;
  item.sequence = nextSequence++;
  items.push_back(item);
  markDirty();
  return spriteCount() - 1;
}

size_t
GameVisual::addCenteredSprite(TextureHandle textureHandle,
                              float centerX,
                              float centerY,
                              float width,
                              float height,
                              const TextureRegion& region,
                              ColorRgba tint)
{
  Rect2 rect{ centerX - width * 0.5f, centerY - height * 0.5f, width, height };
  const size_t index = addSprite(textureHandle, rect, region, tint);
  SpritePrimitive* sprite = getSprite(index);
  if (sprite != nullptr) {
    sprite->transform.pivotX = 0.5f;
    sprite->transform.pivotY = 0.5f;
  }
  return index;
}

size_t
GameVisual::addText(const std::string& content,
                    float x,
                    float y,
                    float sizePt,
                    ColorRgba color)
{
  TextPrimitive text;
  text.content = content;
  text.x = x;
  text.y = y;
  text.sizePt = sizePt;
  text.color = color;
  texts.push_back(text);
  VisualItem item;
  item.kind = VisualItemKind::Text;
  item.index = texts.size() - 1;
  item.sequence = nextSequence++;
  items.push_back(item);
  markDirty();
  return textCount() - 1;
}

ShapePrimitive*
GameVisual::getShape(size_t index)
{
  if (index >= shapes.size()) {
    return nullptr;
  }
  markDirty();
  return &shapes[index];
}

SpritePrimitive*
GameVisual::getSprite(size_t index)
{
  if (index >= sprites.size()) {
    return nullptr;
  }
  markDirty();
  return &sprites[index];
}

TextPrimitive*
GameVisual::getText(size_t index)
{
  if (index >= texts.size()) {
    return nullptr;
  }
  markDirty();
  return &texts[index];
}

std::vector<unsigned int>
GameVisual::buildIndices(unsigned int capacity) const
{
  std::vector<unsigned int> indices(static_cast<size_t>(capacity) * 6);
  for (unsigned int i = 0; i < capacity; ++i) {
    indices[static_cast<size_t>(i * 6 + 0)] = i * 4 + 0;
    indices[static_cast<size_t>(i * 6 + 1)] = i * 4 + 1;
    indices[static_cast<size_t>(i * 6 + 2)] = i * 4 + 2;
    indices[static_cast<size_t>(i * 6 + 3)] = i * 4 + 2;
    indices[static_cast<size_t>(i * 6 + 4)] = i * 4 + 3;
    indices[static_cast<size_t>(i * 6 + 5)] = i * 4 + 0;
  }
  return indices;
}

void
GameVisual::enrollGpuResources()
{
  if (renderer == nullptr) {
    return;
  }
  if (gpuReady && shapeMeshHandle.isValid() && spriteMeshHandle.isValid()) {
    return;
  }

  renderer->ensureBuiltinStyles();
  const std::vector<unsigned int> indices = buildIndices(quadCapacity);
  const size_t shapeBytes =
    static_cast<size_t>(quadCapacity) * 4 * sizeof(ShapeVertex);
  shapeMeshHandle =
    renderer->enrollDynamicMesh(shapeBytes,
                                indices.data(),
                                indices.size() * sizeof(unsigned int),
                                MeshVertexLayout::Pos3Color4U8);
  const size_t spriteBytes =
    static_cast<size_t>(quadCapacity) * 4 * sizeof(SpriteVertex);
  spriteMeshHandle =
    renderer->enrollDynamicMesh(spriteBytes,
                                indices.data(),
                                indices.size() * sizeof(unsigned int),
                                MeshVertexLayout::Pos3Color4U8Uv2);
  gpuReady = shapeMeshHandle.isValid() && spriteMeshHandle.isValid();
  gpuQuadCapacity = gpuReady ? quadCapacity : 0;
  geometryDirty = true;
}

bool
GameVisual::ensureCpuCapacity(unsigned int required)
{
  if (required <= quadCapacity) {
    return true;
  }
  if (required > maxQuadCount) {
    if (!capacityWarningLogged) {
      capacityWarningLogged = true;
      Logger::LogWarning("GameVisual reached its configured quad safety limit");
    }
    return false;
  }
  unsigned int next = quadCapacity;
  while (next < required && next < maxQuadCount) {
    const unsigned int doubled =
      next > maxQuadCount / 2 ? maxQuadCount : next * 2;
    next = std::max(next + 1, doubled);
  }
  quadCapacity = std::min(next, maxQuadCount);
  shapeVerts.resize(static_cast<size_t>(quadCapacity) * 4);
  spriteVerts.resize(static_cast<size_t>(quadCapacity) * 4);
  return required <= quadCapacity;
}

bool
GameVisual::ensureGpuCapacity()
{
  if (!gpuReady || renderer == nullptr) {
    return false;
  }
  if (gpuQuadCapacity >= quadCapacity) {
    return true;
  }
  const std::vector<unsigned int> indices = buildIndices(quadCapacity);
  const bool shapeOk = renderer->replaceDynamicMesh(
    shapeMeshHandle,
    static_cast<size_t>(quadCapacity) * 4 * sizeof(ShapeVertex),
    indices.data(),
    indices.size() * sizeof(unsigned int),
    MeshVertexLayout::Pos3Color4U8);
  const bool spriteOk = renderer->replaceDynamicMesh(
    spriteMeshHandle,
    static_cast<size_t>(quadCapacity) * 4 * sizeof(SpriteVertex),
    indices.data(),
    indices.size() * sizeof(unsigned int),
    MeshVertexLayout::Pos3Color4U8Uv2);
  if (shapeOk && spriteOk) {
    gpuQuadCapacity = quadCapacity;
    return true;
  }
  return false;
}

int
GameVisual::drawOrder(const VisualItem& item) const
{
  if (item.kind == VisualItemKind::Shape && item.index < shapes.size()) {
    return shapes[item.index].drawOrder;
  }
  if (item.kind == VisualItemKind::Sprite && item.index < sprites.size()) {
    return sprites[item.index].drawOrder;
  }
  if (item.kind == VisualItemKind::Text && item.index < texts.size()) {
    return texts[item.index].drawOrder;
  }
  return 0;
}

RenderStyleHandle
GameVisual::itemStyle(const VisualItem& item) const
{
  if (item.kind == VisualItemKind::Shape && item.index < shapes.size() &&
      shapes[item.index].styleHandle.isValid()) {
    return shapes[item.index].styleHandle;
  }
  if (item.kind == VisualItemKind::Sprite && item.index < sprites.size() &&
      sprites[item.index].styleHandle.isValid()) {
    return sprites[item.index].styleHandle;
  }
  if (item.kind == VisualItemKind::Text && item.index < texts.size() &&
      texts[item.index].styleHandle.isValid()) {
    return texts[item.index].styleHandle;
  }
  if (renderer == nullptr) {
    return RenderStyleHandle{};
  }
  return renderer->getBuiltinStyleHandle(item.kind == VisualItemKind::Sprite
                                           ? RenderStyleId::Sprite
                                           : RenderStyleId::Shape);
}

GameVisual::Point2
GameVisual::transformPoint(Point2 point,
                           const Rect2& bounds,
                           const Transform2D& local) const
{
  const float pivotX = bounds.x + bounds.w * local.pivotX;
  const float pivotY = bounds.y + bounds.h * local.pivotY;
  const float scaledX = (point.x - pivotX) * local.scaleX;
  const float scaledY = (point.y - pivotY) * local.scaleY;
  const float cosine = std::cos(local.rotationRadians);
  const float sine = std::sin(local.rotationRadians);
  Point2 transformed;
  transformed.x = pivotX + scaledX * cosine - scaledY * sine + local.x;
  transformed.y = pivotY + scaledX * sine + scaledY * cosine + local.y;
  return transformed;
}

GameVisual::Point2
GameVisual::applyHostTransform(Point2 point, const Rect2& bounds) const
{
  return transformPoint(point, bounds, transform);
}

Rect2
GameVisual::contentBounds() const
{
  bool any = false;
  float minX = 0.0f;
  float minY = 0.0f;
  float maxX = 0.0f;
  float maxY = 0.0f;
  for (size_t i = 0; i < shapes.size(); ++i) {
    const ShapePrimitive& shape = shapes[i];
    if (!shape.visible) {
      continue;
    }
    if (shape.kind == ShapeKind::Line) {
      includeBounds(any,
                    minX,
                    minY,
                    maxX,
                    maxY,
                    std::min(shape.x0, shape.x1),
                    std::min(shape.y0, shape.y1),
                    std::max(shape.x0, shape.x1),
                    std::max(shape.y0, shape.y1));
    } else {
      includeBounds(any,
                    minX,
                    minY,
                    maxX,
                    maxY,
                    shape.rect.x,
                    shape.rect.y,
                    shape.rect.x + shape.rect.w,
                    shape.rect.y + shape.rect.h);
    }
  }
  for (size_t i = 0; i < sprites.size(); ++i) {
    const SpritePrimitive& sprite = sprites[i];
    if (sprite.visible) {
      includeBounds(any,
                    minX,
                    minY,
                    maxX,
                    maxY,
                    sprite.rect.x,
                    sprite.rect.y,
                    sprite.rect.x + sprite.rect.w,
                    sprite.rect.y + sprite.rect.h);
    }
  }
  for (size_t i = 0; i < texts.size(); ++i) {
    const TextPrimitive& text = texts[i];
    if (text.visible) {
      const float width =
        static_cast<float>(text.content.size()) * text.sizePt * 0.67f;
      includeBounds(any,
                    minX,
                    minY,
                    maxX,
                    maxY,
                    text.x,
                    text.y,
                    text.x + width,
                    text.y + text.sizePt);
    }
  }
  return any ? Rect2{ minX, minY, maxX - minX, maxY - minY } : Rect2{};
}

bool
GameVisual::pushShapeQuad(Point2 p0,
                          Point2 p1,
                          Point2 p2,
                          Point2 p3,
                          ColorRgba color)
{
  if (!ensureCpuCapacity(shapeQuadCount + spriteQuadCount + 1)) {
    return false;
  }
  const unsigned int base = shapeQuadCount * 4;
  shapeVerts[base + 0] = {
    p0.x, p0.y, 0.0f, color.r, color.g, color.b, color.a
  };
  shapeVerts[base + 1] = {
    p1.x, p1.y, 0.0f, color.r, color.g, color.b, color.a
  };
  shapeVerts[base + 2] = {
    p2.x, p2.y, 0.0f, color.r, color.g, color.b, color.a
  };
  shapeVerts[base + 3] = {
    p3.x, p3.y, 0.0f, color.r, color.g, color.b, color.a
  };
  shapeQuadCount += 1;
  return true;
}

bool
GameVisual::pushLineAsQuad(Point2 p0,
                           Point2 p1,
                           float width,
                           ColorRgba color,
                           const Rect2& hostBounds)
{
  p0 = applyHostTransform(p0, hostBounds);
  p1 = applyHostTransform(p1, hostBounds);
  const float dx = p1.x - p0.x;
  const float dy = p1.y - p0.y;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (length < 0.000001f) {
    const float half = width * 0.5f;
    return pushShapeQuad({ p0.x - half, p0.y - half },
                         { p0.x + half, p0.y - half },
                         { p0.x + half, p0.y + half },
                         { p0.x - half, p0.y + half },
                         color);
  }
  const float nx = (-dy / length) * width * 0.5f;
  const float ny = (dx / length) * width * 0.5f;
  return pushShapeQuad({ p0.x + nx, p0.y + ny },
                       { p1.x + nx, p1.y + ny },
                       { p1.x - nx, p1.y - ny },
                       { p0.x - nx, p0.y - ny },
                       color);
}

bool
GameVisual::pushSpriteQuad(const SpritePrimitive& sprite,
                           const Rect2& hostBounds)
{
  if (!ensureCpuCapacity(shapeQuadCount + spriteQuadCount + 1)) {
    return false;
  }
  const Rect2 bounds = sprite.rect;
  Point2 p0 = transformPoint({ bounds.x, bounds.y }, bounds, sprite.transform);
  Point2 p1 =
    transformPoint({ bounds.x + bounds.w, bounds.y }, bounds, sprite.transform);
  Point2 p2 = transformPoint(
    { bounds.x + bounds.w, bounds.y + bounds.h }, bounds, sprite.transform);
  Point2 p3 =
    transformPoint({ bounds.x, bounds.y + bounds.h }, bounds, sprite.transform);
  p0 = applyHostTransform(p0, hostBounds);
  p1 = applyHostTransform(p1, hostBounds);
  p2 = applyHostTransform(p2, hostBounds);
  p3 = applyHostTransform(p3, hostBounds);

  float u0 = sprite.region.u0;
  float v0 = sprite.region.v0;
  float u1 = sprite.region.u1;
  float v1 = sprite.region.v1;
  if (sprite.flipX) {
    std::swap(u0, u1);
  }
  if (sprite.flipY) {
    std::swap(v0, v1);
  }
  const unsigned int base = spriteQuadCount * 4;
  const ColorRgba color = sprite.tint;
  spriteVerts[base + 0] = { p0.x,    p0.y,    0.0f, color.r, color.g,
                            color.b, color.a, u0,   v0 };
  spriteVerts[base + 1] = { p1.x,    p1.y,    0.0f, color.r, color.g,
                            color.b, color.a, u1,   v0 };
  spriteVerts[base + 2] = { p2.x,    p2.y,    0.0f, color.r, color.g,
                            color.b, color.a, u1,   v1 };
  spriteVerts[base + 3] = { p3.x,    p3.y,    0.0f, color.r, color.g,
                            color.b, color.a, u0,   v1 };
  spriteQuadCount += 1;
  return true;
}

bool
GameVisual::pushTextRun(const TextPrimitive& text, const Rect2& hostBounds)
{
  if (text.content.empty()) {
    return true;
  }
  const unsigned int remaining =
    maxQuadCount - shapeQuadCount - spriteQuadCount;
  if (remaining == 0) {
    return false;
  }
  unsigned int estimatedQuads =
    static_cast<unsigned int>(text.content.size() * 16u);
  if (estimatedQuads < 32u) {
    estimatedQuads = 32u;
  }
  if (estimatedQuads > remaining) {
    estimatedQuads = remaining;
  }
  size_t tempBytes =
    static_cast<size_t>(estimatedQuads) * 4 * sizeof(ShapeVertex);
  if (textTessellateScratch.size() < tempBytes) {
    textTessellateScratch.resize(tempBytes);
  }
  unsigned char color[4] = {
    text.color.r, text.color.g, text.color.b, text.color.a
  };
  char* mutableText = const_cast<char*>(text.content.c_str());
  int quadCount = stb_easy_font_print(0.0f,
                                      0.0f,
                                      mutableText,
                                      color,
                                      textTessellateScratch.data(),
                                      static_cast<int>(tempBytes));
  if (quadCount < 0) {
    quadCount = 0;
  }
  if (quadCount >= static_cast<int>(estimatedQuads) &&
      estimatedQuads < remaining) {
    estimatedQuads = remaining;
    tempBytes = static_cast<size_t>(estimatedQuads) * 4 * sizeof(ShapeVertex);
    if (textTessellateScratch.size() < tempBytes) {
      textTessellateScratch.resize(tempBytes);
    }
    quadCount = stb_easy_font_print(0.0f,
                                    0.0f,
                                    mutableText,
                                    color,
                                    textTessellateScratch.data(),
                                    static_cast<int>(tempBytes));
    if (quadCount < 0) {
      quadCount = 0;
    }
  }
  if (!ensureCpuCapacity(shapeQuadCount + spriteQuadCount +
                         static_cast<unsigned int>(quadCount))) {
    quadCount =
      static_cast<int>(maxQuadCount - shapeQuadCount - spriteQuadCount);
  }
  const float scale = text.sizePt / 12.0f;
  const ShapeVertex* source =
    reinterpret_cast<const ShapeVertex*>(textTessellateScratch.data());
  for (int q = 0; q < quadCount; ++q) {
    Point2 points[4];
    for (int vertex = 0; vertex < 4; ++vertex) {
      const ShapeVertex& value = source[q * 4 + vertex];
      points[vertex].x = value.x * scale + text.x;
      points[vertex].y = value.y * scale + text.y;
      points[vertex] = applyHostTransform(points[vertex], hostBounds);
    }
    if (!pushShapeQuad(
          points[0], points[1], points[2], points[3], text.color)) {
      return false;
    }
  }
  return true;
}

void
GameVisual::appendBatch(BatchKind kind,
                        RenderStyleHandle styleHandle,
                        TextureHandle textureHandle,
                        unsigned int firstQuad,
                        unsigned int quadCount)
{
  if (quadCount == 0) {
    return;
  }
  if (!drawBatches.empty()) {
    DrawBatch& last = drawBatches.back();
    if (last.kind == kind && last.styleHandle == styleHandle &&
        last.textureHandle == textureHandle &&
        last.firstQuad + last.quadCount == firstQuad) {
      last.quadCount += quadCount;
      return;
    }
  }
  DrawBatch batch;
  batch.kind = kind;
  batch.styleHandle = styleHandle;
  batch.textureHandle = textureHandle;
  batch.firstQuad = firstQuad;
  batch.quadCount = quadCount;
  drawBatches.push_back(batch);
}

void
GameVisual::rebuildGeometry()
{
  shapeQuadCount = 0;
  spriteQuadCount = 0;
  drawBatches.clear();
  const Rect2 hostBounds = contentBounds();
  std::vector<VisualItem> ordered = items;
  std::stable_sort(ordered.begin(),
                   ordered.end(),
                   [this](const VisualItem& a, const VisualItem& b) {
                     const int leftOrder = drawOrder(a);
                     const int rightOrder = drawOrder(b);
                     return leftOrder == rightOrder ? a.sequence < b.sequence
                                                    : leftOrder < rightOrder;
                   });

  for (size_t itemIndex = 0; itemIndex < ordered.size(); ++itemIndex) {
    const VisualItem& item = ordered[itemIndex];
    const RenderStyleHandle styleHandle = itemStyle(item);
    if (item.kind == VisualItemKind::Sprite) {
      if (item.index >= sprites.size()) {
        continue;
      }
      const SpritePrimitive& sprite = sprites[item.index];
      if (!sprite.visible || !sprite.textureHandle.isValid()) {
        continue;
      }
      const unsigned int first = spriteQuadCount;
      if (!pushSpriteQuad(sprite, hostBounds)) {
        break;
      }
      appendBatch(BatchKind::Sprite,
                  styleHandle,
                  sprite.textureHandle,
                  first,
                  spriteQuadCount - first);
      continue;
    }

    const unsigned int first = shapeQuadCount;
    if (item.kind == VisualItemKind::Text) {
      if (item.index >= texts.size() || !texts[item.index].visible) {
        continue;
      }
      if (!pushTextRun(texts[item.index], hostBounds)) {
        break;
      }
    } else {
      if (item.index >= shapes.size() || !shapes[item.index].visible) {
        continue;
      }
      const ShapePrimitive& shape = shapes[item.index];
      if (shape.kind == ShapeKind::Line) {
        Rect2 bounds;
        bounds.x = std::min(shape.x0, shape.x1);
        bounds.y = std::min(shape.y0, shape.y1);
        bounds.w = std::abs(shape.x1 - shape.x0);
        bounds.h = std::abs(shape.y1 - shape.y0);
        Point2 p0 =
          transformPoint({ shape.x0, shape.y0 }, bounds, shape.transform);
        Point2 p1 =
          transformPoint({ shape.x1, shape.y1 }, bounds, shape.transform);
        if (!pushLineAsQuad(p0,
                            p1,
                            std::max(shape.lineWidth, 1.0f),
                            shape.color,
                            hostBounds)) {
          break;
        }
      } else if (shape.kind == ShapeKind::FilledRect) {
        const Rect2 bounds = shape.rect;
        Point2 p0 =
          transformPoint({ bounds.x, bounds.y }, bounds, shape.transform);
        Point2 p1 = transformPoint(
          { bounds.x + bounds.w, bounds.y }, bounds, shape.transform);
        Point2 p2 = transformPoint({ bounds.x + bounds.w, bounds.y + bounds.h },
                                   bounds,
                                   shape.transform);
        Point2 p3 = transformPoint(
          { bounds.x, bounds.y + bounds.h }, bounds, shape.transform);
        if (!pushShapeQuad(applyHostTransform(p0, hostBounds),
                           applyHostTransform(p1, hostBounds),
                           applyHostTransform(p2, hostBounds),
                           applyHostTransform(p3, hostBounds),
                           shape.color)) {
          break;
        }
      } else {
        const Rect2 bounds = shape.rect;
        const float thickness = std::max(shape.lineWidth, 1.0f);
        Point2 corners[4] = {
          { bounds.x, bounds.y },
          { bounds.x + bounds.w, bounds.y },
          { bounds.x + bounds.w, bounds.y + bounds.h },
          { bounds.x, bounds.y + bounds.h },
        };
        for (int i = 0; i < 4; ++i) {
          corners[i] = transformPoint(corners[i], bounds, shape.transform);
        }
        bool ok = true;
        for (int i = 0; i < 4; ++i) {
          ok = ok && pushLineAsQuad(corners[i],
                                    corners[(i + 1) % 4],
                                    thickness,
                                    shape.color,
                                    hostBounds);
        }
        if (!ok) {
          break;
        }
      }
    }
    appendBatch(BatchKind::Shape,
                styleHandle,
                TextureHandle{},
                first,
                shapeQuadCount - first);
  }

  shapeUploadPending = shapeQuadCount > 0;
  spriteUploadPending = spriteQuadCount > 0;
  geometryDirty = false;
}

bool
GameVisual::AppendCommands(Renderer* value)
{
  if (!isVisible()) {
    return true;
  }
  if (value == nullptr) {
    return false;
  }
  if (!gpuReady) {
    renderer = value;
    enrollGpuResources();
  }
  if (!gpuReady) {
    return false;
  }
  if (geometryDirty) {
    rebuildGeometry();
  }
  if (!ensureGpuCapacity()) {
    return false;
  }
  if (drawBatches.empty()) {
    return true;
  }

  if (shapeUploadPending) {
    value->pushUpdateBuffer(
      shapeMeshHandle,
      0,
      static_cast<unsigned int>(shapeQuadCount * 4 * sizeof(ShapeVertex)),
      shapeVerts.data());
    shapeUploadPending = false;
  }
  if (spriteUploadPending) {
    value->pushUpdateBuffer(
      spriteMeshHandle,
      0,
      static_cast<unsigned int>(spriteQuadCount * 4 * sizeof(SpriteVertex)),
      spriteVerts.data());
    spriteUploadPending = false;
  }

  float width = 1280.0f;
  float height = 720.0f;
  if (window != nullptr) {
    const std::array<int, 2> dimensions = window->getWindowDimensions();
    width = static_cast<float>(dimensions[0]);
    height = static_cast<float>(dimensions[1]);
  }
  const int usePixels = space == PrimitiveSpace::Pixels ? 1 : 0;
  float mvp[16] = {
    1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
  };
  if (space == PrimitiveSpace::World && camera != nullptr &&
      window != nullptr) {
    const std::array<int, 2> dimensions = window->getWindowDimensions();
    const float aspect =
      static_cast<float>(dimensions[0]) /
      static_cast<float>(dimensions[1] > 0 ? dimensions[1] : 1);
    const glm::mat4 matrix = camera->GetMVPMatrix(aspect);
    std::memcpy(mvp, &matrix[0][0], 16 * sizeof(float));
  }

  for (size_t i = 0; i < drawBatches.size(); ++i) {
    const DrawBatch& batch = drawBatches[i];
    if (!value->bindStyle(batch.styleHandle)) {
      return false;
    }
    value->pushSetMesh(batch.kind == BatchKind::Shape ? shapeMeshHandle
                                                      : spriteMeshHandle);
    value->pushUniformInt("uUsePixels", usePixels);
    value->pushUniformVec2("u_resolution", width, height);
    value->pushUniformMat4("uMVP", mvp);
    if (batch.kind == BatchKind::Sprite) {
      value->pushUniformInt("uTexture", 0);
      value->pushSetTexture(batch.textureHandle, 0);
    }
    value->pushDrawIndexed(batch.quadCount * 6, batch.firstQuad * 6);
  }
  return true;
}
