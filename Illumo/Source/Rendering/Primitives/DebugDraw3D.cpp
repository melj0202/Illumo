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
  return appendCommandsWithModel(value, modelMatrix);
}

void
DebugDraw3D::appendSceneCommands(Renderer* value, const Matrix4& worldTransform)
{
  (void)appendCommandsWithModel(value, worldTransform * modelMatrix);
}

bool
DebugDraw3D::appendCommandsWithModel(Renderer* value,
                                     const glm::mat4& activeModelMatrix)
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
  if (!ensureMeshCapacity(&lineMeshHandle,
                          &lineMeshIndices,
                          &lineMeshCapacity,
                          lineDrawVertices.size()) ||
      !ensureMeshCapacity(&triangleMeshHandle,
                          &triangleMeshIndices,
                          &triangleMeshCapacity,
                          triangleDrawVertices.size())) {
    return false;
  }

  if (lineUploadPending) {
    value->pushUpdateBuffer(
      lineMeshHandle,
      0,
      static_cast<unsigned int>(lineDrawVertices.size() * sizeof(Vertex)),
      lineDrawVertices.data());
    lineUploadPending = false;
  }
  if (triangleUploadPending) {
    value->pushUpdateBuffer(
      triangleMeshHandle,
      0,
      static_cast<unsigned int>(triangleDrawVertices.size() * sizeof(Vertex)),
      triangleDrawVertices.data());
    triangleUploadPending = false;
  }

  const glm::mat4 modelViewProjection = viewProjection * activeModelMatrix;
  const float* matrix = glm::value_ptr(modelViewProjection);
  if (!lineDrawVertices.empty() && lineMeshHandle.isValid()) {
    if (!value->bindStyle(lineStyleHandle)) {
      return false;
    }
    value->pushSetMesh(lineMeshHandle);
    // Shape is shared with 2D UI, which leaves this shader in pixel mode.
    // Reassert world mode for every 3D draw instead of relying on prior state.
    value->pushUniformInt("uUsePixels", 0);
    value->pushUniformMat4("uMVP", matrix);
    value->pushDrawIndexed(static_cast<unsigned int>(lineDrawVertices.size()));
  }
  if (!triangleDrawVertices.empty() && triangleMeshHandle.isValid()) {
    if (!value->bindStyle(triangleStyleHandle)) {
      return false;
    }
    value->pushSetMesh(triangleMeshHandle);
    value->pushUniformInt("uUsePixels", 0);
    value->pushUniformMat4("uMVP", matrix);
    value->pushDrawIndexed(
      static_cast<unsigned int>(triangleDrawVertices.size()));
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
  expandIndexedVertices(lineVertices, lineIndices, &lineDrawVertices);
  expandIndexedVertices(
    triangleVertices, triangleIndices, &triangleDrawVertices);
  lineUploadPending = !lineDrawVertices.empty();
  triangleUploadPending = !triangleDrawVertices.empty();
  geometryDirty = false;
}

bool
DebugDraw3D::ensureMeshCapacity(MeshHandle* meshHandle,
                                std::vector<unsigned int>* meshIndices,
                                size_t* capacity,
                                size_t required)
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

  const size_t vertexCapacityBytes = nextCapacity * sizeof(Vertex);
  const size_t indexBytes = meshIndices->size() * sizeof(unsigned int);
  if (meshHandle->isValid()) {
    if (!renderer->replaceDynamicMesh(*meshHandle,
                                      vertexCapacityBytes,
                                      meshIndices->data(),
                                      indexBytes,
                                      MeshVertexLayout::Pos3Color4U8)) {
      return false;
    }
  } else {
    *meshHandle = renderer->enrollDynamicMesh(vertexCapacityBytes,
                                              meshIndices->data(),
                                              indexBytes,
                                              MeshVertexLayout::Pos3Color4U8);
    if (!meshHandle->isValid()) {
      return false;
    }
  }
  *capacity = nextCapacity;
  return true;
}

void
DebugDraw3D::expandIndexedVertices(const std::vector<Vertex>& vertices,
                                   const std::vector<unsigned int>& indices,
                                   std::vector<Vertex>* drawVertices)
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
  lineMeshCapacity = 0;
  triangleMeshCapacity = 0;
  lineMeshIndices.clear();
  triangleMeshIndices.clear();
  lineUploadPending = false;
  triangleUploadPending = false;
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
