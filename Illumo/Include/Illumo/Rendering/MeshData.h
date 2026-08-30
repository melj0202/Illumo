#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct MeshVertex
{
  glm::vec3 position = glm::vec3(0.0f);
  glm::vec3 normal = glm::vec3(0.0f, 0.0f, 1.0f);
  glm::vec2 texCoords = glm::vec2(0.0f);
  glm::vec4 color = glm::vec4(1.0f);
};

struct MeshMaterial
{
  std::string name;
  glm::vec3 ambient = glm::vec3(0.1f);
  glm::vec3 diffuse = glm::vec3(0.7f);
  glm::vec3 specular = glm::vec3(1.0f);
  float shininess = 32.0f;
  std::string diffuseTexturePath;
  std::string specularTexturePath;
  std::string normalTexturePath;
};

struct MeshSubmesh
{
  std::string name;
  int materialIndex = -1;
  uint32_t indexOffset = 0;
  uint32_t indexCount = 0;
};

struct MeshData
{
  std::vector<MeshVertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<MeshSubmesh> submeshes;
  std::vector<MeshMaterial> materials;
  glm::vec3 minBounds = glm::vec3(0.0f);
  glm::vec3 maxBounds = glm::vec3(0.0f);

  bool isEmpty() const { return vertices.empty(); }
  void clear();
  void computeBounds();
  void computeNormalsIfMissing();
  void centerAndScale(float targetRadius = 1.0f);
  std::vector<float> toPos3Color3Uv2() const;
};
