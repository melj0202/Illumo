#include "VulkanMesh.h"

VulkanMesh::VulkanMesh(const void* vertices,
                       size_t vertexSize,
                       const void* indices,
                       size_t indexSize,
                       MeshVertexLayout layout,
                       bool dynamic)
  : _layout(layout)
  , _dynamic(dynamic)
{
  (void)vertices;
  (void)vertexSize;
  (void)indices;
  (void)indexSize;
}

VulkanMesh::~VulkanMesh()
{
  Destroy();
}

void
VulkanMesh::Destroy()
{
}
