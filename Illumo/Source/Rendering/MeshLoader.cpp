#include <Illumo/Rendering/MeshLoader.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tinyobjloader/tiny_obj_loader.h>

void
MeshData::clear()
{
  vertices.clear();
  indices.clear();
  submeshes.clear();
  materials.clear();
  minBounds = glm::vec3(0.0f);
  maxBounds = glm::vec3(0.0f);
}

void
MeshData::computeBounds()
{
  if (vertices.empty()) {
    minBounds = glm::vec3(0.0f);
    maxBounds = glm::vec3(0.0f);
    return;
  }

  glm::vec3 minimum(std::numeric_limits<float>::max());
  glm::vec3 maximum(std::numeric_limits<float>::lowest());

  for (size_t i = 0; i < vertices.size(); ++i) {
    const glm::vec3& pos = vertices[i].position;
    minimum.x = std::min(minimum.x, pos.x);
    minimum.y = std::min(minimum.y, pos.y);
    minimum.z = std::min(minimum.z, pos.z);
    maximum.x = std::max(maximum.x, pos.x);
    maximum.y = std::max(maximum.y, pos.y);
    maximum.z = std::max(maximum.z, pos.z);
  }

  minBounds = minimum;
  maxBounds = maximum;
}

void
MeshData::computeNormalsIfMissing()
{
  if (vertices.empty() || indices.empty()) {
    return;
  }

  bool hasMissingNormals = false;
  for (size_t i = 0; i < vertices.size(); ++i) {
    float lengthSq = glm::dot(vertices[i].normal, vertices[i].normal);
    if (lengthSq < 0.0001f) {
      hasMissingNormals = true;
      break;
    }
  }

  if (!hasMissingNormals) {
    return;
  }

  std::vector<glm::vec3> accumulatedNormals(vertices.size(), glm::vec3(0.0f));

  size_t triangleCount = indices.size() / 3;
  for (size_t t = 0; t < triangleCount; ++t) {
    uint32_t i0 = indices[t * 3 + 0];
    uint32_t i1 = indices[t * 3 + 1];
    uint32_t i2 = indices[t * 3 + 2];

    if (i0 >= vertices.size() || i1 >= vertices.size() ||
        i2 >= vertices.size()) {
      continue;
    }

    const glm::vec3& p0 = vertices[i0].position;
    const glm::vec3& p1 = vertices[i1].position;
    const glm::vec3& p2 = vertices[i2].position;

    glm::vec3 edge1 = p1 - p0;
    glm::vec3 edge2 = p2 - p0;
    glm::vec3 faceNormal = glm::cross(edge1, edge2);
    float length = glm::length(faceNormal);
    if (length > 0.00001f) {
      faceNormal /= length;
      accumulatedNormals[i0] += faceNormal;
      accumulatedNormals[i1] += faceNormal;
      accumulatedNormals[i2] += faceNormal;
    }
  }

  for (size_t i = 0; i < vertices.size(); ++i) {
    float lengthSq = glm::dot(vertices[i].normal, vertices[i].normal);
    if (lengthSq < 0.0001f) {
      float accLength = glm::length(accumulatedNormals[i]);
      if (accLength > 0.00001f) {
        vertices[i].normal = accumulatedNormals[i] / accLength;
      } else {
        vertices[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
      }
    }
  }
}

void
MeshData::centerAndScale(float targetRadius)
{
  if (vertices.empty()) {
    return;
  }

  computeBounds();
  glm::vec3 center = (minBounds + maxBounds) * 0.5f;

  float maxDistance = 0.0f;
  for (size_t i = 0; i < vertices.size(); ++i) {
    float dist = glm::length(vertices[i].position - center);
    if (dist > maxDistance) {
      maxDistance = dist;
    }
  }

  float scaleFactor = 1.0f;
  if (maxDistance > 0.00001f && targetRadius > 0.00001f) {
    scaleFactor = targetRadius / maxDistance;
  }

  for (size_t i = 0; i < vertices.size(); ++i) {
    vertices[i].position = (vertices[i].position - center) * scaleFactor;
  }

  computeBounds();
}

std::vector<float>
MeshData::toPos3Color3Uv2() const
{
  std::vector<float> result;
  result.reserve(vertices.size() * 8);

  for (size_t i = 0; i < vertices.size(); ++i) {
    const MeshVertex& vertex = vertices[i];
    result.push_back(vertex.position.x);
    result.push_back(vertex.position.y);
    result.push_back(vertex.position.z);
    result.push_back(vertex.color.r);
    result.push_back(vertex.color.g);
    result.push_back(vertex.color.b);
    result.push_back(vertex.texCoords.x);
    result.push_back(vertex.texCoords.y);
  }

  return result;
}

namespace {

struct VertexKey
{
  int vertexIndex;
  int normalIndex;
  int texcoordIndex;

  bool operator==(const VertexKey& other) const
  {
    return vertexIndex == other.vertexIndex &&
           normalIndex == other.normalIndex &&
           texcoordIndex == other.texcoordIndex;
  }
};

struct VertexKeyHash
{
  size_t operator()(const VertexKey& key) const
  {
    size_t h1 = std::hash<int>()(key.vertexIndex);
    size_t h2 = std::hash<int>()(key.normalIndex);
    size_t h3 = std::hash<int>()(key.texcoordIndex);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
  }
};

class TinyObjLoaderBackend : public IMeshLoaderBackend
{
public:
  bool loadFromFile(const std::string& filePath,
                    const MeshLoadOptions& options,
                    MeshLoadResult* outResult) override
  {
    if (outResult == nullptr) {
      return false;
    }

    outResult->success = false;
    outResult->error.clear();
    outResult->warning.clear();
    outResult->mesh.clear();

    tinyobj::ObjReaderConfig readerConfig;
    readerConfig.triangulate = options.triangulate;
    if (!options.materialSearchPath.empty()) {
      readerConfig.mtl_search_path = options.materialSearchPath;
    } else {
      std::filesystem::path path(filePath);
      readerConfig.mtl_search_path = path.parent_path().string();
    }

    tinyobj::ObjReader reader;
    if (!reader.ParseFromFile(filePath, readerConfig)) {
      outResult->error = reader.Error();
      outResult->warning = reader.Warning();
      return false;
    }

    outResult->warning = reader.Warning();
    return convertToMeshData(reader, options, outResult);
  }

  bool loadFromMemory(const std::string& fileContent,
                      const MeshLoadOptions& options,
                      const std::string& baseDir,
                      MeshLoadResult* outResult) override
  {
    if (outResult == nullptr) {
      return false;
    }

    outResult->success = false;
    outResult->error.clear();
    outResult->warning.clear();
    outResult->mesh.clear();

    tinyobj::ObjReaderConfig readerConfig;
    readerConfig.triangulate = options.triangulate;
    if (!options.materialSearchPath.empty()) {
      readerConfig.mtl_search_path = options.materialSearchPath;
    } else {
      readerConfig.mtl_search_path = baseDir;
    }

    tinyobj::ObjReader reader;
    if (!reader.ParseFromString(fileContent, "", readerConfig)) {
      outResult->error = reader.Error();
      outResult->warning = reader.Warning();
      return false;
    }

    outResult->warning = reader.Warning();
    return convertToMeshData(reader, options, outResult);
  }

private:
  bool convertToMeshData(const tinyobj::ObjReader& reader,
                         const MeshLoadOptions& options,
                         MeshLoadResult* outResult)
  {
    const tinyobj::attrib_t& attrib = reader.GetAttrib();
    const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();
    const std::vector<tinyobj::material_t>& materials = reader.GetMaterials();

    MeshData& mesh = outResult->mesh;

    // Convert materials
    for (size_t m = 0; m < materials.size(); ++m) {
      const tinyobj::material_t& srcMat = materials[m];
      MeshMaterial mat;
      mat.name = srcMat.name;
      mat.ambient =
        glm::vec3(srcMat.ambient[0], srcMat.ambient[1], srcMat.ambient[2]);
      mat.diffuse =
        glm::vec3(srcMat.diffuse[0], srcMat.diffuse[1], srcMat.diffuse[2]);
      mat.specular =
        glm::vec3(srcMat.specular[0], srcMat.specular[1], srcMat.specular[2]);
      mat.shininess = srcMat.shininess;
      mat.diffuseTexturePath = srcMat.diffuse_texname;
      mat.specularTexturePath = srcMat.specular_texname;
      mat.normalTexturePath = srcMat.bump_texname;
      mesh.materials.push_back(mat);
    }

    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> uniqueVertices;

    for (size_t s = 0; s < shapes.size(); ++s) {
      const tinyobj::shape_t& shape = shapes[s];
      MeshSubmesh submesh;
      submesh.name = shape.name;
      submesh.indexOffset = static_cast<uint32_t>(mesh.indices.size());

      if (!shape.mesh.material_ids.empty()) {
        submesh.materialIndex = shape.mesh.material_ids[0];
      }

      size_t indexOffset = 0;
      for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
        size_t faceVertices = shape.mesh.num_face_vertices[f];

        for (size_t v = 0; v < faceVertices; ++v) {
          tinyobj::index_t idx = shape.mesh.indices[indexOffset + v];
          VertexKey key{ idx.vertex_index,
                         idx.normal_index,
                         idx.texcoord_index };

          std::unordered_map<VertexKey, uint32_t, VertexKeyHash>::iterator it =
            uniqueVertices.find(key);
          if (it == uniqueVertices.end()) {
            MeshVertex vertex;
            if (idx.vertex_index >= 0) {
              vertex.position.x = attrib.vertices[3 * idx.vertex_index + 0];
              vertex.position.y = attrib.vertices[3 * idx.vertex_index + 1];
              vertex.position.z = attrib.vertices[3 * idx.vertex_index + 2];

              if (!attrib.colors.empty()) {
                vertex.color.r = attrib.colors[3 * idx.vertex_index + 0];
                vertex.color.g = attrib.colors[3 * idx.vertex_index + 1];
                vertex.color.b = attrib.colors[3 * idx.vertex_index + 2];
                vertex.color.a = 1.0f;
              }
            }

            if (idx.normal_index >= 0 && !attrib.normals.empty()) {
              vertex.normal.x = attrib.normals[3 * idx.normal_index + 0];
              vertex.normal.y = attrib.normals[3 * idx.normal_index + 1];
              vertex.normal.z = attrib.normals[3 * idx.normal_index + 2];
            }

            if (idx.texcoord_index >= 0 && !attrib.texcoords.empty()) {
              vertex.texCoords.x = attrib.texcoords[2 * idx.texcoord_index + 0];
              float texV = attrib.texcoords[2 * idx.texcoord_index + 1];
              vertex.texCoords.y =
                options.flipTexCoordsV ? (1.0f - texV) : texV;
            }

            uint32_t newIndex = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(vertex);
            uniqueVertices[key] = newIndex;
            mesh.indices.push_back(newIndex);
          } else {
            mesh.indices.push_back(it->second);
          }
        }
        indexOffset += faceVertices;
      }

      submesh.indexCount =
        static_cast<uint32_t>(mesh.indices.size()) - submesh.indexOffset;
      mesh.submeshes.push_back(submesh);
    }

    mesh.computeBounds();

    if (options.generateNormalsIfMissing) {
      mesh.computeNormalsIfMissing();
    }

    if (options.centerAndNormalize) {
      mesh.centerAndScale(options.targetRadius);
    }

    outResult->success = !mesh.vertices.empty();
    return outResult->success;
  }
};

static std::mutex g_backendMutex;
static std::shared_ptr<IMeshLoaderBackend> g_customBackend;

} // namespace

