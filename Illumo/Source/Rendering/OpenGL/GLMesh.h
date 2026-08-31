#pragma once
#include <GL/glew.h>
#include <Illumo/Rendering/IMesh.h>
#include <cstring>
#include <vector>

class GLMesh : public IMesh
{
public:
  static const unsigned int kCanvasFloatsPerVertex = 8;
  static const unsigned int kCanvasStrideBytes =
    kCanvasFloatsPerVertex * sizeof(float);
  static const unsigned int kUiStrideBytes = 16; // pos3 float + color4 ubyte
  static const unsigned int kSpriteStrideBytes =
    24; // pos3 float + color4 ubyte + uv2 float
  static const unsigned int kLitPrimitiveStrideBytes =
    36; // pos3 float (12) + norm3 float (12) + color4 ubyte (4) + uv2 float (8)
  static const unsigned int kLitMeshStrideBytes =
    32; // pos3 float (12) + norm3 float (12) + uv2 float (8)

  // Static mesh (Canvas / proof). Default layout Pos3Color3Uv2.
  GLMesh(const void* vertices,
         size_t vertexSize,
         const void* indices,
         size_t indexSize)
    : GLMesh(vertices,
             vertexSize,
             indices,
             indexSize,
             MeshVertexLayout::Pos3Color3Uv2,
             false)
  {
  }

  // Full create: dynamic=true means vertexSize is VBO capacity; vertices may be
  // null.
  GLMesh(const void* vertices,
         size_t vertexSize,
         const void* indices,
         size_t indexSize,
         MeshVertexLayout layout,
         bool dynamic)
  {
    _vaoID = 0;
    _vboID = 0;
    _eboID = 0;
    _uploadedIndexCount = 0;
    _hasIndexBuffer = false;
    _layout = layout;
    _dynamic = dynamic;
    _vboCapacityBytes = vertexSize;

    if (vertices && vertexSize > 0 &&
        layout == MeshVertexLayout::Pos3Color3Uv2) {
      const float* floatVerts = static_cast<const float*>(vertices);
      size_t floatCount = vertexSize / sizeof(float);
      _vertexData.assign(floatVerts, floatVerts + floatCount);
    }

    storeIndices(indices, indexSize);
    uploadToGpu(vertices, vertexSize);
  }

  GLMesh(const std::vector<float>& vertexData,
         const std::vector<unsigned int>& indexData)
  {
    _vaoID = 0;
    _vboID = 0;
    _eboID = 0;
    _uploadedIndexCount = 0;
    _hasIndexBuffer = false;
    _layout = MeshVertexLayout::Pos3Color3Uv2;
    _dynamic = false;
    _vertexData = vertexData;
    _indexData = indexData;
    _vboCapacityBytes = vertexData.size() * sizeof(float);
    uploadToGpu(_vertexData.empty() ? nullptr : _vertexData.data(),
                _vboCapacityBytes);
  }

  GLMesh(const std::vector<float>& vertexData)
  {
    _vaoID = 0;
    _vboID = 0;
    _eboID = 0;
    _uploadedIndexCount = 0;
    _hasIndexBuffer = false;
    _layout = MeshVertexLayout::Pos3Color3Uv2;
    _dynamic = false;
    _vertexData = vertexData;
    _vboCapacityBytes = vertexData.size() * sizeof(float);
    uploadToGpu(_vertexData.empty() ? nullptr : _vertexData.data(),
                _vboCapacityBytes);
  }

  ~GLMesh() = default;

  void Bind() const { glBindVertexArray(_vaoID); }

  void Unbind() const { glBindVertexArray(0); }

  void UpdateVertexData(const void* data,
                        size_t sizeBytes,
                        size_t offsetBytes = 0) const
  {
    if (!data || sizeBytes == 0) {
      return;
    }
    glBindBuffer(GL_ARRAY_BUFFER, _vboID);
    glBufferSubData(GL_ARRAY_BUFFER,
                    static_cast<GLintptr>(offsetBytes),
                    static_cast<GLsizeiptr>(sizeBytes),
                    data);
  }

  unsigned int getUploadedIndexCount() const { return _uploadedIndexCount; }
  bool hasIndexBuffer() const { return _hasIndexBuffer; }
  MeshVertexLayout getLayout() const { return _layout; }

  void Destroy() override
  {
    if (_vaoID != 0) {
      glDeleteVertexArrays(1, &_vaoID);
      _vaoID = 0;
    }
    if (_vboID != 0) {
      glDeleteBuffers(1, &_vboID);
      _vboID = 0;
    }
    if (_eboID != 0) {
      glDeleteBuffers(1, &_eboID);
      _eboID = 0;
    }
    _uploadedIndexCount = 0;
    _hasIndexBuffer = false;
  }

private:
  unsigned int _uploadedIndexCount;
  bool _hasIndexBuffer;
  MeshVertexLayout _layout;
  bool _dynamic;
  size_t _vboCapacityBytes;

