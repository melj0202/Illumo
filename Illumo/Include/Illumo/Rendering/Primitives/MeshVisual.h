#pragma once

#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/IMesh.h>
#include <Illumo/Rendering/ISceneRenderAttachment.h>
#include <Illumo/Rendering/MeshData.h>
#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <Illumo/Scene/Transform3D.h>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

class Camera;
class Renderer;
struct MeshAssetInfo;

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
  void setLightingEnabled(bool enabled) { lightingEnabled = enabled; }
  bool isLightingEnabled() const { return lightingEnabled; }
  void setLightDirection(const glm::vec3& direction);
  const glm::vec3& getLightDirection() const { return lightDirection; }
  void setLightColor(const glm::vec3& color);
  const glm::vec3& getLightColor() const { return lightColor; }
  void setAmbientColor(const glm::vec3& color);
  const glm::vec3& getAmbientColor() const { return ambientColor; }
  void setShadowsEnabled(bool enabled) { shadowsEnabled = enabled; }
  bool isShadowsEnabled() const { return shadowsEnabled; }
  void setShadowMapSize(int size);
  int getShadowMapSize() const { return shadowMapSize; }
  void setShadowRadius(float radius);
  float getShadowRadius() const { return shadowRadius; }
  void setLightDistance(float distance);
  float getLightDistance() const { return lightDistance; }
  void setShadowCasterDistance(float distance);
  float getShadowCasterDistance() const { return shadowCasterDistance; }
  void setShadowBias(float bias);
  float getShadowBias() const { return shadowBias; }
  void setShadowSlopeScale(float scale);
  float getShadowSlopeScale() const { return shadowSlopeScale; }
  void setShadowNormalOffset(float offset);
  float getShadowNormalOffset() const { return shadowNormalOffset; }
  void setShadowPcfEnabled(bool enabled) { shadowPcfEnabled = enabled; }
  bool isShadowPcfEnabled() const { return shadowPcfEnabled; }
  void setMotionBlurEnabled(bool enabled) { motionBlurEnabled = enabled; }
  bool isMotionBlurEnabled() const { return motionBlurEnabled; }
  void setMotionBlurAmount(float amount);
  float getMotionBlurAmount() const { return motionBlurAmount; }
  void setMotionBlurMax(float ndcUnits);
  float getMotionBlurMax() const { return motionBlurMax; }

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
  void addMesh(const MeshData& mesh,
               ColorRgba tint = ColorRgba{ 255, 255, 255, 255 });
  // Non-owning managed binding. The acquiring owner must keep the matching
  // AssetManager reference alive until this binding is cleared or unused.
  void setMeshAsset(const MeshAssetInfo& asset,
                    ColorRgba tint = ColorRgba{ 255, 255, 255, 255 });
  void clearMeshAsset();
  MeshHandle getMeshAssetHandle() const { return meshAssetHandle; }

  size_t spriteCount() const { return sprites.size(); }

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;
  void CollectShadowCasters(Renderer* renderer) override;
  void AppendShadowCommands(Renderer* renderer) override;
  void appendSceneCommands(Renderer* renderer,
                           const Matrix4& worldTransform) override;
  bool getSceneLocalBounds(AxisAlignedBounds3* bounds) const override;
  uint64_t getSceneBoundsRevision() const override { return boundsRevision; }
  void collectSceneShadowCasters(Renderer* renderer,
                                 const Matrix4& worldTransform) override;
  void appendSceneShadowCommands(Renderer* renderer,
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

  struct LitVertex
  {
    float x;
    float y;
    float z;
    float nx = 0.0f;
    float ny = 1.0f;
    float nz = 0.0f;
    unsigned char r = 255;
    unsigned char g = 255;
    unsigned char b = 255;
    unsigned char a = 255;
    float u = 0.0f;
    float v = 0.0f;
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
  std::vector<LitVertex> triangleVertices;
  std::vector<unsigned int> triangleIndices;
  std::vector<ColorVertex> lineDrawVertices;
  std::vector<unsigned int> lineMeshIndices;
  std::vector<SpriteItem> sprites;
  std::vector<SpriteVertex> spriteVertices;
  std::vector<unsigned int> spriteMeshIndices;
  MeshHandle lineMeshHandle{};
  MeshHandle triangleMeshHandle{};
  MeshHandle spriteMeshHandle{};
  MeshHandle meshAssetHandle{};
  unsigned int meshAssetIndexCount = 0;
  ColorRgba meshAssetTint{ 255, 255, 255, 255 };
  glm::vec3 meshAssetBoundsMin = glm::vec3(0.0f);
  glm::vec3 meshAssetBoundsMax = glm::vec3(0.0f);
  bool meshAssetBoundsValid = false;
  RenderStyleHandle lineStyleHandle{};
  RenderStyleHandle triangleStyleHandle{};
  RenderStyleHandle spriteStyleHandle{};
  RenderStyleHandle litMeshStyleHandle{};
  size_t lineMeshCapacity = 0;
  size_t triangleVertexCapacity = 0;
  size_t triangleIndexCapacity = 0;
  size_t spriteMeshCapacity = 0;
  bool geometryDirty = true;
  uint64_t boundsRevision = 1;
  bool lineUploadPending = false;
  bool triangleUploadPending = false;
  bool spriteUploadPending = false;
  bool lightingEnabled = true;
  bool shadowsEnabled = true;
  bool shadowPcfEnabled = true;
  bool motionBlurEnabled = true;
  bool hasPreviousMvp = false;
  glm::mat4 previousColoredMvp = glm::mat4(1.0f);
  uint64_t previousFrameSerial = 0;
  float motionBlurAmount = 0.5f;
  float motionBlurMax = 0.2f;
  glm::vec3 lineBoundsMin = glm::vec3(0.0f);
  glm::vec3 lineBoundsMax = glm::vec3(0.0f);
  bool lineBoundsValid = false;
  glm::vec3 triangleBoundsMin = glm::vec3(0.0f);
  glm::vec3 triangleBoundsMax = glm::vec3(0.0f);
  bool triangleBoundsValid = false;
  glm::vec3 spriteBoundsMin = glm::vec3(0.0f);
  glm::vec3 spriteBoundsMax = glm::vec3(0.0f);
  bool spriteBoundsValid = false;
  bool geometryBoundsReliable = true;
  glm::vec3 lightDirection = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));
  glm::vec3 lightColor = glm::vec3(1.0f, 0.95f, 0.9f);
  glm::vec3 ambientColor = glm::vec3(0.2f, 0.22f, 0.25f);
  int shadowMapSize = 1024;
  float shadowRadius = 2.5f;
  float lightDistance = 8.0f;
  float shadowCasterDistance = 100.0f;
  float shadowBias = 0.001f;
  float shadowSlopeScale = 0.004f;
  float shadowNormalOffset = 0.015f;

  void ensureStyles();
  bool prepareForCommands(Renderer* renderer);
  void collectShadowCasterWithWorld(Renderer* renderer,
                                    const glm::mat4& nodeWorld);
  void appendShadowCommandsWithWorld(Renderer* renderer,
                                     const glm::mat4& nodeWorld);
  bool appendCommandsWithWorld(Renderer* renderer,
                               const glm::mat4& nodeWorld,
                               bool cameraCull = true);
  bool getLocalShadowBounds(AxisAlignedBounds3* bounds) const;
  bool getWorldShadowBounds(const glm::mat4& nodeWorld,
                            AxisAlignedBounds3* bounds) const;
  void rebuildMeshes();
  bool ensureMeshCapacity(MeshHandle* meshHandle,
                          std::vector<unsigned int>* meshIndices,
                          size_t* capacity,
                          size_t required,
                          MeshVertexLayout layout);
  bool ensureTriangleMeshCapacity(size_t requiredVertices,
                                  size_t requiredIndices);
  static void expandIndexedVertices(const std::vector<ColorVertex>& vertices,
                                    const std::vector<unsigned int>& indices,
                                    std::vector<ColorVertex>* drawVertices);
  void releaseMeshes();
  void releaseStyles();
  bool resolveViewProjection(Renderer* renderer,
                             glm::mat4* viewProjection,
                             glm::mat4* view) const;
};
