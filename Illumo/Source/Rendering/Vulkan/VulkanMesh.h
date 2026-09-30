#pragma once
#include <Illumo/Rendering/IMesh.h>

// Stub mesh resource; VulkanBackend does not yet enroll any GPU buffer, so
// every instance reports invalid.
class VulkanMesh : public IMesh
{
public:
  VulkanMesh(const void* vertices,
             size_t vertexSize,
             const void* indices,
             size_t indexSize,
             MeshVertexLayout layout,
             bool dynamic);
  ~VulkanMesh() override;
  VulkanMesh(const VulkanMesh&) = delete;
  VulkanMesh& operator=(const VulkanMesh&) = delete;
  VulkanMesh(VulkanMesh&&) = delete;
  VulkanMesh& operator=(VulkanMesh&&) = delete;

  bool isValid() const { return false; }
  MeshVertexLayout getLayout() const { return _layout; }
  void Destroy() override;

private:
  MeshVertexLayout _layout;
  bool _dynamic;
};
