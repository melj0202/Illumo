#include <Illumo/Rendering/IMesh.h>
#include <Illumo/Rendering/Primitives/DebugDraw3D.h>
#include <Illumo/Rendering/Renderer.h>
#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

DebugDraw3D::DebugDraw3D() = default;

DebugDraw3D::~DebugDraw3D()
{
  releaseMeshes();
  releaseStyles();
}

void
DebugDraw3D::prepare(Renderer* value)
{
  if (value == nullptr || (renderer != nullptr && renderer != value)) {
    return;
  }
  renderer = value;
  ensureStyles();
}

void
DebugDraw3D::setViewProjection(const glm::mat4& value)
{
  viewProjection = value;
}

void
DebugDraw3D::setModelMatrix(const glm::mat4& value)
{
  modelMatrix = value;
}

void
DebugDraw3D::clearPrimitives()
{
  lineVertices.clear();
  lineIndices.clear();
  triangleVertices.clear();
  triangleIndices.clear();
  geometryDirty = true;
}

void
DebugDraw3D::addAxes(const glm::vec3& origin, float length)
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
DebugDraw3D::addGrid(int halfLineCount, float spacing, ColorRgba color)
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
DebugDraw3D::addWireCube(const glm::vec3& center,
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
DebugDraw3D::addSolidCube(const glm::vec3& center,
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

glm::mat4
DebugDraw3D::makePerspectiveViewProjection(const glm::vec3& eye,
                                           const glm::vec3& target,
                                           const glm::vec3& up,
                                           float fieldOfViewDegrees,
                                           float aspectRatio,
                                           float nearPlane,
                                           float farPlane)
{
  const float safeFieldOfView = std::clamp(fieldOfViewDegrees, 1.0f, 179.0f);
  const float safeAspectRatio = std::max(aspectRatio, 0.0001f);
  const float safeNearPlane = std::max(nearPlane, 0.0001f);
  const float safeFarPlane = std::max(farPlane, safeNearPlane + 0.0001f);
  return glm::perspective(glm::radians(safeFieldOfView),
                          safeAspectRatio,
                          safeNearPlane,
                          safeFarPlane) *
         glm::lookAt(eye, target, up);
}

bool
DebugDraw3D::AppendCommands(Renderer* value)
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
  if (!lineStyleHandle.isValid() || !triangleStyleHandle.isValid()) {
    return false;
  }
  if (geometryDirty) {
    rebuildMeshes();
  }

  const glm::mat4 modelViewProjection = viewProjection * modelMatrix;
  const float* matrix = glm::value_ptr(modelViewProjection);
  if (lineMeshHandle.isValid()) {
    if (!value->bindStyle(lineStyleHandle)) {
      return false;
    }
    value->pushSetMesh(lineMeshHandle);
    // Shape is shared with 2D UI, which leaves this shader in pixel mode.
    // Reassert world mode for every 3D draw instead of relying on prior state.
    value->pushUniformInt("uUsePixels", 0);
    value->pushUniformMat4("uMVP", matrix);
    value->pushDrawIndexed(static_cast<unsigned int>(lineIndices.size()));
  }
  if (triangleMeshHandle.isValid()) {
    if (!value->bindStyle(triangleStyleHandle)) {
      return false;
    }
    value->pushSetMesh(triangleMeshHandle);
    value->pushUniformInt("uUsePixels", 0);
    value->pushUniformMat4("uMVP", matrix);
    value->pushDrawIndexed(static_cast<unsigned int>(triangleIndices.size()));
  }
  return true;
}

void
DebugDraw3D::addLine(const glm::vec3& start,
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
DebugDraw3D::ensureStyles()
{
  if (renderer == nullptr ||
      (lineStyleHandle.isValid() && triangleStyleHandle.isValid())) {
    return;
  }
  const RenderStyle* shapeStyle = renderer->getStyle(RenderStyleId::Shape);
  if (shapeStyle == nullptr) {
    return;
  }
  if (!lineStyleHandle.isValid()) {
    RenderStyle lineStyle = *shapeStyle;
    lineStyle.pipeline.depthTestEnabled = true;
    lineStyle.pipeline.blendEnabled = false;
    lineStyle.pipeline.faceCullingEnabled = false;
    lineStyle.pipeline.primitives = Primitives::Lines;
    lineStyleHandle = renderer->createStyle(lineStyle);
  }
  if (!triangleStyleHandle.isValid()) {
    RenderStyle triangleStyle = *shapeStyle;
    triangleStyle.pipeline.depthTestEnabled = true;
    triangleStyle.pipeline.blendEnabled = false;
    triangleStyle.pipeline.faceCullingEnabled = false;
    triangleStyle.pipeline.primitives = Primitives::Triangles;
    triangleStyleHandle = renderer->createStyle(triangleStyle);
  }
}

void
DebugDraw3D::rebuildMeshes()
{
  releaseMeshes();
  if (renderer == nullptr) {
    return;
  }
  if (!lineVertices.empty()) {
    lineMeshHandle =
      renderer->enrollMesh(lineVertices.data(),
                           lineVertices.size() * sizeof(Vertex),
                           lineIndices.data(),
                           lineIndices.size() * sizeof(unsigned int),
                           MeshVertexLayout::Pos3Color4U8,
                           false);
  }
  if (!triangleVertices.empty()) {
    triangleMeshHandle =
      renderer->enrollMesh(triangleVertices.data(),
                           triangleVertices.size() * sizeof(Vertex),
                           triangleIndices.data(),
                           triangleIndices.size() * sizeof(unsigned int),
                           MeshVertexLayout::Pos3Color4U8,
                           false);
  }
  geometryDirty = false;
}

void
DebugDraw3D::releaseMeshes()
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
}

void
DebugDraw3D::releaseStyles()
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
}
