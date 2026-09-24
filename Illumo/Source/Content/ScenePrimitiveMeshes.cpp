#include "ScenePrimitiveMeshes.h"

#include <cmath>

static void
addVertex(MeshData& mesh, const glm::vec3& position, const glm::vec3& normal)
{
  MeshVertex vertex;
  vertex.position = position;
  vertex.normal = normal;
  vertex.texCoords =
    glm::vec2(position.x * 0.5f + 0.5f, position.y * 0.5f + 0.5f);
  vertex.color = glm::vec4(1.0f);
  mesh.vertices.push_back(vertex);
}

static void
addTriangle(MeshData& mesh,
            const glm::vec3& a,
            const glm::vec3& b,
            const glm::vec3& c)
{
  const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
  const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
  addVertex(mesh, a, normal);
  addVertex(mesh, b, normal);
  addVertex(mesh, c, normal);
  mesh.indices.push_back(base);
  mesh.indices.push_back(base + 1);
  mesh.indices.push_back(base + 2);
}

// Both windings so a flat shape shows from either side whatever the culling.
static void
addFlatTriangle(MeshData& mesh,
                const glm::vec3& a,
                const glm::vec3& b,
                const glm::vec3& c)
{
  addTriangle(mesh, a, b, c);
  addTriangle(mesh, a, c, b);
}

static void
addQuad(MeshData& mesh,
        const glm::vec3& a,
        const glm::vec3& b,
        const glm::vec3& c,
        const glm::vec3& d)
{
  addTriangle(mesh, a, b, c);
  addTriangle(mesh, a, c, d);
}

int
scenePrimitiveMeshSlot(ScenePrimitiveShape shape)
{
  switch (shape) {
    case ScenePrimitiveShape::Rect:
      return 0;
    case ScenePrimitiveShape::Ellipse:
      return 1;
    case ScenePrimitiveShape::Triangle:
      return 2;
    case ScenePrimitiveShape::Cube:
      return 3;
    case ScenePrimitiveShape::Pyramid:
      return 4;
    case ScenePrimitiveShape::Sphere:
      return 5;
    case ScenePrimitiveShape::WireCube:
    case ScenePrimitiveShape::WireSphere:
      return -1;
  }
  return -1;
}

bool
buildScenePrimitiveMesh(ScenePrimitiveShape shape, MeshData& mesh)
{
  mesh.clear();
  switch (shape) {
    case ScenePrimitiveShape::Rect:
      addFlatTriangle(mesh,
                      glm::vec3(-1.0f, -1.0f, 0.0f),
                      glm::vec3(1.0f, -1.0f, 0.0f),
                      glm::vec3(1.0f, 1.0f, 0.0f));
      addFlatTriangle(mesh,
                      glm::vec3(-1.0f, -1.0f, 0.0f),
                      glm::vec3(1.0f, 1.0f, 0.0f),
                      glm::vec3(-1.0f, 1.0f, 0.0f));
      break;
    case ScenePrimitiveShape::Ellipse: {
      const int segments = 48;
      for (int index = 0; index < segments; ++index) {
        const float first = 6.28318530718f * static_cast<float>(index) /
                            static_cast<float>(segments);
        const float second = 6.28318530718f * static_cast<float>(index + 1) /
                             static_cast<float>(segments);
        addFlatTriangle(mesh,
                        glm::vec3(0.0f),
                        glm::vec3(std::cos(first), std::sin(first), 0.0f),
                        glm::vec3(std::cos(second), std::sin(second), 0.0f));
      }
      break;
    }
    case ScenePrimitiveShape::Triangle:
      addFlatTriangle(mesh,
                      glm::vec3(-1.0f, -1.0f, 0.0f),
                      glm::vec3(1.0f, -1.0f, 0.0f),
                      glm::vec3(0.0f, 1.0f, 0.0f));
      break;
    case ScenePrimitiveShape::Cube: {
      const glm::vec3 p[8] = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 },
                               { -1, 1, -1 },  { -1, -1, 1 }, { 1, -1, 1 },
                               { 1, 1, 1 },    { -1, 1, 1 } };
      addQuad(mesh, p[4], p[5], p[6], p[7]); // +Z
      addQuad(mesh, p[1], p[0], p[3], p[2]); // -Z
      addQuad(mesh, p[5], p[1], p[2], p[6]); // +X
      addQuad(mesh, p[0], p[4], p[7], p[3]); // -X
      addQuad(mesh, p[7], p[6], p[2], p[3]); // +Y
      addQuad(mesh, p[0], p[1], p[5], p[4]); // -Y
      break;
    }
    case ScenePrimitiveShape::Pyramid: {
      const glm::vec3 apex(0.0f, 1.0f, 0.0f);
      const glm::vec3 b[4] = {
        { -1, -1, -1 }, { 1, -1, -1 }, { 1, -1, 1 }, { -1, -1, 1 }
      };
      addQuad(mesh, b[0], b[1], b[2], b[3]); // base, facing -Y
      addTriangle(mesh, b[3], b[2], apex);
      addTriangle(mesh, b[2], b[1], apex);
      addTriangle(mesh, b[1], b[0], apex);
      addTriangle(mesh, b[0], b[3], apex);
      break;
    }
    case ScenePrimitiveShape::Sphere: {
      const int rings = 16;
      const int sectors = 24;
      for (int ring = 0; ring <= rings; ++ring) {
        const float phi =
          3.14159265359f * static_cast<float>(ring) / static_cast<float>(rings);
        for (int sector = 0; sector <= sectors; ++sector) {
          const float theta = 6.28318530718f * static_cast<float>(sector) /
                              static_cast<float>(sectors);
          const glm::vec3 point(std::sin(phi) * std::cos(theta),
                                std::cos(phi),
                                std::sin(phi) * std::sin(theta));
          MeshVertex vertex;
          vertex.position = point;
          vertex.normal = point;
          vertex.texCoords =
            glm::vec2(static_cast<float>(sector) / static_cast<float>(sectors),
                      static_cast<float>(ring) / static_cast<float>(rings));
          mesh.vertices.push_back(vertex);
        }
      }
      for (int ring = 0; ring < rings; ++ring) {
        for (int sector = 0; sector < sectors; ++sector) {
          const uint32_t a =
            static_cast<uint32_t>(ring * (sectors + 1) + sector);
          const uint32_t b = a + static_cast<uint32_t>(sectors + 1);
          mesh.indices.insert(mesh.indices.end(),
                              { a, a + 1, b, b, a + 1, b + 1 });
        }
      }
      break;
    }
    case ScenePrimitiveShape::WireCube:
    case ScenePrimitiveShape::WireSphere:
      return false;
  }
  mesh.computeBounds();
  return true;
}
