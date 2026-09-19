#pragma once
#include <GL/glew.h>
#include <Illumo/Rendering/IMesh.h>
#include <cstring>
#include <limits>
#include <vector>

class GLMesh : public IMesh
{
public:
  unsigned int getVAOID() const { return _vaoID; }
  unsigned int getVBOID() const { return _vboID; }
  unsigned int getEBOID() const { return _eboID; }

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
  static const unsigned int kPos3StrideBytes = 12; // pos3 float (12)

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

  // Full create: dynamic=true means the sizes are VBO/EBO capacities; either
  // data pointer may be null.
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
    _eboCapacityBytes = indexSize;

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
    _eboCapacityBytes = indexData.size() * sizeof(unsigned int);
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
    _eboCapacityBytes = 0;
    uploadToGpu(_vertexData.empty() ? nullptr : _vertexData.data(),
                _vboCapacityBytes);
  }

  ~GLMesh() override { Destroy(); }
  GLMesh(const GLMesh&) = delete;
  GLMesh& operator=(const GLMesh&) = delete;
  GLMesh(GLMesh&&) = delete;
  GLMesh& operator=(GLMesh&&) = delete;
  bool isValid() const { return _vaoID != 0 && _vboID != 0; }

  void Bind() const { glBindVertexArray(_vaoID); }

  void Unbind() const { glBindVertexArray(0); }

  bool UpdateVertexData(const void* data,
                        size_t sizeBytes,
                        size_t offsetBytes = 0) const
  {
    if (!data || sizeBytes == 0 || !isValid() ||
        offsetBytes > _vboCapacityBytes ||
        sizeBytes > _vboCapacityBytes - offsetBytes) {
      return false;
    }
    glBindBuffer(GL_ARRAY_BUFFER, _vboID);
    glBufferSubData(GL_ARRAY_BUFFER,
                    static_cast<GLintptr>(offsetBytes),
                    static_cast<GLsizeiptr>(sizeBytes),
                    data);
    return true;
  }

  bool UpdateIndexData(const void* data,
                       size_t sizeBytes,
                       size_t offsetBytes = 0) const
  {
    if (!data || sizeBytes == 0 || !_hasIndexBuffer ||
        offsetBytes > _eboCapacityBytes ||
        sizeBytes > _eboCapacityBytes - offsetBytes) {
      return false;
    }
    // GL_ELEMENT_ARRAY_BUFFER binding belongs to the active VAO. The copy
    // target updates the same buffer without disturbing renderer VAO state.
    glBindBuffer(GL_COPY_WRITE_BUFFER, _eboID);
    glBufferSubData(GL_COPY_WRITE_BUFFER,
                    static_cast<GLintptr>(offsetBytes),
                    static_cast<GLsizeiptr>(sizeBytes),
                    data);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    return true;
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
  unsigned int _vaoID = 0;
  unsigned int _vboID = 0;
  unsigned int _eboID = 0;
  unsigned int _uploadedIndexCount;
  bool _hasIndexBuffer;
  MeshVertexLayout _layout;
  bool _dynamic;
  size_t _vboCapacityBytes;
  size_t _eboCapacityBytes;

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
    } else if (_layout == MeshVertexLayout::Pos3) {
      // Pos3: location 0 pos3 (12 bytes)
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(
        0, 3, GL_FLOAT, GL_FALSE, kPos3StrideBytes, reinterpret_cast<void*>(0));
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
    if (vertexSize == 0 || (!vertices && !_dynamic) ||
        vertexSize >
          static_cast<size_t>(std::numeric_limits<GLsizeiptr>::max()) ||
        _eboCapacityBytes >
          static_cast<size_t>(std::numeric_limits<GLsizeiptr>::max())) {
      return;
    }
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

    GLint64 vertexCapacity = 0;
    glGetBufferParameteri64v(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vertexCapacity);
    bool storageValid =
      _vaoID != 0 && _vboID != 0 &&
      vertexCapacity == static_cast<GLint64>(_vboCapacityBytes);
    if (_eboCapacityBytes > 0) {
      glGenBuffers(1, &_eboID);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _eboID);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(_eboCapacityBytes),
                   _indexData.empty() ? nullptr : _indexData.data(),
                   usage);
      _uploadedIndexCount =
        static_cast<unsigned int>(_eboCapacityBytes / sizeof(unsigned int));
      _hasIndexBuffer = true;
      GLint64 indexCapacity = 0;
      glGetBufferParameteri64v(
        GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &indexCapacity);
      storageValid = storageValid && _eboID != 0 &&
                     indexCapacity == static_cast<GLint64>(_eboCapacityBytes);
    }

    setupAttributes();

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    if (!storageValid) {
      Destroy();
    }
  }
};