std::shared_ptr<IMeshLoaderBackend>
MeshLoader::getActiveBackend()
{
  std::lock_guard<std::mutex> lock(g_backendMutex);
  if (g_customBackend != nullptr) {
    return g_customBackend;
  }
  static std::shared_ptr<IMeshLoaderBackend> defaultBackend =
    std::make_shared<TinyObjLoaderBackend>();
  return defaultBackend;
}

MeshLoadResult
MeshLoader::loadFromFile(const std::string& filePath,
                         const MeshLoadOptions& options)
{
  MeshLoadResult result;
  std::shared_ptr<IMeshLoaderBackend> backend = getActiveBackend();
  if (backend != nullptr) {
    backend->loadFromFile(filePath, options, &result);
  } else {
    result.error = "No active mesh loader backend";
  }
  return result;
}

MeshLoadResult
MeshLoader::loadFromMemory(const std::string& fileContent,
                           const MeshLoadOptions& options,
                           const std::string& baseDir)
{
  MeshLoadResult result;
  std::shared_ptr<IMeshLoaderBackend> backend = getActiveBackend();
  if (backend != nullptr) {
    backend->loadFromMemory(fileContent, options, baseDir, &result);
  } else {
    result.error = "No active mesh loader backend";
  }
  return result;
}

void
MeshLoader::setCustomBackend(std::shared_ptr<IMeshLoaderBackend> backend)
{
  std::lock_guard<std::mutex> lock(g_backendMutex);
  g_customBackend = backend;
}

void
MeshLoader::resetBackend()
{
  std::lock_guard<std::mutex> lock(g_backendMutex);
  g_customBackend.reset();
}
