#pragma once

#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/IMesh.h>
#include <Illumo/Rendering/ISceneRenderAttachment.h>
#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <Illumo/Scene/Transform3D.h>
#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

class Camera;
class Renderer;

enum class MeshFacing : unsigned char
{
  World = 0,
  Billboard = 1,
};

// World mesh host: colored lines/triangles and textured quads on the canonical
// uMVP look. One SceneGraph attachment or one World drawable. A sprite is a
// textured quad; MeshFacing::Billboard faces the camera.
class MeshVisual
  : public DrawableBase
  , public ISceneRenderAttachment
{
public:
  MeshVisual();
  ~MeshVisual() override;

  MeshVisual(const MeshVisual&) = delete;
  MeshVisual& operator=(const MeshVisual&) = delete;
  MeshVisual(MeshVisual&&) = delete;
  MeshVisual& operator=(MeshVisual&&) = delete;

  void prepare(Renderer* renderer);
  void setModelMatrix(const glm::mat4& value);
  const glm::mat4& getModelMatrix() const { return modelMatrix; }

  void clearPrimitives();
  size_t addQuad(const glm::vec3& center,
                 const glm::vec2& size,
                 ColorRgba color);
  size_t addSprite(TextureHandle textureHandle,
                   const glm::vec3& center,
                   const glm::vec2& size,
                   ColorRgba tint = ColorRgba{},
                   MeshFacing facing = MeshFacing::World,
                   const TextureRegion& region = TextureRegion{});
  void addLine(const glm::vec3& start, const glm::vec3& end, ColorRgba color);
  void addAxes(const glm::vec3& origin = glm::vec3(0.0f), float length = 1.0f);
  void addGrid(int halfLineCount,
               float spacing = 1.0f,
               ColorRgba color = ColorRgba{ 96, 96, 96, 255 });
  void addWireCube(const glm::vec3& center,
                   const glm::vec3& halfExtent,
                   ColorRgba color = ColorRgba{ 255, 255, 255, 255 });
  void addSolidCube(const glm::vec3& center,
                    const glm::vec3& halfExtent,
                    ColorRgba color = ColorRgba{ 200, 200, 200, 255 });
  void addSolidPyramid(const glm::vec3& center,
                       const glm::vec3& halfExtent,
                       ColorRgba color = ColorRgba{ 200, 180, 120, 255 });
  void addWireSphere(const glm::vec3& center,
                     float radius,
                     ColorRgba color = ColorRgba{ 180, 220, 255, 255 });
  void addSolidTriangle(const glm::vec3& a,
                        const glm::vec3& b,
                        const glm::vec3& c,
                        ColorRgba color);
  void addSolidEllipse(const glm::vec3& center,
                       const glm::vec2& radius,
                       ColorRgba color = ColorRgba{ 180, 220, 255, 255 },
                       int segments = 24);

  size_t spriteCount() const { return sprites.size(); }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;
  void appendSceneCommands(Renderer* renderer,
                           const Matrix4& worldTransform) override;

private:
  struct ColorVertex
  {
    float x;
    float y;
    float z;
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
  };

  struct SpriteVertex
  {
    float x;
    float y;
    float z;
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
    float u;
    float v;
  };

  struct SpriteItem
  {
    TextureHandle textureHandle{};
    Transform3D local;
    ColorRgba tint;
    MeshFacing facing = MeshFacing::World;
    TextureRegion region;
  };

  Renderer* renderer = nullptr;
  glm::mat4 modelMatrix = glm::mat4(1.0f);
  std::vector<ColorVertex> lineVertices;
  std::vector<unsigned int> lineIndices;
  std::vector<ColorVertex> triangleVertices;
  std::vector<unsigned int> triangleIndices;
  std::vector<ColorVertex> lineDrawVertices;
  std::vector<ColorVertex> triangleDrawVertices;
  std::vector<unsigned int> lineMeshIndices;
  std::vector<unsigned int> triangleMeshIndices;
  std::vector<SpriteItem> sprites;
  std::vector<SpriteVertex> spriteVertices;
  std::vector<unsigned int> spriteMeshIndices;
  MeshHandle lineMeshHandle{};
  MeshHandle triangleMeshHandle{};
  MeshHandle spriteMeshHandle{};
  RenderStyleHandle lineStyleHandle{};
  RenderStyleHandle triangleStyleHandle{};
  RenderStyleHandle spriteStyleHandle{};
  size_t lineMeshCapacity = 0;
  size_t triangleMeshCapacity = 0;
  size_t spriteMeshCapacity = 0;
  bool geometryDirty = true;
  bool lineUploadPending = false;
  bool triangleUploadPending = false;
  bool spriteUploadPending = false;

  void ensureStyles();
  bool appendCommandsWithWorld(Renderer* renderer, const glm::mat4& nodeWorld);
  void rebuildMeshes();
  bool ensureMeshCapacity(MeshHandle* meshHandle,
                          std::vector<unsigned int>* meshIndices,
                          size_t* capacity,
                          size_t required,
                          MeshVertexLayout layout);
  static void expandIndexedVertices(const std::vector<ColorVertex>& vertices,
                                    const std::vector<unsigned int>& indices,
                                    std::vector<ColorVertex>* drawVertices);
  void releaseMeshes();
  void releaseStyles();
  bool resolveViewProjection(Renderer* renderer,
                             glm::mat4* viewProjection,
                             glm::mat4* view) const;
};