  void storeIndices(const void* indices, size_t indexSize)
  {
    if (!indices || indexSize == 0) {
      return;
    }
    if (indexSize % sizeof(unsigned int) == 0) {
      const unsigned int* uintIndices =
        static_cast<const unsigned int*>(indices);
      size_t indexCount = indexSize / sizeof(unsigned int);
      _indexData.assign(uintIndices, uintIndices + indexCount);
    } else {
      const unsigned char* byteIndices =
        static_cast<const unsigned char*>(indices);
      size_t indexCount = indexSize / sizeof(unsigned char);
      _indexData.resize(indexCount);
      for (size_t i = 0; i < indexCount; ++i) {
        _indexData[i] = static_cast<unsigned int>(byteIndices[i]);
      }
    }
  }

  void setupAttributes()
  {
    if (_layout == MeshVertexLayout::Pos3Color4U8) {
      // UI: location 0 pos3, location 1 color4 ubyte normalized
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, kUiStrideBytes, reinterpret_cast<void*>(0));
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1,
                            4,
                            GL_UNSIGNED_BYTE,
                            GL_TRUE,
                            kUiStrideBytes,
                            reinterpret_cast<void*>(12));
    } else if (_layout == MeshVertexLayout::Pos3Color4U8Uv2) {
      // Sprites: location 0 pos3, 1 color4 ubyte, 2 uv2
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0,
                            3,
                            GL_FLOAT,
                            GL_FALSE,
                            kSpriteStrideBytes,
                            reinterpret_cast<void*>(0));
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1,
                            4,
                            GL_UNSIGNED_BYTE,
                            GL_TRUE,
                            kSpriteStrideBytes,
                            reinterpret_cast<void*>(12));
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(2,
                            2,
                            GL_FLOAT,
                            GL_FALSE,
                            kSpriteStrideBytes,
                            reinterpret_cast<void*>(16));
    } else if (_layout == MeshVertexLayout::Pos3Norm3Color4U8Uv2) {
      // Lit primitives: location 0 pos3, 1 color4 ubyte, 2 uv2, 3 normal3
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0,
                            3,
                            GL_FLOAT,
                            GL_FALSE,
                            kLitPrimitiveStrideBytes,
                            reinterpret_cast<void*>(0));
      glEnableVertexAttribArray(3);
      glVertexAttribPointer(3,
                            3,
                            GL_FLOAT,
                            GL_FALSE,
                            kLitPrimitiveStrideBytes,
                            reinterpret_cast<void*>(12));
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1,
                            4,
                            GL_UNSIGNED_BYTE,
                            GL_TRUE,
                            kLitPrimitiveStrideBytes,
                            reinterpret_cast<void*>(24));
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(2,
                            2,
                            GL_FLOAT,
                            GL_FALSE,
                            kLitPrimitiveStrideBytes,
                            reinterpret_cast<void*>(28));
    } else if (_layout == MeshVertexLayout::Pos3Norm3Uv2) {
      // Lit meshes: location 0 pos3, 3 normal3, 2 uv2
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0,
                            3,
                            GL_FLOAT,
                            GL_FALSE,
                            kLitMeshStrideBytes,
                            reinterpret_cast<void*>(0));
      glEnableVertexAttribArray(3);
      glVertexAttribPointer(3,
                            3,
                            GL_FLOAT,
                            GL_FALSE,
                            kLitMeshStrideBytes,
                            reinterpret_cast<void*>(12));
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(2,
                            2,
                            GL_FLOAT,
                            GL_FALSE,
                            kLitMeshStrideBytes,
                            reinterpret_cast<void*>(24));
    } else {
      // Canvas: location 0 pos3, location 2 uv2
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0,
                            3,
                            GL_FLOAT,
                            GL_FALSE,
                            kCanvasStrideBytes,
                            reinterpret_cast<void*>(0));
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(2,
                            2,
                            GL_FLOAT,
                            GL_FALSE,
                            kCanvasStrideBytes,
                            reinterpret_cast<void*>(6 * sizeof(float)));
    }
  }

  void uploadToGpu(const void* vertices, size_t vertexSize)
  {
    glGenVertexArrays(1, &_vaoID);
    glGenBuffers(1, &_vboID);
    glBindVertexArray(_vaoID);

    glBindBuffer(GL_ARRAY_BUFFER, _vboID);
    GLenum usage = _dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    if (_dynamic) {
      glBufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(_vboCapacityBytes),
                   vertices,
                   usage);
    } else if (vertices && vertexSize > 0) {
      glBufferData(
        GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertexSize), vertices, usage);
      _vboCapacityBytes = vertexSize;
    } else if (!_vertexData.empty()) {
      glBufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(_vertexData.size() * sizeof(float)),
                   _vertexData.data(),
                   usage);
      _vboCapacityBytes = _vertexData.size() * sizeof(float);
    }

    if (!_indexData.empty()) {
      glGenBuffers(1, &_eboID);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _eboID);
      glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(_indexData.size() * sizeof(unsigned int)),
        _indexData.data(),
        GL_STATIC_DRAW);
      _uploadedIndexCount = static_cast<unsigned int>(_indexData.size());
      _hasIndexBuffer = true;
    }

    setupAttributes();

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  }
};
