#pragma once

#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/ShapePrimitive.h>
#include <Illumo/Rendering/Primitives/SpritePrimitive.h>
#include <Illumo/Rendering/Primitives/TextPrimitive.h>
#include <Illumo/Rendering/RenderLayerId.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Camera;
class Renderer;

// Painter-correct host for composed 2D primitives. Items are ordered by
// drawOrder then stable insertion sequence; only adjacent compatible items are
// batched, so texture grouping never changes alpha composition.
class GameVisual : public DrawableBase
{
public:
  static const unsigned int kInitialQuadCapacity = 1024;
  static const unsigned int kDefaultMaxQuads = 65536;
  // Source compatibility for existing capacity checks.
  static const unsigned int kMaxQuads = kDefaultMaxQuads;

  explicit GameVisual(unsigned int maxQuads = kDefaultMaxQuads);
  ~GameVisual() override;

  void setRenderer(Renderer* renderer);
  void setWindow(IRenderWindow* window);
  void setCamera(Camera* camera);
  void setSpace(PrimitiveSpace space);
  PrimitiveSpace getSpace() const { return space; }

  void setTransform(const Transform2D& value);
  const Transform2D& getTransform() const { return transform; }
  void setLayerHint(RenderLayerId layer) { layerHint = layer; }
  RenderLayerId getLayerHint() const { return layerHint; }

  void prepare(Renderer* renderer);
  void clearPrimitives();
  size_t shapeCount() const { return shapes.size(); }
  size_t spriteCount() const { return sprites.size(); }
  size_t textCount() const { return texts.size(); }
  unsigned int getQuadCapacity() const { return quadCapacity; }
  unsigned int getMaxQuads() const { return maxQuadCount; }

  size_t addFilledRect(float x, float y, float w, float h, ColorRgba color);
  size_t addOutlineRect(float x,
                        float y,
                        float w,
                        float h,
                        ColorRgba color,
                        float lineWidth = 1.0f);
  size_t addLine(float x0,
                 float y0,
                 float x1,
                 float y1,
                 ColorRgba color,
                 float lineWidth = 1.0f);
  size_t addSprite(TextureHandle textureHandle,
                   float x,
                   float y,
                   float w,
                   float h,
                   ColorRgba tint = ColorRgba{},
                   float u0 = 0.0f,
                   float v0 = 0.0f,
                   float u1 = 1.0f,
                   float v1 = 1.0f);
  size_t addSprite(TextureHandle textureHandle,
                   const Rect2& rect,
                   const TextureRegion& region,
                   ColorRgba tint = ColorRgba{});
  size_t addCenteredSprite(TextureHandle textureHandle,
                           float centerX,
                           float centerY,
                           float width,
                           float height,
                           const TextureRegion& region = TextureRegion{},
                           ColorRgba tint = ColorRgba{});
  size_t addText(const std::string& content,
                 float x,
                 float y,
                 float sizePt,
                 ColorRgba color);

  ShapePrimitive* getShape(size_t index);
  SpritePrimitive* getSprite(size_t index);
  TextPrimitive* getText(size_t index);

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  struct ShapeVertex
  {
    float x, y, z;
    unsigned char r, g, b, a;
  };

  struct SpriteVertex
  {
    float x, y, z;
    unsigned char r, g, b, a;
    float u, v;
  };

  enum class VisualItemKind : unsigned char
  {
    Shape,
    Sprite,
    Text
  };

  struct VisualItem
  {
    VisualItemKind kind = VisualItemKind::Shape;
    size_t index = 0;
    uint64_t sequence = 0;
  };

  enum class BatchKind : unsigned char
  {
    Shape,
    Sprite
  };

  struct DrawBatch
  {
    BatchKind kind = BatchKind::Shape;
    RenderStyleHandle styleHandle{};
    TextureHandle textureHandle{};
    unsigned int firstQuad = 0;
    unsigned int quadCount = 0;
  };

  struct Point2
  {
    float x = 0.0f;
    float y = 0.0f;
  };

  Renderer* renderer = nullptr;
  IRenderWindow* window = nullptr;
  Camera* camera = nullptr;
  PrimitiveSpace space = PrimitiveSpace::Pixels;
  RenderLayerId layerHint = RenderLayerId::World;
  Transform2D transform;

  std::vector<ShapePrimitive> shapes;
  std::vector<SpritePrimitive> sprites;
  std::vector<TextPrimitive> texts;
  std::vector<VisualItem> items;
  uint64_t nextSequence = 0;

  MeshHandle shapeMeshHandle{};
  MeshHandle spriteMeshHandle{};
  bool gpuReady = false;
  bool geometryDirty = true;
  bool shapeUploadPending = false;
  bool spriteUploadPending = false;
  bool capacityWarningLogged = false;

  std::vector<ShapeVertex> shapeVerts;
  std::vector<SpriteVertex> spriteVerts;
  std::vector<unsigned char> textTessellateScratch;
  std::vector<DrawBatch> drawBatches;
  unsigned int shapeQuadCount = 0;
  unsigned int spriteQuadCount = 0;
  unsigned int quadCapacity = 0;
  unsigned int gpuQuadCapacity = 0;
  unsigned int maxQuadCount;

  void markDirty() { geometryDirty = true; }
  void enrollGpuResources();
  void rebuildGeometry();
  bool ensureCpuCapacity(unsigned int required);
  bool ensureGpuCapacity();
  std::vector<unsigned int> buildIndices(unsigned int capacity) const;
  int drawOrder(const VisualItem& item) const;
  RenderStyleHandle itemStyle(const VisualItem& item) const;
  Point2 transformPoint(Point2 point,
                        const Rect2& bounds,
                        const Transform2D& local) const;
  Point2 applyHostTransform(Point2 point, const Rect2& contentBounds) const;
  Rect2 contentBounds() const;
  bool pushShapeQuad(Point2 p0,
                     Point2 p1,
                     Point2 p2,
                     Point2 p3,
                     ColorRgba color);
  bool pushLineAsQuad(Point2 p0,
                      Point2 p1,
                      float width,
                      ColorRgba color,
                      const Rect2& hostBounds);
  bool pushSpriteQuad(const SpritePrimitive& sprite, const Rect2& hostBounds);
  bool pushTextRun(const TextPrimitive& text, const Rect2& hostBounds);
  void appendBatch(BatchKind kind,
                   RenderStyleHandle styleHandle,
                   TextureHandle textureHandle,
                   unsigned int firstQuad,
                   unsigned int quadCount);
};
