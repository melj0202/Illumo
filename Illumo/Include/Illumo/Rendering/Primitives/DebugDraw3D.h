#pragma once

#include <Illumo/Rendering/Drawable.h>
#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <Illumo/Rendering/ResourceHandle.h>
#include <glm/glm.hpp>
#include <vector>

class Renderer;

// Small token-based 3D diagnostic drawable. It deliberately owns no camera,
// material, or asset policy: callers supply the complete view-projection
// matrix and compose it in the World layer before 2D presentation.
class DebugDraw3D : public DrawableBase
{
public:
  DebugDraw3D();
  ~DebugDraw3D() override;

  DebugDraw3D(const DebugDraw3D&) = delete;
  DebugDraw3D& operator=(const DebugDraw3D&) = delete;
  DebugDraw3D(DebugDraw3D&&) = delete;
  DebugDraw3D& operator=(DebugDraw3D&&) = delete;

  void prepare(Renderer* renderer);
  void setViewProjection(const glm::mat4& value);
  const glm::mat4& getViewProjection() const { return viewProjection; }
  void setModelMatrix(const glm::mat4& value);
  const glm::mat4& getModelMatrix() const { return modelMatrix; }

  void clearPrimitives();
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

  static glm::mat4 makePerspectiveViewProjection(const glm::vec3& eye,
                                                 const glm::vec3& target,
                                                 const glm::vec3& up,
                                                 float fieldOfViewDegrees,
                                                 float aspectRatio,
                                                 float nearPlane,
                                                 float farPlane);

  void Draw() override {}
  bool AppendCommands(Renderer* renderer) override;

private:
  struct Vertex
  {
    float x;
    float y;
    float z;
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
  };

  Renderer* renderer = nullptr;
  glm::mat4 viewProjection = glm::mat4(1.0f);
  glm::mat4 modelMatrix = glm::mat4(1.0f);
  std::vector<Vertex> lineVertices;
  std::vector<unsigned int> lineIndices;
  std::vector<Vertex> triangleVertices;
  std::vector<unsigned int> triangleIndices;
  MeshHandle lineMeshHandle{};
  MeshHandle triangleMeshHandle{};
  RenderStyleHandle lineStyleHandle{};
  RenderStyleHandle triangleStyleHandle{};
  bool geometryDirty = true;

  void addLine(const glm::vec3& start, const glm::vec3& end, ColorRgba color);
  void ensureStyles();
  void rebuildMeshes();
  void releaseMeshes();
  void releaseStyles();
};
