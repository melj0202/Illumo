#pragma once

#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/IRenderWindow.h>
#include <Illumo/Rendering/Primitives/ShapePrimitive.h>
#include <Illumo/Rendering/Primitives/SpritePrimitive.h>
#include <Illumo/Rendering/Primitives/TextPrimitive.h>
#include <Illumo/Rendering/RenderLayerId.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

class Camera;
class Renderer;

// One item of a GameVisual by value, as VisualStore operations carry it.
using GameVisualItem =
  std::variant<ShapePrimitive, SpritePrimitive, TextPrimitive>;

// Painter-correct host for composed 2D primitives. Items are ordered by
// drawOrder then stable insertion sequence; only adjacent compatible items are
// batched, so texture grouping never changes alpha composition.
class GameVisual : public DrawableBase
{
public:
  static constexpr unsigned int kInitialQuadCapacity = 1024;
  static constexpr unsigned int kDefaultMaxQuads = 65536;
  // Source compatibility for existing capacity checks.
  static constexpr unsigned int kMaxQuads = kDefaultMaxQuads;

  explicit GameVisual(unsigned int maxQuads = kDefaultMaxQuads);
  ~GameVisual() override;
  GameVisual(const GameVisual&) = delete;
  GameVisual& operator=(const GameVisual&) = delete;
  GameVisual(GameVisual&&) = delete;
  GameVisual& operator=(GameVisual&&) = delete;

  void setRenderer(Renderer* renderer);
  void setWindow(IRenderWindow* window);
  void setCamera(Camera* camera);
  void setSpace(PrimitiveSpace space);
  PrimitiveSpace getSpace() const { return space; }

  void setTransform(const Transform2D& value);
  const Transform2D& getTransform() const { return transform; }
  // Optional top-left logical-pixel clip. Pixel-space geometry outside the
  // clip is culled before upload; partial geometry is clipped by scissor.
  void setPixelClipRect(const Rect2& value);
  void clearPixelClipRect();
  bool hasPixelClipRect() const { return pixelClipEnabled; }
  const Rect2& getPixelClipRect() const { return pixelClipRect; }
  void setLayerHint(RenderLayerId layer) { layerHint = layer; }
  RenderLayerId getLayerHint() const { return layerHint; }

  void prepare(Renderer* renderer);
  void clearPrimitives();
  size_t shapeCount() const { return shapes.size(); }
  size_t spriteCount() const { return sprites.size(); }
  size_t textCount() const { return texts.size(); }
  unsigned int getQuadCapacity() const { return quadCapacity; }
  unsigned int getMaxQuads() const { return maxQuadCount; }
  // Quads tessellated by the most recent geometry rebuild (AppendCommands).
  unsigned int builtQuadCount() const
  {
    return shapeQuadCount + spriteQuadCount;
  }

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
  size_t addFilledEllipse(float x, float y, float w, float h, ColorRgba color);
  size_t addFilledTriangle(float x0,
                           float y0,
                           float x1,
                           float y1,
                           float x2,
                           float y2,
                           ColorRgba color);
  // Per-vertex-color shapes for gradients, soft shadows and glows. The quad
  // must be convex with vertices in fan order; colors interpolate across its
  // two triangles (0,1,2) and (2,3,0), so prefer two-color axis gradients or
  // radial fans over four unrelated corner colors.
  size_t addGradientQuad(float x0,
                         float y0,
                         float x1,
                         float y1,
                         float x2,
                         float y2,
                         float x3,
                         float y3,
                         ColorRgba c0,
                         ColorRgba c1,
                         ColorRgba c2,
                         ColorRgba c3);
  size_t addGradientRect(float x,
                         float y,
                         float w,
                         float h,
                         ColorRgba topLeft,
                         ColorRgba topRight,
                         ColorRgba bottomRight,
                         ColorRgba bottomLeft);
  size_t addGradientTriangle(float x0,
                             float y0,
                             float x1,
                             float y1,
                             float x2,
                             float y2,
                             ColorRgba c0,
                             ColorRgba c1,
                             ColorRgba c2);
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
  const ShapePrimitive* getShape(size_t index) const;
  const TextPrimitive* getText(size_t index) const;

  // Painter order (drawOrder, then insertion) for CPU consumers such as
  // SoftwareCanvas that draw the same primitives without the GPU path.
  enum class PrimitiveKind : unsigned char
  {
    Shape,
    Sprite,
    Text
  };
  struct PrimitiveRef
  {
    PrimitiveKind kind = PrimitiveKind::Shape;
    size_t index = 0;
  };
  std::vector<PrimitiveRef> paintOrder() const;
  // The item at insertion index `index` (index < itemCount()).
  PrimitiveRef itemAt(size_t index) const;
  const SpritePrimitive* getSprite(size_t index) const;
  Camera* getCamera() const { return camera; }
  // The logical size Pixels-space geometry lays out in on `renderer` this
  // frame (window over UI scale), as AppendCommands computes it.
  std::array<float, 2> pixelResolution(Renderer* renderer) const;
  // Whether World-space geometry would use the renderer's frame camera.
  bool drawsWithFrameCamera(Renderer* renderer) const;

  // Items by insertion index, for owners that edit a visual in place
  // (VisualStore). setItem at itemCount() appends; an index past it fails.
  // Replaced and removed primitives stay in the per-kind lists, hidden, as
  // free slots for later items of their kind, so the per-kind counts above
  // include them.
  size_t itemCount() const { return items.size(); }
  bool setItem(size_t index, const GameVisualItem& value);
  // Typed forms: replacing an item of the same kind reuses its storage.
  bool setItem(size_t index, const ShapePrimitive& value);
  bool setItem(size_t index, const SpritePrimitive& value);
  bool setItem(size_t index, const TextPrimitive& value);
  void removeItems(size_t first, size_t count);

  // Scales every vertex alpha; 1 leaves geometry bit-identical.
  void setOpacity(float value);
  float getOpacity() const { return opacity; }

  // Everything the draw tokens of one frame depend on. Equal states emit
  // identical draw tokens, so a recording of them can be replayed.
  struct FrameState
  {
    float resolutionX = 0.0f;
    float resolutionY = 0.0f;
    std::array<float, 16> mvp{};
    bool clip = false;
    std::array<int, 4> clipRect{};
    std::array<int, 5> enclosingClip{};
    uint64_t geometryRevision = 0;
    bool operator==(const FrameState&) const = default;
  };
  // Replaces the window and camera for one frame: Pixels space uses this
  // logical size as-is (no UI scale), World space this view projection.
  // Host visuals drawn for a guest use the guest frame's values.
  struct FrameOverride
  {
    float width = 0.0f;
    float height = 0.0f;
    bool hasWorldMvp = false;
    std::array<float, 16> worldMvp{};
  };
  // AppendCommands in two steps. prepareFrame rebuilds geometry when needed,
  // grows and uploads the meshes (direct tokens, never recorded) and fills
  // `state`. emitDraws pushes the clip and the batches, and may run inside a
  // Renderer recording.
  bool prepareFrame(Renderer* renderer,
                    FrameState* state,
                    const FrameOverride* frame = nullptr);
  bool emitDraws(Renderer* renderer, const FrameState& state);

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
  std::weak_ptr<const void> rendererLifetime;
  IRenderWindow* window = nullptr;
  Camera* camera = nullptr;
  PrimitiveSpace space = PrimitiveSpace::Pixels;
  RenderLayerId layerHint = RenderLayerId::World;
  Transform2D transform;
  Rect2 pixelClipRect;
  Rect2 geometryCullRect;
  bool pixelClipEnabled = false;
  bool geometryCullEnabled = false;

  std::vector<ShapePrimitive> shapes;
  std::vector<SpritePrimitive> sprites;
  std::vector<TextPrimitive> texts;
  std::vector<VisualItem> items;
  uint64_t nextSequence = 0;
  // Free slots of the per-kind lists left by setItem and removeItems.
  std::vector<size_t> freeShapes;
  std::vector<size_t> freeSprites;
  std::vector<size_t> freeTexts;
  // A backend took this visual whole (IBackend::AppendVisual) at least once,
  // so it must hear of its destruction.
  bool proxied = false;
  float opacity = 1.0f;
  uint64_t geometryRevision = 0;
  // Retained scratch: cleared text strings reused by addText (bounded) and
  // the draw-ordered item list rebuilt with the geometry.
  static constexpr size_t kSpareTextContent = 256;
  std::vector<std::string> spareTextContent;
  std::vector<VisualItem> orderedItems;

  MeshHandle shapeMeshHandle{};
  MeshHandle spriteMeshHandle{};
  bool gpuReady = false;
  bool geometryDirty = true;
  bool pendingFontGeometry = false;
  bool shapeUploadPending = false;
  bool spriteUploadPending = false;
  bool capacityWarningLogged = false;
  bool geometryTruncated = false;

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
  ColorRgba faded(ColorRgba color) const;
  template<typename Primitive>
  bool placeItem(size_t index,
                 VisualItemKind kind,
                 std::vector<Primitive>& list,
                 std::vector<size_t>& freeSlots,
                 const Primitive& value);
  std::array<int, 2> frameDimensions(Renderer* renderer, bool* fromFrame) const;
  void orphan(const VisualItem& item);
  void enrollGpuResources();
  void rebuildGeometry(const Rect2* cullRect);
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
  bool quadOutsideCullRect(Point2 p0, Point2 p1, Point2 p2, Point2 p3) const;
  bool pushShapeQuad(Point2 p0,
                     Point2 p1,
                     Point2 p2,
                     Point2 p3,
                     ColorRgba color);
  bool pushShapeQuadColors(Point2 p0,
                           Point2 p1,
                           Point2 p2,
                           Point2 p3,
                           const std::array<ColorRgba, 4>& colors);
  bool pushGradientQuad(const ShapePrimitive& shape, const Rect2& hostBounds);
  bool pushLineAsQuad(Point2 p0,
                      Point2 p1,
                      float width,
                      ColorRgba color,
                      const Rect2& hostBounds);
  bool pushSpriteQuad(const SpritePrimitive& sprite, const Rect2& hostBounds);
  // One pass of `text` in `font`. With a partner, glyph advances blend toward
  // the partner's by `blend` and each glyph centers in its blended cell;
  // `alpha` scales the text color's alpha.
  bool pushTextRun(const TextPrimitive& text,
                   const Font& font,
                   const Font* partner,
                   float blend,
                   float alpha,
                   const Rect2& hostBounds);
  size_t appendShape(const ShapePrimitive& shape);
  bool pushFilledEllipse(const ShapePrimitive& shape, const Rect2& hostBounds);
  bool pushFilledTriangle(const ShapePrimitive& shape, const Rect2& hostBounds);
  void appendBatch(BatchKind kind,
                   RenderStyleHandle styleHandle,
                   TextureHandle textureHandle,
                   unsigned int firstQuad,
                   unsigned int quadCount);
};
